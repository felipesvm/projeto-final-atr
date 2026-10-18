#include "tarefa_periodica.hpp"

#include <pthread.h>
#include <sched.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "console.hpp"
#include "tempo.hpp"

namespace amr {

namespace {
void atualizar_maximo(std::atomic<uint32_t>& alvo, uint32_t valor) {
    uint32_t atual = alvo.load(std::memory_order_relaxed);
    while (valor > atual && !alvo.compare_exchange_weak(atual, valor, std::memory_order_relaxed)) {
    }
}

std::atomic<bool> aviso_emitido{false};
}  // namespace

bool tempo_real_desativado() {
    const char* v = std::getenv("AMR_SEM_RT");
    return v && *v && std::strcmp(v, "0") != 0;
}

bool aplicar_prioridade(int prioridade, const std::string& nome) {
    pthread_setname_np(pthread_self(), nome.substr(0, 15).c_str());
    if (prioridade <= 0 || tempo_real_desativado()) return false;
    sched_param p{};
    p.sched_priority = prioridade;
    int r = pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);
    if (r != 0) {
        if (!aviso_emitido.exchange(true))
            LOG("rt", "sem permissão para SCHED_FIFO (%s); threads seguem na política normal", std::strerror(r));
        return false;
    }
    return true;
}

TarefaPeriodica::TarefaPeriodica(std::string nome, int64_t periodo_ns, int prioridade, Corpo corpo)
    : nome_(std::move(nome)), periodo_ns_(periodo_ns), prioridade_(prioridade), corpo_(std::move(corpo)) {}

TarefaPeriodica::~TarefaPeriodica() { parar(); }

void TarefaPeriodica::iniciar() {
    if (ativa_.exchange(true)) return;
    thread_ = std::thread(&TarefaPeriodica::executar, this);
}

void TarefaPeriodica::parar() {
    ativa_ = false;
    if (thread_.joinable()) thread_.join();
}

MetricasTempoReal TarefaPeriodica::metricas() const {
    return MetricasTempoReal{ciclos_.load(), perdas_.load(), execucao_max_us_.load(), atraso_max_us_.load()};
}

void TarefaPeriodica::executar() {
    aplicar_prioridade(prioridade_, nome_);
    int64_t proxima = agora_ns() + periodo_ns_;
    uint32_t consecutivas = 0;
    uint64_t n = 0;

    while (ativa_.load(std::memory_order_relaxed)) {
        timespec alvo = para_timespec(proxima);
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &alvo, nullptr) == EINTR) {
        }
        if (!ativa_.load(std::memory_order_relaxed)) break;

        int64_t inicio = agora_ns();
        atualizar_maximo(atraso_max_us_, uint32_t((inicio - proxima) / 1000));

        corpo_(InfoCiclo{++n, proxima, consecutivas});

        int64_t fim = agora_ns();
        atualizar_maximo(execucao_max_us_, uint32_t((fim - inicio) / 1000));
        ciclos_.fetch_add(1, std::memory_order_relaxed);

        int64_t prazo = proxima + periodo_ns_;
        if (fim > prazo) {
            int64_t perdidas = (fim - proxima) / periodo_ns_;
            perdas_.fetch_add(uint64_t(perdidas), std::memory_order_relaxed);
            consecutivas += uint32_t(perdidas);
            proxima += (perdidas + 1) * periodo_ns_;
        } else {
            consecutivas = 0;
            proxima = prazo;
        }
    }
}

}  // namespace amr
