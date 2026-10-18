// T_ORG_HMI: ponte entre o organizador e a tela de operação (MQTT).
// Comandos que chegam da tela viram mensagens OPERADOR na própria fila de entrada,
// e são tratados pela T_ORG_EVT como qualquer outro evento.

#include <algorithm>
#include <cmath>

#include "../../third_party/nlohmann/json.hpp"
#include "../comum/console.hpp"
#include "../comum/topicos.hpp"
#include "organizador.hpp"

namespace amr {

using json = nlohmann::json;

namespace {
constexpr int PRIORIDADE_HMI = 35;

const char* nome_sistema(uint32_t e) {
    switch (EstadoSistema(e)) {
        case EstadoSistema::INICIANDO: return "INICIANDO";
        case EstadoSistema::OPERANDO: return "OPERANDO";
        case EstadoSistema::PARADA_GERAL: return "PARADA_GERAL";
        case EstadoSistema::ENCERRANDO: return "ENCERRANDO";
    }
    return "?";
}

json metricas(const MetricasTempoReal& m) {
    return {{"ciclos", m.ciclos}, {"perdas", m.perdas_prazo}, {"c_max_us", m.execucao_max_us}, {"lat_max_us", m.atraso_max_us}};
}

double arred(double v, double passo = 0.01) { return std::round(v / passo) * passo; }
}  // namespace

void Organizador::iniciar_hmi(const std::string& broker) {
    auto cfg = ConfigMqtt::de_endereco(broker, "amr_organizador");
    cfg.topico_testamento = topicos::status_processo("organizador");
    mqtt_ = std::make_unique<ClienteMqtt>(cfg);
    auto cb = [this](const std::string& t, const std::string& p) { comando_da_tela(t, p); };
    mqtt_->assinar(topicos::COMANDOS, 1, cb);
    mqtt_->assinar(topicos::CAIXA_NOVA, 1, cb);
    mqtt_->iniciar();
    t_hmi_ = std::make_unique<TarefaPeriodica>("T_ORG_HMI", 200 * NS_POR_MS, PRIORIDADE_HMI, [this](const InfoCiclo&) { ciclo_hmi(); });
    t_hmi_->iniciar();
}

void Organizador::parar_hmi() {
    if (t_hmi_) t_hmi_->parar();
    if (mqtt_) {
        mqtt_->publicar(topicos::status_processo("organizador"), "offline", 1, true);
        mqtt_->parar();
    }
}

void Organizador::ciclo_hmi() {
    if (!mqtt_ || !mqtt_->conectado()) {
        anunciado_online_ = false;
        return;
    }
    if (!anunciado_online_) anunciado_online_ = mqtt_->publicar(topicos::status_processo("organizador"), "online", 1, true);

    std::deque<std::pair<int64_t, std::string>> novos;
    {
        std::lock_guard<std::mutex> t(mtx_alarmes_);
        novos.swap(alarmes_);
    }
    for (auto& [t, texto] : novos)
        mqtt_->publicar(topicos::ALARME, json{{"t", data_hora_iso(t)}, {"texto", texto}}.dump(), 1);

    mqtt_->publicar(topicos::ESTADO, montar_estado(), 0, true);
}

std::string Organizador::montar_estado() {
    const int64_t agora = agora_ns();
    auto& cab = frota_->cabecalho();

    std::vector<SlotRobo> slots;
    std::vector<int> indices;
    for (uint32_t s = 0; s < n_max_; ++s) {
        auto& sl = frota_->robo(s);
        TravaCompartilhada t(&sl.mtx);
        if (!sl.ocupado || sl.id < 0) continue;
        slots.push_back(sl);
        indices.push_back(int(s));
    }
    std::vector<SlotEquipe> equipes;
    std::vector<int> indices_equipe;
    for (uint32_t e = 0; e < n_max_; ++e) {
        auto& eq = frota_->equipe(e);
        TravaCompartilhada t(&eq.mtx);
        if (eq.estado == EstadoEquipe::LIVRE) continue;
        equipes.push_back(eq);
        indices_equipe.push_back(int(e));
    }
    std::vector<EspelhoPassagem> passagens;
    for (uint32_t p = 0; p < frota_->n_passagens(); ++p) {
        auto& esp = frota_->passagem(p);
        TravaCompartilhada t(&esp.mtx);
        passagens.push_back(esp);
    }

    json robos = json::array();
    json caixas = json::array();
    json kpi;
    {
        std::lock_guard<std::mutex> t(mtx_modelo_);
        for (auto& sl : slots) {
            auto it = modelo_.robos.find(sl.id);
            robos.push_back({{"id", sl.id},
                             {"slot", indices[size_t(&sl - slots.data())]},
                             {"pid", sl.pid},
                             {"estado", nome_estado(sl.estado)},
                             {"modo", sl.modo == ModoOperacao::AUTO ? "AUTO" : "MANUAL"},
                             {"x", arred(sl.x)},
                             {"y", arred(sl.y)},
                             {"sp_x", arred(sl.sp_x)},
                             {"sp_y", arred(sl.sp_y)},
                             {"bateria", arred(sl.bateria, 0.1)},
                             {"distancia", arred(sl.distancia, 0.1)},
                             {"equipe", sl.equipe},
                             {"caixa", sl.caixa},
                             {"falha", bool(sl.falha)},
                             {"atracada", bool(sl.atracada)},
                             {"acoplar", bool(sl.acoplar)},
                             {"bateria_baixa", bool(sl.bateria_baixa)},
                             {"parado", bool(sl.parado)},
                             {"evitando", bool(sl.evitando)},
                             {"desvios", sl.desvios},
                             {"heartbeat", sl.heartbeat},
                             {"situacao", it != modelo_.robos.end() ? nome_situacao(it->second.situacao) : "?"},
                             {"pronto", it != modelo_.robos.end() && it->second.pronto},
                             {"nav", metricas(sl.nav)},
                             {"status", metricas(sl.status)}});
        }
        int pendentes = 0;
        for (auto& c : modelo_.caixas) {
            if (c.estado != EstadoCaixa::ENTREGUE) pendentes++;
            bool recente = c.estado != EstadoCaixa::ENTREGUE || agora - c.t_entregue < 600 * NS_POR_S;
            if (!recente) continue;
            caixas.push_back({{"id", c.def.id},
                              {"tamanho", c.def.tamanho},
                              {"baia", c.def.baia},
                              {"zona", c.def.zona},
                              {"estado", nome_estado(c.estado)},
                              {"x", arred(c.origem.x)},
                              {"y", arred(c.origem.y)},
                              {"equipe", c.equipe},
                              {"aguardando_frota", c.aguardando_frota},
                              {"robos", c.robos},
                              {"lead_s", c.t_entregue ? arred(double(c.t_entregue - c.t_disponivel) / NS_POR_S, 0.1) : 0.0},
                              {"espera_s", c.t_pronta ? arred(double(agora - c.t_pronta) / NS_POR_S, 0.1) : 0.0}});
        }
        double soma = 0;
        int ultimos = 0, ultimos_5min = 0;
        for (auto& [instante, lead] : modelo_.entregas) {
            soma += lead;
            ultimos++;
            if (agora - instante < 300 * NS_POR_S) ultimos_5min++;
        }
        uint64_t desvios = 0;
        for (auto& sl : slots) desvios += sl.desvios;
        double minutos = std::min(5.0, double(agora - cab.t_inicio_ns) / (60.0 * NS_POR_S));
        kpi = {{"entregues", modelo_.entregues},
               {"pendentes", pendentes},
               {"taxa_min", minutos > 0.05 ? arred(ultimos_5min / minutos) : 0.0},
               {"lead_medio_s", ultimos ? arred(soma / ultimos, 0.1) : 0.0},
               {"desvios", desvios},
               {"fila_max", modelo_.ocupacao_max_fila},
               {"perdas_registro", static_cast<BufferEventos*>(shm_eventos_.dados())->perdas.load()},
               {"registros", static_cast<BufferEventos*>(shm_eventos_.dados())->gravados.load()}};
    }

    json eqs = json::array();
    for (auto& e : equipes) {
        json membros = json::array();
        for (int i = 0; i < e.k; ++i) membros.push_back(e.membros[i]);
        eqs.push_back({{"indice", indices_equipe[size_t(&e - equipes.data())]},
                       {"estado", nome_estado(e.estado)},
                       {"caixa", e.caixa},
                       {"k", e.k},
                       {"lider", e.lider},
                       {"membros", membros},
                       {"passo", e.passo},
                       {"rota_n", e.rota_n}});
    }
    json pas = json::array();
    for (size_t i = 0; i < passagens.size(); ++i) {
        auto& p = passagens[i];
        json ocup = json::array();
        for (int k = 0; k < MAX_OCUPANTES; ++k)
            if (p.ocupantes[k] >= 0) ocup.push_back(p.ocupantes[k]);
        bool esperando = p.esperador >= 0 && agora - p.ultima_tentativa_ns < 300 * NS_POR_MS;
        pas.push_back({{"nome", cenario_.passagens[i].nome},
                       {"capacidade", p.capacidade},
                       {"ocupacao", p.ocupacao},
                       {"ocupacao_max", p.ocupacao_max},
                       {"ocupantes", ocup},
                       {"esperador", esperando ? p.esperador : -1},
                       {"espera_media_s", arred(p.espera_media_s, 0.1)},
                       {"entradas", p.entradas}});
    }

    json j = {{"t_s", arred(double(agora - cab.t_inicio_ns) / NS_POR_S, 0.1)},
              {"sistema",
               {{"estado", nome_sistema(cab.estado_sistema.load())},
                {"parada_geral", cab.parada_geral.load() != 0},
                {"anticolisao", cab.anticolisao.load() != 0},
                {"rotas_adaptativas", cab.rotas_adaptativas.load() != 0},
                {"n_max", n_max_}}},
              {"robos", robos},
              {"equipes", eqs},
              {"passagens", pas},
              {"caixas", caixas},
              {"kpi", kpi}};
    return j.dump();
}

// Callback da thread MQTT: valida, converte e enfileira; não toca no modelo.
void Organizador::comando_da_tela(const std::string& topico, const std::string& payload) {
    json j = json::parse(payload, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    auto& cab = frota_->cabecalho();
    CorpoOperador op{};
    std::string acao = topico == topicos::CAIXA_NOVA ? "nova_caixa" : j.value("acao", "");
    int robo = j.value("robo", -1);

    auto ao_robo = [&](TipoMsg tipo) {
        op.comando = ComandoOperador::ENVIAR_AO_ROBO;
        op.robo = robo;
        op.tipo_robo = tipo;
    };

    if (acao == "parada_geral") op.comando = ComandoOperador::PARADA_GERAL;
    else if (acao == "liberar_parada") op.comando = ComandoOperador::LIBERAR_PARADA;
    else if (acao == "anticolisao") {
        cab.anticolisao.store(j.value("ativo", true) ? 1 : 0);
        alarme("T_ORG_HMI", "anticolisão %s pelo operador", j.value("ativo", true) ? "ligada" : "desligada");
        return;
    } else if (acao == "rotas_adaptativas") {
        cab.rotas_adaptativas.store(j.value("ativo", true) ? 1 : 0);
        LOG("T_ORG_HMI", "rotas adaptativas %s", j.value("ativo", true) ? "ligadas" : "desligadas");
        return;
    } else if (acao == "nova_caixa") {
        op.comando = ComandoOperador::NOVA_CAIXA;
        op.caixa = j.value("id", 0);
        op.tamanho = j.value("tamanho", 1);
        op.baia = j.value("baia", 1);
        op.zona = j.value("zona", 1);
    } else if (acao == "remover_robo") {
        op.comando = ComandoOperador::REMOVER_ROBO;
        op.robo = robo;
    } else if (acao == "modo") {
        ao_robo(TipoMsg::MODO);
        op.cmd.modo = j.value("modo", "auto") == "manual" ? ModoOperacao::MANUAL : ModoOperacao::AUTO;
    } else if (acao == "parar") ao_robo(TipoMsg::PARAR);
    else if (acao == "retomar") ao_robo(TipoMsg::RETOMAR);
    else if (acao == "reconhecer") ao_robo(TipoMsg::RECONHECER_FALHA);
    else if (acao == "abortar") ao_robo(TipoMsg::ABORTAR);
    else if (acao == "destino") {
        ao_robo(TipoMsg::DESTINO_MANUAL);
        Ponto alvo{j.value("x", -1.0), j.value("y", -1.0)};
        if (j.contains("local")) {
            std::string tipo = j["local"].value("tipo", "");
            int id = j["local"].value("id", 0);
            const Baia* b = tipo == "baia" ? cenario_.baia(id) : nullptr;
            const Zona* z = tipo == "zona" ? cenario_.zona(id) : nullptr;
            if (b) alvo = b->pr;
            if (z) alvo = z->ponto;
            if (tipo == "recarga" && id >= 0 && size_t(id) < cenario_.estacoes_recarga.size()) alvo = cenario_.estacoes_recarga[size_t(id)];
        }
        if (alvo.x < 0 || alvo.y < 0 || alvo.x >= cenario_.largura || alvo.y >= cenario_.altura) {
            LOG("T_ORG_HMI", "destino inválido para o robô %d", robo);
            return;
        }
        op.cmd.x = float(alvo.x);
        op.cmd.y = float(alvo.y);
    } else {
        LOG("T_ORG_HMI", "comando desconhecido da tela: %s", acao.c_str());
        return;
    }

    Mensagem m = nova_mensagem(TipoMsg::OPERADOR, ORIGEM_ORGANIZADOR);
    m.operador = op;
    if (!entrada_.enviar(&m, sizeof m, prioridade(TipoMsg::OPERADOR), 100 * NS_POR_MS))
        LOG("T_ORG_HMI", "fila de entrada cheia: comando %s descartado", acao.c_str());
}

}  // namespace amr
