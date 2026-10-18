#include <libgen.h>
#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../comum/console.hpp"
#include "gestor.hpp"

using namespace amr;

int main(int argc, char** argv) {
    std::string cenario = "config/cenario.json";
    std::string dir = argv[0];
    dir = dirname(dir.data());
    std::string robo = dir + "/amr_robo";
    std::string broker = std::getenv("AMR_MQTT") ? std::getenv("AMR_MQTT") : "";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--cenario" && i + 1 < argc) cenario = argv[++i];
        else if (a == "--robo" && i + 1 < argc) robo = argv[++i];
        else if (a == "--broker" && i + 1 < argc) broker = argv[++i];
        else {
            std::fprintf(stderr, "uso: amr_gestor [--cenario arquivo] [--robo executavel]\n");
            return 2;
        }
    }

    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, nullptr);

    console::iniciar("gestor");
    int codigo = 0;
    try {
        Gestor gestor(robo, cenario, broker);
        gestor.iniciar();
        LOG("main", "aguardando anúncios de robôs");
        int sinal = 0;
        sigwait(&sinais, &sinal);
        LOG("main", "sinal %d recebido: encerrando os robôs", sinal);
        gestor.parar();
    } catch (const std::exception& e) {
        LOG("main", "erro fatal: %s", e.what());
        codigo = 1;
    }
    console::encerrar();
    return codigo;
}
