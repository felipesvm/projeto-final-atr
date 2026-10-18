#!/usr/bin/env bash
# Executa T-01 a T-12 em sequência. Os testes longos aceitam DURACAO, EQUIPES, INJECOES etc.
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
make -s
for t in "$DIR"/t[0-9][0-9]_*.sh; do
    echo "=== $(basename "$t")"
    bash "$t" || true
done
echo
echo "RESUMO"
cat testes/resultados/T-*/resultado.txt
