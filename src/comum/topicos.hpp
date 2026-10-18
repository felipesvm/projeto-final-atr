#pragma once

#include <string>

// Tópicos MQTT. Estado periódico em QoS 0; eventos e comandos em QoS 1.
namespace amr::topicos {

inline std::string sensores(int robo) { return "amr/hw/" + std::to_string(robo) + "/sensores"; }
inline std::string atuadores(int robo) { return "amr/hw/" + std::to_string(robo) + "/atuadores"; }
inline std::string rota(int robo) { return "amr/robo/" + std::to_string(robo) + "/rota"; }
inline std::string status_processo(const std::string& nome) { return "amr/status/" + nome; }

inline const std::string ROBO_NOVO = "amr/sim/robo_novo";
inline const std::string CAIXA_NOVA = "amr/sim/caixa_nova";
inline const std::string COMANDOS = "amr/cmd/#";
inline const std::string ESTADO = "amr/sup/estado";
inline const std::string ALARME = "amr/sup/alarme";
inline const std::string LOG_EVENTO = "amr/log/evento";
inline const std::string LOG_PEDIDO = "amr/log/pedido";
inline const std::string LOG_HISTORICO = "amr/log/historico";

}  // namespace amr::topicos
