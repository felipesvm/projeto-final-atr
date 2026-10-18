#pragma once

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "../comum/eventos.hpp"
#include "../comum/frota.hpp"
#include "../comum/ipc.hpp"
#include "../comum/mqtt.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include "../comum/tarefa_periodica.hpp"

namespace amr {

// Toda a escrita em disco fica neste processo, longe das threads de tempo real.
class Coletor {
public:
    Coletor(const std::string& raiz_logs, const std::string& broker);
    ~Coletor();

    void iniciar();
    void parar();
    const std::string& pasta() const { return pasta_; }

private:
    void laco_consumo();
    void laco_publicacao();
    void amostrar_frota();
    void gravar(const Registro& r);
    void sincronizar();

    std::string pasta_;
    MemoriaCompartilhada shm_frota_;
    MemoriaCompartilhada shm_eventos_;
    std::unique_ptr<Frota> frota_;
    BufferEventos* buffer_ = nullptr;
    SemaforoNomeado vazios_, itens_;

    std::FILE* eventos_ = nullptr;
    std::FILE* cargas_ = nullptr;
    std::FILE* amostras_ = nullptr;

    std::atomic<bool> ativo_{false};
    std::thread t_cons_;
    std::thread t_pub_;
    std::unique_ptr<ClienteMqtt> mqtt_;
    std::mutex mtx_pub_;
    std::condition_variable cv_pub_;
    std::deque<std::string> a_publicar_;
    std::deque<std::string> historico_;
    bool pedido_historico_ = false;
    std::unique_ptr<TarefaPeriodica> t_snap_;
};

}  // namespace amr
