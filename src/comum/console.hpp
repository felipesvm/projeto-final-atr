#pragma once

#include <string>

namespace amr {

// As threads só enfileiram a linha; quem escreve no terminal é uma thread própria.
namespace console {

void iniciar(const std::string& processo);
void encerrar();
void escrever(const char* origem, const char* formato, ...) __attribute__((format(printf, 2, 3)));

}  // namespace console

#define LOG(origem, ...) ::amr::console::escrever(origem, __VA_ARGS__)

}  // namespace amr
