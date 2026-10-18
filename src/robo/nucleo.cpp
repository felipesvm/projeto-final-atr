#include "nucleo.hpp"

#include <cstdio>

#include "../comum/nomes.hpp"

namespace amr {

namespace {
Cenario carregar(const ConfigRobo& cfg) { return Cenario::carregar(cfg.cenario); }
}  // namespace

NucleoRobo::NucleoRobo(ConfigRobo c)
    : cfg(std::move(c)), cenario(carregar(cfg)), mapa(cenario), p(cenario.parametros) {
    shm = MemoriaCompartilhada::abrir(nomes::SHM_FROTA);
    frota = std::make_unique<Frota>(shm.dados());
    if (cfg.slot < 0 || uint32_t(cfg.slot) >= frota->n_max()) throw std::runtime_error("slot fora da tabela da frota");
    slot = &frota->robo(uint32_t(cfg.slot));
    semaforos = SemaforosPassagem::abrir(frota->cabecalho().semid, int(frota->n_passagens()));
    org = FilaMensagens::abrir(nomes::FILA_ORGANIZADOR, FilaMensagens::Acesso::Escrita);
    comandos = FilaMensagens::criar(nomes::fila_comandos(cfg.id), 16, sizeof(Mensagem), FilaMensagens::Acesso::Leitura);
}

Mensagem NucleoRobo::evento(TipoMsg tipo) const {
    Mensagem m = nova_mensagem(tipo, origem());
    m.evento.robo = cfg.id;
    m.evento.equipe = -1;
    m.evento.caixa = -1;
    return m;
}

void NucleoRobo::registrar(Categoria cat, const char* texto, int caixa) {
    char origem_txt[16];
    std::snprintf(origem_txt, sizeof origem_txt, "robo_%d", cfg.id);
    Registro r = novo_registro(origem_txt, cat, "%s", texto);
    r.robo = cfg.id;
    r.caixa = caixa;
    rastreador.registrar(r);
}

}  // namespace amr
