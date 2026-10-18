#pragma once

// Executor mínimo de testes, sem dependências externas.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace teste {

struct Caso {
    const char* nome;
    std::function<void()> corpo;
};

inline std::vector<Caso>& casos() {
    static std::vector<Caso> v;
    return v;
}

inline int& falhas_no_caso() {
    static int n = 0;
    return n;
}

struct Registrador {
    Registrador(const char* nome, std::function<void()> corpo) { casos().push_back({nome, std::move(corpo)}); }
};

inline int executar_todos() {
    int falharam = 0;
    for (auto& c : casos()) {
        falhas_no_caso() = 0;
        c.corpo();
        bool ok = falhas_no_caso() == 0;
        std::printf("[%s] %s\n", ok ? " OK " : "FALHA", c.nome);
        falharam += !ok;
    }
    std::printf("\n%zu testes, %d falha(s)\n", casos().size(), falharam);
    return falharam == 0 ? 0 : 1;
}

}  // namespace teste

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)
#define TESTE(nome)                                                             \
    static void CONCAT(teste_, __LINE__)();                                     \
    static teste::Registrador CONCAT(reg_, __LINE__)(nome, CONCAT(teste_, __LINE__)); \
    static void CONCAT(teste_, __LINE__)()

#define VERIFICA(cond)                                                                  \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::printf("    %s:%d: falhou: %s\n", __FILE__, __LINE__, #cond);          \
            ++teste::falhas_no_caso();                                                  \
        }                                                                               \
    } while (0)

#define VERIFICA_PERTO(a, b, tol) VERIFICA(std::fabs(double(a) - double(b)) <= (tol))
