#include "console.hpp"

#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

#include "tempo.hpp"

namespace amr::console {

namespace {

struct Estado {
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::string> linhas;
    std::thread escritor;
    std::string processo = "amr";
    bool ativo = false;
};

Estado& estado() {
    static Estado e;
    return e;
}

constexpr size_t MAX_LINHAS = 4096;

void laco_escrita() {
    auto& e = estado();
    std::unique_lock<std::mutex> trava(e.mtx);
    for (;;) {
        e.cv.wait(trava, [&] { return !e.linhas.empty() || !e.ativo; });
        std::deque<std::string> lote;
        lote.swap(e.linhas);
        bool continuar = e.ativo;
        trava.unlock();
        for (auto& l : lote) std::fputs(l.c_str(), stdout);
        std::fflush(stdout);
        trava.lock();
        if (!continuar && e.linhas.empty()) return;
    }
}

}  // namespace

void iniciar(const std::string& processo) {
    auto& e = estado();
    std::lock_guard<std::mutex> trava(e.mtx);
    if (e.ativo) return;
    e.processo = processo;
    e.ativo = true;
    e.escritor = std::thread(laco_escrita);
}

void encerrar() {
    auto& e = estado();
    {
        std::lock_guard<std::mutex> trava(e.mtx);
        if (!e.ativo) return;
        e.ativo = false;
    }
    e.cv.notify_all();
    if (e.escritor.joinable()) e.escritor.join();
}

void escrever(const char* origem, const char* formato, ...) {
    char corpo[512];
    va_list args;
    va_start(args, formato);
    std::vsnprintf(corpo, sizeof corpo, formato, args);
    va_end(args);

    auto& e = estado();
    std::string hora = data_hora_iso(parede_ns()).substr(11);
    char linha[700];
    std::snprintf(linha, sizeof linha, "%s [%s] %s: %s\n", hora.c_str(), e.processo.c_str(), origem, corpo);

    {
        std::lock_guard<std::mutex> trava(e.mtx);
        if (!e.ativo) {
            std::fputs(linha, stdout);
            return;
        }
        if (e.linhas.size() < MAX_LINHAS) e.linhas.emplace_back(linha);
    }
    e.cv.notify_one();
}

}  // namespace amr::console
