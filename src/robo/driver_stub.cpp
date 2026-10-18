#include "driver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "../comum/console.hpp"
#include "../comum/nomes.hpp"
#include "../comum/protocolo.hpp"
#include "../comum/tarefa_periodica.hpp"

namespace amr {

namespace {
constexpr int64_t PERIODO_NS = 50 * NS_POR_MS;
constexpr int PRIORIDADE_IO = 70;
}  // namespace

DriverStub::DriverStub(int id, Ponto inicial, const Cenario& cenario, ImagemES& imagem)
    : id_(id), cenario_(cenario), p_(cenario.parametros), img_(imagem), x_(inicial.x), y_(inicial.y),
      bateria_(cenario.parametros.bateria_inicial) {
    fila_teste_ = FilaMensagens::criar(nomes::fila_hardware(id), 10, sizeof(Mensagem), FilaMensagens::Acesso::Leitura);
}

void DriverStub::aplicar_injecoes() {
    Mensagem m;
    while (fila_teste_.receber(&m, sizeof m, 0)) {
        if (m.cab.tipo != TipoMsg::INJETAR) continue;
        switch (m.injecao.tipo) {
            case Injecao::DEFEITO_ATIVA:
                falha_ = true;
                LOG("T_IO", "defeito injetado t_mono=%lld", (long long)agora_ns());
                break;
            case Injecao::DEFEITO_REMOVE:
                falha_ = false;
                LOG("T_IO", "defeito removido");
                break;
            case Injecao::BATERIA:
                bateria_ = std::clamp(double(m.injecao.valor), 0.0, 100.0);
                LOG("T_IO", "bateria ajustada para %.1f%%", bateria_);
                break;
            case Injecao::COMUNICACAO_CORTA:
                comunicacao_ = false;
                LOG("T_IO", "quadros de entrada suspensos");
                break;
            case Injecao::COMUNICACAO_VOLTA:
                comunicacao_ = true;
                LOG("T_IO", "quadros de entrada normalizados");
                break;
        }
    }
}

bool DriverStub::na_estacao() const {
    for (auto& e : cenario_.estacoes_recarga)
        if (distancia({x_, y_}, e) < 0.5) return true;
    return false;
}

void DriverStub::integrar(const Saidas& s, double dt) {
    double vdx = 0, vdy = 0;
    if (!falha_) {
        double ex = s.sp_x - x_, ey = s.sp_y - y_;
        double d = std::hypot(ex, ey);
        if (d > 1e-3) {
            double v = std::min(p_.velocidade_max, std::sqrt(2 * p_.aceleracao * d));
            vdx = v * ex / d;
            vdy = v * ey / d;
        }
    }
    double ax = vdx - vx_, ay = vdy - vy_;
    double dv = std::hypot(ax, ay), dv_max = p_.aceleracao * dt;
    if (dv > dv_max) {
        ax *= dv_max / dv;
        ay *= dv_max / dv;
    }
    vx_ += ax;
    vy_ += ay;
    x_ += vx_ * dt;
    y_ += vy_ * dt;

    double rapidez = std::hypot(vx_, vy_);
    if (rapidez < 0.02 && na_estacao())
        bateria_ = std::min(100.0, bateria_ + p_.recarga * dt);
    else
        bateria_ = std::max(0.0, bateria_ - (rapidez > 0.02 ? p_.descarga_movendo : p_.descarga_parado) * dt);

    if (s.acoplar != acoplar_anterior_) {
        acoplar_anterior_ = s.acoplar;
        tempo_comando_ = 0;
    }
    tempo_comando_ += dt;
    if (tempo_comando_ >= p_.tempo_atracar_s) atracada_ = s.acoplar;
}

void DriverStub::executar(std::atomic<bool>& ativo) {
    aplicar_prioridade(PRIORIDADE_IO, "T_IO");
    auto proximo = std::chrono::steady_clock::now();
    const double dt = double(PERIODO_NS) / NS_POR_S;
    Saidas saida = img_.ler_saidas();
    saida.sp_x = x_;
    saida.sp_y = y_;

    while (ativo.load()) {
        proximo += std::chrono::nanoseconds(PERIODO_NS);
        // acorda antes do tique se a T_NAV publicar uma saída nova
        for (;;) {
            Saidas s = img_.esperar_saida(proximo);
            if (s.seq > 0) saida = s;
            if (std::chrono::steady_clock::now() >= proximo || !ativo.load()) break;
        }
        aplicar_injecoes();
        integrar(saida, dt);
        if (!comunicacao_) continue;
        Entradas e;
        e.x = x_;
        e.y = y_;
        e.bateria = bateria_;
        e.falha = falha_;
        e.atracada = atracada_;
        e.seq = ++seq_;
        e.t_ns = agora_ns();
        img_.escrever_entradas(e);
    }
    FilaMensagens::remover(nomes::fila_hardware(id_));
}

}  // namespace amr
