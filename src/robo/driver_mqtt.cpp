#include "driver_mqtt.hpp"

#include <chrono>
#include <cmath>

#include "../../third_party/nlohmann/json.hpp"
#include "../comum/console.hpp"
#include "../comum/tarefa_periodica.hpp"
#include "../comum/topicos.hpp"

namespace amr {

using json = nlohmann::json;

namespace {
constexpr int PRIORIDADE_IO = 70;
constexpr int64_t REENVIO_NS = 200 * NS_POR_MS;

// Tabela 1 do enunciado: posições inteiras. Usamos centímetros.
int para_cm(double m) { return int(std::lround(m * 100.0)); }
double de_cm(int cm) { return cm / 100.0; }
}  // namespace

DriverMQTT::DriverMQTT(int id, ImagemES& imagem, RotaVisivel& rota, const std::string& broker)
    : id_(id), img_(imagem), rota_(rota), cliente_([&] {
          auto c = ConfigMqtt::de_endereco(broker, "amr_robo_" + std::to_string(id));
          c.topico_testamento = topicos::status_processo("robo_" + std::to_string(id));
          return c;
      }()) {
    cliente_.assinar(topicos::sensores(id), 0, [this](const std::string&, const std::string& p) { receber(p); });
}

// Callback da thread do cliente MQTT: só converte e copia para a imagem de entradas.
void DriverMQTT::receber(const std::string& payload) {
    json j = json::parse(payload, nullptr, false);
    if (j.is_discarded()) return;
    Entradas e;
    e.x = de_cm(j.value("i_posicao_x", 0));
    e.y = de_cm(j.value("i_posicao_y", 0));
    e.bateria = j.value("i_percentual_bateria", 0);
    e.falha = j.value("i_falha", false);
    e.atracada = j.value("i_carga_atracada", false);
    e.seq = j.value("seq", uint64_t(0));
    e.t_ns = agora_ns();
    img_.escrever_entradas(e);
}

void DriverMQTT::executar(std::atomic<bool>& ativo) {
    aplicar_prioridade(PRIORIDADE_IO, "T_IO");
    cliente_.iniciar();
    uint64_t ultima_seq = 0;
    uint32_t versao_rota = 0;
    bool anunciou = false;
    int64_t ultimo_envio = 0;
    while (ativo.load()) {
        // acorda com setpoint novo ou, no máximo, a cada 200 ms para reenviar o último
        Saidas s = img_.esperar_saida(std::chrono::steady_clock::now() + std::chrono::milliseconds(200));
        if (!ativo.load()) break;
        if (!cliente_.conectado()) continue;
        if (!anunciou) anunciou = cliente_.publicar(topicos::status_processo("robo_" + std::to_string(id_)), "online", 1, true);

        int64_t agora = agora_ns();
        if (s.seq > 0 && (s.seq != ultima_seq || agora - ultimo_envio >= REENVIO_NS)) {
            json j = {{"o_sp_posicao_x", para_cm(s.sp_x)},
                      {"o_sp_posicao_y", para_cm(s.sp_y)},
                      {"o_acoplar_e_erguer", s.acoplar},
                      {"seq", s.seq}};
            if (cliente_.publicar(topicos::atuadores(id_), j.dump(), 0)) {
                ultima_seq = s.seq;
                ultimo_envio = agora;
            }
        }

        std::vector<Ponto> pontos;
        uint32_t versao;
        {
            std::lock_guard<std::mutex> t(rota_.mtx);
            versao = rota_.versao;
            if (versao != versao_rota) pontos = rota_.pontos;
        }
        if (versao != versao_rota) {
            json r = json::array();
            for (auto& p : pontos) r.push_back({para_cm(p.x), para_cm(p.y)});
            if (cliente_.publicar(topicos::rota(id_), r.dump(), 0, true)) versao_rota = versao;
        }
    }
    cliente_.publicar(topicos::rota(id_), "[]", 0, true);
    cliente_.publicar(topicos::status_processo("robo_" + std::to_string(id_)), "offline", 1, true);
    cliente_.parar();
}

}  // namespace amr
