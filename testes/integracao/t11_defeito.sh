#!/usr/bin/env bash
# T-11 — tempo entre a injeção de um defeito e o congelamento do setpoint
INJECOES="${INJECOES:-100}"
source "$(dirname "$0")/comum.sh" T-11
iniciar_sistema --robos 4 --cenario config/testes_misto.json
esperar_robos_prontos 4
sleep 5
feitas=0
while (( feitas < INJECOES )); do
    robo=$("$BIN/amr_monitor" --uma-vez --csv | awk -F';' '$4 ~ /DESLOCANDO|TRANSPORTANDO/ {print $3; exit}')
    if [[ -z "$robo" ]]; then sleep 0.5; continue; fi
    "$BIN/injetar_evento" defeito "$robo" on
    sleep 0.5
    "$BIN/injetar_evento" defeito "$robo" off
    sleep 0.3
    "$BIN/injetar_evento" reconhecer "$robo"
    feitas=$((feitas + 1))
    sleep 1.2
done
parar_sistema
python3 testes/integracao/analisar_defeito.py "$EVID" | tee "$EVID/resumo.txt"
grep -q "^APROVADO" "$EVID/resumo.txt" && resultado "T-11 APROVADO: $(sed -n 2p "$EVID/resumo.txt")" \
    || { resultado "T-11 REPROVADO: $(sed -n 2p "$EVID/resumo.txt")"; exit 1; }
