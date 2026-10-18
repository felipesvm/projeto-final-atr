#!/usr/bin/env bash
# T-13 — sistema completo por MQTT: comandos da tela (manual/automático, destino, parar, reconhecer,
# parada geral) e da simulação (defeito, bateria, robô novo, caixa nova), conferidos no estado publicado.
source "$(dirname "$0")/comum.sh" T-13
plataforma
"$BIN/limpar_ipc" > /dev/null
./run.sh --bin "$BIN" --robos 6 --nao-abrir > "$EVID/execucao.log" 2>&1 &
RUN_PID=$!
sleep 20
if node interface/ferramentas/testar_comandos.mjs | tee "$EVID/saida.txt"; then
    resultado "T-13 APROVADO: $(tail -1 "$EVID/saida.txt")"
else
    resultado "T-13 REPROVADO: $(tail -1 "$EVID/saida.txt")"
    exit 1
fi
