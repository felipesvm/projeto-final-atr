#!/usr/bin/env bash
# Rampa de frota para a análise de tempo de resposta e escalonabilidade (Etapa 2):
# roda o sistema completo com N robôs por DURACAO segundos e registra as métricas de tempo real.
#   VALORES="2 4 8 16 24 32" DURACAO=90 bash testes/integracao/escalabilidade.sh
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
VALORES="${VALORES:-2 4 8 16 24 32}"
DURACAO="${DURACAO:-90}"
CENARIO="${CENARIO:-config/testes_misto.json}"
SAIDA=testes/resultados/escalabilidade
mkdir -p "$SAIDA"
make -s
echo "n;entregues;colisoes;desvios;nav_perdas;status_perdas;nav_c_max_ms;nav_atraso_max_ms;status_c_max_ms;status_atraso_max_ms;sim_tick_max_ms;carga_cpu" > "$SAIDA/resumo.csv"
for n in $VALORES; do
    echo "== N=$n"
    ./bin/limpar_ipc > /dev/null
    ./run.sh --robos "$n" --n-max "$(( n > 32 ? n : 32 ))" --cenario "$CENARIO" --nao-abrir > "$SAIDA/execucao_N$n.log" 2>&1 &
    pid=$!
    sleep 15
    node interface/ferramentas/acompanhar.mjs --json --duracao-ms "$((DURACAO * 1000))" > "$SAIDA/N$n.json"
    carga=$(cut -d' ' -f1 /proc/loadavg)
    kill -TERM "$pid"; wait "$pid" 2>/dev/null || true
    python3 - "$SAIDA/N$n.json" "$n" "$carga" >> "$SAIDA/resumo.csv" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
campos = ["entregues", "colisoes", "desvios", "nav_perdas", "status_perdas", "nav_c_max_ms", "nav_atraso_max_ms",
          "status_c_max_ms", "status_atraso_max_ms", "sim_tick_max_ms"]
print(";".join([sys.argv[2]] + [str(r[c]) for c in campos] + [sys.argv[3]]))
PY
    tail -1 "$SAIDA/resumo.csv"
done
{
    echo "plataforma: $(uname -srm); $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //') ($(nproc) núcleos)"
    echo "escalonamento: ${AMR_SEM_RT:+política normal}${AMR_SEM_RT:-SCHED_FIFO solicitado}"
} > "$SAIDA/plataforma.txt"
column -s';' -t "$SAIDA/resumo.csv"
