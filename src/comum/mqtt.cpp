#include "mqtt.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

#include "console.hpp"
#include "tempo.hpp"

namespace amr {

namespace {
enum : uint8_t {
    CONNECT = 1, CONNACK = 2, PUBLISH = 3, PUBACK = 4, SUBSCRIBE = 8, SUBACK = 9,
    PINGREQ = 12, PINGRESP = 13, DISCONNECT = 14,
};

void por_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x & 0xFF));
}

void por_texto(std::vector<uint8_t>& v, const std::string& s) {
    por_u16(v, uint16_t(s.size()));
    v.insert(v.end(), s.begin(), s.end());
}

std::vector<uint8_t> montar(uint8_t cabecalho, const std::vector<uint8_t>& corpo) {
    std::vector<uint8_t> p{cabecalho};
    size_t n = corpo.size();
    do {
        uint8_t b = n % 128;
        n /= 128;
        if (n > 0) b |= 0x80;
        p.push_back(b);
    } while (n > 0);
    p.insert(p.end(), corpo.begin(), corpo.end());
    return p;
}
}  // namespace

ConfigMqtt ConfigMqtt::de_endereco(const std::string& endereco, std::string id) {
    ConfigMqtt c;
    c.id_cliente = std::move(id);
    auto dp = endereco.rfind(':');
    if (dp == std::string::npos) {
        if (!endereco.empty()) c.host = endereco;
    } else {
        c.host = endereco.substr(0, dp);
        c.porta = std::stoi(endereco.substr(dp + 1));
    }
    return c;
}

ClienteMqtt::ClienteMqtt(ConfigMqtt cfg) : cfg_(std::move(cfg)) {}

ClienteMqtt::~ClienteMqtt() { parar(); }

void ClienteMqtt::assinar(const std::string& filtro, int qos, Callback cb) {
    std::lock_guard<std::mutex> t(mtx_assinaturas_);
    assinaturas_.push_back({filtro, qos, std::move(cb)});
}

void ClienteMqtt::iniciar() {
    if (ativo_.exchange(true)) return;
    thread_ = std::thread(&ClienteMqtt::laco, this);
}

void ClienteMqtt::parar() {
    if (!ativo_.exchange(false)) return;
    {
        std::lock_guard<std::mutex> t(mtx_envio_);
        if (sock_ >= 0) {
            auto p = montar(DISCONNECT << 4, {});
            ::send(sock_, p.data(), p.size(), MSG_NOSIGNAL);
            ::shutdown(sock_, SHUT_RDWR);
        }
    }
    if (thread_.joinable()) thread_.join();
}

namespace {
std::vector<std::string> niveis(const std::string& s) {
    std::vector<std::string> v;
    size_t inicio = 0;
    for (;;) {
        size_t fim = s.find('/', inicio);
        v.push_back(s.substr(inicio, fim == std::string::npos ? std::string::npos : fim - inicio));
        if (fim == std::string::npos) return v;
        inicio = fim + 1;
    }
}
}  // namespace

bool ClienteMqtt::casa(const std::string& filtro, const std::string& topico) {
    auto f = niveis(filtro), t = niveis(topico);
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] == "#") return true;
        if (i >= t.size() || (f[i] != "+" && f[i] != t[i])) return false;
    }
    return f.size() == t.size();
}

bool ClienteMqtt::enviar(const std::vector<uint8_t>& pacote) {
    std::lock_guard<std::mutex> t(mtx_envio_);
    if (sock_ < 0) return false;
    size_t enviado = 0;
    while (enviado < pacote.size()) {
        ssize_t r = ::send(sock_, pacote.data() + enviado, pacote.size() - enviado, MSG_NOSIGNAL);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        enviado += size_t(r);
    }
    ultimo_envio_ns_ = agora_ns();
    return true;
}

bool ClienteMqtt::publicar(const std::string& topico, const std::string& payload, int qos, bool retido) {
    if (!conectado_.load()) return false;
    std::vector<uint8_t> corpo;
    por_texto(corpo, topico);
    if (qos > 0) {
        std::lock_guard<std::mutex> t(mtx_envio_);
        if (++proximo_id_ == 0) proximo_id_ = 1;
        por_u16(corpo, proximo_id_);
    }
    corpo.insert(corpo.end(), payload.begin(), payload.end());
    uint8_t cab = uint8_t(PUBLISH << 4) | uint8_t((qos > 0 ? 1 : 0) << 1) | (retido ? 1 : 0);
    return enviar(montar(cab, corpo));
}

bool ClienteMqtt::ler_pacote(uint8_t& tipo, std::vector<uint8_t>& corpo, int espera_ms) {
    pollfd p{sock_, POLLIN, 0};
    int r = ::poll(&p, 1, espera_ms);
    if (r == 0) {
        tipo = 0;
        return true;  // nada chegou no prazo
    }
    if (r < 0) return errno == EINTR;
    auto ler = [&](uint8_t* buf, size_t n) {
        size_t lido = 0;
        while (lido < n) {
            ssize_t k = ::recv(sock_, buf + lido, n - lido, 0);
            if (k < 0 && errno == EINTR) continue;
            if (k <= 0) return false;
            lido += size_t(k);
        }
        return true;
    };
    uint8_t cab;
    if (!ler(&cab, 1)) return false;
    size_t tamanho = 0, mult = 1;
    for (int i = 0; i < 4; ++i) {
        uint8_t b;
        if (!ler(&b, 1)) return false;
        tamanho += (b & 0x7F) * mult;
        mult *= 128;
        if (!(b & 0x80)) break;
    }
    corpo.resize(tamanho);
    if (tamanho > 0 && !ler(corpo.data(), tamanho)) return false;
    tipo = cab;
    return true;
}

bool ClienteMqtt::conectar() {
    addrinfo dicas{}, *res = nullptr;
    dicas.ai_family = AF_UNSPEC;
    dicas.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(cfg_.host.c_str(), std::to_string(cfg_.porta).c_str(), &dicas, &res) != 0) return false;
    int s = -1;
    for (auto* a = res; a; a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s < 0) continue;
        if (::connect(s, a->ai_addr, a->ai_addrlen) == 0) break;
        ::close(s);
        s = -1;
    }
    freeaddrinfo(res);
    if (s < 0) return false;
    int um = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &um, sizeof um);
    timeval prazo{0, 300 * 1000};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &prazo, sizeof prazo);
    {
        std::lock_guard<std::mutex> t(mtx_envio_);
        sock_ = s;
    }

    std::vector<uint8_t> corpo;
    por_texto(corpo, "MQTT");
    corpo.push_back(4);  // versão 3.1.1
    uint8_t flags = 0x02;  // sessão limpa
    if (!cfg_.topico_testamento.empty()) flags |= 0x04 | 0x20 | 0x08;  // will, will retain, will QoS 1
    corpo.push_back(flags);
    por_u16(corpo, uint16_t(cfg_.keepalive_s));
    por_texto(corpo, cfg_.id_cliente);
    if (!cfg_.topico_testamento.empty()) {
        por_texto(corpo, cfg_.topico_testamento);
        por_texto(corpo, cfg_.mensagem_testamento);
    }
    if (!enviar(montar(CONNECT << 4, corpo))) return false;

    uint8_t tipo = 0;
    std::vector<uint8_t> resp;
    if (!ler_pacote(tipo, resp, 3000) || (tipo >> 4) != CONNACK || resp.size() < 2 || resp[1] != 0) return false;

    std::vector<uint8_t> sub;
    {
        std::lock_guard<std::mutex> t(mtx_assinaturas_);
        if (!assinaturas_.empty()) {
            por_u16(sub, 1);
            for (auto& a : assinaturas_) {
                por_texto(sub, a.filtro);
                sub.push_back(uint8_t(a.qos));
            }
        }
    }
    if (!sub.empty() && !enviar(montar(uint8_t(SUBSCRIBE << 4) | 0x02, sub))) return false;
    conectado_ = true;
    return true;
}

void ClienteMqtt::desconectar() {
    conectado_ = false;
    std::lock_guard<std::mutex> t(mtx_envio_);
    if (sock_ >= 0) ::close(sock_);
    sock_ = -1;
}

void ClienteMqtt::tratar_publish(uint8_t flags, const std::vector<uint8_t>& corpo) {
    if (corpo.size() < 2) return;
    size_t n = (size_t(corpo[0]) << 8) | corpo[1];
    if (corpo.size() < 2 + n) return;
    std::string topico(corpo.begin() + 2, corpo.begin() + 2 + long(n));
    size_t pos = 2 + n;
    int qos = (flags >> 1) & 0x3;
    if (qos > 0) {
        if (corpo.size() < pos + 2) return;
        std::vector<uint8_t> ack{corpo[pos], corpo[pos + 1]};
        pos += 2;
        enviar(montar(PUBACK << 4, ack));
    }
    std::string payload(corpo.begin() + long(pos), corpo.end());
    std::vector<Callback> alvos;
    {
        std::lock_guard<std::mutex> t(mtx_assinaturas_);
        for (auto& a : assinaturas_)
            if (casa(a.filtro, topico)) alvos.push_back(a.cb);
    }
    for (auto& cb : alvos) cb(topico, payload);
}

void ClienteMqtt::laco() {
    pthread_setname_np(pthread_self(), "mqtt_rx");
    bool avisou = false;
    while (ativo_.load()) {
        if (!conectar()) {
            desconectar();
            if (!avisou) LOG("mqtt", "%s: broker %s:%d indisponível, tentando de novo", cfg_.id_cliente.c_str(),
                             cfg_.host.c_str(), cfg_.porta);
            avisou = true;
            for (int i = 0; i < 10 && ativo_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (avisou) LOG("mqtt", "%s: conectado ao broker", cfg_.id_cliente.c_str());
        avisou = false;
        while (ativo_.load()) {
            uint8_t tipo = 0;
            std::vector<uint8_t> corpo;
            if (!ler_pacote(tipo, corpo, 250)) break;
            if (tipo != 0 && (tipo >> 4) == PUBLISH) tratar_publish(tipo & 0x0F, corpo);
            if (agora_ns() - ultimo_envio_ns_.load() > int64_t(cfg_.keepalive_s) * NS_POR_S / 2)
                if (!enviar(montar(PINGREQ << 4, {}))) break;
        }
        desconectar();
        if (ativo_.load()) LOG("mqtt", "%s: conexão perdida", cfg_.id_cliente.c_str());
    }
}

}  // namespace amr
