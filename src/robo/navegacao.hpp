#pragma once

#include <map>
#include <vector>

#include "../comum/tarefa_periodica.hpp"
#include "dominio.hpp"
#include "nucleo.hpp"

namespace amr {

// T_NAV — Comando de Navegação (50 ms). Nenhuma chamada desta classe bloqueia:
// filas com prazo zero, semop com IPC_NOWAIT e travas curtas no padrão copiar-e-soltar.
class Navegacao {
public:
    explicit Navegacao(NucleoRobo& r);
    void ciclo(const InfoCiclo& info);

private:
    enum class Objetivo { NENHUM, ACOPLAMENTO, MANUAL, RECARGA, ESTACIONAR };
    enum class Andamento { EM_CURSO, CHEGOU, FALHOU };

    struct Posse {
        int passagem;
        int unidades;
        std::vector<int> slots;  // robôs que precisam sair da passagem para liberá-la
        bool entrou = false;
    };

    struct VisaoContexto {
        EstadoRobo estado;
        ModoOperacao modo;
        bool inibido, parado, sem_comunicacao, bateria_critica, recarga_pendente, encerrar;
        Missao missao;
    };

    VisaoContexto ler_contexto();
    bool mudar_estado(EstadoRobo de, EstadoRobo para);
    void processar_comandos(const VisaoContexto& c, const Ponto& pos);
    void avaliar_prazo(const InfoCiclo& info);

    void pedir_rota(const Ponto& origem, const Ponto& destino, OpcoesRota op, Finalidade f);
    bool rota_nova(Rota& rota, bool& ok);
    bool verificar_rota(const Ponto& pos, Rota& rota, bool& ok);
    Andamento seguir(const Ponto& pos, Saidas& s, bool segurar, EstadoRobo estado);

    bool tentar_passagens(const std::vector<int>& passagens, int k, const std::vector<int>& slots);
    bool possui(int passagem) const;
    void liberar_passagens(bool abandonar_nao_iniciadas);
    void desistir_espera();

    void publicar_rota_propria();
    void aplicar_anticolisao(const VisaoContexto& c, const Ponto& pos, Saidas& s);
    void iniciar_missao(const Missao& m, const Ponto& pos);
    void iniciar_recarga(const Ponto& pos, EstadoRobo de);
    void encerrar_missao(EstadoRobo de, EstadoRobo para);

    void ciclo_equipe(const VisaoContexto& c, const Entradas& e, Saidas& s, bool segurar);
    void transportar(const VisaoContexto& c, const SlotEquipe& eq, const Ponto& pos, Saidas& s, bool segurar);
    SlotEquipe ler_equipe(const Missao& m, bool& valida);
    std::vector<int> slots_dos_membros(const SlotEquipe& eq);
    Ponto posicao_do_slot(int slot);

    NucleoRobo& r_;
    Emissor emissor_;
    SeguidorRota seguidor_;
    uint32_t versao_aplicada_ = 0;
    Finalidade finalidade_rota_ = Finalidade::PROPRIA;
    int64_t t_pedido_ = 0;
    bool timeout_rota_avisado_ = false;
    bool aguardando_rota_ = false;
    Objetivo objetivo_ = Objetivo::NENHUM;
    Ponto destino_;

    std::vector<Posse> posses_;
    int passagem_espera_ = -1;
    int64_t espera_desde_ = 0;
    bool espera_avisada_ = false;
    std::vector<int> passagens_evitadas_;

    Saidas saida_;
    int64_t ocioso_desde_ = 0;
    Ponto vaga_estacionamento() const;
    bool congelado_prazo_ = false;
    bool segurando_ = false;

    bool atracado_marcado_ = false;
    bool solto_marcado_ = false;
    bool destino_avisado_ = false;
    bool entregue_avisado_ = false;
    bool aborto_avisado_ = false;
    std::vector<int> slots_equipe_;
    uint32_t versao_rota_equipe_ = 0;
    int passo_visivel_ = -1;

    Anticolisao anticolisao_;
    std::vector<Vizinho> vizinhos_;
    struct Historico {
        Ponto pos;
        Ponto vel;
        int64_t t = 0;
    };
    std::map<int, Historico> historico_;
    bool desviando_ = false;
    Ponto desvio_;
    int64_t desvio_ate_ = 0;
    int obstaculo_desvio_ = -1;
    int lado_desvio_ = 0;
    int64_t lado_valido_ate_ = 0;
    int64_t ultimo_replanejamento_ = 0;
    bool na_entrada_ = false;
    Ponto acostamento_;
};

}  // namespace amr
