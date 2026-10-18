#pragma once

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "../comum/cenario.hpp"
#include "../comum/frota.hpp"
#include "../comum/ipc.hpp"
#include "../comum/protocolo.hpp"

namespace amr {

enum class Situacao { ATIVO, SUSPEITO, OFFLINE, TERMINADO };
const char* nome_situacao(Situacao s);

struct RoboOrg {
    int id = -1;
    int slot = -1;
    int pid = 0;
    bool pronto = false;
    bool com_defeito = false;
    Situacao situacao = Situacao::ATIVO;
    uint64_t heartbeat = 0;
    int64_t t_heartbeat = 0;
    int equipe = -1;
    std::shared_ptr<FilaMensagens> fila;
    std::deque<Mensagem> pendentes;
};

enum class EstadoCaixa { NA_FILA, NO_PR, DESIGNADA, EM_TRANSPORTE, INTERROMPIDA, ENTREGUE };
const char* nome_estado(EstadoCaixa e);

struct CaixaOrg {
    DefCaixa def;
    EstadoCaixa estado = EstadoCaixa::NA_FILA;
    Ponto origem;
    Ponto destino;
    int64_t t_pronta = 0;
    int64_t t_disponivel = 0;
    int64_t t_entregue = 0;
    int equipe = -1;
    bool aguardando_frota = false;
    int frota_no_alarme = 0;
    int interrupcoes = 0;
    std::vector<int> robos;  // equipe que pegou a caixa por último
};

struct EquipeOrg {
    bool ativa = false;
    uint32_t epoca = 0;
    EstadoEquipe estado = EstadoEquipe::LIVRE;
    int caixa = -1;
    int k = 0;
    std::vector<int> membros;
    std::vector<Ponto> afastamentos;
    uint32_t chegadas = 0;
    uint32_t atracados = 0;
    std::set<int> abortos_concluidos;
    bool posicao_carga_conhecida = false;
    Ponto posicao_carga;
    int64_t t_criacao = 0, t_go = 0, t_aborto = 0, t_fim = 0;
};

// Alterações em memória compartilhada decididas sob mtx_modelo (N3) e aplicadas depois de soltá-lo,
// porque os mutexes de equipe (N1) e de slot (N2) ficam acima na hierarquia.
struct Efeitos {
    struct Equipe {
        int indice;
        uint32_t epoca;
        EstadoEquipe estado;
        int64_t t_go;
        bool inicializar;
        int caixa, k;
        std::vector<int> membros;
        std::vector<Ponto> afastamentos;
    };
    struct Envio {
        std::shared_ptr<FilaMensagens> fila;
        int robo;
        Mensagem msg;
    };
    std::vector<Equipe> equipes;
    std::vector<Envio> envios;
    std::vector<std::pair<int, uint32_t>> slots_liberados;
    std::vector<int> remover_das_passagens;
};

struct Modelo {
    std::map<int, RoboOrg> robos;
    std::vector<CaixaOrg> caixas;
    std::vector<EquipeOrg> equipes;
    std::map<int, int> caixa_no_pr;  // baia -> caixa
    std::map<int, int> descargas;    // zona -> próximo ponto de descarga
    uint64_t entregues = 0;
    std::deque<std::pair<int64_t, double>> entregas;  // instante e lead time de cada entrega
    long ocupacao_max_fila = 0;
};

}  // namespace amr
