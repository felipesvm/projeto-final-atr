#pragma once

#include "../comum/tarefa_periodica.hpp"
#include "dominio.hpp"
#include "nucleo.hpp"

namespace amr {

// T_STATUS — Status do Robô (100 ms): defeito, bateria, comunicação, distância e publicação do slot.
class StatusRobo {
public:
    explicit StatusRobo(NucleoRobo& r);
    void ciclo(const InfoCiclo& info);
    void observar(const TarefaPeriodica* nav, const TarefaPeriodica* status) {
        nav_ = nav;
        status_ = status;
    }

private:
    Ponto estacao_mais_proxima(const Ponto& pos) const;
    void evento(TipoMsg tipo, const Ponto& pos, float valor, int equipe, const char* texto);

    NucleoRobo& r_;
    Emissor emissor_;
    MonitorBateria bateria_;
    DetectorBorda borda_falha_;
    const TarefaPeriodica* nav_ = nullptr;
    const TarefaPeriodica* status_ = nullptr;

    Ponto ultima_{};
    bool primeira_ = true;
    double distancia_ = 0;
    uint64_t heartbeat_ = 0;
    bool pronto_avisado_ = false;
    uint64_t morrer_no_ciclo_ = 0;
};

}  // namespace amr
