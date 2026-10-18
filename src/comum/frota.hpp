#pragma once

#include <pthread.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "protocolo.hpp"

namespace amr {

constexpr uint32_t VERSAO_LEIAUTE = 1;
constexpr int MAX_MEMBROS = 4;
constexpr int MAX_ROTA = 128;
constexpr int MAX_OCUPANTES = 8;

enum class EstadoRobo : uint8_t {
    INICIALIZANDO,
    DISPONIVEL,
    DESLOCANDO,
    AGUARDANDO_PASSAGEM,
    AGUARDANDO_EQUIPE,
    ACOPLANDO,
    TRANSPORTANDO,
    DESACOPLANDO,
    ABORTANDO,
    DESLOCANDO_RECARGA,
    RECARREGANDO,
    DEFEITO,
    SEM_COMUNICACAO,
};

enum class EstadoEquipe : uint8_t {
    LIVRE,
    FORMANDO,
    REUNIDA,
    ACOPLANDO,
    TRANSPORTE,
    DESACOPLANDO,
    ABORTANDO,
    CONCLUIDA,
};

enum class EstadoSistema : uint32_t { INICIANDO, OPERANDO, PARADA_GERAL, ENCERRANDO };

const char* nome_estado(EstadoRobo e);
const char* nome_estado(EstadoEquipe e);

struct MetricasTempoReal {
    uint64_t ciclos;
    uint64_t perdas_prazo;
    uint32_t execucao_max_us;
    uint32_t atraso_max_us;
};

// Um slot por robô. Escritor único (a T_STATUS do robô dono); o gestor só mexe em ocupado/pid/época.
struct alignas(64) SlotRobo {
    pthread_mutex_t mtx;
    uint8_t ocupado;
    uint8_t suspeito;
    uint32_t epoca;
    int32_t id;
    int32_t pid;
    EstadoRobo estado;
    ModoOperacao modo;
    uint8_t falha;
    uint8_t atracada;
    uint8_t acoplar;
    uint8_t bateria_baixa;
    uint8_t parado;
    float x, y;
    float sp_x, sp_y;
    float bateria;
    double distancia;
    int32_t equipe;
    int32_t caixa;
    uint64_t heartbeat;
    int64_t t_atualizacao_ns;
    MetricasTempoReal nav;
    MetricasTempoReal status;
    uint32_t desvios;  // vezes em que a anticolisão segurou ou desviou o robô
    uint8_t evitando;
};

// Uma equipe por caixa em transporte. O organizador escreve estado e t_go;
// os membros escrevem as máscaras e o líder escreve a rota da carga.
struct alignas(64) SlotEquipe {
    pthread_mutex_t mtx;
    EstadoEquipe estado;
    uint32_t epoca;
    int32_t caixa;
    int32_t k;
    int32_t lider;
    int32_t membros[MAX_MEMBROS];
    float off_x[MAX_MEMBROS], off_y[MAX_MEMBROS];
    int32_t rota_n;
    uint32_t rota_versao;
    int32_t passo;
    uint32_t mascara_chegada;
    uint32_t mascara_atracado;
    int64_t t_go_ns;
    float rota_x[MAX_ROTA], rota_y[MAX_ROTA];
    int8_t rota_passagem[MAX_ROTA];
};

// Espelho informativo das passagens; quem manda é o semáforo System V.
struct alignas(64) EspelhoPassagem {
    pthread_mutex_t mtx;
    int32_t capacidade;
    int32_t ocupacao;
    int32_t ocupacao_max;
    int32_t ocupantes[MAX_OCUPANTES];
    int32_t unidades[MAX_OCUPANTES];
    int32_t esperador;
    int32_t esperador_k;
    int64_t espera_desde_ns;
    int64_t ultima_tentativa_ns;
    uint64_t entradas;
    float espera_media_s;  // média móvel das esperas, usada como custo pelo planejador
    uint32_t esperas;
};

struct alignas(64) CabecalhoFrota {
    uint32_t versao;
    uint32_t n_max;
    uint32_t n_passagens;
    int32_t semid;
    int64_t t_inicio_ns;
    std::atomic<uint32_t> parada_geral;
    std::atomic<uint32_t> estado_sistema;
    std::atomic<uint32_t> pronto;
    std::atomic<uint32_t> anticolisao;
    std::atomic<uint32_t> rotas_adaptativas;
};

static_assert(std::atomic<uint32_t>::is_always_lock_free, "atômicos usados entre processos precisam ser livres de trava");

// Acesso tipado ao segmento /amr_frota. O tamanho depende de N_max, lido do cabeçalho.
class Frota {
public:
    static size_t tamanho(uint32_t n_max, uint32_t n_passagens);
    static void inicializar(void* base, uint32_t n_max, const std::vector<int>& capacidades, int semid);

    explicit Frota(void* base);

    CabecalhoFrota& cabecalho() const { return *cab_; }
    uint32_t n_max() const { return cab_->n_max; }
    uint32_t n_passagens() const { return cab_->n_passagens; }
    SlotRobo& robo(uint32_t slot) const { return robos_[slot]; }
    SlotEquipe& equipe(uint32_t indice) const { return equipes_[indice]; }
    EspelhoPassagem& passagem(uint32_t indice) const { return passagens_[indice]; }

private:
    CabecalhoFrota* cab_;
    SlotRobo* robos_;
    SlotEquipe* equipes_;
    EspelhoPassagem* passagens_;
};

}  // namespace amr
