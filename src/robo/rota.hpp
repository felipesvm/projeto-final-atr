#pragma once

#include "nucleo.hpp"

namespace amr {

// T_ROTA: calcula com mtx_nav solto e descarta o resultado se chegou pedido mais novo.
void executar_planejador(NucleoRobo& r);

}  // namespace amr
