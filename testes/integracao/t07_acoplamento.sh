#!/usr/bin/env bash
# T-07 — dispersão entre os comandos de acoplamento dos membros de cada equipe
EQUIPES="${EQUIPES:-50}"
LIMITE="${LIMITE:-1500}"
source "$(dirname "$0")/comum.sh" T-07
iniciar_sistema --robos 8 --cenario config/testes_equipes.json
for _ in $(seq "$LIMITE"); do
    n=$(python3 testes/integracao/analisar_acoplamento.py "$EVID" --contar)
    (( n >= EQUIPES )) && break
    sleep 1
done
parar_sistema
python3 testes/integracao/analisar_acoplamento.py "$EVID" | tee "$EVID/resumo.txt"
grep -q "^APROVADO" "$EVID/resumo.txt" && resultado "T-07 APROVADO: $(sed -n 2p "$EVID/resumo.txt")" \
    || { resultado "T-07 REPROVADO: $(sed -n 2p "$EVID/resumo.txt")"; exit 1; }
