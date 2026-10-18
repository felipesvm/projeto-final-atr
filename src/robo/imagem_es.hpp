#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace amr {

struct Entradas {
    double x = 0, y = 0;
    double bateria = 100;
    bool falha = false;
    bool atracada = false;
    uint64_t seq = 0;
    int64_t t_ns = 0;
};

struct Saidas {
    double sp_x = 0, sp_y = 0;
    bool acoplar = false;
    uint64_t seq = 0;
};

// Imagem de E/S do robô: entradas escritas pela T_IO, saídas escritas pela T_NAV.
class ImagemES {
public:
    Entradas ler_entradas() {
        std::lock_guard<std::mutex> t(mtx_in_);
        return in_;
    }

    void escrever_entradas(const Entradas& e) {
        std::lock_guard<std::mutex> t(mtx_in_);
        if (e.seq > in_.seq) in_ = e;  // quadro antigo é descartado
    }

    Saidas ler_saidas() {
        std::lock_guard<std::mutex> t(mtx_out_);
        return out_;
    }

    void escrever_saidas(const Saidas& s) {
        bool mudou;
        {
            std::lock_guard<std::mutex> t(mtx_out_);
            mudou = s.sp_x != out_.sp_x || s.sp_y != out_.sp_y || s.acoplar != out_.acoplar;
            if (mudou) {
                out_ = s;
                out_.seq = ++seq_saida_;
                nova_ = true;
            }
        }
        if (mudou) cv_out_.notify_one();
    }

    // Espera uma saída nova até o prazo; devolve a saída mais recente em qualquer caso.
    template <typename Relogio, typename Duracao>
    Saidas esperar_saida(const std::chrono::time_point<Relogio, Duracao>& ate) {
        std::unique_lock<std::mutex> t(mtx_out_);
        cv_out_.wait_until(t, ate, [&] { return nova_; });
        nova_ = false;
        return out_;
    }

    void acordar() { cv_out_.notify_all(); }

private:
    std::mutex mtx_in_;
    Entradas in_;
    std::mutex mtx_out_;
    std::condition_variable cv_out_;
    Saidas out_;
    bool nova_ = false;
    uint64_t seq_saida_ = 0;
};

}  // namespace amr
