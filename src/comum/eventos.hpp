#pragma once

#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "ipc.hpp"

namespace amr {

constexpr uint32_t CAPACIDADE_EVENTOS = 1024;

enum class Categoria : uint8_t { CARGA, ROBO, SISTEMA, OPERADOR };

const char* nome_categoria(Categoria c);

struct Registro {
    int64_t t_parede_ns;
    int64_t t_mono_ns;
    char origem[16];
    Categoria categoria;
    int32_t robo;
    int32_t caixa;
    int32_t baia;
    int32_t zona;
    int32_t robos[4];
    char texto[96];
};

struct BufferEventos {
    pthread_mutex_t mtx;
    uint32_t capacidade;
    uint32_t cabeca;
    uint32_t cauda;
    std::atomic<uint64_t> perdas;
    std::atomic<uint64_t> gravados;
    Registro itens[CAPACIDADE_EVENTOS];
};

Registro novo_registro(const char* origem, Categoria cat, const char* formato, ...)
    __attribute__((format(printf, 3, 4)));

// Produtor do buffer /amr_eventos. TempoReal: sem_trywait e conta a perda.
// ComPrazo: guarda localmente e descarrega com sem_timedwait de 10 ms, fora de outras travas.
class Rastreador {
public:
    enum class Modo { TempoReal, ComPrazo };

    Rastreador() = default;
    explicit Rastreador(Modo modo);

    void registrar(const Registro& r);
    void descarregar();
    bool conectado() const { return buffer_ != nullptr; }

private:
    bool gravar(const Registro& r, int64_t espera_ns);

    Modo modo_ = Modo::TempoReal;
    MemoriaCompartilhada shm_;
    BufferEventos* buffer_ = nullptr;
    SemaforoNomeado vazios_;
    SemaforoNomeado itens_;
    std::mutex mtx_pendentes_;
    std::mutex mtx_descarga_;
    std::deque<Registro> pendentes_;
};

}  // namespace amr
