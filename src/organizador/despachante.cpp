#include "despachante.hpp"

#include <algorithm>

namespace amr {

namespace {
std::vector<int> mais_baratos(std::vector<RoboLivre>& livres, const CaixaPronta& caixa, int quantidade,
                              const Despachante::Custo& custo) {
    auto c = [&](const RoboLivre& r) { return custo ? custo(r, caixa) : distancia(r.posicao, caixa.origem); };
    std::stable_sort(livres.begin(), livres.end(), [&](const RoboLivre& a, const RoboLivre& b) { return c(a) < c(b); });
    std::vector<int> ids;
    for (int i = 0; i < quantidade; ++i) ids.push_back(livres[size_t(i)].id);
    livres.erase(livres.begin(), livres.begin() + quantidade);
    return ids;
}
}  // namespace

ResultadoDespacho Despachante::decidir(std::vector<CaixaPronta> caixas, std::vector<RoboLivre> livres,
                                       int robos_operacionais, const Custo& custo) const {
    ResultadoDespacho r;
    std::stable_sort(caixas.begin(), caixas.end(),
                     [](const CaixaPronta& a, const CaixaPronta& b) { return a.espera_s > b.espera_s; });

    for (const auto& c : caixas) {
        if (c.tamanho > robos_operacionais) {
            // reservar robôs para ela prenderia a frota para sempre
            r.aguardando_frota.push_back(c.id);
            continue;
        }
        if (int(livres.size()) >= c.tamanho) {
            r.designacoes.push_back({c.id, mais_baratos(livres, c, c.tamanho, custo)});
            continue;
        }
        if (c.espera_s >= envelhecimento_s_) {
            r.reserva = c.id;
            break;
        }
    }
    return r;
}

}  // namespace amr
