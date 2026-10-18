#!/usr/bin/env bash
# T-12 — o mesmo sistema com N = 1, 2, 4, 8 e 16, mudando só o parâmetro de lançamento
DIR="$(cd "$(dirname "$0")" && pwd)"
RAIZ="$(cd "$DIR/../.." && pwd)"
cd "$RAIZ"
rm -rf testes/resultados/T-12
mkdir -p testes/resultados/T-12
falhas=0
for n in ${VALORES:-1 2 4 8 16}; do
    NOME="T-12/N$n" N="$n" DURACAO="${DURACAO:-30}" bash "$DIR/t03_status.sh" || falhas=$((falhas + 1))
    ev="testes/resultados/T-12/N$n"
    grandes=$(awk -F';' '$9 ~ /designada/ {split($8,r,","); if (length(r) > 1) c++} END {print c+0}' "$ev/cargas.csv")
    aguardando=$(grep -c "aguardando frota" "$ev/cargas.csv" || true)
    echo "N=$n: $(cat "$ev/resultado.txt"); designações com mais de um robô: $grandes; caixas aguardando frota: $aguardando" \
        | tee -a testes/resultados/T-12/resumo.txt
    if (( n == 1 && grandes > 0 )); then falhas=$((falhas + 1)); fi
done
if (( falhas == 0 )); then
    echo "T-12 APROVADO" | tee testes/resultados/T-12/resultado.txt
else
    echo "T-12 REPROVADO ($falhas falha(s))" | tee testes/resultados/T-12/resultado.txt
    exit 1
fi
