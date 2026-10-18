#!/usr/bin/env bash
# T-01 — lógica de domínio isolada (sem threads nem IPC)
source "$(dirname "$0")/comum.sh" T-01
plataforma
make -s "$BIN/teste_dominio" > /dev/null
if "./$BIN/teste_dominio" | tee "$EVID/saida.txt"; then
    resultado "T-01 APROVADO: $(tail -1 "$EVID/saida.txt")"
else
    resultado "T-01 REPROVADO: $(tail -1 "$EVID/saida.txt")"
    exit 1
fi
