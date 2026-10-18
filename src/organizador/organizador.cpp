#include "organizador.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>

#include "../comum/console.hpp"
#include "../comum/nomes.hpp"
#include "../robo/dominio.hpp"

namespace amr {

namespace {
constexpr const char* ORIGEM = "organizador";
constexpr int64_t ANTECEDENCIA_TGO_NS = 100 * NS_POR_MS;
constexpr int64_t SUSPEITO_NS = 300 * NS_POR_MS;
constexpr int64_t OFFLINE_NS = 1 * NS_POR_S;
constexpr int64_t PRAZO_ABORTO_NS = 5 * NS_POR_S;
constexpr int64_t PERMANENCIA_CONCLUIDA_NS = 1 * NS_POR_S;

CaixaOrg nova_caixa(const DefCaixa& d) {
    CaixaOrg c;
    c.def = d;
    return c;
}

std::string lista(const std::vector<int>& v) {
    std::string s;
    for (int x : v) s += (s.empty() ? "" : ",") + std::to_string(x);
    return s;
}
}  // namespace

const char* nome_situacao(Situacao s) {
    switch (s) {
        case Situacao::ATIVO: return "ATIVO";
        case Situacao::SUSPEITO: return "SUSPEITO";
        case Situacao::OFFLINE: return "OFFLINE";
        case Situacao::TERMINADO: return "TERMINADO";
    }
    return "?";
}

const char* nome_estado(EstadoCaixa e) {
    switch (e) {
        case EstadoCaixa::NA_FILA: return "NA_FILA";
        case EstadoCaixa::NO_PR: return "NO_PR";
        case EstadoCaixa::DESIGNADA: return "DESIGNADA";
        case EstadoCaixa::EM_TRANSPORTE: return "EM_TRANSPORTE";
        case EstadoCaixa::INTERROMPIDA: return "INTERROMPIDA";
        case EstadoCaixa::ENTREGUE: return "ENTREGUE";
    }
    return "?";
}

Organizador::Organizador(Cenario cenario, uint32_t n_max, std::string broker)
    : cenario_(std::move(cenario)), n_max_(n_max), broker_(std::move(broker)), mapa_(cenario_), despachante_(cenario_.parametros.envelhecimento_s) {
    criar_ipc();
    modelo_.equipes.resize(n_max_);
    for (auto& d : cenario_.caixas) modelo_.caixas.push_back(nova_caixa(d));
}

Organizador::~Organizador() {
    parar();
    remover_ipc();
}

void Organizador::criar_ipc() {
    // restos de uma execução interrompida são descartados
    MemoriaCompartilhada::remover(nomes::SHM_FROTA);
    MemoriaCompartilhada::remover(nomes::SHM_EVENTOS);
    FilaMensagens::remover(nomes::FILA_ORGANIZADOR);

    semaforos_ = SemaforosPassagem::criar(cenario_.capacidades());
    shm_frota_ = MemoriaCompartilhada::criar(nomes::SHM_FROTA, Frota::tamanho(n_max_, uint32_t(cenario_.passagens.size())));
    Frota::inicializar(shm_frota_.dados(), n_max_, cenario_.capacidades(), semaforos_.id());
    frota_ = std::make_unique<Frota>(shm_frota_.dados());

    shm_eventos_ = MemoriaCompartilhada::criar(nomes::SHM_EVENTOS, sizeof(BufferEventos));
    auto* buf = new (shm_eventos_.dados()) BufferEventos{};
    iniciar_mutex_compartilhado(&buf->mtx);
    buf->capacidade = CAPACIDADE_EVENTOS;
    vazios_ = SemaforoNomeado::criar(nomes::SEM_VAZIOS, CAPACIDADE_EVENTOS);
    itens_ = SemaforoNomeado::criar(nomes::SEM_ITENS, 0);

    entrada_ = FilaMensagens::criar(nomes::FILA_ORGANIZADOR, 64, sizeof(Mensagem), FilaMensagens::Acesso::LeituraEscrita);
    rastreador_ = std::make_unique<Rastreador>(Rastreador::Modo::ComPrazo);

    LOG("main", "IPC criado: N_max=%u, %zu passagens, fila de entrada com %ld mensagens", n_max_,
        cenario_.passagens.size(), entrada_.capacidade());
}

void Organizador::remover_ipc() {
    {
        std::lock_guard<std::mutex> t(mtx_modelo_);
        for (auto& [id, r] : modelo_.robos) FilaMensagens::remover(nomes::fila_comandos(id));
    }
    for (uint32_t s = 0; frota_ && s < n_max_; ++s) {
        int id = frota_->robo(s).id;
        if (id >= 0) FilaMensagens::remover(nomes::fila_comandos(id));
    }
    semaforos_.remover();
    FilaMensagens::remover(nomes::FILA_ORGANIZADOR);
    SemaforoNomeado::remover(nomes::SEM_VAZIOS);
    SemaforoNomeado::remover(nomes::SEM_ITENS);
    MemoriaCompartilhada::remover(nomes::SHM_EVENTOS);
    MemoriaCompartilhada::remover(nomes::SHM_FROTA);
}

void Organizador::iniciar() {
    ativo_ = true;
    auto& cab = frota_->cabecalho();
    cab.estado_sistema.store(uint32_t(EstadoSistema::OPERANDO));
    cab.pronto.store(1);
    registrar(registro(Categoria::SISTEMA, "sistema iniciado com N_max=%u", n_max_));

    t_evt_ = std::thread(&Organizador::laco_eventos, this);
    t_desp_ = std::make_unique<TarefaPeriodica>("T_ORG_DESP", 200 * NS_POR_MS, 60, [this](const InfoCiclo&) { ciclo_despacho(); });
    t_sup_ = std::make_unique<TarefaPeriodica>("T_ORG_SUP", 500 * NS_POR_MS, 55, [this](const InfoCiclo&) { ciclo_supervisao(); });
    t_desp_->iniciar();
    t_sup_->iniciar();
    if (!broker_.empty()) iniciar_hmi(broker_);
}

void Organizador::parar() {
    if (!ativo_.exchange(false)) return;
    frota_->cabecalho().estado_sistema.store(uint32_t(EstadoSistema::ENCERRANDO));
    parar_hmi();
    if (t_evt_.joinable()) t_evt_.join();
    if (t_desp_) t_desp_->parar();
    if (t_sup_) t_sup_->parar();
    registrar(registro(Categoria::SISTEMA, "sistema encerrado"));
    rastreador_->descarregar();
}

void Organizador::alarme(const char* origem, const char* formato, ...) {
    char texto[256];
    va_list args;
    va_start(args, formato);
    std::vsnprintf(texto, sizeof texto, formato, args);
    va_end(args);
    LOG(origem, "ALARME %s", texto);
    std::lock_guard<std::mutex> t(mtx_alarmes_);
    alarmes_.push_back({parede_ns(), texto});
    while (alarmes_.size() > 100) alarmes_.pop_front();
}

Registro Organizador::registro(Categoria cat, const char* formato, ...) {
    Registro r = novo_registro(ORIGEM, cat, "%s", "");
    va_list args;
    va_start(args, formato);
    std::vsnprintf(r.texto, sizeof r.texto, formato, args);
    va_end(args);
    return r;
}

std::vector<Organizador::FotoSlot> Organizador::fotografar_frota() {
    std::vector<FotoSlot> v(n_max_);
    for (uint32_t s = 0; s < n_max_; ++s) {
        auto& sl = frota_->robo(s);
        TravaCompartilhada t(&sl.mtx);
        if (t.dono_morreu()) {
            sl.suspeito = 1;
            LOG("organizador", "EOWNERDEAD no slot %u (robô %d): dono morreu com a trava; mutex recuperado e slot marcado SUSPEITO",
                s, sl.id);
        }
        v[s] = {sl.ocupado != 0, sl.id,  sl.epoca, sl.estado,     sl.modo,      {sl.x, sl.y},
                sl.bateria_baixa != 0, sl.falha != 0, sl.parado != 0, sl.heartbeat, sl.suspeito != 0, sl.bateria};
    }
    return v;
}

// ------------------------------------------------------------------ envio e efeitos

void Organizador::enviar_robo(int robo, const Mensagem& m, Efeitos& ef) {
    auto it = modelo_.robos.find(robo);
    if (it == modelo_.robos.end() || !it->second.fila) return;
    ef.envios.push_back({it->second.fila, robo, m});
}

void Organizador::aplicar(Efeitos& ef) {
    for (auto& e : ef.equipes) {
        auto& eq = frota_->equipe(uint32_t(e.indice));
        TravaCompartilhada t(&eq.mtx);
        if (e.inicializar) {
            eq.epoca = e.epoca;
            eq.caixa = e.caixa;
            eq.k = e.k;
            eq.lider = e.membros.front();
            for (int i = 0; i < MAX_MEMBROS; ++i) {
                eq.membros[i] = i < e.k ? e.membros[size_t(i)] : -1;
                eq.off_x[i] = i < e.k ? float(e.afastamentos[size_t(i)].x) : 0;
                eq.off_y[i] = i < e.k ? float(e.afastamentos[size_t(i)].y) : 0;
            }
            eq.rota_n = 0;
            eq.passo = 0;
            eq.mascara_chegada = 0;
            eq.mascara_atracado = 0;
        } else if (eq.epoca != e.epoca) {
            continue;
        }
        eq.estado = e.estado;
        if (e.t_go >= 0) eq.t_go_ns = e.t_go;
    }

    for (auto& [slot, epoca] : ef.slots_liberados) {
        auto& sl = frota_->robo(uint32_t(slot));
        TravaCompartilhada t(&sl.mtx);
        if (t.dono_morreu())
            LOG("T_ORG_EVT", "EOWNERDEAD no slot %d (robô %d): dono morreu com a trava; mutex recuperado ao liberar o slot",
                slot, sl.id);
        if (sl.epoca != epoca) continue;
        sl.ocupado = 0;
        sl.id = -1;
        sl.pid = 0;
        sl.equipe = -1;
        sl.caixa = -1;
        sl.suspeito = 0;
    }

    for (int robo : ef.remover_das_passagens) {
        for (uint32_t p = 0; p < frota_->n_passagens(); ++p) {
            auto& esp = frota_->passagem(p);
            int unidades = 0;
            {
                TravaCompartilhada t(&esp.mtx);
                for (int i = 0; i < MAX_OCUPANTES; ++i)
                    if (esp.ocupantes[i] == robo) {
                        unidades += esp.unidades[i];
                        esp.ocupantes[i] = -1;
                        esp.unidades[i] = 0;
                    }
                esp.ocupacao -= unidades;
                if (esp.esperador == robo) esp.esperador = -1;
            }
            if (unidades > 0) {
                semaforos_.liberar(int(p), unidades);
                LOG("T_ORG_EVT", "operador confirmou a remoção do robô %d: %d vaga(s) de %s devolvidas", robo, unidades,
                    cenario_.passagens[p].nome.c_str());
            }
        }
    }

    std::vector<std::pair<int, Mensagem>> falharam;
    for (auto& e : ef.envios)
        if (!e.fila->enviar(&e.msg, sizeof e.msg, prioridade(e.msg.cab.tipo), 0)) falharam.push_back({e.robo, e.msg});
    if (!falharam.empty()) {
        std::lock_guard<std::mutex> t(mtx_modelo_);
        for (auto& [robo, m] : falharam) {
            auto it = modelo_.robos.find(robo);
            if (it != modelo_.robos.end()) it->second.pendentes.push_back(m);
        }
    }
    rastreador_->descarregar();
}

// ------------------------------------------------------------------ equipes

void Organizador::abortar_equipe(int indice, const char* motivo, Efeitos& ef) {
    if (indice < 0 || indice >= int(modelo_.equipes.size())) return;
    auto& eq = modelo_.equipes[size_t(indice)];
    if (!eq.ativa || eq.estado == EstadoEquipe::ABORTANDO || eq.estado == EstadoEquipe::CONCLUIDA) return;
    const int64_t agora = agora_ns();
    eq.estado = EstadoEquipe::ABORTANDO;
    eq.t_aborto = agora;
    eq.t_go = agora + ANTECEDENCIA_TGO_NS;
    ef.equipes.push_back({indice, eq.epoca, EstadoEquipe::ABORTANDO, eq.t_go, false, 0, 0, {}, {}});

    for (auto& c : modelo_.caixas) {
        if (c.def.id != eq.caixa) continue;
        if (c.estado == EstadoCaixa::EM_TRANSPORTE) {
            c.estado = EstadoCaixa::INTERROMPIDA;
            c.interrupcoes++;
        } else if (c.estado == EstadoCaixa::DESIGNADA) {
            c.estado = c.interrupcoes > 0 ? EstadoCaixa::INTERROMPIDA : EstadoCaixa::NO_PR;
        }
        Registro r = registro(Categoria::CARGA, "caixa %d: transporte abortado (%s)", c.def.id, motivo);
        r.caixa = c.def.id;
        for (size_t i = 0; i < eq.membros.size() && i < 4; ++i) r.robos[i] = eq.membros[i];
        registrar(r);
    }
    alarme("T_ORG_EVT", "equipe %d abortada: %s (robôs %s)", indice, motivo, lista(eq.membros).c_str());
}

void Organizador::liberar_equipe(int indice, Efeitos& ef) {
    auto& eq = modelo_.equipes[size_t(indice)];
    for (int id : eq.membros) {
        auto it = modelo_.robos.find(id);
        if (it != modelo_.robos.end() && it->second.equipe == indice) it->second.equipe = -1;
    }
    for (auto& c : modelo_.caixas)
        if (c.equipe == indice) c.equipe = -1;
    ef.equipes.push_back({indice, eq.epoca, EstadoEquipe::LIVRE, -1, false, 0, 0, {}, {}});
    uint32_t epoca = eq.epoca;
    eq = EquipeOrg{};
    eq.epoca = epoca;
}

// ------------------------------------------------------------------ T_ORG_EVT

void Organizador::laco_eventos() {
    aplicar_prioridade(65, "T_ORG_EVT");
    Mensagem m;
    while (ativo_.load()) {
        if (!entrada_.receber(&m, sizeof m, NS_POR_S)) continue;
        Efeitos ef;
        {
            std::lock_guard<std::mutex> t(mtx_modelo_);
            modelo_.ocupacao_max_fila = std::max(modelo_.ocupacao_max_fila, entrada_.pendentes() + 1);
            tratar(m, ef);
        }
        aplicar(ef);
    }
}

void Organizador::tratar(const Mensagem& m, Efeitos& ef) {
    const int64_t agora = agora_ns();
    const auto& ev = m.evento;

    switch (m.cab.tipo) {
        case TipoMsg::NOVO_ROBO: {
            const auto& p = m.processo;
            RoboOrg r;
            r.id = p.robo;
            r.slot = p.slot;
            r.pid = p.pid;
            r.t_heartbeat = agora;
            try {
                r.fila = std::make_shared<FilaMensagens>(FilaMensagens::criar(
                    nomes::fila_comandos(p.robo), 16, sizeof(Mensagem), FilaMensagens::Acesso::Escrita));
            } catch (const std::exception& e) {
                LOG("T_ORG_EVT", "robô %d sem fila de comandos: %s", p.robo, e.what());
            }
            modelo_.robos[p.robo] = std::move(r);
            LOG("T_ORG_EVT", "NOVO_ROBO %d (pid %d, slot %d)", p.robo, p.pid, p.slot);
            Registro reg = registro(Categoria::SISTEMA, "robô %d criado (pid %d, slot %d)", p.robo, p.pid, p.slot);
            reg.robo = p.robo;
            registrar(reg);
            break;
        }
        case TipoMsg::ROBO_PRONTO: {
            auto it = modelo_.robos.find(ev.robo);
            if (it == modelo_.robos.end()) break;
            it->second.pronto = true;
            LOG("T_ORG_EVT", "robô %d pronto: entra no despacho", ev.robo);
            break;
        }
        case TipoMsg::ROBO_TERMINOU: {
            const auto& p = m.processo;
            auto it = modelo_.robos.find(p.robo);
            if (it == modelo_.robos.end()) break;
            auto& r = it->second;
            r.situacao = Situacao::TERMINADO;
            if (r.equipe >= 0) abortar_equipe(r.equipe, "processo do membro terminou", ef);
            ef.slots_liberados.push_back({r.slot, p.epoca});
            LOG("T_ORG_EVT", "ROBO_TERMINOU %d (pid %d, %s %d): retirado do despacho", p.robo, p.pid,
                p.codigo >= 128 ? "sinal" : "código", p.codigo >= 128 ? p.codigo - 128 : p.codigo);
            Registro reg = registro(Categoria::SISTEMA, "processo do robô %d terminou", p.robo);
            reg.robo = p.robo;
            registrar(reg);
            FilaMensagens::remover(nomes::fila_comandos(p.robo));
            modelo_.robos.erase(it);
            break;
        }
        case TipoMsg::FALHA:
        case TipoMsg::BATERIA_CRITICA:
        case TipoMsg::SEM_COMUNICACAO: {
            auto it = modelo_.robos.find(ev.robo);
            if (it == modelo_.robos.end()) break;
            if (m.cab.tipo == TipoMsg::FALHA) it->second.com_defeito = true;
            alarme("T_ORG_EVT", "%s do robô %d em (%.1f, %.1f)", nome_tipo(m.cab.tipo), ev.robo, ev.x, ev.y);
            if (it->second.equipe >= 0) abortar_equipe(it->second.equipe, nome_tipo(m.cab.tipo), ef);
            break;
        }
        case TipoMsg::BATERIA_BAIXA:
        case TipoMsg::RECARGA_CONCLUIDA:
            LOG("T_ORG_EVT", "%s do robô %d (%.1f%%)", nome_tipo(m.cab.tipo), ev.robo, ev.valor);
            break;
        case TipoMsg::EM_POSICAO: {
            if (ev.equipe < 0 || ev.equipe >= int(modelo_.equipes.size())) break;
            auto& eq = modelo_.equipes[size_t(ev.equipe)];
            if (!eq.ativa) break;
            if (ev.fase == Fase::ACOPLAMENTO && eq.estado == EstadoEquipe::FORMANDO) {
                auto pos = std::find(eq.membros.begin(), eq.membros.end(), ev.robo);
                if (pos == eq.membros.end()) break;
                eq.chegadas |= 1u << (pos - eq.membros.begin());
                if (eq.chegadas == (1u << eq.k) - 1) {
                    eq.estado = EstadoEquipe::REUNIDA;
                    eq.t_go = agora + ANTECEDENCIA_TGO_NS;
                    ef.equipes.push_back({ev.equipe, eq.epoca, EstadoEquipe::REUNIDA, eq.t_go, false, 0, 0, {}, {}});
                    LOG("T_ORG_EVT", "equipe %d reunida (%d robôs): carga liberada, t_go em 100 ms", ev.equipe, eq.k);
                }
            } else if (ev.fase == Fase::DESTINO && eq.estado == EstadoEquipe::TRANSPORTE) {
                eq.estado = EstadoEquipe::DESACOPLANDO;
                eq.t_go = agora + ANTECEDENCIA_TGO_NS;
                ef.equipes.push_back({ev.equipe, eq.epoca, EstadoEquipe::DESACOPLANDO, eq.t_go, false, 0, 0, {}, {}});
                LOG("T_ORG_EVT", "equipe %d no destino: desacoplamento em t_go", ev.equipe);
            }
            break;
        }
        case TipoMsg::ATRACADO: {
            if (ev.equipe < 0 || ev.equipe >= int(modelo_.equipes.size())) break;
            auto& eq = modelo_.equipes[size_t(ev.equipe)];
            if (!eq.ativa || eq.estado != EstadoEquipe::REUNIDA) break;
            auto pos = std::find(eq.membros.begin(), eq.membros.end(), ev.robo);
            if (pos == eq.membros.end()) break;
            eq.atracados |= 1u << (pos - eq.membros.begin());
            if (eq.atracados != (1u << eq.k) - 1) break;
            eq.estado = EstadoEquipe::TRANSPORTE;
            ef.equipes.push_back({ev.equipe, eq.epoca, EstadoEquipe::TRANSPORTE, -1, false, 0, 0, {}, {}});
            for (auto& c : modelo_.caixas) {
                if (c.def.id != eq.caixa) continue;
                bool na_baia = c.estado == EstadoCaixa::DESIGNADA && c.interrupcoes == 0;
                c.estado = EstadoCaixa::EM_TRANSPORTE;
                if (na_baia) modelo_.caixa_no_pr.erase(c.def.baia);
                Registro r = na_baia ? registro(Categoria::CARGA, "pegou carga na Baia %d", c.def.baia)
                                     : registro(Categoria::CARGA, "retomou carga interrompida em (%.1f, %.1f)", c.origem.x, c.origem.y);
                r.caixa = c.def.id;
                r.baia = c.def.baia;
                r.zona = c.def.zona;
                for (size_t i = 0; i < eq.membros.size() && i < 4; ++i) r.robos[i] = eq.membros[i];
                registrar(r);
                LOG("T_ORG_EVT", "equipe %d atracada: caixa %d em transporte", ev.equipe, c.def.id);
            }
            break;
        }
        case TipoMsg::ENTREGUE: {
            if (ev.equipe < 0 || ev.equipe >= int(modelo_.equipes.size())) break;
            auto& eq = modelo_.equipes[size_t(ev.equipe)];
            if (!eq.ativa || eq.estado != EstadoEquipe::DESACOPLANDO) break;
            eq.estado = EstadoEquipe::CONCLUIDA;
            eq.t_fim = agora;
            ef.equipes.push_back({ev.equipe, eq.epoca, EstadoEquipe::CONCLUIDA, -1, false, 0, 0, {}, {}});
            for (auto& c : modelo_.caixas) {
                if (c.def.id != eq.caixa) continue;
                c.estado = EstadoCaixa::ENTREGUE;
                c.origem = c.destino;
                c.equipe = -1;
                c.t_entregue = agora;
                modelo_.entregues++;
                modelo_.entregas.push_back({agora, double(agora - c.t_disponivel) / NS_POR_S});
                while (modelo_.entregas.size() > 500) modelo_.entregas.pop_front();
                Registro r = registro(Categoria::CARGA, "entregou caixa %d na Zona %d", c.def.id, c.def.zona);
                r.caixa = c.def.id;
                r.baia = c.def.baia;
                r.zona = c.def.zona;
                for (size_t i = 0; i < eq.membros.size() && i < 4; ++i) r.robos[i] = eq.membros[i];
                registrar(r);
                LOG("T_ORG_EVT", "caixa %d entregue na zona %d pela equipe %d (%s)", c.def.id, c.def.zona, ev.equipe,
                    lista(eq.membros).c_str());
            }
            break;
        }
        case TipoMsg::ABORTO_CONCLUIDO: {
            if (ev.equipe < 0 || ev.equipe >= int(modelo_.equipes.size())) break;
            auto& eq = modelo_.equipes[size_t(ev.equipe)];
            if (!eq.ativa || eq.estado != EstadoEquipe::ABORTANDO) break;
            eq.abortos_concluidos.insert(ev.robo);
            auto pos = std::find(eq.membros.begin(), eq.membros.end(), ev.robo);
            if (pos != eq.membros.end() && !eq.posicao_carga_conhecida) {
                const Ponto& off = eq.afastamentos[size_t(pos - eq.membros.begin())];
                eq.posicao_carga = {ev.x - off.x, ev.y - off.y};
                eq.posicao_carga_conhecida = true;
                for (auto& c : modelo_.caixas)
                    if (c.def.id == eq.caixa && c.estado == EstadoCaixa::INTERROMPIDA) c.origem = eq.posicao_carga;
            }
            break;
        }
        case TipoMsg::PASSAGEM_ESPERA:
            LOG("T_ORG_EVT", "robô %d aguardando vaga em %s", ev.robo,
                ev.extra >= 0 && ev.extra < int(cenario_.passagens.size()) ? cenario_.passagens[size_t(ev.extra)].nome.c_str() : "?");
            break;
        case TipoMsg::ROTA_TIMEOUT:
        case TipoMsg::SEM_ROTA:
        case TipoMsg::DEADLINE_NAV:
            alarme("T_ORG_EVT", "%s do robô %d", nome_tipo(m.cab.tipo), ev.robo);
            break;
        case TipoMsg::OPERADOR:
            tratar_operador(m.operador, ef);
            break;
        default:
            LOG("T_ORG_EVT", "mensagem inesperada %s", nome_tipo(m.cab.tipo));
            break;
    }
}

void Organizador::tratar_operador(const CorpoOperador& op, Efeitos& ef) {
    auto& cab = frota_->cabecalho();
    switch (op.comando) {
        case ComandoOperador::PARADA_GERAL:
            cab.parada_geral.store(1);
            cab.estado_sistema.store(uint32_t(EstadoSistema::PARADA_GERAL));
            alarme("T_ORG_EVT", "PARADA GERAL acionada pelo operador");
            registrar(registro(Categoria::OPERADOR, "parada geral acionada"));
            break;
        case ComandoOperador::LIBERAR_PARADA:
            cab.parada_geral.store(0);
            cab.estado_sistema.store(uint32_t(EstadoSistema::OPERANDO));
            LOG("T_ORG_EVT", "parada geral liberada");
            registrar(registro(Categoria::OPERADOR, "parada geral liberada"));
            break;
        case ComandoOperador::NOVA_CAIXA: {
            DefCaixa d{op.caixa, op.tamanho, op.baia, op.zona};
            if (d.id <= 0) {
                d.id = 1;
                for (auto& c : modelo_.caixas) d.id = std::max(d.id, c.def.id + 1);
            }
            bool repetida = std::any_of(modelo_.caixas.begin(), modelo_.caixas.end(),
                                        [&](const CaixaOrg& c) { return c.def.id == d.id; });
            if (repetida || d.tamanho < 1 || d.tamanho > MAX_MEMBROS || !cenario_.baia(d.baia) || !cenario_.zona(d.zona)) {
                LOG("T_ORG_EVT", "caixa %d recusada (id repetido ou baia/zona/tamanho inválidos)", d.id);
                break;
            }
            modelo_.caixas.push_back(nova_caixa(d));
            LOG("T_ORG_EVT", "caixa %d (tamanho %d) incluída na fila da Baia %d", d.id, d.tamanho, d.baia);
            Registro r = registro(Categoria::OPERADOR, "caixa %d incluída (tamanho %d, baia %d, zona %d)", d.id, d.tamanho, d.baia, d.zona);
            r.caixa = d.id;
            registrar(r);
            break;
        }
        case ComandoOperador::ENVIAR_AO_ROBO: {
            auto it = modelo_.robos.find(op.robo);
            if (it == modelo_.robos.end()) {
                LOG("T_ORG_EVT", "comando para robô desconhecido %d", op.robo);
                break;
            }
            if (op.tipo_robo == TipoMsg::ABORTAR && it->second.equipe >= 0)
                abortar_equipe(it->second.equipe, "comando do operador", ef);
            // passar para manual no meio de uma missão devolve a caixa ao despacho
            if (op.tipo_robo == TipoMsg::MODO && op.cmd.modo == ModoOperacao::MANUAL && it->second.equipe >= 0)
                abortar_equipe(it->second.equipe, "robô passou para o modo manual", ef);
            Mensagem m = nova_mensagem(op.tipo_robo, ORIGEM_ORGANIZADOR);
            m.comando = op.cmd;
            enviar_robo(op.robo, m, ef);
            LOG("T_ORG_EVT", "operador -> robô %d: %s", op.robo, nome_tipo(op.tipo_robo));
            Registro r = registro(Categoria::OPERADOR, "%s enviado ao robô %d", nome_tipo(op.tipo_robo), op.robo);
            r.robo = op.robo;
            registrar(r);
            break;
        }
        case ComandoOperador::REMOVER_ROBO:
            ef.remover_das_passagens.push_back(op.robo);
            break;
    }
}

// ------------------------------------------------------------------ T_ORG_DESP

void Organizador::abastecer_pontos_de_retirada() {
    const int64_t agora = agora_ns();
    for (auto& b : cenario_.baias) {
        if (modelo_.caixa_no_pr.count(b.id)) continue;
        for (auto& c : modelo_.caixas) {
            if (c.estado != EstadoCaixa::NA_FILA || c.def.baia != b.id) continue;
            c.estado = EstadoCaixa::NO_PR;
            c.origem = b.pr;
            c.t_pronta = agora;
            c.t_disponivel = agora;
            modelo_.caixa_no_pr[b.id] = c.def.id;
            Registro r = registro(Categoria::CARGA, "caixa %d disponibilizada na Baia %d", c.def.id, b.id);
            r.caixa = c.def.id;
            r.baia = b.id;
            registrar(r);
            break;
        }
    }
}

void Organizador::ciclo_despacho() {
    const auto fotos = fotografar_frota();
    const int64_t agora = agora_ns();
    const double d = cenario_.parametros.afastamento_formacao;
    Efeitos ef;
    {
        std::lock_guard<std::mutex> t(mtx_modelo_);
        for (auto& [id, r] : modelo_.robos) {
            while (!r.pendentes.empty() && r.fila) {
                auto& m = r.pendentes.front();
                if (!r.fila->enviar(&m, sizeof m, prioridade(m.cab.tipo), 0)) break;
                r.pendentes.pop_front();
            }
        }
        abastecer_pontos_de_retirada();

        std::vector<RoboLivre> livres;
        int operacionais = 0;
        for (auto& [id, r] : modelo_.robos) {
            if (!r.pronto || r.situacao != Situacao::ATIVO || r.slot < 0) continue;
            const auto& f = fotos[size_t(r.slot)];
            if (!f.ocupado || f.id != id) continue;
            bool funcional = !f.falha && f.estado != EstadoRobo::DEFEITO && f.estado != EstadoRobo::SEM_COMUNICACAO;
            if (funcional) operacionais++;
            if (funcional && r.equipe < 0 && f.estado == EstadoRobo::DISPONIVEL && f.modo == ModoOperacao::AUTO &&
                !f.bateria_baixa && !f.parado)
                livres.push_back({id, f.pos, f.bateria});
        }

        std::vector<CaixaPronta> prontas;
        for (auto& c : modelo_.caixas)
            if ((c.estado == EstadoCaixa::NO_PR || c.estado == EstadoCaixa::INTERROMPIDA) && c.equipe < 0)
                prontas.push_back({c.def.id, c.def.tamanho, c.origem, double(agora - c.t_pronta) / NS_POR_S});

        // custo real: caminho na grade (BFS a partir da caixa) mais uma penalidade por bateria gasta
        std::map<int, std::vector<int>> campos;
        for (auto& c : prontas) campos[c.id] = mapa_.distancias(c.origem);
        auto custo = [&](const RoboLivre& r, const CaixaPronta& c) {
            int celulas = mapa_.distancia_celulas(campos[c.id], r.posicao);
            double caminho = celulas >= 0 ? celulas : 1e6;
            return caminho + (100.0 - r.bateria) * 0.2;
        };
        ResultadoDespacho res = despachante_.decidir(prontas, livres, operacionais, custo);

        for (auto& c : modelo_.caixas) {
            bool aguarda = operacionais > 0 &&
                           std::find(res.aguardando_frota.begin(), res.aguardando_frota.end(), c.def.id) != res.aguardando_frota.end();
            if (aguarda && (!c.aguardando_frota || c.frota_no_alarme != operacionais)) {
                c.frota_no_alarme = operacionais;
                alarme("T_ORG_DESP", "caixa %d aguarda frota: exige %d robôs, há %d operacionais", c.def.id,
                    c.def.tamanho, operacionais);
                Registro r = registro(Categoria::CARGA, "caixa %d aguardando frota (tamanho %d, %d operacionais)",
                                      c.def.id, c.def.tamanho, operacionais);
                r.caixa = c.def.id;
                registrar(r);
            }
            c.aguardando_frota = aguarda;
        }

        for (auto& des : res.designacoes) {
            auto livre = std::find_if(modelo_.equipes.begin(), modelo_.equipes.end(), [](const EquipeOrg& e) { return !e.ativa; });
            if (livre == modelo_.equipes.end()) break;
            int indice = int(livre - modelo_.equipes.begin());
            auto caixa = std::find_if(modelo_.caixas.begin(), modelo_.caixas.end(),
                                      [&](const CaixaOrg& c) { return c.def.id == des.caixa; });
            const Zona* zona = cenario_.zona(caixa->def.zona);

            auto& eq = *livre;
            eq.ativa = true;
            eq.epoca++;
            eq.estado = EstadoEquipe::FORMANDO;
            eq.caixa = des.caixa;
            eq.k = int(des.robos.size());
            eq.membros = des.robos;
            // cada membro recebe o ponto de acoplamento do lado de onde vem: testa as permutações
            // (no máximo 4! = 24) e fica com a de menor soma de distâncias, para ninguém cruzar a carga
            std::vector<int> perm(size_t(eq.k));
            for (int i = 0; i < eq.k; ++i) perm[size_t(i)] = i;
            std::vector<int> melhor = perm;
            double menor = 1e18;
            do {
                double soma = 0;
                for (int i = 0; i < eq.k; ++i) {
                    Ponto off = afastamento_formacao(eq.k, perm[size_t(i)], d);
                    const auto& f = fotos[size_t(modelo_.robos[eq.membros[size_t(i)]].slot)];
                    soma += distancia(f.pos, {caixa->origem.x + off.x, caixa->origem.y + off.y});
                }
                if (soma < menor) {
                    menor = soma;
                    melhor = perm;
                }
            } while (std::next_permutation(perm.begin(), perm.end()));
            eq.afastamentos.clear();
            for (int i = 0; i < eq.k; ++i) eq.afastamentos.push_back(afastamento_formacao(eq.k, melhor[size_t(i)], d));
            eq.t_criacao = agora;
            caixa->estado = EstadoCaixa::DESIGNADA;
            caixa->equipe = indice;
            // quatro pontos de descarga por zona, em rodízio, para equipes seguidas não pararem umas sobre as outras
            {
                int n = modelo_.descargas[zona->id]++ % 4;
                caixa->destino = {zona->ponto.x + (n % 2 ? 0.75 : -0.75), zona->ponto.y + (n / 2 ? 0.75 : -0.75)};
            }
            caixa->robos = eq.membros;
            ef.equipes.push_back({indice, eq.epoca, EstadoEquipe::FORMANDO, 0, true, eq.caixa, eq.k, eq.membros, eq.afastamentos});

            for (int i = 0; i < eq.k; ++i) {
                int id = eq.membros[size_t(i)];
                modelo_.robos[id].equipe = indice;
                Mensagem m = nova_mensagem(TipoMsg::MISSAO_EQUIPE, ORIGEM_ORGANIZADOR);
                auto& mi = m.missao;
                mi.equipe = indice;
                mi.epoca = eq.epoca;
                mi.papel = i == 0 ? Papel::LIDER : Papel::MEMBRO;
                mi.indice = i;
                mi.caixa = eq.caixa;
                mi.k = eq.k;
                mi.off_x = float(eq.afastamentos[size_t(i)].x);
                mi.off_y = float(eq.afastamentos[size_t(i)].y);
                mi.pr_x = float(caixa->origem.x);
                mi.pr_y = float(caixa->origem.y);
                mi.zona_x = float(caixa->destino.x);
                mi.zona_y = float(caixa->destino.y);
                mi.zona = zona->id;
                enviar_robo(id, m, ef);
            }
            LOG("T_ORG_DESP", "caixa %d (tamanho %d) -> equipe %d, robôs %s (líder %d)", eq.caixa, eq.k, indice,
                lista(eq.membros).c_str(), eq.membros.front());
            Registro r = registro(Categoria::CARGA, "caixa %d designada à equipe %d", eq.caixa, indice);
            r.caixa = eq.caixa;
            r.baia = caixa->def.baia;
            r.zona = caixa->def.zona;
            for (int i = 0; i < eq.k && i < 4; ++i) r.robos[i] = eq.membros[size_t(i)];
            registrar(r);
        }
        if (res.reserva >= 0 && res.reserva != ultima_reserva_)
            LOG("T_ORG_DESP", "reserva progressiva: robôs livres retidos para a caixa %d", res.reserva);
        ultima_reserva_ = res.reserva;
    }
    aplicar(ef);
}

// ------------------------------------------------------------------ T_ORG_SUP

void Organizador::ciclo_supervisao() {
    const auto fotos = fotografar_frota();
    const int64_t agora = agora_ns();
    const auto& p = cenario_.parametros;

    struct Espera {
        int esperador;
        int64_t desde;
    };
    std::vector<Espera> esperas;
    for (uint32_t i = 0; i < frota_->n_passagens(); ++i) {
        auto& esp = frota_->passagem(i);
        TravaCompartilhada t(&esp.mtx);
        bool viva = agora - esp.ultima_tentativa_ns < 300 * NS_POR_MS;
        esperas.push_back({viva ? esp.esperador : -1, esp.espera_desde_ns});
    }

    Efeitos ef;
    {
        std::lock_guard<std::mutex> t(mtx_modelo_);
        for (auto& [id, r] : modelo_.robos) {
            if (!r.pronto || r.situacao == Situacao::TERMINADO) continue;
            const auto& f = fotos[size_t(r.slot)];
            if (f.suspeito && r.situacao == Situacao::ATIVO) {
                r.situacao = Situacao::SUSPEITO;
                LOG("T_ORG_SUP", "robô %d SUSPEITO: slot %d recuperado após a morte do dono", id, r.slot);
                continue;
            }
            if (f.heartbeat != r.heartbeat) {
                r.heartbeat = f.heartbeat;
                r.t_heartbeat = agora;
                if (r.situacao != Situacao::ATIVO) {
                    LOG("T_ORG_SUP", "robô %d voltou a publicar status", id);
                    r.situacao = Situacao::ATIVO;
                }
                continue;
            }
            int64_t idade = agora - r.t_heartbeat;
            Situacao nova = idade > OFFLINE_NS ? Situacao::OFFLINE : idade > SUSPEITO_NS ? Situacao::SUSPEITO : r.situacao;
            if (nova != r.situacao) {
                r.situacao = nova;
                alarme("T_ORG_SUP", "robô %d %s (heartbeat parado há %lld ms)", id, nome_situacao(nova),
                    (long long)(idade / NS_POR_MS));
                if (nova == Situacao::OFFLINE && r.equipe >= 0) abortar_equipe(r.equipe, "membro sem heartbeat", ef);
            }
        }

        for (size_t i = 0; i < modelo_.equipes.size(); ++i) {
            auto& eq = modelo_.equipes[i];
            if (!eq.ativa) continue;
            if (eq.estado == EstadoEquipe::FORMANDO && agora - eq.t_criacao > int64_t(p.prazo_reuniao_s * NS_POR_S)) {
                abortar_equipe(int(i), "prazo de reunião excedido", ef);
            } else if (eq.estado == EstadoEquipe::REUNIDA && agora - eq.t_go > int64_t(p.prazo_acoplamento_s * NS_POR_S)) {
                abortar_equipe(int(i), "prazo de acoplamento excedido", ef);
            } else if (eq.estado == EstadoEquipe::ABORTANDO) {
                bool todos = true;
                for (int id : eq.membros) {
                    auto it = modelo_.robos.find(id);
                    bool precisa = it != modelo_.robos.end() && !it->second.com_defeito && it->second.situacao == Situacao::ATIVO;
                    if (precisa && !eq.abortos_concluidos.count(id)) todos = false;
                }
                if (todos || agora - eq.t_aborto > PRAZO_ABORTO_NS) {
                    for (auto& c : modelo_.caixas)
                        if (c.def.id == eq.caixa && c.estado == EstadoCaixa::INTERROMPIDA) {
                            LOG("T_ORG_SUP", "caixa %d interrompida em (%.1f, %.1f): volta ao despacho", c.def.id, c.origem.x, c.origem.y);
                            Registro r = registro(Categoria::CARGA, "caixa %d interrompida em (%.1f, %.1f), aguardando nova equipe",
                                                  c.def.id, c.origem.x, c.origem.y);
                            r.caixa = c.def.id;
                            registrar(r);
                        }
                    liberar_equipe(int(i), ef);
                }
            } else if (eq.estado == EstadoEquipe::CONCLUIDA && agora - eq.t_fim > PERMANENCIA_CONCLUIDA_NS) {
                liberar_equipe(int(i), ef);
            }
        }

        auto& avisados = esperas_avisadas_;
        avisados.resize(esperas.size(), -1);
        for (size_t i = 0; i < esperas.size(); ++i) {
            auto& e = esperas[i];
            if (e.esperador >= 0 && agora - e.desde > int64_t(p.espera_rota_alternativa_s * NS_POR_S)) {
                if (avisados[i] != e.esperador)
                    alarme("T_ORG_SUP", "robô %d parado há mais de %.0f s na entrada de %s", e.esperador,
                        p.espera_rota_alternativa_s, cenario_.passagens[i].nome.c_str());
                avisados[i] = e.esperador;
            } else if (e.esperador < 0) {
                avisados[i] = -1;
            }
        }

        if (agora - ultimo_resumo_ > 10 * NS_POR_S) {
            ultimo_resumo_ = agora;
            int ativos = 0, pendentes = 0;
            for (auto& [id, r] : modelo_.robos) ativos += r.situacao == Situacao::ATIVO;
            for (auto& c : modelo_.caixas) pendentes += c.estado != EstadoCaixa::ENTREGUE;
            LOG("T_ORG_SUP", "resumo: %d robôs ativos, %llu caixas entregues, %d pendentes, fila de entrada máx %ld, perdas de registro %llu",
                ativos, (unsigned long long)modelo_.entregues, pendentes, modelo_.ocupacao_max_fila,
                (unsigned long long)static_cast<BufferEventos*>(shm_eventos_.dados())->perdas.load());
        }
    }
    aplicar(ef);
}

}  // namespace amr
