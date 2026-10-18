#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <cstring>
#include <string>
#include <thread>

#include "../comum/console.hpp"
#include "../comum/tarefa_periodica.hpp"
#include "driver.hpp"
#include "driver_mqtt.hpp"
#include "navegacao.hpp"
#include "nucleo.hpp"
#include "rota.hpp"
#include "status.hpp"

using namespace amr;

namespace {

ConfigRobo ler_argumentos(int argc, char** argv) {
    ConfigRobo cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto valor = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("falta o valor de " + a);
            return argv[++i];
        };
        if (a == "--id") cfg.id = std::stoi(valor());
        else if (a == "--slot") cfg.slot = std::stoi(valor());
        else if (a == "--x") cfg.inicial.x = std::stod(valor());
        else if (a == "--y") cfg.inicial.y = std::stod(valor());
        else if (a == "--cenario") cfg.cenario = valor();
        else if (a == "--rastrear-status") cfg.rastrear_status = true;
        else if (a == "--driver") cfg.driver = valor();
        else if (a == "--broker") cfg.broker = valor();
        else throw std::runtime_error("argumento desconhecido: " + a);
    }
    if (cfg.id < 0 || cfg.slot < 0) throw std::runtime_error("uso: amr_robo --id N --slot S [--x X --y Y]");
    if (const char* v = std::getenv("AMR_RASTREAR_STATUS"); v && std::strcmp(v, "1") == 0) cfg.rastrear_status = true;
    if (const char* v = std::getenv("AMR_DRIVER"); v && *v) cfg.driver = v;
    if (const char* v = std::getenv("AMR_MQTT"); v && *v) cfg.broker = v;
    if (cfg.driver != "mqtt" && cfg.driver != "stub") throw std::runtime_error("driver deve ser mqtt ou stub");
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    // os sinais de término ficam para a thread principal; as demais herdam a máscara
    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, nullptr);

    ConfigRobo cfg;
    try {
        cfg = ler_argumentos(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "amr_robo: %s\n", e.what());
        return 2;
    }
    console::iniciar("robo_" + std::to_string(cfg.id));

    int codigo = 0;
    try {
        NucleoRobo nucleo(cfg);
        std::unique_ptr<DriverES> driver;
        if (cfg.driver == "stub")
            driver = std::make_unique<DriverStub>(cfg.id, cfg.inicial, nucleo.cenario, nucleo.imagem);
        else
            driver = std::make_unique<DriverMQTT>(cfg.id, nucleo.imagem, nucleo.rota_visivel, cfg.broker);
        std::thread t_io([&] { driver->executar(nucleo.ativo); });

        Navegacao navegacao(nucleo);
        StatusRobo status(nucleo);
        TarefaPeriodica t_status("T_STATUS", 100 * NS_POR_MS, 75, [&](const InfoCiclo& i) { status.ciclo(i); });
        TarefaPeriodica t_nav("T_NAV", 50 * NS_POR_MS, 80, [&](const InfoCiclo& i) { navegacao.ciclo(i); });
        status.observar(&t_nav, &t_status);
        std::thread t_rota([&] { executar_planejador(nucleo); });

        t_status.iniciar();
        t_nav.iniciar();
        LOG("main", "pid %d no slot %d, posição inicial (%.1f, %.1f)", getpid(), cfg.slot, cfg.inicial.x, cfg.inicial.y);

        timespec espera{0, 200 * 1000 * 1000};
        while (nucleo.ativo.load()) {
            int s = sigtimedwait(&sinais, nullptr, &espera);
            if (s == SIGINT || s == SIGTERM) break;
        }
        LOG("main", "encerrando");
        nucleo.ativo = false;
        nucleo.cv_rota.notify_all();
        t_nav.parar();
        t_status.parar();
        nucleo.imagem.acordar();
        t_rota.join();
        t_io.join();
    } catch (const std::exception& e) {
        LOG("main", "erro fatal: %s", e.what());
        codigo = 1;
    }
    console::encerrar();
    return codigo;
}
