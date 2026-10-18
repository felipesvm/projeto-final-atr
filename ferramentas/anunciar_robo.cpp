// Substitui, na Etapa 1, o anúncio de robô novo que o simulador fará via MQTT na Etapa 2.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "../src/comum/cenario.hpp"
#include "../src/comum/ipc.hpp"
#include "../src/comum/nomes.hpp"
#include "../src/comum/protocolo.hpp"

using namespace amr;

namespace {
void uso() {
    std::fprintf(stderr,
                 "uso: anunciar_robo ID [X Y]\n"
                 "     anunciar_robo --n N [--primeiro ID] [--intervalo-ms T] [--cenario arquivo]\n");
}

Ponto vaga(const Cenario& c, int i) {
    const auto& e = c.estacionamento;
    int por_linha = e.w > 0 ? e.w : 10;
    int linhas = e.h > 0 ? e.h : 1;
    int col = i % por_linha, lin = (i / por_linha) % linhas;
    double desloc = 0.25 * double(i / (por_linha * linhas));  // passa da capacidade: sobrepõe com deslocamento
    return {e.x + col + 0.5 + desloc, e.y + lin + 0.5};
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        uso();
        return 2;
    }
    std::string cenario = "config/cenario.json";
    int n = 0, primeiro = 1, intervalo_ms = 0, id = -1;
    double x = -1, y = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--n" && i + 1 < argc) n = std::stoi(argv[++i]);
        else if (a == "--primeiro" && i + 1 < argc) primeiro = std::stoi(argv[++i]);
        else if (a == "--intervalo-ms" && i + 1 < argc) intervalo_ms = std::stoi(argv[++i]);
        else if (a == "--cenario" && i + 1 < argc) cenario = argv[++i];
        else if (id < 0 && a[0] != '-') id = std::stoi(a);
        else if (x < 0 && a[0] != '-') x = std::stod(a);
        else if (y < 0 && a[0] != '-') y = std::stod(a);
        else {
            uso();
            return 2;
        }
    }
    try {
        Cenario c = Cenario::carregar(cenario);
        auto fila = FilaMensagens::abrir(nomes::FILA_ANUNCIO, FilaMensagens::Acesso::Escrita);
        auto anunciar = [&](int hw, Ponto p) {
            Mensagem m = nova_mensagem(TipoMsg::ANUNCIO, ORIGEM_FERRAMENTA);
            m.anuncio = {hw, float(p.x), float(p.y)};
            fila.enviar(&m, sizeof m, 20, -1);
            std::printf("anunciado robô %d em (%.1f, %.1f)\n", hw, p.x, p.y);
        };
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                anunciar(primeiro + i, vaga(c, primeiro + i - 1));
                if (intervalo_ms > 0 && i + 1 < n) std::this_thread::sleep_for(std::chrono::milliseconds(intervalo_ms));
            }
        } else if (id >= 0) {
            anunciar(id, x >= 0 && y >= 0 ? Ponto{x, y} : vaga(c, id - 1));
        } else {
            uso();
            return 2;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "anunciar_robo: %s (o gestor está em execução?)\n", e.what());
        return 1;
    }
    return 0;
}
