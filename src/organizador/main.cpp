#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../comum/console.hpp"
#include "organizador.hpp"

using namespace amr;

int main(int argc, char** argv) {
    std::string cenario = "config/cenario.json";
    uint32_t n_max = 32;
    std::string broker = std::getenv("AMR_MQTT") ? std::getenv("AMR_MQTT") : "";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--cenario" && i + 1 < argc) cenario = argv[++i];
        else if (a == "--broker" && i + 1 < argc) broker = argv[++i];
        else if (a == "--n-max" && i + 1 < argc) n_max = uint32_t(std::stoul(argv[++i]));
        else {
            std::fprintf(stderr, "uso: amr_organizador [--cenario arquivo] [--n-max N]\n");
            return 2;
        }
    }
    if (n_max < 1 || n_max > 1024) {
        std::fprintf(stderr, "N_max deve estar entre 1 e 1024\n");
        return 2;
    }

    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, nullptr);

    console::iniciar("organizador");
    int codigo = 0;
    try {
        Organizador org(Cenario::carregar(cenario), n_max, broker);
        org.iniciar();
        int sinal = 0;
        sigwait(&sinais, &sinal);
        LOG("main", "sinal %d recebido: encerrando", sinal);
        org.parar();
    } catch (const std::exception& e) {
        LOG("main", "erro fatal: %s", e.what());
        codigo = 1;
    }
    console::encerrar();
    return codigo;
}
