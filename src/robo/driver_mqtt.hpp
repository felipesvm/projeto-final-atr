#pragma once

#include <atomic>
#include <string>

#include "../comum/mqtt.hpp"
#include "driver.hpp"
#include "nucleo.hpp"

namespace amr {

// Etapa 2: quadros de sensores e atuadores trocados com o simulador (Tabela 1 do enunciado).
class DriverMQTT : public DriverES {
public:
    DriverMQTT(int id, ImagemES& imagem, RotaVisivel& rota, const std::string& broker);
    void executar(std::atomic<bool>& ativo) override;

private:
    void receber(const std::string& payload);

    int id_;
    ImagemES& img_;
    RotaVisivel& rota_;
    ClienteMqtt cliente_;
};

}  // namespace amr
