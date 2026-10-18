#!/usr/bin/env bash
# T-04 — troca de dados organizador -> robô: entrega pela fila de comandos e prioridade do PARAR
RODADAS="${RODADAS:-20}"
source "$(dirname "$0")/comum.sh" T-04
iniciar_sistema --robos 1 --cenario config/testes_vazio.json
esperar_robos_prontos 1
sleep 1
for r in $(seq "$RODADAS"); do
    echo "== rodada $r" >> "$EVID/envios.txt"
    "$BIN/rajada_comandos" 1 8 >> "$EVID/envios.txt"
    sleep 0.3
    "$BIN/injetar_evento" retomar 1
    sleep 0.3
done
sleep 1
parar_sistema
python3 testes/integracao/analisar_comandos.py "$EVID" | tee "$EVID/resumo.txt"
grep -q "^APROVADO" "$EVID/resumo.txt" && resultado "T-04 APROVADO: $(sed -n 2p "$EVID/resumo.txt")" \
    || { resultado "T-04 REPROVADO: $(sed -n 2p "$EVID/resumo.txt")"; exit 1; }
