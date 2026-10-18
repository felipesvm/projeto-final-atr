#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "../comum/cenario.hpp"
#include "../comum/eventos.hpp"
#include "../comum/frota.hpp"
#include "../comum/ipc.hpp"
#include "../comum/mapa.hpp"
#include "../comum/protocolo.hpp"
#include "imagem_es.hpp"

namespace amr {

struct ConfigRobo {
    int id = -1;
    int slot = -1;
    Ponto inicial;
    std::string cenario = "config/cenario.json";
    bool rastrear_status = false;
    std::string driver = "mqtt";  // "mqtt" (simulador) ou "stub" (cinemática local da Etapa 1)
    std::string broker = "127.0.0.1:1883";
};

// Rota atual do robô para a tela de operação (T_NAV escreve, T_IO publica); nível N4.
struct RotaVisivel {
    std::mutex mtx;
    uint32_t versao = 0;
    std::vector<Ponto> pontos;

    void definir(std::vector<Ponto> p) {
        std::lock_guard<std::mutex> t(mtx);
        pontos = std::move(p);
        ++versao;
    }
};

enum class Finalidade { PROPRIA, CARGA };

struct Missao {
    bool ativa = false;
    int equipe = -1;
    uint32_t epoca = 0;
    Papel papel = Papel::MEMBRO;
    int indice = 0;
    int caixa = -1;
    int k = 1;
    Ponto afastamento, pr, zona;
};

struct PedidoRota {
    uint32_t versao = 0;
    Ponto origem, destino;
    OpcoesRota opcoes;
    Finalidade finalidade = Finalidade::PROPRIA;
};

struct RotaPlanejada {
    uint32_t versao = 0;
    bool ok = false;
    Rota rota;
};

// Contexto de navegação, protegido por mtx_nav (nível N3).
struct Contexto {
    EstadoRobo estado = EstadoRobo::INICIALIZANDO;
    EstadoRobo estado_salvo = EstadoRobo::INICIALIZANDO;
    ModoOperacao modo = ModoOperacao::AUTO;
    bool inibido = false;
    bool parado = false;
    bool sem_comunicacao = false;
    bool bateria_baixa = false;
    bool bateria_critica = false;
    bool recarga_pendente = false;
    Ponto destino_recarga;  // escolhido pela T_STATUS quando a bateria pede recarga
    bool encerrar = false;
    Missao missao;
    PedidoRota pedido;
    RotaPlanejada planejada;
};

// Envio não bloqueante para o organizador; o que não couber na fila fica pendente para o ciclo seguinte.
class Emissor {
public:
    explicit Emissor(FilaMensagens& fila) : fila_(fila) {}
    void enviar(const Mensagem& m) {
        pendentes_.push_back(m);
        retentar();
    }
    void retentar() {
        while (!pendentes_.empty()) {
            auto& m = pendentes_.front();
            if (!fila_.enviar(&m, sizeof m, prioridade(m.cab.tipo), 0)) return;
            pendentes_.pop_front();
        }
    }

private:
    FilaMensagens& fila_;
    std::deque<Mensagem> pendentes_;
};

struct NucleoRobo {
    explicit NucleoRobo(ConfigRobo cfg);

    ConfigRobo cfg;
    Cenario cenario;
    Mapa mapa;
    const Parametros& p;

    MemoriaCompartilhada shm;
    std::unique_ptr<Frota> frota;
    SlotRobo* slot = nullptr;
    SemaforosPassagem semaforos;
    FilaMensagens org;
    FilaMensagens comandos;
    Rastreador rastreador{Rastreador::Modo::TempoReal};

    ImagemES imagem;
    RotaVisivel rota_visivel;
    std::atomic<uint32_t> desvios{0};
    std::atomic<bool> evitando{false};

    std::mutex mtx_nav;
    std::condition_variable cv_rota;
    Contexto ctx;

    std::atomic<bool> ativo{true};

    uint16_t origem() const { return uint16_t(cfg.id); }
    Mensagem evento(TipoMsg tipo) const;
    void registrar(Categoria cat, const char* texto, int caixa = -1);
};

}  // namespace amr
