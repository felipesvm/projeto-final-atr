#!/usr/bin/env bash
# T-03 — troca de dados robô -> organizador pela tabela da frota
N="${N:-8}"
DURACAO="${DURACAO:-60}"
NOME="${NOME:-T-03}"
source "$(dirname "$0")/comum.sh" "$NOME"
export AMR_RASTREAR_STATUS=1
iniciar_sistema --robos "$N"
esperar_robos_prontos "$N"
"$BIN/amr_monitor" --csv --intervalo-ms 50 > "$EVID/monitor.csv" &
MON=$!
sleep "$DURACAO"
kill "$MON" 2>/dev/null || true
parar_sistema
python3 testes/integracao/analisar_status.py "$EVID" "$N" | tee "$EVID/resumo.txt"
grep -q "^APROVADO" "$EVID/resumo.txt" && resultado "$NOME APROVADO (N=$N): $(sed -n 2p "$EVID/resumo.txt")" \
    || { resultado "$NOME REPROVADO (N=$N): $(sed -n 2p "$EVID/resumo.txt")"; exit 1; }
