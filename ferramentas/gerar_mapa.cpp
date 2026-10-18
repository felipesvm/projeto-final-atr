// Gera um galpão aleatório a cada execução (ponto extra de IA): duas paredes internas com passagens
// de capacidade 1 a 4, prateleiras sorteadas e fila de caixas por baia. Só aceita o mapa se todas as
// baias alcançam todas as zonas com equipes de 1 a 4 robôs e se as estações de recarga são alcançáveis.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>

#include "../src/comum/cenario.hpp"
#include "../src/comum/mapa.hpp"
#include "../third_party/nlohmann/json.hpp"

using namespace amr;
using json = nlohmann::json;

namespace {

struct Gerado {
    Cenario c;
    std::vector<int> paredes;
};

bool sobrepoe(const Retangulo& a, const Retangulo& b, int margem) {
    return a.x - margem < b.x + b.w && b.x - margem < a.x + a.w && a.y - margem < b.y + b.h && b.y - margem < a.y + a.h;
}

Gerado sortear(std::mt19937& rng, const Parametros& parametros) {
    auto entre = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    Gerado g;
    auto& c = g.c;
    c.largura = 36;
    c.altura = 24;
    c.parametros = parametros;

    int n_baias = entre(3, 4), n_zonas = entre(3, 4);
    for (int i = 0; i < n_baias; ++i) c.baias.push_back({i + 1, {3.5, 2.5 + i * (16.0 / (n_baias - 1))}});
    for (int i = 0; i < n_zonas; ++i) c.zonas.push_back({i + 1, {c.largura - 3.5, 2.5 + i * (16.0 / (n_zonas - 1))}});
    c.estacoes_recarga = {{0.5, 0.5}, {1.5, 0.5}, {c.largura - 0.5, 0.5}, {c.largura - 1.5, 0.5}};
    c.estacionamento = {1, 20, 9, 4};

    // duas paredes internas, cada uma com quatro passagens; capacidades 1..4 embaralhadas
    int xs[2] = {entre(11, 13), entre(22, 24)};
    char nome = 'A';
    for (int w = 0; w < 2; ++w) {
        int x = xs[w], espessura = entre(1, 2);
        g.paredes.push_back(x);
        c.obstaculos.push_back({x, 0, espessura, c.altura});
        std::vector<int> caps = {1, 2, 3, 4};
        std::shuffle(caps.begin(), caps.end(), rng);
        // faixas verticais disjuntas para as passagens
        int y = entre(1, 2);
        for (int i = 0; i < 4; ++i) {
            int cap = caps[size_t(i)];
            int altura = cap == 1 ? 1 : cap == 4 ? entre(2, 3) : entre(1, 2);
            if (y + altura > c.altura - 2) break;
            c.passagens.push_back({std::string(1, nome) + std::to_string(i + 1), {x, y, espessura, altura}, cap});
            y += altura + entre(3, 4);
        }
        nome++;
    }

    // prateleiras: blocos 1x2 a 1x4 (ou deitados) longe das áreas de trabalho
    std::vector<Retangulo> reservadas;
    for (auto& b : c.baias) reservadas.push_back({int(b.pr.x) - 2, int(b.pr.y) - 1, 4, 3});
    for (auto& z : c.zonas) reservadas.push_back({int(z.ponto.x) - 2, int(z.ponto.y) - 1, 4, 3});
    for (auto& e : c.estacoes_recarga) reservadas.push_back({int(e.x) - 1, int(e.y), 3, 2});
    reservadas.push_back({c.estacionamento.x - 1, c.estacionamento.y - 1, c.estacionamento.w + 2, c.estacionamento.h + 1});
    for (auto& p : c.passagens) reservadas.push_back({p.area.x - 2, p.area.y - 1, p.area.w + 4, p.area.h + 2});

    int prateleiras = entre(8, 14);
    for (int t = 0; t < 400 && prateleiras > 0; ++t) {
        bool deitada = entre(0, 1);
        int comp = entre(2, 4);
        Retangulo r{entre(1, c.largura - 5), entre(1, c.altura - 5), deitada ? comp : 1, deitada ? 1 : comp};
        bool conflito = std::any_of(reservadas.begin(), reservadas.end(), [&](const Retangulo& q) { return sobrepoe(r, q, 0); });
        for (int x : xs) conflito = conflito || (r.x <= x + 2 && r.x + r.w >= x - 1);
        for (auto& o : c.obstaculos) conflito = conflito || sobrepoe(r, o, 1);
        if (conflito) continue;
        c.obstaculos.push_back(r);
        prateleiras--;
    }

    std::discrete_distribution<int> tamanho({30, 30, 25, 15});
    int id = 1;
    for (auto& b : c.baias)
        for (int i = 0; i < 6; ++i) c.caixas.push_back({id++, tamanho(rng) + 1, b.id, entre(1, n_zonas)});
    return g;
}

bool valido(const Cenario& c) {
    Mapa m(c);
    const Ponto vaga{c.estacionamento.x + 0.5, c.estacionamento.y + 0.5};
    for (auto& b : c.baias) {
        if (!m.planejar(vaga, b.pr, {})) return false;
        for (auto& z : c.zonas)
            for (int k = 1; k <= 4; ++k) {
                OpcoesRota op;
                op.k = k;
                if (!m.planejar(b.pr, z.ponto, op)) return false;
            }
    }
    for (auto& z : c.zonas) {
        if (!m.planejar(z.ponto, vaga, {})) return false;
        for (auto& e : c.estacoes_recarga)
            if (!m.planejar(z.ponto, e, {})) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    unsigned semente = std::random_device{}();
    std::string saida = "runtime/cenario_aleatorio.json";
    std::string base = "config/cenario.json";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--semente" && i + 1 < argc) semente = unsigned(std::stoul(argv[++i]));
        else if (a == "--saida" && i + 1 < argc) saida = argv[++i];
        else if (a == "--base" && i + 1 < argc) base = argv[++i];
        else {
            std::fprintf(stderr, "uso: gerar_mapa [--semente S] [--saida arquivo] [--base cenario_dos_parametros]\n");
            return 2;
        }
    }
    try {
        Parametros parametros = Cenario::carregar(base).parametros;
        std::mt19937 rng(semente);
        Gerado g;
        int tentativas = 0;
        do {
            g = sortear(rng, parametros);
            ++tentativas;
        } while (!valido(g.c) && tentativas < 200);
        if (tentativas >= 200) throw std::runtime_error("nenhum mapa válido em 200 tentativas");

        const auto& c = g.c;
        auto ret = [](const Retangulo& r) { return json{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}}; };
        json j;
        j["descricao"] = "Galpão gerado aleatoriamente (semente " + std::to_string(semente) + ")";
        j["semente"] = semente;
        j["galpao"] = {{"largura", c.largura}, {"altura", c.altura}};
        j["obstaculos"] = json::array();
        for (auto& o : c.obstaculos) j["obstaculos"].push_back(ret(o));
        j["passagens"] = json::array();
        for (auto& p : c.passagens) {
            json q = ret(p.area);
            q["nome"] = p.nome;
            q["capacidade"] = p.capacidade;
            j["passagens"].push_back(q);
        }
        for (auto& b : c.baias) j["baias"].push_back({{"id", b.id}, {"x", b.pr.x}, {"y", b.pr.y}});
        for (auto& z : c.zonas) j["zonas"].push_back({{"id", z.id}, {"x", z.ponto.x}, {"y", z.ponto.y}});
        for (auto& e : c.estacoes_recarga) j["estacoes_recarga"].push_back({{"x", e.x}, {"y", e.y}});
        j["estacionamento"] = ret(c.estacionamento);
        for (auto& cx : c.caixas)
            j["caixas"].push_back({{"id", cx.id}, {"tamanho", cx.tamanho}, {"baia", cx.baia}, {"zona", cx.zona}});
        const auto& p = c.parametros;
        j["parametros"] = {{"velocidade_max", p.velocidade_max}, {"aceleracao", p.aceleracao},
                           {"tolerancia_chegada", p.tolerancia_chegada}, {"afastamento_formacao", p.afastamento_formacao},
                           {"descarga_movendo", p.descarga_movendo}, {"descarga_parado", p.descarga_parado},
                           {"recarga", p.recarga}, {"bateria_inicial", p.bateria_inicial}, {"bateria_baixa", p.bateria_baixa},
                           {"bateria_critica", p.bateria_critica}, {"bateria_recarregada", p.bateria_recarregada},
                           {"tempo_atracar_s", p.tempo_atracar_s}, {"envelhecimento_s", p.envelhecimento_s},
                           {"prazo_reuniao_s", p.prazo_reuniao_s}, {"prazo_acoplamento_s", p.prazo_acoplamento_s},
                           {"espera_cessao_s", p.espera_cessao_s}, {"espera_rota_alternativa_s", p.espera_rota_alternativa_s}};
        std::ofstream(saida) << j.dump(2) << "\n";
        Cenario::carregar(saida);
        std::printf("mapa aleatório gerado (semente %u, %d tentativa(s)): %zu passagens, %zu obstáculos, %zu caixas -> %s\n",
                    semente, tentativas, c.passagens.size(), c.obstaculos.size(), c.caixas.size(), saida.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "gerar_mapa: %s\n", e.what());
        return 1;
    }
    return 0;
}
