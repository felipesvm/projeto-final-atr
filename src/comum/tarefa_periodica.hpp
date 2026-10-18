#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "frota.hpp"

namespace amr {

struct InfoCiclo {
    uint64_t numero;
    int64_t liberacao_ns;
    uint32_t perdas_consecutivas;
};

// Liberação em instante absoluto; liberações perdidas são puladas, não executadas em rajada.
class TarefaPeriodica {
public:
    using Corpo = std::function<void(const InfoCiclo&)>;

    TarefaPeriodica(std::string nome, int64_t periodo_ns, int prioridade, Corpo corpo);
    ~TarefaPeriodica();
    TarefaPeriodica(const TarefaPeriodica&) = delete;
    TarefaPeriodica& operator=(const TarefaPeriodica&) = delete;

    void iniciar();
    void parar();
    MetricasTempoReal metricas() const;
    const std::string& nome() const { return nome_; }

private:
    void executar();

    std::string nome_;
    int64_t periodo_ns_;
    int prioridade_;
    Corpo corpo_;
    std::thread thread_;
    std::atomic<bool> ativa_{false};
    std::atomic<uint64_t> ciclos_{0};
    std::atomic<uint64_t> perdas_{0};
    std::atomic<uint32_t> execucao_max_us_{0};
    std::atomic<uint32_t> atraso_max_us_{0};
};

// Aplica SCHED_FIFO à thread chamadora. Sem permissão, avisa uma vez e segue com a política normal.
bool aplicar_prioridade(int prioridade, const std::string& nome);
bool tempo_real_desativado();

}  // namespace amr
