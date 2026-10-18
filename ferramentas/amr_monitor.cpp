// Observa a tabela da frota sem alterá-la: copia cada slot sob o seu mutex e imprime.

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "../src/comum/eventos.hpp"
#include "../src/comum/frota.hpp"
#include "../src/comum/ipc.hpp"
#include "../src/comum/nomes.hpp"

using namespace amr;

namespace {
void imprimir(Frota& frota, BufferEventos* ev, bool csv) {
    auto& cab = frota.cabecalho();
    double t = double(agora_ns() - cab.t_inicio_ns) / NS_POR_S;
    if (!csv) {
        std::printf("\033[H\033[2J");
        std::printf("t=%.1f s   N_max=%u   parada_geral=%u   registros gravados=%llu   perdas=%llu\n\n", t, cab.n_max,
                    cab.parada_geral.load(), ev ? (unsigned long long)ev->gravados.load() : 0ULL,
                    ev ? (unsigned long long)ev->perdas.load() : 0ULL);
        std::printf("%-4s %-4s %-7s %-20s %-6s %-14s %-6s %-8s %-3s %-3s %-8s %-18s\n", "SLOT", "ID", "PID", "ESTADO", "MODO",
                    "POSICAO", "BAT%", "DIST(m)", "EQ", "CX", "HB", "NAV ciclos/perdas/Cmax");
    }
    for (uint32_t s = 0; s < frota.n_max(); ++s) {
        SlotRobo c;
        {
            auto& sl = frota.robo(s);
            TravaCompartilhada trava(&sl.mtx);
            if (!sl.ocupado) continue;
            c = sl;
        }
        if (csv) {
            std::printf("%.3f;%u;%d;%s;%.3f;%.3f;%.3f;%llu;%d;%d\n", t, s, c.id, nome_estado(c.estado), c.x, c.y,
                        c.bateria, (unsigned long long)c.heartbeat, c.equipe, c.caixa);
            continue;
        }
        char pos[32];
        std::snprintf(pos, sizeof pos, "(%5.1f,%5.1f)", c.x, c.y);
        std::printf("%-4u %-4d %-7d %-20s %-6s %-14s %-6.1f %-8.1f %-3d %-3d %-8llu %llu/%llu/%uus%s\n", s, c.id, c.pid,
                    nome_estado(c.estado), c.modo == ModoOperacao::AUTO ? "AUTO" : "MAN", pos, c.bateria, c.distancia,
                    c.equipe, c.caixa, (unsigned long long)c.heartbeat, (unsigned long long)c.nav.ciclos,
                    (unsigned long long)c.nav.perdas_prazo, c.nav.execucao_max_us, c.suspeito ? "  SUSPEITO" : "");
    }
    if (csv) return;

    std::printf("\nPASSAGENS\n");
    for (uint32_t p = 0; p < frota.n_passagens(); ++p) {
        auto& esp = frota.passagem(p);
        TravaCompartilhada trava(&esp.mtx);
        std::printf("  P%u  capacidade %d  ocupação %d (máx %d)  entradas %llu  ocupantes [", p + 1, esp.capacidade,
                    esp.ocupacao, esp.ocupacao_max, (unsigned long long)esp.entradas);
        for (int i = 0; i < MAX_OCUPANTES; ++i)
            if (esp.ocupantes[i] >= 0) std::printf(" %d(%d)", esp.ocupantes[i], esp.unidades[i]);
        std::printf(" ]%s\n", esp.esperador >= 0 ? ("  esperando: " + std::to_string(esp.esperador)).c_str() : "");
    }
    std::printf("\nEQUIPES\n");
    for (uint32_t e = 0; e < frota.n_max(); ++e) {
        auto& eq = frota.equipe(e);
        TravaCompartilhada trava(&eq.mtx);
        if (eq.estado == EstadoEquipe::LIVRE) continue;
        std::printf("  %u  %-12s caixa %d  k=%d  membros [", e, nome_estado(eq.estado), eq.caixa, eq.k);
        for (int i = 0; i < eq.k; ++i) std::printf(" %d", eq.membros[i]);
        std::printf(" ]  passo %d/%d  chegadas 0x%x  atracados 0x%x\n", eq.passo, eq.rota_n, eq.mascara_chegada,
                    eq.mascara_atracado);
    }
    std::fflush(stdout);
}
}  // namespace

int main(int argc, char** argv) {
    bool uma_vez = false, csv = false;
    int intervalo_ms = 1000;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--uma-vez") uma_vez = true;
        else if (a == "--csv") csv = true;
        else if (a == "--intervalo-ms" && i + 1 < argc) intervalo_ms = std::stoi(argv[++i]);
        else {
            std::fprintf(stderr, "uso: amr_monitor [--uma-vez] [--csv] [--intervalo-ms T]\n");
            return 2;
        }
    }
    try {
        auto shm = MemoriaCompartilhada::abrir(nomes::SHM_FROTA);
        Frota frota(shm.dados());
        MemoriaCompartilhada shm_ev;
        BufferEventos* ev = nullptr;
        if (MemoriaCompartilhada::existe(nomes::SHM_EVENTOS)) {
            shm_ev = MemoriaCompartilhada::abrir(nomes::SHM_EVENTOS);
            ev = static_cast<BufferEventos*>(shm_ev.dados());
        }
        if (csv) std::printf("t;slot;id;estado;x;y;bateria;heartbeat;equipe;caixa\n");
        do {
            imprimir(frota, ev, csv);
            if (uma_vez) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalo_ms));
        } while (MemoriaCompartilhada::existe(nomes::SHM_FROTA));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "amr_monitor: %s (o organizador está em execução?)\n", e.what());
        return 1;
    }
    return 0;
}
