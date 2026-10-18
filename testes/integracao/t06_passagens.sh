#!/usr/bin/env bash
# T-06 — capacidade das passagens com 6 robôs e equipes de 2 e 3
DURACAO="${DURACAO:-600}"
source "$(dirname "$0")/comum.sh" T-06
iniciar_sistema --robos 6 --cenario config/testes_misto.json
fim=$((SECONDS + DURACAO))
: > "$EVID/espelho.txt"
while (( SECONDS < fim )); do
    "$BIN/amr_monitor" --uma-vez | sed 's/\x1b\[[0-9;]*[HJ]//g' | grep -E "^  P[0-9]" >> "$EVID/espelho.txt" || true
    sleep 0.5
done
parar_sistema
python3 testes/integracao/analisar_passagens.py "$EVID" | tee "$EVID/resumo.txt"
grep -q "^APROVADO" "$EVID/resumo.txt" && resultado "T-06 APROVADO: $(sed -n 2p "$EVID/resumo.txt")" \
    || { resultado "T-06 REPROVADO: $(sed -n 2p "$EVID/resumo.txt")"; exit 1; }
