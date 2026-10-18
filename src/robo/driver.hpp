#pragma once

#include <atomic>
#include <memory>

#include "../comum/cenario.hpp"
#include "../comum/ipc.hpp"
#include "imagem_es.hpp"

namespace amr {

// Fronteira com o hardware. Na Etapa 2 entra um DriverMQTT com a mesma interface.
class DriverES {
public:
    virtual ~DriverES() = default;
    virtual void executar(std::atomic<bool>& ativo) = 0;
};

// Etapa 1: cinemática local, bateria, atracamento e injeções vindas da fila de teste.
class DriverStub : public DriverES {
public:
    DriverStub(int id, Ponto inicial, const Cenario& cenario, ImagemES& imagem);
    void executar(std::atomic<bool>& ativo) override;

private:
    void aplicar_injecoes();
    void integrar(const Saidas& s, double dt);
    bool na_estacao() const;

    int id_;
    const Cenario& cenario_;
    const Parametros& p_;
    ImagemES& img_;
    FilaMensagens fila_teste_;

    double x_, y_, vx_ = 0, vy_ = 0;
    double bateria_;
    bool falha_ = false;
    bool comunicacao_ = true;
    bool atracada_ = false;
    bool acoplar_anterior_ = false;
    double tempo_comando_ = 0;
    uint64_t seq_ = 0;
};

}  // namespace amr
