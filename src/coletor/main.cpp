#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../comum/console.hpp"
#include "coletor.hpp"

using namespace amr;

int main(int argc, char** argv) {
    std::string raiz = "logs";
    std::string broker = std::getenv("AMR_MQTT") ? std::getenv("AMR_MQTT") : "";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--logs" && i + 1 < argc) raiz = argv[++i];
        else if (a == "--broker" && i + 1 < argc) broker = argv[++i];
        else {
            std::fprintf(stderr, "uso: amr_coletor [--logs pasta]\n");
            return 2;
        }
    }

    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, nullptr);

    console::iniciar("coletor");
    int codigo = 0;
    try {
        Coletor coletor(raiz, broker);
        coletor.iniciar();
        LOG("main", "gravando em %s", coletor.pasta().c_str());
        int sinal = 0;
        sigwait(&sinais, &sinal);
        coletor.parar();
        LOG("main", "arquivos sincronizados em disco");
    } catch (const std::exception& e) {
        LOG("main", "erro fatal: %s", e.what());
        codigo = 1;
    }
    console::encerrar();
    return codigo;
}
