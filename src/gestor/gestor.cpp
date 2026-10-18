#include "gestor.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <vector>

#include "../comum/console.hpp"
#include "../comum/nomes.hpp"
#include "../comum/tarefa_periodica.hpp"
#include "../comum/topicos.hpp"
#include "../../third_party/nlohmann/json.hpp"

namespace amr {

Gestor::Gestor(std::string executavel_robo, std::string cenario, std::string broker)
    : executavel_robo_(std::move(executavel_robo)), cenario_(std::move(cenario)) {
    if (!broker.empty()) {
        auto cfg = ConfigMqtt::de_endereco(broker, "amr_gestor");
        cfg.topico_testamento = topicos::status_processo("gestor");
        mqtt_ = std::make_unique<ClienteMqtt>(cfg);
        // anúncio do simulador: {"hw_id": 7, "i_posicao_x": 150, "i_posicao_y": 1650} (cm)
        mqtt_->assinar(topicos::ROBO_NOVO, 1, [this](const std::string&, const std::string& p) {
            auto j = nlohmann::json::parse(p, nullptr, false);
            if (j.is_discarded() || !j.contains("hw_id")) return;
            receber_anuncio(j["hw_id"].get<int>(), j.value("i_posicao_x", 0) / 100.0f, j.value("i_posicao_y", 0) / 100.0f, "MQTT");
        });
    }
    shm_ = MemoriaCompartilhada::abrir(nomes::SHM_FROTA);
    frota_ = std::make_unique<Frota>(shm_.dados());
    FilaMensagens::remover(nomes::FILA_ANUNCIO);
    anuncios_ = FilaMensagens::criar(nomes::FILA_ANUNCIO, 64, sizeof(Mensagem), FilaMensagens::Acesso::Leitura);
    org_ = FilaMensagens::abrir(nomes::FILA_ORGANIZADOR, FilaMensagens::Acesso::Escrita);
}

Gestor::~Gestor() {
    parar();
    FilaMensagens::remover(nomes::FILA_ANUNCIO);
}

void Gestor::iniciar() {
    ativo_ = true;
    if (mqtt_) mqtt_->iniciar();
    t_reap_ = std::thread(&Gestor::laco_coleta, this);
    t_spawn_ = std::thread(&Gestor::laco_criacao, this);
    t_anuncio_ = std::thread(&Gestor::laco_anuncios, this);
}

void Gestor::parar() {
    if (!ativo_.exchange(false)) return;
    if (mqtt_) {
        mqtt_->publicar(topicos::status_processo("gestor"), "offline", 1, true);
        mqtt_->parar();
    }
    cv_spawn_.notify_all();
    if (t_anuncio_.joinable()) t_anuncio_.join();
    if (t_spawn_.joinable()) t_spawn_.join();

    std::unique_lock<std::mutex> trava(mtx_filhos_);
    for (auto& [pid, f] : filhos_) kill(pid, SIGTERM);
    if (!cv_filhos_.wait_for(trava, std::chrono::seconds(3), [&] { return filhos_.empty(); })) {
        for (auto& [pid, f] : filhos_) kill(pid, SIGKILL);
        cv_filhos_.wait_for(trava, std::chrono::seconds(2), [&] { return filhos_.empty(); });
    }
    trava.unlock();
    cv_filhos_.notify_all();
    if (t_reap_.joinable()) t_reap_.join();
    rastreador_.descarregar();
}

void Gestor::avisar_organizador(const Mensagem& m) {
    // o gestor não tem requisito de tempo real: pode esperar a fila esvaziar
    for (int tentativa = 0; tentativa < 50; ++tentativa) {
        try {
            if (org_.enviar(&m, sizeof m, prioridade(m.cab.tipo), 100 * NS_POR_MS)) return;
        } catch (const ErroSistema& e) {
            LOG("gestor", "organizador indisponível: %s", e.what());
            return;
        }
    }
    LOG("gestor", "fila do organizador cheia: %s descartado", nome_tipo(m.cab.tipo));
}

// T_GES_ANUNCIO
void Gestor::laco_anuncios() {
    pthread_setname_np(pthread_self(), "T_GES_ANUNCIO");
    Mensagem m;
    bool online = false;
    while (ativo_.load()) {
        if (!anuncios_.receber(&m, sizeof m, 500 * NS_POR_MS)) {
            if (mqtt_ && mqtt_->conectado() && !online) online = mqtt_->publicar(topicos::status_processo("gestor"), "online", 1, true);
            if (mqtt_ && !mqtt_->conectado()) online = false;
            continue;
        }
        if (m.cab.tipo == TipoMsg::ANUNCIO) receber_anuncio(m.anuncio.hw_id, m.anuncio.x, m.anuncio.y, "fila de teste");
        if (mqtt_ && mqtt_->conectado() && !online) online = mqtt_->publicar(topicos::status_processo("gestor"), "online", 1, true);
    }
}

void Gestor::receber_anuncio(int id, float x, float y, const char* origem) {
    {
        std::lock_guard<std::mutex> t(mtx_fila_);
        if (id < 0 || id > 65000 || ids_.count(id)) {
            LOG("T_GES_ANUNCIO", "anúncio do robô %d recusado (id inválido ou duplicado)", id);
            return;
        }
        ids_.insert(id);
        fila_.push_back({id, x, y});
    }
    LOG("T_GES_ANUNCIO", "robô %d anunciado (%s) em (%.2f, %.2f)", id, origem, x, y);
    cv_spawn_.notify_one();
}

// T_GES_SPAWN
void Gestor::laco_criacao() {
    pthread_setname_np(pthread_self(), "T_GES_SPAWN");
    std::unique_lock<std::mutex> trava(mtx_fila_);
    while (ativo_.load()) {
        cv_spawn_.wait_for(trava, std::chrono::milliseconds(500), [&] { return !fila_.empty() || !ativo_.load(); });
        while (!fila_.empty() && ativo_.load()) {
            Pedido p = fila_.front();
            fila_.pop_front();
            trava.unlock();
            criar_robo(p);
            trava.lock();
        }
    }
}

int Gestor::reservar_slot(int id, uint32_t& epoca) {
    for (uint32_t s = 0; s < frota_->n_max(); ++s) {
        auto& sl = frota_->robo(s);
        TravaCompartilhada t(&sl.mtx);
        if (t.dono_morreu()) LOG("T_GES_SPAWN", "EOWNERDEAD no slot %u: mutex recuperado", s);
        if (sl.ocupado) continue;
        sl.ocupado = 1;
        sl.suspeito = 0;
        sl.epoca++;
        sl.id = id;
        sl.pid = 0;
        sl.estado = EstadoRobo::INICIALIZANDO;
        sl.heartbeat = 0;
        sl.equipe = -1;
        sl.caixa = -1;
        sl.distancia = 0;
        epoca = sl.epoca;
        return int(s);
    }
    return -1;
}

void Gestor::devolver_slot(int slot, uint32_t epoca) {
    auto& sl = frota_->robo(uint32_t(slot));
    TravaCompartilhada t(&sl.mtx);
    if (sl.epoca != epoca) return;
    sl.ocupado = 0;
    sl.id = -1;
}

void Gestor::criar_robo(const Pedido& p) {
    uint32_t epoca = 0;
    int slot = reservar_slot(p.id, epoca);
    if (slot < 0) {
        LOG("T_GES_SPAWN", "frota cheia (N_max=%u): robô %d recusado", frota_->n_max(), p.id);
        std::lock_guard<std::mutex> t(mtx_fila_);
        ids_.erase(p.id);
        return;
    }

    // tudo que o filho usa é montado antes do fork: entre fork e exec só chamadas async-signal-safe
    std::vector<std::string> args = {executavel_robo_,
                                     "--id",   std::to_string(p.id),
                                     "--slot", std::to_string(slot),
                                     "--x",    std::to_string(p.x),
                                     "--y",    std::to_string(p.y),
                                     "--cenario", cenario_};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    std::lock_guard<std::mutex> trava(mtx_filhos_);
    pid_t pid = fork();
    if (pid == 0) {
        execv(argv[0], argv.data());
        _exit(127);
    }
    if (pid < 0) {
        LOG("T_GES_SPAWN", "fork falhou para o robô %d: %s", p.id, std::strerror(errno));
        devolver_slot(slot, epoca);
        std::lock_guard<std::mutex> t(mtx_fila_);
        ids_.erase(p.id);
        return;
    }
    {
        auto& sl = frota_->robo(uint32_t(slot));
        TravaCompartilhada t(&sl.mtx);
        sl.pid = pid;
    }
    filhos_[pid] = {p.id, slot, epoca};
    cv_filhos_.notify_all();

    Mensagem m = nova_mensagem(TipoMsg::NOVO_ROBO, ORIGEM_GESTOR);
    m.processo = {p.id, pid, slot, epoca, 0, p.x, p.y};
    avisar_organizador(m);
    LOG("T_GES_SPAWN", "robô %d criado: pid %d, slot %d", p.id, pid, slot);
    Registro r = novo_registro("gestor", Categoria::SISTEMA, "processo criado para o robô %d (pid %d)", p.id, pid);
    r.robo = p.id;
    rastreador_.registrar(r);
    rastreador_.descarregar();
}

// T_GES_REAP
void Gestor::laco_coleta() {
    pthread_setname_np(pthread_self(), "T_GES_REAP");
    for (;;) {
        {
            std::unique_lock<std::mutex> trava(mtx_filhos_);
            cv_filhos_.wait_for(trava, std::chrono::milliseconds(500), [&] { return !filhos_.empty() || !ativo_.load(); });
            if (filhos_.empty()) {
                if (!ativo_.load()) return;
                continue;
            }
        }
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid < 0) {
            if (errno != EINTR) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        Filho f{};
        {
            std::lock_guard<std::mutex> trava(mtx_filhos_);
            auto it = filhos_.find(pid);
            if (it == filhos_.end()) continue;
            f = it->second;
            filhos_.erase(it);
        }
        cv_filhos_.notify_all();
        {
            std::lock_guard<std::mutex> t(mtx_fila_);
            ids_.erase(f.id);
        }

        int codigo = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        bool normal = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        LOG("T_GES_REAP", "robô %d (pid %d) terminou: %s %d%s", f.id, pid, WIFEXITED(status) ? "código" : "sinal",
            WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status), normal ? "" : " (inesperado)");
        Mensagem m = nova_mensagem(TipoMsg::ROBO_TERMINOU, ORIGEM_GESTOR);
        m.processo = {f.id, pid, f.slot, f.epoca, codigo, 0, 0};
        avisar_organizador(m);
        Registro r = novo_registro("gestor", Categoria::SISTEMA, "robô %d terminou (%s)", f.id, normal ? "normal" : "inesperado");
        r.robo = f.id;
        rastreador_.registrar(r);
        rastreador_.descarregar();
    }
}

}  // namespace amr
