#pragma once

// Classes de domínio do robô: sem threads, sem IPC, testáveis isoladamente.

#include <cmath>
#include <cstddef>
#include <vector>

#include "../comum/cenario.hpp"
#include "../comum/mapa.hpp"

namespace amr {

class SeguidorRota {
public:
    void definir(Rota rota) {
        rota_ = std::move(rota);
        indice_ = 0;
    }
    void limpar() { definir({}); }

    bool vazia() const { return rota_.empty(); }
    bool concluida() const { return indice_ >= rota_.size(); }
    size_t indice() const { return indice_; }
    const Waypoint* alvo() const { return concluida() ? nullptr : &rota_[indice_]; }
    const Waypoint* proximo() const { return indice_ + 1 < rota_.size() ? &rota_[indice_ + 1] : nullptr; }
    bool eh_ultimo() const { return indice_ + 1 == rota_.size(); }
    void avancar() {
        if (!concluida()) ++indice_;
    }

    bool chegou(const Ponto& pos, double tolerancia) const {
        auto* a = alvo();
        return a && distancia(pos, {a->x, a->y}) <= tolerancia;
    }

    // Passagens consecutivas a partir de i, até a primeira célula livre:
    // precisam ser adquiridas juntas para não segurar uma esperando a outra.
    std::vector<int> passagens_em_sequencia(size_t i) const {
        std::vector<int> v;
        for (; i < rota_.size() && rota_[i].passagem >= 0; ++i)
            if (v.empty() || v.back() != rota_[i].passagem) v.push_back(rota_[i].passagem);
        return v;
    }

    const Rota& rota() const { return rota_; }

private:
    Rota rota_;
    size_t indice_ = 0;
};

enum class NivelBateria { NORMAL, BAIXA, CRITICA };

// Avalia o nível da bateria com histerese: só volta a NORMAL depois de recarregada.
class MonitorBateria {
public:
    MonitorBateria(double baixa, double critica, double recarregada)
        : baixa_(baixa), critica_(critica), recarregada_(recarregada) {}

    NivelBateria nivel() const { return nivel_; }

    // Retorna verdadeiro quando o nível mudou nesta avaliação.
    bool avaliar(double carga) {
        NivelBateria novo = nivel_;
        if (carga <= critica_)
            novo = NivelBateria::CRITICA;
        else if (carga <= baixa_ && nivel_ == NivelBateria::NORMAL)
            novo = NivelBateria::BAIXA;
        else if (carga >= recarregada_)
            novo = NivelBateria::NORMAL;
        bool mudou = novo != nivel_;
        nivel_ = novo;
        return mudou;
    }

private:
    double baixa_, critica_, recarregada_;
    NivelBateria nivel_ = NivelBateria::NORMAL;
};

class DetectorBorda {
public:
    // Retorna +1 na borda de subida, -1 na de descida e 0 sem mudança.
    int atualizar(bool valor) {
        int borda = valor == anterior_ ? 0 : (valor ? 1 : -1);
        anterior_ = valor;
        return borda;
    }

private:
    bool anterior_ = false;
};

struct Vizinho {
    int id;
    Ponto pos;
    Ponto vel;  // velocidade estimada a partir das últimas posições publicadas
    int equipe;
    bool parado;
    int prioridade;
};

// Anticolisão descentralizada: cada robô prevê, com as posições e velocidades dos vizinhos, o ponto
// de maior aproximação nos próximos segundos. Em conflito, cede quem tem menor prioridade; robô parado
// no caminho é contornado. Nenhuma decisão passa pelo organizador.
class Anticolisao {
public:
    enum class Acao { SEGUIR, ESPERAR, DESVIAR };
    struct Decisao {
        Acao acao = Acao::SEGUIR;
        int obstaculo = -1;
        double esperando_s = 0;
        Ponto lado_direito, lado_esquerdo;  // candidatos de desvio; quem chama confere o mapa
    };

    struct Config {
        double horizonte_s = 2.0;
        double distancia_segura = 0.65;  // distância mínima prevista entre centros
        double bolha = 0.6;              // vizinho mais perto que isso, à frente, para o robô na hora
        double alcance = 3.0;
    };

    Anticolisao() = default;
    explicit Anticolisao(Config c) : cfg_(c) {}

    // em_formacao: equipe cujos membros se ignoram (carga acoplada); colegas fora da formação
    // usam distâncias menores, compatíveis com os pontos de acoplamento a 0,6 m uns dos outros
    Decisao avaliar(const Ponto& pos, const Ponto& alvo, double vmax, int prioridade, int em_formacao, int minha_equipe,
                    const std::vector<Vizinho>& vizinhos, double agora_s) {
        Decisao d;
        double dx = alvo.x - pos.x, dy = alvo.y - pos.y;
        double dist_alvo = std::hypot(dx, dy);
        if (dist_alvo < 0.05) {
            espera_desde_ = -1;
            return d;
        }
        double ux = dx / dist_alvo, uy = dy / dist_alvo;
        double v = std::min(vmax, std::sqrt(2.0 * dist_alvo));  // intenção: a velocidade que o robô vai ter
        Ponto minha{ux * v, uy * v};

        // cada conflito previsto pede uma ação; vale a mais restritiva (esperar > desviar > seguir)
        Acao acao = Acao::SEGUIR;
        int obstaculo = -1;
        if (espera_desde_ < 0) espera_desde_ = agora_s;
        double esperando = agora_s - espera_desde_;
        auto peso = [](Acao a) { return a == Acao::ESPERAR ? 2 : a == Acao::DESVIAR ? 1 : 0; };
        for (const auto& viz : vizinhos) {
            if (em_formacao >= 0 && viz.equipe == em_formacao) continue;
            const bool colega = minha_equipe >= 0 && viz.equipe == minha_equipe;
            if (colega && esperando > 5.0) continue;  // colegas presos um pelo outro: um deles segue
            const double segura = colega ? 0.42 : cfg_.distancia_segura;
            const double bolha = colega ? 0.42 : cfg_.bolha;
            double rx = viz.pos.x - pos.x, ry = viz.pos.y - pos.y;
            double dist = std::hypot(rx, ry);
            if (dist > cfg_.alcance) continue;
            double frente = rx * ux + ry * uy;
            bool colado = dist < bolha && frente > 0.2 * dist;
            if (!colado) {
                double wx = viz.vel.x - minha.x, wy = viz.vel.y - minha.y;
                double w2 = wx * wx + wy * wy;
                if (w2 < 1e-6) continue;
                double t = -(rx * wx + ry * wy) / w2;
                if (t < 0 || t > cfg_.horizonte_s) continue;
                if (std::hypot(rx + wx * t, ry + wy * t) > segura) continue;
                if (frente < -0.1) continue;  // conflito por trás: quem está atrás é que precisa reagir
            }
            bool parado = viz.parado && std::hypot(viz.vel.x, viz.vel.y) < 0.05;
            Acao a;
            if (parado)
                a = esperando > 0.4 ? Acao::DESVIAR : Acao::ESPERAR;
            else if (viz.prioridade > prioridade)
                a = esperando > 4.0 ? Acao::DESVIAR : Acao::ESPERAR;
            else if (colado)
                a = esperando > 1.5 ? Acao::DESVIAR : Acao::ESPERAR;
            else
                a = Acao::SEGUIR;  // tem a vez: o outro vai ceder
            if (peso(a) > peso(acao) || obstaculo < 0) {
                if (peso(a) >= peso(acao)) {
                    acao = a;
                    obstaculo = viz.id;
                }
            }
        }
        if (acao == Acao::SEGUIR) {
            espera_desde_ = -1;
            return d;
        }
        d.acao = acao;
        d.obstaculo = obstaculo;
        d.esperando_s = esperando;
        d.lado_direito = {pos.x + uy * 0.7 + ux * 0.4, pos.y - ux * 0.7 + uy * 0.4};
        d.lado_esquerdo = {pos.x - uy * 0.7 + ux * 0.4, pos.y + ux * 0.7 + uy * 0.4};
        return d;
    }

    void reiniciar() { espera_desde_ = -1; }

private:
    Config cfg_;
    double espera_desde_ = -1;
};

// Afastamento de cada membro em relação ao ponto da carga; o membro 0 é o líder.
inline Ponto afastamento_formacao(int k, int indice, double d) {
    switch (k) {
        case 2: return indice == 0 ? Ponto{-d, 0} : Ponto{d, 0};
        case 3: {
            const Ponto v[] = {{-d, -d}, {d, -d}, {0, d}};
            return v[indice % 3];
        }
        case 4: {
            const Ponto v[] = {{-d, -d}, {d, -d}, {-d, d}, {d, d}};
            return v[indice % 4];
        }
        default: return {0, 0};
    }
}

}  // namespace amr
