#include "rota.hpp"

#include <chrono>
#include <cmath>

#include "../comum/console.hpp"
#include "../comum/tarefa_periodica.hpp"

namespace amr {

void executar_planejador(NucleoRobo& r) {
    aplicar_prioridade(40, "T_ROTA");
    uint32_t atendida = 0;
    std::unique_lock<std::mutex> trava(r.mtx_nav);
    while (r.ativo.load()) {
        r.cv_rota.wait_for(trava, std::chrono::milliseconds(200),
                           [&] { return r.ctx.pedido.versao > atendida || !r.ativo.load(); });
        if (!r.ativo.load()) break;
        if (r.ctx.pedido.versao <= atendida) continue;

        PedidoRota pedido = r.ctx.pedido;
        trava.unlock();
        if (r.frota->cabecalho().rotas_adaptativas.load()) {
            // passagem congestionada fica "mais longa": espera média aprendida + fila atual
            pedido.opcoes.custo_passagem.assign(r.frota->n_passagens(), 0.0);
            for (uint32_t p = 0; p < r.frota->n_passagens(); ++p) {
                auto& esp = r.frota->passagem(p);
                TravaCompartilhada t(&esp.mtx);
                double lotada = esp.ocupacao >= esp.capacidade ? 3.0 : 0.0;
                pedido.opcoes.custo_passagem[p] = (esp.espera_media_s + lotada) * r.p.velocidade_max;
            }
        }
        // células com robôs parados (estacionados, esperando, com defeito) ficam fora da rota quando houver
        // alternativa: o desvio vira caminho planejado, e não manobra de última hora
        OpcoesRota com_parados = pedido.opcoes;
        if (pedido.finalidade == Finalidade::PROPRIA) {
            const int ox = int(std::floor(pedido.origem.x)), oy = int(std::floor(pedido.origem.y));
            const int dx = int(std::floor(pedido.destino.x)), dy = int(std::floor(pedido.destino.y));
            for (uint32_t i = 0; i < r.frota->n_max(); ++i) {
                if (int(i) == r.cfg.slot) continue;
                auto& sl = r.frota->robo(i);
                TravaCompartilhada t(&sl.mtx);
                if (!sl.ocupado || sl.heartbeat == 0 || std::hypot(sl.sp_x - sl.x, sl.sp_y - sl.y) > 0.1) continue;
                int cx = int(std::floor(sl.x)), cy = int(std::floor(sl.y));
                if ((cx == ox && cy == oy) || (cx == dx && cy == dy)) continue;
                com_parados.celulas_evitadas.push_back({cx, cy});
            }
        }
        auto rota = r.mapa.planejar(pedido.origem, pedido.destino, com_parados);
        if (!rota) rota = r.mapa.planejar(pedido.origem, pedido.destino, pedido.opcoes);
        trava.lock();

        atendida = pedido.versao;
        if (r.ctx.pedido.versao != pedido.versao) continue;
        r.ctx.planejada.versao = pedido.versao;
        r.ctx.planejada.ok = rota.has_value();
        r.ctx.planejada.rota = rota ? std::move(*rota) : Rota{};
    }
}

}  // namespace amr
