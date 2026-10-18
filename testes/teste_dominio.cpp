// T-01: lógica de domínio isolada, sem threads nem IPC.

#include <algorithm>

#include "../src/comum/cenario.hpp"
#include "../src/comum/mapa.hpp"
#include "../src/comum/protocolo.hpp"
#include "../src/organizador/despachante.hpp"
#include "../src/robo/dominio.hpp"
#include "teste.hpp"

using namespace amr;

namespace {

Cenario galpao() { return Cenario::carregar("config/cenario.json"); }

bool usa_passagem(const Rota& r, int p) {
    return std::any_of(r.begin(), r.end(), [&](const Waypoint& w) { return w.passagem == p; });
}

bool vizinhos(const Rota& r) {
    for (size_t i = 1; i + 1 < r.size(); ++i)
        if (std::fabs(r[i].x - r[i - 1].x) + std::fabs(r[i].y - r[i - 1].y) > 1.01) return false;
    return true;
}

}  // namespace

TESTE("protocolo: mensagens têm 128 bytes") { VERIFICA(sizeof(Mensagem) == 128); }

TESTE("protocolo: PARAR tem prioridade maior que MISSAO_EQUIPE") {
    VERIFICA(prioridade(TipoMsg::PARAR) > prioridade(TipoMsg::MISSAO_EQUIPE));
    VERIFICA(prioridade(TipoMsg::FALHA) > prioridade(TipoMsg::EM_POSICAO));
}

TESTE("mapa: rota individual atravessa a parede pela passagem mais curta") {
    Cenario c = galpao();
    Mapa m(c);
    auto r = m.planejar({4.5, 3.5}, {25.5, 3.5}, {});
    VERIFICA(r.has_value());
    if (!r) return;
    VERIFICA(usa_passagem(*r, 0));
    VERIFICA(vizinhos(*r));
    VERIFICA_PERTO(r->back().x, 25.5, 1e-9);
    VERIFICA_PERTO(r->back().y, 3.5, 1e-9);
    for (auto& w : *r) VERIFICA(m.livre(int(w.x), int(w.y)));
}

TESTE("mapa: equipe de 2 não usa passagem de capacidade 1") {
    Cenario c = galpao();
    Mapa m(c);
    OpcoesRota op;
    op.k = 2;
    auto r = m.planejar({4.5, 3.5}, {25.5, 3.5}, op);
    VERIFICA(r.has_value());
    if (!r) return;
    VERIFICA(!usa_passagem(*r, 0));
    VERIFICA(usa_passagem(*r, 1));
}

TESTE("mapa: equipe de 4 só passa pelo corredor principal") {
    Cenario c = galpao();
    Mapa m(c);
    OpcoesRota op;
    op.k = 4;
    auto r = m.planejar({4.5, 8.5}, {25.5, 8.5}, op);
    VERIFICA(r.has_value());
    if (!r) return;
    for (int p = 0; p < 3; ++p) VERIFICA(!usa_passagem(*r, p));
    bool subiu = std::any_of(r->begin(), r->end(), [](const Waypoint& w) { return w.y >= 16; });
    VERIFICA(subiu);
}

TESTE("mapa: passagem bloqueada é evitada na rota alternativa") {
    Cenario c = galpao();
    Mapa m(c);
    OpcoesRota op;
    op.bloqueadas = {0};
    auto r = m.planejar({4.5, 3.5}, {25.5, 3.5}, op);
    VERIFICA(r.has_value());
    if (r) VERIFICA(!usa_passagem(*r, 0));
}

TESTE("mapa: destino dentro de obstáculo não tem rota") {
    Cenario c = galpao();
    Mapa m(c);
    VERIFICA(!m.planejar({4.5, 3.5}, {14.5, 6.5}, {}).has_value());
}

TESTE("mapa: posição dentro da passagem considera a margem") {
    Cenario c = galpao();
    Mapa m(c);
    VERIFICA(m.dentro_da_passagem(0, {14.9, 3.5}, 0.3));
    VERIFICA(m.dentro_da_passagem(0, {13.8, 3.5}, 0.3));
    VERIFICA(!m.dentro_da_passagem(0, {13.5, 3.5}, 0.3));
}

TESTE("seguidor: avança pelos waypoints e reconhece o fim") {
    SeguidorRota s;
    s.definir({{1, 1, -1}, {2, 1, 0}, {3, 1, 0}, {4, 1, -1}});
    VERIFICA(s.chegou({1.1, 1.0}, 0.2));
    VERIFICA(!s.chegou({1.5, 1.0}, 0.2));
    auto seq = s.passagens_em_sequencia(1);
    VERIFICA(seq.size() == 1 && seq[0] == 0);
    for (int i = 0; i < 3; ++i) s.avancar();
    VERIFICA(s.eh_ultimo());
    s.avancar();
    VERIFICA(s.concluida());
    VERIFICA(s.alvo() == nullptr);
}

TESTE("seguidor: passagens consecutivas são agrupadas para aquisição conjunta") {
    SeguidorRota s;
    s.definir({{1, 1, -1}, {2, 1, 0}, {3, 1, 1}, {4, 1, 1}, {5, 1, -1}, {6, 1, 2}});
    auto seq = s.passagens_em_sequencia(1);
    VERIFICA(seq.size() == 2 && seq[0] == 0 && seq[1] == 1);
}

TESTE("bateria: baixa, crítica e histerese até recarregar") {
    MonitorBateria b(30, 15, 95);
    VERIFICA(!b.avaliar(80));
    VERIFICA(b.avaliar(29.9) && b.nivel() == NivelBateria::BAIXA);
    VERIFICA(!b.avaliar(31));
    VERIFICA(b.nivel() == NivelBateria::BAIXA);
    VERIFICA(b.avaliar(15) && b.nivel() == NivelBateria::CRITICA);
    VERIFICA(!b.avaliar(60));
    VERIFICA(b.avaliar(95) && b.nivel() == NivelBateria::NORMAL);
}

TESTE("defeito: borda de subida e de descida") {
    DetectorBorda d;
    VERIFICA(d.atualizar(false) == 0);
    VERIFICA(d.atualizar(true) == 1);
    VERIFICA(d.atualizar(true) == 0);
    VERIFICA(d.atualizar(false) == -1);
}

TESTE("formação: afastamentos distintos para cada membro") {
    for (int k = 1; k <= 4; ++k)
        for (int i = 0; i < k; ++i)
            for (int j = i + 1; j < k; ++j)
                VERIFICA(distancia(afastamento_formacao(k, i, 0.3), afastamento_formacao(k, j, 0.3)) > 0.1);
}

TESTE("despacho: tudo-ou-nada com os robôs mais próximos") {
    Despachante d(30);
    auto r = d.decidir({{1, 2, {0, 0}, 5}}, {{10, {9, 9}}, {11, {1, 0}}, {12, {0, 1}}}, 3);
    VERIFICA(r.designacoes.size() == 1);
    if (r.designacoes.empty()) return;
    auto& rob = r.designacoes[0].robos;
    VERIFICA(rob.size() == 2);
    VERIFICA(std::find(rob.begin(), rob.end(), 10) == rob.end());
}

TESTE("despacho: sem robôs suficientes, a caixa não recebe equipe parcial") {
    Despachante d(30);
    auto r = d.decidir({{1, 3, {0, 0}, 5}, {2, 1, {0, 0}, 1}}, {{10, {0, 0}}, {11, {0, 0}}}, 4);
    VERIFICA(r.designacoes.size() == 1);
    if (!r.designacoes.empty()) VERIFICA(r.designacoes[0].caixa == 2);
}

TESTE("despacho: caixa envelhecida reserva os robôs livres") {
    Despachante d(30);
    auto r = d.decidir({{1, 3, {0, 0}, 40}, {2, 1, {0, 0}, 1}}, {{10, {0, 0}}, {11, {0, 0}}}, 4);
    VERIFICA(r.designacoes.empty());
    VERIFICA(r.reserva == 1);
}

TESTE("despacho: caixa maior que a frota fica aguardando e não bloqueia as demais") {
    Despachante d(30);
    auto r = d.decidir({{1, 4, {0, 0}, 100}, {2, 1, {0, 0}, 1}}, {{10, {0, 0}}, {11, {0, 0}}, {12, {0, 0}}}, 3);
    VERIFICA(r.aguardando_frota.size() == 1 && r.aguardando_frota[0] == 1);
    VERIFICA(r.reserva == -1);
    VERIFICA(r.designacoes.size() == 1 && r.designacoes[0].caixa == 2);
}

TESTE("despacho: com N = 1 só caixas de tamanho 1 são designadas") {
    Despachante d(30);
    auto r = d.decidir({{1, 2, {0, 0}, 10}, {2, 1, {0, 0}, 5}}, {{10, {0, 0}}}, 1);
    VERIFICA(r.designacoes.size() == 1 && r.designacoes[0].caixa == 2);
    VERIFICA(r.aguardando_frota.size() == 1 && r.aguardando_frota[0] == 1);
}

TESTE("mapa: custo adaptativo desvia da passagem congestionada") {
    Cenario c = galpao();
    Mapa m(c);
    OpcoesRota op;
    auto direta = m.planejar({4.5, 3.5}, {25.5, 3.5}, op);
    op.custo_passagem = {40.0, 0.0, 0.0};  // P1 com espera aprendida alta
    auto alternativa = m.planejar({4.5, 3.5}, {25.5, 3.5}, op);
    VERIFICA(direta && alternativa);
    if (!direta || !alternativa) return;
    VERIFICA(usa_passagem(*direta, 0));
    VERIFICA(!usa_passagem(*alternativa, 0));
}

TESTE("mapa: células evitadas ficam fora da rota") {
    Cenario c = galpao();
    Mapa m(c);
    OpcoesRota op;
    op.celulas_evitadas = {{6, 3}};
    auto r = m.planejar({4.5, 3.5}, {8.5, 3.5}, op);
    VERIFICA(r.has_value());
    if (!r) return;
    for (auto& w : *r) VERIFICA(!(int(w.x) == 6 && int(w.y) == 3));
}

TESTE("mapa: distâncias da BFS contornam a parede") {
    Cenario c = galpao();
    Mapa m(c);
    auto campo = m.distancias({4.5, 3.5});
    VERIFICA(m.distancia_celulas(campo, {4.5, 3.5}) == 0);
    VERIFICA(m.distancia_celulas(campo, {6.5, 3.5}) == 2);
    VERIFICA(m.distancia_celulas(campo, {14.5, 6.5}) == -1);  // dentro da parede
    VERIFICA(m.distancia_celulas(campo, {17.5, 3.5}) == 13);
}

TESTE("despacho: custo informado escolhe pelo caminho real") {
    Despachante d(30);
    // o robô 10 está mais perto em linha reta, mas o custo informado diz o contrário
    auto custo = [](const RoboLivre& r, const CaixaPronta&) { return r.id == 10 ? 50.0 : 5.0; };
    auto r = d.decidir({{1, 1, {0, 0}, 5}}, {{10, {0.5, 0}}, {11, {5, 0}}}, 2, custo);
    VERIFICA(r.designacoes.size() == 1 && r.designacoes[0].robos[0] == 11);
}

namespace {
Vizinho viz(int id, Ponto p, Ponto v, int prioridade, bool parado = false, int equipe = -1) {
    return {id, p, v, equipe, parado, prioridade};
}
}  // namespace

TESTE("anticolisão: caminho livre segue") {
    Anticolisao a;
    auto d = a.avaliar({0, 0}, {5, 0}, 1.0, 100, -1, -1, {viz(2, {0, 3}, {0, 0}, 50)}, 0);
    VERIFICA(d.acao == Anticolisao::Acao::SEGUIR);
}

TESTE("anticolisão: cruzamento com robô de maior prioridade faz esperar") {
    Anticolisao a;
    // o outro vem de baixo e cruza a minha trajetória no mesmo instante
    auto d = a.avaliar({0, 0}, {5, 0}, 1.0, 100, -1, -1, {viz(2, {1.5, -1.5}, {0, 1}, 200)}, 0);
    VERIFICA(d.acao == Anticolisao::Acao::ESPERAR);
    VERIFICA(d.obstaculo == 2);
}

TESTE("anticolisão: quem tem a vez segue no cruzamento") {
    Anticolisao a;
    auto d = a.avaliar({0, 0}, {5, 0}, 1.0, 300, -1, -1, {viz(2, {1.5, -1.5}, {0, 1}, 200)}, 0);
    VERIFICA(d.acao == Anticolisao::Acao::SEGUIR);
}

TESTE("anticolisão: robô parado no caminho é contornado depois de uma breve espera") {
    Anticolisao a;
    std::vector<Vizinho> v = {viz(2, {1.0, 0}, {0, 0}, 50, true)};
    VERIFICA(a.avaliar({0, 0}, {5, 0}, 1.0, 100, -1, -1, v, 0).acao == Anticolisao::Acao::ESPERAR);
    VERIFICA(a.avaliar({0, 0}, {5, 0}, 1.0, 100, -1, -1, v, 1.0).acao == Anticolisao::Acao::DESVIAR);
}

TESTE("anticolisão: colega de formação é ignorado") {
    Anticolisao a;
    auto d = a.avaliar({0, 0}, {5, 0}, 1.0, 100, 7, 7, {viz(2, {0.5, 0}, {1, 0}, 300, false, 7)}, 0);
    VERIFICA(d.acao == Anticolisao::Acao::SEGUIR);
}

int main() { return teste::executar_todos(); }
