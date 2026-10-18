#!/usr/bin/env bash
# T-10 — buffer de eventos com o coletor pausado (SIGSTOP) por 30 s
PAUSA="${PAUSA:-30}"
source "$(dirname "$0")/comum.sh" T-10
iniciar_sistema --robos 4 --cenario config/testes_misto.json
sleep 20
"$BIN/amr_monitor" --uma-vez --csv > "$EVID/monitor_antes.csv"
coletor=$(pgrep -x amr_coletor)
t0=$(date +%s%N)
kill -STOP "$coletor"
sleep "$PAUSA"
"$BIN/amr_monitor" --uma-vez > "$EVID/monitor_pausado.txt" 2>&1 || true
"$BIN/amr_monitor" --uma-vez --csv > "$EVID/monitor_pausado.csv"
kill -CONT "$coletor"
t1=$(date +%s%N)
sleep 10
parar_sistema
python3 testes/integracao/analisar_coletor.py "$EVID" "$t0" "$t1" | tee "$EVID/resumo.txt"
grep -q "^APROVADO" "$EVID/resumo.txt" && resultado "T-10 APROVADO: $(sed -n 2p "$EVID/resumo.txt")" \
    || { resultado "T-10 REPROVADO: $(sed -n 2p "$EVID/resumo.txt")"; exit 1; }
