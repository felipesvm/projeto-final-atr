#pragma once

#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "../comum/eventos.hpp"
#include "../comum/frota.hpp"
#include "../comum/ipc.hpp"
#include "../comum/mqtt.hpp"

namespace amr {

// Cria um processo por robô anunciado (fork + execv) e acompanha o término de cada um (waitpid).
class Gestor {
public:
    Gestor(std::string executavel_robo, std::string cenario, std::string broker);
    ~Gestor();

    void iniciar();
    void parar();

private:
    struct Pedido {
        int id;
        float x, y;
    };
    struct Filho {
        int id;
        int slot;
        uint32_t epoca;
    };

    void laco_anuncios();
    void receber_anuncio(int id, float x, float y, const char* origem);
    void laco_criacao();
    void laco_coleta();
    void criar_robo(const Pedido& p);
    int reservar_slot(int id, uint32_t& epoca);
    void devolver_slot(int slot, uint32_t epoca);
    void avisar_organizador(const Mensagem& m);

    std::string executavel_robo_;
    std::string cenario_;
    MemoriaCompartilhada shm_;
    std::unique_ptr<Frota> frota_;
    FilaMensagens anuncios_;
    FilaMensagens org_;
    Rastreador rastreador_{Rastreador::Modo::ComPrazo};

    std::atomic<bool> ativo_{false};
    std::mutex mtx_fila_;
    std::condition_variable cv_spawn_;
    std::deque<Pedido> fila_;
    std::set<int> ids_;

    std::mutex mtx_filhos_;
    std::condition_variable cv_filhos_;
    std::map<pid_t, Filho> filhos_;

    std::unique_ptr<ClienteMqtt> mqtt_;
    std::thread t_anuncio_, t_spawn_, t_reap_;
};

}  // namespace amr
