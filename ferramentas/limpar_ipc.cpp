// Remove objetos de IPC deixados por uma execução interrompida.

#include <dirent.h>
#include <sys/sem.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../src/comum/frota.hpp"
#include "../src/comum/ipc.hpp"
#include "../src/comum/nomes.hpp"

using namespace amr;

int main() {
    int removidos = 0;
    if (MemoriaCompartilhada::existe(nomes::SHM_FROTA)) {
        try {
            auto shm = MemoriaCompartilhada::abrir(nomes::SHM_FROTA);
            Frota frota(shm.dados());
            if (frota.cabecalho().semid >= 0 && semctl(frota.cabecalho().semid, 0, IPC_RMID) == 0) ++removidos;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "aviso: %s\n", e.what());
        }
    }
    for (const auto& n : {nomes::SHM_FROTA, nomes::SHM_EVENTOS}) MemoriaCompartilhada::remover(n);
    for (const auto& n : {nomes::SEM_VAZIOS, nomes::SEM_ITENS}) SemaforoNomeado::remover(n);
    for (const auto& n : {nomes::FILA_ORGANIZADOR, nomes::FILA_ANUNCIO}) FilaMensagens::remover(n);
    for (int id = 0; id <= 1024; ++id) {
        FilaMensagens::remover(nomes::fila_comandos(id));
        FilaMensagens::remover(nomes::fila_hardware(id));
    }
    // filas com id fora da faixa acima aparecem em /dev/mqueue, quando montado
    if (DIR* d = opendir("/dev/mqueue")) {
        while (dirent* e = readdir(d))
            if (std::strncmp(e->d_name, "amr_", 4) == 0) FilaMensagens::remover("/" + std::string(e->d_name));
        closedir(d);
    }
    std::printf("objetos de IPC do sistema AMR removidos%s\n", removidos ? " (incluindo semáforos System V)" : "");
    return 0;
}
