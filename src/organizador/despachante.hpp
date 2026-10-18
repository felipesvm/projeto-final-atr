#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "../comum/cenario.hpp"

namespace amr {

struct RoboLivre {
    int id;
    Ponto posicao;
    double bateria = 100;
};

struct CaixaPronta {
    int id;
    int tamanho;
    Ponto origem;
    double espera_s;
};

struct Designacao {
    int caixa;
    std::vector<int> robos;  // o primeiro é o líder
};

struct ResultadoDespacho {
    std::vector<Designacao> designacoes;
    std::vector<int> aguardando_frota;  // caixas maiores que a frota operacional
    int reserva = -1;                   // caixa com reserva progressiva neste ciclo
};

// Tudo-ou-nada, da caixa mais antiga para a mais nova. Passado T_aging, a mais antiga
// que não couber reserva os robôs livres até juntar o suficiente.
class Despachante {
public:
    // custo de mandar um robô buscar uma caixa; por padrão, distância em linha reta
    using Custo = std::function<double(const RoboLivre&, const CaixaPronta&)>;

    explicit Despachante(double envelhecimento_s) : envelhecimento_s_(envelhecimento_s) {}

    ResultadoDespacho decidir(std::vector<CaixaPronta> caixas, std::vector<RoboLivre> livres, int robos_operacionais,
                              const Custo& custo = {}) const;

private:
    double envelhecimento_s_;
};

}  // namespace amr
