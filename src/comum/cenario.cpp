#include "cenario.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>

#include "../../third_party/nlohmann/json.hpp"

namespace amr {

using json = nlohmann::json;

double distancia(const Ponto& a, const Ponto& b) { return std::hypot(a.x - b.x, a.y - b.y); }

namespace {
Retangulo ler_retangulo(const json& j) { return {j.at("x"), j.at("y"), j.at("w"), j.at("h")}; }
Ponto ler_ponto(const json& j) { return {j.at("x"), j.at("y")}; }

template <typename T>
void ler_opcional(const json& j, const char* chave, T& destino) {
    if (j.contains(chave)) destino = j.at(chave).get<T>();
}
}  // namespace

Cenario Cenario::carregar(const std::string& caminho) {
    std::ifstream arq(caminho);
    if (!arq) throw std::runtime_error("não foi possível abrir o cenário " + caminho);
    json j = json::parse(arq);

    Cenario c;
    c.largura = j.at("galpao").at("largura");
    c.altura = j.at("galpao").at("altura");
    for (auto& o : j.value("obstaculos", json::array())) c.obstaculos.push_back(ler_retangulo(o));
    for (auto& p : j.value("passagens", json::array()))
        c.passagens.push_back({p.at("nome"), ler_retangulo(p), p.at("capacidade")});
    for (auto& b : j.value("baias", json::array())) c.baias.push_back({b.at("id"), ler_ponto(b)});
    for (auto& z : j.value("zonas", json::array())) c.zonas.push_back({z.at("id"), ler_ponto(z)});
    for (auto& e : j.value("estacoes_recarga", json::array())) c.estacoes_recarga.push_back(ler_ponto(e));
    if (j.contains("estacionamento")) c.estacionamento = ler_retangulo(j.at("estacionamento"));
    for (auto& cx : j.value("caixas", json::array()))
        c.caixas.push_back({cx.at("id"), cx.at("tamanho"), cx.at("baia"), cx.at("zona")});

    if (j.contains("parametros")) {
        const json& p = j.at("parametros");
        auto& d = c.parametros;
        ler_opcional(p, "velocidade_max", d.velocidade_max);
        ler_opcional(p, "aceleracao", d.aceleracao);
        ler_opcional(p, "tolerancia_chegada", d.tolerancia_chegada);
        ler_opcional(p, "afastamento_formacao", d.afastamento_formacao);
        ler_opcional(p, "descarga_movendo", d.descarga_movendo);
        ler_opcional(p, "descarga_parado", d.descarga_parado);
        ler_opcional(p, "recarga", d.recarga);
        ler_opcional(p, "bateria_inicial", d.bateria_inicial);
        ler_opcional(p, "bateria_baixa", d.bateria_baixa);
        ler_opcional(p, "bateria_critica", d.bateria_critica);
        ler_opcional(p, "bateria_recarregada", d.bateria_recarregada);
        ler_opcional(p, "tempo_atracar_s", d.tempo_atracar_s);
        ler_opcional(p, "envelhecimento_s", d.envelhecimento_s);
        ler_opcional(p, "prazo_reuniao_s", d.prazo_reuniao_s);
        ler_opcional(p, "prazo_acoplamento_s", d.prazo_acoplamento_s);
        ler_opcional(p, "espera_cessao_s", d.espera_cessao_s);
        ler_opcional(p, "espera_rota_alternativa_s", d.espera_rota_alternativa_s);
    }
    for (auto& cx : c.caixas) {
        if (cx.tamanho < 1 || cx.tamanho > 4) throw std::runtime_error("caixa com tamanho fora de 1..4");
        if (!c.baia(cx.baia) || !c.zona(cx.zona)) throw std::runtime_error("caixa com baia ou zona inexistente");
    }
    return c;
}

const Baia* Cenario::baia(int id) const {
    for (auto& b : baias)
        if (b.id == id) return &b;
    return nullptr;
}

const Zona* Cenario::zona(int id) const {
    for (auto& z : zonas)
        if (z.id == id) return &z;
    return nullptr;
}

std::vector<int> Cenario::capacidades() const {
    std::vector<int> v;
    for (auto& p : passagens) v.push_back(p.capacidade);
    return v;
}

}  // namespace amr
