#include "status.hpp"

#include <signal.h>

#include <cstdlib>

#include "../comum/console.hpp"

namespace amr {

namespace {
constexpr int64_t IDADE_MAX_QUADRO_NS = 250 * NS_POR_MS;
}

StatusRobo::StatusRobo(NucleoRobo& r)
    : r_(r), emissor_(r.org), bateria_(r.p.bateria_baixa, r.p.bateria_critica, r.p.bateria_recarregada) {
    // gancho do teste T-09 ("CICLO@ID_MIN"): robôs com id >= ID_MIN morrem segurando o mutex do slot
    if (const char* v = std::getenv("AMR_TESTE_MORRER_NO_SLOT")) {
        char* resto = nullptr;
        uint64_t ciclo = std::strtoull(v, &resto, 10);
        int id_min = resto && *resto == '@' ? std::atoi(resto + 1) : 0;
        if (r_.cfg.id >= id_min) morrer_no_ciclo_ = ciclo;
    }
}

Ponto StatusRobo::estacao_mais_proxima(const Ponto& pos) const {
    const auto& estacoes = r_.cenario.estacoes_recarga;
    if (estacoes.empty()) return pos;
    const Ponto* melhor = &estacoes.front();
    for (auto& e : estacoes)
        if (distancia(pos, e) < distancia(pos, *melhor)) melhor = &e;
    return *melhor;
}

void StatusRobo::evento(TipoMsg tipo, const Ponto& pos, float valor, int equipe, const char* texto) {
    Mensagem m = r_.evento(tipo);
    m.evento.x = float(pos.x);
    m.evento.y = float(pos.y);
    m.evento.valor = valor;
    m.evento.equipe = equipe;
    emissor_.enviar(m);
    r_.registrar(Categoria::ROBO, texto);
}

void StatusRobo::ciclo(const InfoCiclo&) {
    emissor_.retentar();
    const Entradas e = r_.imagem.ler_entradas();
    if (e.seq == 0) return;
    const Saidas s = r_.imagem.ler_saidas();
    const int64_t agora = agora_ns();
    const Ponto pos{e.x, e.y};

    if (!primeira_) distancia_ += distancia(pos, ultima_);
    primeira_ = false;
    ultima_ = pos;

    const bool quadro_velho = agora - e.t_ns > IDADE_MAX_QUADRO_NS;
    const int borda = borda_falha_.atualizar(e.falha);
    const bool mudou_bateria = bateria_.avaliar(e.bateria);

    bool falha_nova = false, bateria_baixa = false, bateria_critica = false, recarga_ok = false;
    bool comunicacao_caiu = false, comunicacao_voltou = false;
    struct {
        EstadoRobo estado;
        ModoOperacao modo;
        bool bateria_baixa, parado;
        Missao missao;
    } c;
    {
        std::lock_guard<std::mutex> t(r_.mtx_nav);
        auto& ctx = r_.ctx;
        if (ctx.estado == EstadoRobo::INICIALIZANDO) ctx.estado = EstadoRobo::DISPONIVEL;
        if (borda > 0) {
            ctx.inibido = true;
            if (ctx.sem_comunicacao)
                ctx.estado_salvo = EstadoRobo::DEFEITO;
            else
                ctx.estado = EstadoRobo::DEFEITO;
            falha_nova = true;
        }
        if (quadro_velho && !ctx.sem_comunicacao) {
            ctx.sem_comunicacao = true;
            ctx.estado_salvo = ctx.estado;
            ctx.estado = EstadoRobo::SEM_COMUNICACAO;
            comunicacao_caiu = true;
        } else if (!quadro_velho && ctx.sem_comunicacao) {
            ctx.sem_comunicacao = false;
            ctx.estado = ctx.estado_salvo;
            comunicacao_voltou = true;
        }
        if (mudou_bateria) {
            switch (bateria_.nivel()) {
                case NivelBateria::BAIXA:
                    ctx.bateria_baixa = ctx.recarga_pendente = true;
                    ctx.destino_recarga = estacao_mais_proxima(pos);
                    bateria_baixa = true;
                    break;
                case NivelBateria::CRITICA:
                    ctx.bateria_baixa = ctx.bateria_critica = ctx.recarga_pendente = true;
                    ctx.destino_recarga = estacao_mais_proxima(pos);
                    bateria_critica = true;
                    break;
                case NivelBateria::NORMAL:
                    ctx.bateria_baixa = ctx.bateria_critica = ctx.recarga_pendente = false;
                    break;
            }
        }
        if (ctx.estado == EstadoRobo::RECARREGANDO && e.bateria >= r_.p.bateria_recarregada) {
            ctx.estado = EstadoRobo::DISPONIVEL;
            ctx.bateria_baixa = ctx.bateria_critica = ctx.recarga_pendente = false;
            recarga_ok = true;
        }
        c = {ctx.estado, ctx.modo, ctx.bateria_baixa, ctx.parado, ctx.missao};
    }

    // a equipe é abortada fora de mtx_nav: o mutex da equipe (N1) fica acima de mtx_nav (N3)
    if ((falha_nova || bateria_critica) && c.missao.ativa) {
        auto& eq = r_.frota->equipe(uint32_t(c.missao.equipe));
        TravaCompartilhada t(&eq.mtx);
        if (eq.epoca == c.missao.epoca && eq.estado != EstadoEquipe::LIVRE && eq.estado != EstadoEquipe::CONCLUIDA)
            eq.estado = EstadoEquipe::ABORTANDO;
    }

    const int equipe = c.missao.ativa ? c.missao.equipe : -1;
    if (falha_nova) {
        LOG("T_STATUS", "defeito detectado t_mono=%lld", (long long)agora);
        evento(TipoMsg::FALHA, pos, 0, equipe, "defeito detectado: robô parado");
    }
    if (comunicacao_caiu) {
        LOG("T_STATUS", "quadro de entrada com %lld ms: SEM_COMUNICACAO", (long long)((agora - e.t_ns) / NS_POR_MS));
        evento(TipoMsg::SEM_COMUNICACAO, pos, float((agora - e.t_ns) / NS_POR_MS), equipe, "sem comunicação com o hardware");
    }
    if (comunicacao_voltou) LOG("T_STATUS", "quadros normalizados: estado %s", nome_estado(c.estado));
    if (bateria_baixa) evento(TipoMsg::BATERIA_BAIXA, pos, float(e.bateria), equipe, "bateria baixa");
    if (bateria_critica) evento(TipoMsg::BATERIA_CRITICA, pos, float(e.bateria), equipe, "bateria crítica");
    if (recarga_ok) evento(TipoMsg::RECARGA_CONCLUIDA, pos, float(e.bateria), -1, "recarga concluída");

    MetricasTempoReal mnav = nav_ ? nav_->metricas() : MetricasTempoReal{};
    MetricasTempoReal mstatus = status_ ? status_->metricas() : MetricasTempoReal{};
    {
        TravaCompartilhada t(&r_.slot->mtx);
        auto& sl = *r_.slot;
        sl.estado = c.estado;
        sl.modo = c.modo;
        sl.x = float(e.x);
        sl.y = float(e.y);
        sl.sp_x = float(s.sp_x);
        sl.sp_y = float(s.sp_y);
        sl.bateria = float(e.bateria);
        sl.falha = e.falha;
        sl.atracada = e.atracada;
        sl.acoplar = s.acoplar;
        sl.bateria_baixa = c.bateria_baixa;
        sl.parado = c.parado;
        sl.distancia = distancia_;
        sl.equipe = equipe;
        sl.caixa = c.missao.ativa ? c.missao.caixa : -1;
        sl.heartbeat = ++heartbeat_;
        sl.t_atualizacao_ns = agora;
        sl.nav = mnav;
        sl.status = mstatus;
        sl.desvios = r_.desvios.load();
        sl.evitando = r_.evitando.load();
        if (morrer_no_ciclo_ && heartbeat_ == morrer_no_ciclo_) raise(SIGKILL);
        if (r_.cfg.rastrear_status)
            LOG("T_STATUS", "publicado hb=%llu estado=%s pos=(%.3f,%.3f) bat=%.3f", (unsigned long long)sl.heartbeat,
                nome_estado(sl.estado), sl.x, sl.y, sl.bateria);
    }

    if (!pronto_avisado_) {
        pronto_avisado_ = true;
        Mensagem m = r_.evento(TipoMsg::ROBO_PRONTO);
        emissor_.enviar(m);
        r_.registrar(Categoria::ROBO, "processo pronto: primeiro status publicado");
    }
}

}  // namespace amr
