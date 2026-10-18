#pragma once

#include <string>

namespace amr::nomes {

inline const std::string SHM_FROTA = "/amr_frota";
inline const std::string SHM_EVENTOS = "/amr_eventos";
inline const std::string SEM_VAZIOS = "/amr_ev_vazios";
inline const std::string SEM_ITENS = "/amr_ev_itens";
inline const std::string FILA_ORGANIZADOR = "/amr_org_inbox";
inline const std::string FILA_ANUNCIO = "/amr_teste_anuncio";

inline std::string fila_comandos(int robo) { return "/amr_cmd_" + std::to_string(robo); }
inline std::string fila_hardware(int robo) { return "/amr_teste_hw_" + std::to_string(robo); }

}  // namespace amr::nomes
