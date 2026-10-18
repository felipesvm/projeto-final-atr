#pragma once

#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace amr {

class ErroSistema : public std::runtime_error {
public:
    explicit ErroSistema(const std::string& onde);
    int codigo() const { return codigo_; }

private:
    int codigo_;
};

// Segmento POSIX (shm_open + mmap). Quem cria é o dono e decide quando remover o nome.
class MemoriaCompartilhada {
public:
    static MemoriaCompartilhada criar(const std::string& nome, size_t tamanho);
    static MemoriaCompartilhada abrir(const std::string& nome);
    static bool existe(const std::string& nome);
    static void remover(const std::string& nome);

    MemoriaCompartilhada() = default;
    MemoriaCompartilhada(MemoriaCompartilhada&& outra) noexcept;
    MemoriaCompartilhada& operator=(MemoriaCompartilhada&& outra) noexcept;
    MemoriaCompartilhada(const MemoriaCompartilhada&) = delete;
    MemoriaCompartilhada& operator=(const MemoriaCompartilhada&) = delete;
    ~MemoriaCompartilhada();

    void* dados() const { return dados_; }
    size_t tamanho() const { return tamanho_; }

private:
    MemoriaCompartilhada(void* dados, size_t tamanho) : dados_(dados), tamanho_(tamanho) {}
    void* dados_ = nullptr;
    size_t tamanho_ = 0;
};

// Esperas em ns relativos: 0 tenta uma vez, negativo espera sem prazo.
class FilaMensagens {
public:
    enum class Acesso { Leitura, Escrita, LeituraEscrita };

    static FilaMensagens criar(const std::string& nome, long max_mensagens, size_t tamanho_msg, Acesso acesso);
    static FilaMensagens abrir(const std::string& nome, Acesso acesso);
    static void remover(const std::string& nome);

    FilaMensagens() = default;
    FilaMensagens(FilaMensagens&& outra) noexcept;
    FilaMensagens& operator=(FilaMensagens&& outra) noexcept;
    FilaMensagens(const FilaMensagens&) = delete;
    FilaMensagens& operator=(const FilaMensagens&) = delete;
    ~FilaMensagens();

    bool valida() const { return fd_ != mqd_t(-1); }
    bool enviar(const void* msg, size_t tamanho, unsigned prioridade, int64_t espera_ns = 0);
    bool receber(void* msg, size_t tamanho, int64_t espera_ns, unsigned* prioridade = nullptr);
    long capacidade() const;
    long pendentes() const;
    const std::string& nome() const { return nome_; }

private:
    FilaMensagens(mqd_t fd, std::string nome) : fd_(fd), nome_(std::move(nome)) {}
    mqd_t fd_ = mqd_t(-1);
    std::string nome_;
};

class SemaforoNomeado {
public:
    static SemaforoNomeado criar(const std::string& nome, unsigned valor);
    static SemaforoNomeado abrir(const std::string& nome);
    static void remover(const std::string& nome);

    SemaforoNomeado() = default;
    SemaforoNomeado(SemaforoNomeado&& outro) noexcept;
    SemaforoNomeado& operator=(SemaforoNomeado&& outro) noexcept;
    SemaforoNomeado(const SemaforoNomeado&) = delete;
    SemaforoNomeado& operator=(const SemaforoNomeado&) = delete;
    ~SemaforoNomeado();

    bool tentar();
    bool esperar_por(int64_t espera_ns);
    void liberar();
    int valor() const;

private:
    explicit SemaforoNomeado(sem_t* s) : sem_(s) {}
    sem_t* sem_ = nullptr;
};

// Conjunto System V: um semáforo de contagem por passagem estreita.
class SemaforosPassagem {
public:
    struct Operacao {
        int passagem;
        int unidades;
    };

    static SemaforosPassagem criar(const std::vector<int>& capacidades);
    static SemaforosPassagem abrir(int semid, int quantidade);

    SemaforosPassagem() = default;

    // Todas as operações acontecem juntas ou nenhuma acontece (semop atômico, IPC_NOWAIT).
    bool tentar_adquirir(const std::vector<Operacao>& ops);
    void liberar(int passagem, int unidades);
    int valor(int passagem) const;
    void remover();
    int id() const { return semid_; }
    int quantidade() const { return quantidade_; }

private:
    SemaforosPassagem(int semid, int quantidade) : semid_(semid), quantidade_(quantidade) {}
    int semid_ = -1;
    int quantidade_ = 0;
};

// PROCESS_SHARED + ROBUST + PRIO_INHERIT
void iniciar_mutex_compartilhado(pthread_mutex_t* mutex);

class TravaCompartilhada {
public:
    explicit TravaCompartilhada(pthread_mutex_t* mutex);
    ~TravaCompartilhada();
    TravaCompartilhada(const TravaCompartilhada&) = delete;
    TravaCompartilhada& operator=(const TravaCompartilhada&) = delete;

    // EOWNERDEAD: o mutex já foi recuperado, mas os dados protegidos podem estar pela metade
    bool dono_morreu() const { return dono_morreu_; }

private:
    pthread_mutex_t* mutex_;
    bool dono_morreu_ = false;
};

}  // namespace amr
