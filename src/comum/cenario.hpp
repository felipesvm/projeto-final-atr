#pragma once

#include <string>
#include <vector>

namespace amr {

struct Ponto {
    double x = 0, y = 0;
};

double distancia(const Ponto& a, const Ponto& b);

struct Retangulo {
    int x = 0, y = 0, w = 0, h = 0;
    bool contem_celula(int cx, int cy) const { return cx >= x && cx < x + w && cy >= y && cy < y + h; }
};

struct DefPassagem {
    std::string nome;
    Retangulo area;
    int capacidade = 1;
};

struct Baia {
    int id = 0;
    Ponto pr;
};

struct Zona {
    int id = 0;
    Ponto ponto;
};

struct DefCaixa {
    int id = 0;
    int tamanho = 1;
    int baia = 0;
    int zona = 0;
};

struct Parametros {
    double velocidade_max = 1.0;      // m/s
    double aceleracao = 1.0;          // m/s²
    double tolerancia_chegada = 0.2;  // m
    double afastamento_formacao = 0.3;
    double descarga_movendo = 0.05;  // %/s
    double descarga_parado = 0.01;
    double recarga = 2.0;
    double bateria_inicial = 100.0;
    double bateria_baixa = 30.0;
    double bateria_critica = 15.0;
    double bateria_recarregada = 95.0;
    double tempo_atracar_s = 1.0;
    double envelhecimento_s = 30.0;
    double prazo_reuniao_s = 60.0;
    double prazo_acoplamento_s = 3.0;
    double espera_cessao_s = 2.0;
    double espera_rota_alternativa_s = 20.0;
};

struct Cenario {
    int largura = 0, altura = 0;
    std::vector<Retangulo> obstaculos;
    std::vector<DefPassagem> passagens;
    std::vector<Baia> baias;
    std::vector<Zona> zonas;
    std::vector<Ponto> estacoes_recarga;
    Retangulo estacionamento;  // onde a ferramenta de teste posiciona os robôs anunciados
    std::vector<DefCaixa> caixas;
    Parametros parametros;

    static Cenario carregar(const std::string& caminho);
    const Baia* baia(int id) const;
    const Zona* zona(int id) const;
    std::vector<int> capacidades() const;
};

}  // namespace amr
