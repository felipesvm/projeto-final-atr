#!/usr/bin/env bash
# Rampa de frota para a análise de tempo de resposta e escalonabilidade (Etapa 2).
# Roda o sistema completo com N robôs, REPETICOES vezes cada, por DURACAO segundos, e registra as
# métricas de tempo real de todas as threads periódicas, a carga de CPU e as entregas. No fim gera
# os gráficos e um pacote para anexar ao relatório.
#   bash testes/integracao/escalabilidade.sh
#   VALORES="2 8 16" REPETICOES=1 DURACAO=60 bash testes/integracao/escalabilidade.sh   # versão curta
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
VALORES="${VALORES:-2 4 8 16 24 32 48 64}"
REPETICOES="${REPETICOES:-3}"
DURACAO="${DURACAO:-120}"
CENARIO="${CENARIO:-config/cenario.json}"
SAIDA=testes/resultados/escalabilidade
rm -rf "$SAIDA"
mkdir -p "$SAIDA"
make -s
(cd interface && [[ -d node_modules ]] || npm install --no-audit --no-fund --silent)

{
    echo "data: $(date -Iseconds)"
    echo "sistema: $(uname -srm)"
    echo "processador: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "nucleos: $(nproc)"
    echo "memoria: $(free -h | awk '/Mem:/ {print $2}')"
    if grep -qi microsoft /proc/version 2>/dev/null; then echo "ambiente: WSL"; fi
    echo "escalonamento: ${AMR_SEM_RT:+politica normal}${AMR_SEM_RT:-SCHED_FIFO solicitado}"
    echo "cenario: $CENARIO; duracao: ${DURACAO}s; repeticoes: $REPETICOES"
} > "$SAIDA/plataforma.txt"
cat "$SAIDA/plataforma.txt"

cpu_ocupada() {  # tempo de CPU total e ocioso, em ticks, a partir de /proc/stat
    awk '/^cpu / {ocioso = $5 + $6; total = 0; for (i = 2; i <= NF; i++) total += $i; print total, ocioso}' /proc/stat
}

echo "n;rep;robos;prontos;entregues;colisoes;desvios;nav_perdas;status_perdas;nav_c_med_ms;nav_c_max_ms;nav_atraso_max_ms;nav_resp_max_ms;status_c_med_ms;status_c_max_ms;status_atraso_max_ms;status_resp_max_ms;sim_tick_max_ms;cpu_pct" > "$SAIDA/resumo.csv"
for n in $VALORES; do
    for rep in $(seq "$REPETICOES"); do
        echo "== N=$n, repetição $rep"
        ./bin/limpar_ipc > /dev/null
        ./run.sh --robos "$n" --n-max "$(( n > 64 ? n : 64 ))" --cenario "$CENARIO" --nao-abrir > "$SAIDA/execucao_N${n}_r$rep.log" 2>&1 &
        pid=$!
        sleep $(( 10 + n / 4 ))
        read -r t0 o0 < <(cpu_ocupada)
        node interface/ferramentas/acompanhar.mjs --json --duracao-ms "$((DURACAO * 1000))" > "$SAIDA/N${n}_r$rep.json"
        read -r t1 o1 < <(cpu_ocupada)
        kill -TERM "$pid"; wait "$pid" 2>/dev/null || true
        python3 - "$SAIDA/N${n}_r$rep.json" "$n" "$rep" "$t0" "$o0" "$t1" "$o1" >> "$SAIDA/resumo.csv" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
t0, o0, t1, o1 = map(float, sys.argv[4:8])
cpu = 100.0 * (1 - (o1 - o0) / (t1 - t0)) if t1 > t0 else 0
campos = ["robos", "prontos", "entregues", "colisoes", "desvios", "nav_perdas", "status_perdas", "nav_c_med_ms", "nav_c_max_ms",
          "nav_atraso_max_ms", "nav_resp_max_ms", "status_c_med_ms", "status_c_max_ms", "status_atraso_max_ms",
          "status_resp_max_ms", "sim_tick_max_ms"]
print(";".join([sys.argv[2], sys.argv[3]] + [str(r.get(c, 0)) for c in campos] + [f"{cpu:.1f}"]))
PY
        tail -1 "$SAIDA/resumo.csv"
    done
done

python3 testes/integracao/graficos_escalabilidade.py "$SAIDA" || echo "gráficos não gerados (instale python3-matplotlib)"
tar czf testes/resultados/escalabilidade.tar.gz -C testes/resultados escalabilidade
echo
echo "Resultados em $SAIDA e no pacote testes/resultados/escalabilidade.tar.gz"
column -s';' -t "$SAIDA/resumo.csv" 2>/dev/null || cat "$SAIDA/resumo.csv"
