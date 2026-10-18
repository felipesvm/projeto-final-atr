#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "tempo.hpp"

namespace amr {

constexpr uint16_t VERSAO_PROTOCOLO = 1;
constexpr size_t TAMANHO_MENSAGEM = 128;

enum class TipoMsg : uint16_t {
    // organizador -> robô
    PARAR = 1,
    ABORTAR,
    ENCERRAR,
    RETOMAR,
    MODO,
    DESTINO_MANUAL,
    MISSAO_EQUIPE,
    RECONHECER_FALHA,
    // robô / gestor -> organizador
    FALHA = 100,
    BATERIA_CRITICA,
    SEM_COMUNICACAO,
    NOVO_ROBO,
    ROBO_PRONTO,
    ROBO_TERMINOU,
    EM_POSICAO,
    ATRACADO,
    ENTREGUE,
    ABORTO_CONCLUIDO,
    BATERIA_BAIXA,
    RECARGA_CONCLUIDA,
    PASSAGEM_ESPERA,
    ROTA_TIMEOUT,
    SEM_ROTA,
    DEADLINE_NAV,
    // ferramenta de teste -> organizador (no lugar da tela de operação)
    OPERADOR = 200,
    // ferramentas de teste
    ANUNCIO = 300,
    INJETAR,
};

const char* nome_tipo(TipoMsg t);
unsigned prioridade(TipoMsg t);

enum class Papel : int32_t { LIDER = 0, MEMBRO = 1 };
enum class ModoOperacao : uint8_t { AUTO = 0, MANUAL = 1 };

// EM_POSICAO serve tanto para a chegada ao ponto de acoplamento quanto ao destino da carga.
enum class Fase : int32_t { ACOPLAMENTO = 0, DESTINO = 1 };

enum class Injecao : int32_t { DEFEITO_ATIVA = 1, DEFEITO_REMOVE, BATERIA, COMUNICACAO_CORTA, COMUNICACAO_VOLTA };

enum class ComandoOperador : int32_t {
    PARADA_GERAL = 1,
    LIBERAR_PARADA,
    NOVA_CAIXA,
    ENVIAR_AO_ROBO,
    REMOVER_ROBO,
};

struct CabecalhoMsg {
    TipoMsg tipo;
    uint16_t versao;
    uint16_t origem;
    uint16_t reservado;
    uint32_t seq;
    uint64_t t_mono_ns;
};

struct CorpoMissao {
    int32_t equipe;
    uint32_t epoca;
    Papel papel;
    int32_t indice;
    int32_t caixa;
    int32_t k;
    float off_x, off_y;
    float pr_x, pr_y;
    float zona_x, zona_y;
    int32_t zona;
};

struct CorpoEvento {
    int32_t robo;
    int32_t equipe;
    int32_t caixa;
    Fase fase;
    float x, y;
    float valor;
    int32_t extra;
};

struct CorpoProcesso {
    int32_t robo;
    int32_t pid;
    int32_t slot;
    uint32_t epoca;
    int32_t codigo;  // código de saída, ou 128 + número do sinal
    float x, y;
};

struct CorpoComando {
    int32_t motivo;
    int32_t equipe;
    ModoOperacao modo;
    float x, y;
};

struct CorpoAnuncio {
    int32_t hw_id;
    float x, y;
};

struct CorpoInjecao {
    Injecao tipo;
    float valor;
};

struct CorpoOperador {
    ComandoOperador comando;
    int32_t robo;
    TipoMsg tipo_robo;
    int32_t caixa, tamanho, baia, zona;
    CorpoComando cmd;
};

struct Mensagem {
    CabecalhoMsg cab;
    union {
        CorpoMissao missao;
        CorpoEvento evento;
        CorpoProcesso processo;
        CorpoComando comando;
        CorpoAnuncio anuncio;
        CorpoInjecao injecao;
        CorpoOperador operador;
        uint8_t bruto[TAMANHO_MENSAGEM - sizeof(CabecalhoMsg)];
    };
};

static_assert(sizeof(Mensagem) == TAMANHO_MENSAGEM, "mensagens de IPC têm tamanho fixo");

constexpr uint16_t ORIGEM_ORGANIZADOR = 0xFF01;
constexpr uint16_t ORIGEM_GESTOR = 0xFF02;
constexpr uint16_t ORIGEM_COLETOR = 0xFF03;
constexpr uint16_t ORIGEM_FERRAMENTA = 0xFF04;

// Cada processo numera as próprias mensagens; o receptor usa (origem, seq) para detectar lacunas.
inline Mensagem nova_mensagem(TipoMsg tipo, uint16_t origem) {
    static std::atomic<uint32_t> sequencia{0};
    Mensagem m;
    std::memset(&m, 0, sizeof m);
    m.cab.tipo = tipo;
    m.cab.versao = VERSAO_PROTOCOLO;
    m.cab.origem = origem;
    m.cab.seq = ++sequencia;
    m.cab.t_mono_ns = uint64_t(agora_ns());
    return m;
}

}  // namespace amr
