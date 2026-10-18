// Ferramenta do teste T-04: coloca de uma vez na fila de um robô N missões com um PARAR no meio,
// para verificar que a T_NAV recebe o PARAR antes das missões pendentes, sem perdas nem duplicações.

#include <cstdio>
#include <string>

#include "../src/comum/ipc.hpp"
#include "../src/comum/nomes.hpp"
#include "../src/comum/protocolo.hpp"

using namespace amr;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: rajada_comandos ID_ROBO N_MISSOES\n");
        return 2;
    }
    int robo = std::stoi(argv[1]), n = std::stoi(argv[2]);
    try {
        auto fila = FilaMensagens::abrir(nomes::fila_comandos(robo), FilaMensagens::Acesso::Escrita);
        long espaco = fila.capacidade() - 1;
        if (n > espaco) n = int(espaco);
        auto parar = [&] {
            Mensagem p = nova_mensagem(TipoMsg::PARAR, ORIGEM_FERRAMENTA);
            fila.enviar(&p, sizeof p, prioridade(p.cab.tipo), -1);
            std::printf("enviado PARAR seq=%u\n", p.cab.seq);
        };
        for (int i = 0; i < n; ++i) {
            if (i == n / 2) parar();
            Mensagem m = nova_mensagem(TipoMsg::MISSAO_EQUIPE, ORIGEM_FERRAMENTA);
            m.missao.equipe = -1;  // inválida de propósito: o robô registra e recusa
            m.missao.caixa = 1000 + i;
            fila.enviar(&m, sizeof m, prioridade(m.cab.tipo), -1);
            std::printf("enviado MISSAO_EQUIPE seq=%u\n", m.cab.seq);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "rajada_comandos: %s\n", e.what());
        return 1;
    }
    return 0;
}
