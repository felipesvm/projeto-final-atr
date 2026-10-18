// Ferramenta de teste da Etapa 1: injeta eventos de hardware nos robôs e comandos de operador
// no organizador (papel que a tela de operação e o simulador assumem na Etapa 2).

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "../src/comum/ipc.hpp"
#include "../src/comum/nomes.hpp"
#include "../src/comum/protocolo.hpp"

using namespace amr;

namespace {
void uso() {
    std::fprintf(stderr,
                 "uso: injetar_evento COMANDO [argumentos]\n"
                 "  hardware (fila de teste do robô):\n"
                 "    defeito ID on|off        bateria ID NIVEL        comunicacao ID off|on\n"
                 "  operador (fila do organizador):\n"
                 "    parada-geral             liberar-parada\n"
                 "    caixa ID TAMANHO BAIA ZONA\n"
                 "    parar ID   retomar ID   reconhecer ID   abortar ID\n"
                 "    modo ID auto|manual      destino ID X Y          remover-robo ID\n");
}

void injetar(int robo, Injecao tipo, float valor) {
    auto fila = FilaMensagens::abrir(nomes::fila_hardware(robo), FilaMensagens::Acesso::Escrita);
    Mensagem m = nova_mensagem(TipoMsg::INJETAR, ORIGEM_FERRAMENTA);
    m.injecao = {tipo, valor};
    if (!fila.enviar(&m, sizeof m, 10, 100 * NS_POR_MS)) throw std::runtime_error("fila de teste do robô cheia");
}

void operador(const CorpoOperador& op) {
    auto fila = FilaMensagens::abrir(nomes::FILA_ORGANIZADOR, FilaMensagens::Acesso::Escrita);
    Mensagem m = nova_mensagem(TipoMsg::OPERADOR, ORIGEM_FERRAMENTA);
    m.operador = op;
    if (!fila.enviar(&m, sizeof m, prioridade(TipoMsg::OPERADOR), NS_POR_S))
        throw std::runtime_error("fila do organizador cheia");
}

void ao_robo(int robo, TipoMsg tipo, CorpoComando cmd = {}) {
    CorpoOperador op{};
    op.comando = ComandoOperador::ENVIAR_AO_ROBO;
    op.robo = robo;
    op.tipo_robo = tipo;
    op.cmd = cmd;
    operador(op);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        uso();
        return 2;
    }
    std::string c = argv[1];
    auto arg = [&](int i) -> std::string {
        if (i >= argc) throw std::runtime_error("faltam argumentos para " + c);
        return argv[i];
    };
    try {
        if (c == "defeito") {
            injetar(std::stoi(arg(2)), arg(3) == "on" ? Injecao::DEFEITO_ATIVA : Injecao::DEFEITO_REMOVE, 0);
        } else if (c == "bateria") {
            injetar(std::stoi(arg(2)), Injecao::BATERIA, std::stof(arg(3)));
        } else if (c == "comunicacao") {
            injetar(std::stoi(arg(2)), arg(3) == "off" ? Injecao::COMUNICACAO_CORTA : Injecao::COMUNICACAO_VOLTA, 0);
        } else if (c == "parada-geral" || c == "liberar-parada") {
            CorpoOperador op{};
            op.comando = c == "parada-geral" ? ComandoOperador::PARADA_GERAL : ComandoOperador::LIBERAR_PARADA;
            operador(op);
        } else if (c == "caixa") {
            CorpoOperador op{};
            op.comando = ComandoOperador::NOVA_CAIXA;
            op.caixa = std::stoi(arg(2));
            op.tamanho = std::stoi(arg(3));
            op.baia = std::stoi(arg(4));
            op.zona = std::stoi(arg(5));
            operador(op);
        } else if (c == "remover-robo") {
            CorpoOperador op{};
            op.comando = ComandoOperador::REMOVER_ROBO;
            op.robo = std::stoi(arg(2));
            operador(op);
        } else {
            static const std::map<std::string, TipoMsg> simples = {{"parar", TipoMsg::PARAR},
                                                                   {"retomar", TipoMsg::RETOMAR},
                                                                   {"reconhecer", TipoMsg::RECONHECER_FALHA},
                                                                   {"abortar", TipoMsg::ABORTAR}};
            auto it = simples.find(c);
            if (it != simples.end()) {
                ao_robo(std::stoi(arg(2)), it->second);
            } else if (c == "modo") {
                CorpoComando cmd{};
                cmd.modo = arg(3) == "manual" ? ModoOperacao::MANUAL : ModoOperacao::AUTO;
                ao_robo(std::stoi(arg(2)), TipoMsg::MODO, cmd);
            } else if (c == "destino") {
                CorpoComando cmd{};
                cmd.x = std::stof(arg(3));
                cmd.y = std::stof(arg(4));
                ao_robo(std::stoi(arg(2)), TipoMsg::DESTINO_MANUAL, cmd);
            } else {
                uso();
                return 2;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "injetar_evento: %s\n", e.what());
        return 1;
    }
    return 0;
}
