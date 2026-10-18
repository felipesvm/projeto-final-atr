# Funções compartilhadas pelos testes de integração. Uso: source testes/integracao/comum.sh NOME

set -euo pipefail
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$RAIZ"

TESTE="${1:?nome do teste}"
EVID="testes/resultados/$TESTE"
BIN="${BIN:-bin}"
rm -rf "$EVID"
mkdir -p "$EVID"
RUN_PID=""

plataforma() {
    {
        echo "data: $(date -Iseconds)"
        echo "sistema: $(uname -srm)"
        echo "processador: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //') ($(nproc) núcleos)"
        if [[ -n "${AMR_SEM_RT:-}" ]]; then echo "escalonamento: política normal (AMR_SEM_RT)"; else echo "escalonamento: SCHED_FIFO solicitado"; fi
        echo "fs.mqueue.msg_max: $(cat /proc/sys/fs/mqueue/msg_max 2>/dev/null || echo '?')"
    } > "$EVID/plataforma.txt"
}

iniciar_sistema() {
    plataforma
    "$BIN/limpar_ipc" > /dev/null
    ./run.sh --bin "$BIN" --sem-interface "$@" > "$EVID/execucao.log" 2>&1 &
    RUN_PID=$!
    for _ in $(seq 100); do
        "$BIN/amr_monitor" --uma-vez --csv > /dev/null 2>&1 && break
        sleep 0.1
    done
    sleep 1
}

parar_sistema() {
    if [[ -n "$RUN_PID" ]]; then
        kill -TERM "$RUN_PID" 2>/dev/null || true
        wait "$RUN_PID" 2>/dev/null || true
        RUN_PID=""
    fi
    sleep 0.5
    if [[ -d logs/ultima ]]; then cp logs/ultima/*.csv "$EVID/" 2>/dev/null || true; fi
}
trap parar_sistema EXIT

pid_do_robo() { pgrep -f "amr_robo --id $1 " | head -1; }

esperar_robos_prontos() {
    local n="$1"
    for _ in $(seq 200); do
        (( $(grep -c "pronto: entra no despacho" "$EVID/execucao.log" || true) >= n )) && return 0
        sleep 0.1
    done
    echo "robôs não ficaram prontos" >&2
    return 1
}

resultado() {
    echo "$1" | tee "$EVID/resultado.txt"
}
