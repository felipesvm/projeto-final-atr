#include "frota.hpp"

#include <new>
#include <stdexcept>

#include "ipc.hpp"

namespace amr {

namespace {
size_t alinhar(size_t v) { return (v + 63) & ~size_t(63); }

struct Deslocamentos {
    size_t robos, equipes, passagens, total;
};

Deslocamentos calcular(uint32_t n_max, uint32_t n_passagens) {
    Deslocamentos d{};
    d.robos = alinhar(sizeof(CabecalhoFrota));
    d.equipes = d.robos + alinhar(sizeof(SlotRobo) * n_max);
    d.passagens = d.equipes + alinhar(sizeof(SlotEquipe) * n_max);
    d.total = d.passagens + alinhar(sizeof(EspelhoPassagem) * n_passagens);
    return d;
}
}  // namespace

const char* nome_estado(EstadoRobo e) {
    switch (e) {
        case EstadoRobo::INICIALIZANDO: return "INICIALIZANDO";
        case EstadoRobo::DISPONIVEL: return "DISPONIVEL";
        case EstadoRobo::DESLOCANDO: return "DESLOCANDO";
        case EstadoRobo::AGUARDANDO_PASSAGEM: return "AGUARDANDO_PASSAGEM";
        case EstadoRobo::AGUARDANDO_EQUIPE: return "AGUARDANDO_EQUIPE";
        case EstadoRobo::ACOPLANDO: return "ACOPLANDO";
        case EstadoRobo::TRANSPORTANDO: return "TRANSPORTANDO";
        case EstadoRobo::DESACOPLANDO: return "DESACOPLANDO";
        case EstadoRobo::ABORTANDO: return "ABORTANDO";
        case EstadoRobo::DESLOCANDO_RECARGA: return "DESLOCANDO_RECARGA";
        case EstadoRobo::RECARREGANDO: return "RECARREGANDO";
        case EstadoRobo::DEFEITO: return "DEFEITO";
        case EstadoRobo::SEM_COMUNICACAO: return "SEM_COMUNICACAO";
    }
    return "?";
}

const char* nome_estado(EstadoEquipe e) {
    switch (e) {
        case EstadoEquipe::LIVRE: return "LIVRE";
        case EstadoEquipe::FORMANDO: return "FORMANDO";
        case EstadoEquipe::REUNIDA: return "REUNIDA";
        case EstadoEquipe::ACOPLANDO: return "ACOPLANDO";
        case EstadoEquipe::TRANSPORTE: return "TRANSPORTE";
        case EstadoEquipe::DESACOPLANDO: return "DESACOPLANDO";
        case EstadoEquipe::ABORTANDO: return "ABORTANDO";
        case EstadoEquipe::CONCLUIDA: return "CONCLUIDA";
    }
    return "?";
}

size_t Frota::tamanho(uint32_t n_max, uint32_t n_passagens) { return calcular(n_max, n_passagens).total; }

void Frota::inicializar(void* base, uint32_t n_max, const std::vector<int>& capacidades, int semid) {
    auto n_pass = uint32_t(capacidades.size());
    auto d = calcular(n_max, n_pass);
    auto* bytes = static_cast<char*>(base);

    auto* cab = new (bytes) CabecalhoFrota{};
    cab->versao = VERSAO_LEIAUTE;
    cab->n_max = n_max;
    cab->n_passagens = n_pass;
    cab->semid = semid;
    cab->t_inicio_ns = agora_ns();
    cab->parada_geral.store(0);
    cab->estado_sistema.store(uint32_t(EstadoSistema::INICIANDO));
    cab->pronto.store(0);
    cab->anticolisao.store(1);
    cab->rotas_adaptativas.store(1);

    auto* robos = reinterpret_cast<SlotRobo*>(bytes + d.robos);
    for (uint32_t i = 0; i < n_max; ++i) {
        new (&robos[i]) SlotRobo{};
        iniciar_mutex_compartilhado(&robos[i].mtx);
        robos[i].id = -1;
        robos[i].equipe = -1;
        robos[i].caixa = -1;
    }
    auto* equipes = reinterpret_cast<SlotEquipe*>(bytes + d.equipes);
    for (uint32_t i = 0; i < n_max; ++i) {
        new (&equipes[i]) SlotEquipe{};
        iniciar_mutex_compartilhado(&equipes[i].mtx);
        equipes[i].lider = -1;
        equipes[i].caixa = -1;
    }
    auto* passagens = reinterpret_cast<EspelhoPassagem*>(bytes + d.passagens);
    for (uint32_t i = 0; i < n_pass; ++i) {
        new (&passagens[i]) EspelhoPassagem{};
        iniciar_mutex_compartilhado(&passagens[i].mtx);
        passagens[i].capacidade = capacidades[i];
        passagens[i].esperador = -1;
        for (auto& o : passagens[i].ocupantes) o = -1;
    }
}

Frota::Frota(void* base) {
    auto* bytes = static_cast<char*>(base);
    cab_ = reinterpret_cast<CabecalhoFrota*>(bytes);
    if (cab_->versao != VERSAO_LEIAUTE) throw std::runtime_error("versão do leiaute de /amr_frota incompatível");
    auto d = calcular(cab_->n_max, cab_->n_passagens);
    robos_ = reinterpret_cast<SlotRobo*>(bytes + d.robos);
    equipes_ = reinterpret_cast<SlotEquipe*>(bytes + d.equipes);
    passagens_ = reinterpret_cast<EspelhoPassagem*>(bytes + d.passagens);
}

}  // namespace amr
