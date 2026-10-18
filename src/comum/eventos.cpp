#include "eventos.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "nomes.hpp"
#include "tempo.hpp"

namespace amr {

const char* nome_categoria(Categoria c) {
    switch (c) {
        case Categoria::CARGA: return "CARGA";
        case Categoria::ROBO: return "ROBO";
        case Categoria::SISTEMA: return "SISTEMA";
        case Categoria::OPERADOR: return "OPERADOR";
    }
    return "?";
}

Registro novo_registro(const char* origem, Categoria cat, const char* formato, ...) {
    Registro r{};
    r.t_parede_ns = parede_ns();
    r.t_mono_ns = agora_ns();
    std::snprintf(r.origem, sizeof r.origem, "%s", origem);
    r.categoria = cat;
    r.robo = r.caixa = r.baia = r.zona = -1;
    for (auto& x : r.robos) x = -1;
    va_list args;
    va_start(args, formato);
    std::vsnprintf(r.texto, sizeof r.texto, formato, args);
    va_end(args);
    return r;
}

Rastreador::Rastreador(Modo modo) : modo_(modo) {
    shm_ = MemoriaCompartilhada::abrir(nomes::SHM_EVENTOS);
    buffer_ = static_cast<BufferEventos*>(shm_.dados());
    vazios_ = SemaforoNomeado::abrir(nomes::SEM_VAZIOS);
    itens_ = SemaforoNomeado::abrir(nomes::SEM_ITENS);
}

bool Rastreador::gravar(const Registro& r, int64_t espera_ns) {
    bool vaga = espera_ns > 0 ? vazios_.esperar_por(espera_ns) : vazios_.tentar();
    if (!vaga) return false;
    {
        TravaCompartilhada trava(&buffer_->mtx);
        buffer_->itens[buffer_->cauda] = r;
        buffer_->cauda = (buffer_->cauda + 1) % buffer_->capacidade;
    }
    itens_.liberar();
    return true;
}

void Rastreador::registrar(const Registro& r) {
    if (!buffer_) return;
    if (modo_ == Modo::TempoReal) {
        if (!gravar(r, 0)) buffer_->perdas.fetch_add(1);
        return;
    }
    std::lock_guard<std::mutex> trava(mtx_pendentes_);
    if (pendentes_.size() < CAPACIDADE_EVENTOS)
        pendentes_.push_back(r);
    else
        buffer_->perdas.fetch_add(1);
}

void Rastreador::descarregar() {
    if (!buffer_ || modo_ != Modo::ComPrazo) return;
    // uma thread descarrega por vez; as outras seguem sem esperar
    std::unique_lock<std::mutex> vez(mtx_descarga_, std::try_to_lock);
    if (!vez.owns_lock()) return;
    for (;;) {
        Registro r;
        {
            std::lock_guard<std::mutex> trava(mtx_pendentes_);
            if (pendentes_.empty()) return;
            r = pendentes_.front();
        }
        if (!gravar(r, ms_para_ns(10))) return;  // continua pendente para o próximo ciclo
        std::lock_guard<std::mutex> trava(mtx_pendentes_);
        pendentes_.pop_front();
    }
}

}  // namespace amr
