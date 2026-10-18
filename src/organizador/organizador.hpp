#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "../comum/cenario.hpp"
#include "../comum/eventos.hpp"
#include "../comum/frota.hpp"
#include "../comum/ipc.hpp"
#include "../comum/mqtt.hpp"
#include <deque>
#include <string>
#include "../comum/tarefa_periodica.hpp"
#include "../comum/mapa.hpp"
#include "despachante.hpp"
#include "modelo.hpp"

namespace amr {

// Dono dos objetos de IPC. Três threads: eventos (T_ORG_EVT), despacho (T_ORG_DESP) e supervisão (T_ORG_SUP).
class Organizador {
public:
    Organizador(Cenario cenario, uint32_t n_max, std::string broker = "");
    ~Organizador();

    void iniciar();
    void parar();

private:
    struct FotoSlot {
        bool ocupado;
        int id;
        uint32_t epoca;
        EstadoRobo estado;
        ModoOperacao modo;
        Ponto pos;
        bool bateria_baixa, falha, parado;
        uint64_t heartbeat;
        bool suspeito;
        double bateria;
    };

    void criar_ipc();
    void remover_ipc();
    std::vector<FotoSlot> fotografar_frota();

    void laco_eventos();
    void tratar(const Mensagem& m, Efeitos& ef);
    void tratar_operador(const CorpoOperador& op, Efeitos& ef);
    void ciclo_despacho();
    void ciclo_supervisao();

    void abortar_equipe(int indice, const char* motivo, Efeitos& ef);
    void liberar_equipe(int indice, Efeitos& ef);
    void enviar_robo(int robo, const Mensagem& m, Efeitos& ef);
    void aplicar(Efeitos& ef);
    void abastecer_pontos_de_retirada();
    void registrar(const Registro& r) { rastreador_->registrar(r); }
    Registro registro(Categoria cat, const char* formato, ...) __attribute__((format(printf, 3, 4)));
    void alarme(const char* origem, const char* formato, ...) __attribute__((format(printf, 3, 4)));

    // T_ORG_HMI: ponte com a tela de operação via MQTT (hmi.cpp)
    void iniciar_hmi(const std::string& broker);
    void parar_hmi();
    void ciclo_hmi();
    void comando_da_tela(const std::string& topico, const std::string& payload);
    std::string montar_estado();

    Cenario cenario_;
    uint32_t n_max_;
    std::string broker_;
    Mapa mapa_;
    Despachante despachante_;

    MemoriaCompartilhada shm_frota_;
    MemoriaCompartilhada shm_eventos_;
    std::unique_ptr<Frota> frota_;
    SemaforosPassagem semaforos_;
    SemaforoNomeado vazios_, itens_;
    FilaMensagens entrada_;
    std::unique_ptr<Rastreador> rastreador_;

    std::mutex mtx_modelo_;
    Modelo modelo_;

    std::atomic<bool> ativo_{false};
    std::thread t_evt_;
    std::unique_ptr<TarefaPeriodica> t_desp_;
    std::unique_ptr<TarefaPeriodica> t_sup_;
    std::unique_ptr<TarefaPeriodica> t_hmi_;
    std::unique_ptr<ClienteMqtt> mqtt_;
    std::mutex mtx_alarmes_;
    std::deque<std::pair<int64_t, std::string>> alarmes_;
    bool anunciado_online_ = false;
    int64_t ultimo_resumo_ = 0;
    int ultima_reserva_ = -1;
    std::vector<int> esperas_avisadas_;
};

}  // namespace amr
