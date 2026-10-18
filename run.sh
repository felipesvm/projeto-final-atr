#!/usr/bin/env bash
# Sistema de logística colaborativa com AMRs — inicia todos os processos a partir da raiz do repositório.
#
#   ./run.sh                         sistema completo: broker, núcleo C++, simulador e tela 3D
#   ./run.sh --robos 8               frota inicial de 8 robôs (mais robôs podem entrar pela tela)
#   ./run.sh --mapa-aleatorio        mapa com obstáculos e passagens sorteados (ponto extra)
#   ./run.sh --sem-interface         só o núcleo C++ com o driver local da Etapa 1 (sem MQTT)
#
# Outras opções: --n-max M, --cenario arquivo, --semente S, --porta-http P, --mosquitto, --sem-rt,
#                --bin pasta, --nao-abrir. Ctrl+C encerra tudo na ordem certa.

set -euo pipefail
cd "$(dirname "$0")"

ROBOS=6
N_MAX=32
CENARIO=config/cenario.json
BIN=bin
INTERFACE=1
MAPA_ALEATORIO=0
SEMENTE=""
PORTA_HTTP=8080
PORTA_MQTT=1883
PORTA_WS=9001
MOSQUITTO=0
ABRIR=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --robos)          ROBOS="$2"; shift 2 ;;
        --n-max)          N_MAX="$2"; shift 2 ;;
        --cenario)        CENARIO="$2"; shift 2 ;;
        --bin)            BIN="$2"; shift 2 ;;
        --mapa-aleatorio) MAPA_ALEATORIO=1; shift ;;
        --semente)        SEMENTE="$2"; MAPA_ALEATORIO=1; shift 2 ;;
        --porta-http)     PORTA_HTTP="$2"; shift 2 ;;
        --mosquitto)      MOSQUITTO=1; shift ;;
        --sem-interface|--driver-stub) INTERFACE=0; shift ;;
        --sem-rt)         export AMR_SEM_RT=1; shift ;;
        --nao-abrir)      ABRIR=0; shift ;;
        -h|--help)        sed -n '2,11p' "$0"; exit 0 ;;
        *) echo "opção desconhecida: $1 (veja ./run.sh --help)" >&2; exit 2 ;;
    esac
done

if (( ROBOS > N_MAX )); then echo "--robos ($ROBOS) maior que --n-max ($N_MAX)" >&2; exit 2; fi

# ---------------------------------------------------------------- preparação
if [[ ! -x "$BIN/amr_organizador" ]]; then
    echo "compilando o núcleo C++ (make)..."
    make -s -j"$(nproc)"
fi
if (( INTERFACE )); then
    if ! command -v node > /dev/null; then echo "Node.js não encontrado (necessário para o simulador e a tela)" >&2; exit 1; fi
    if [[ ! -d interface/node_modules ]]; then
        echo "instalando as dependências da interface (npm install, só na primeira vez)..."
        (cd interface && npm install --no-audit --no-fund --silent)
    fi
fi
if [[ -w /proc/sys/fs/mqueue/msg_max ]] && (( $(cat /proc/sys/fs/mqueue/msg_max) < 64 )); then
    echo 64 > /proc/sys/fs/mqueue/msg_max || true
fi

mkdir -p logs runtime
if (( MAPA_ALEATORIO )); then
    SEMENTE="${SEMENTE:-$(date +%s)}"
    "$BIN/gerar_mapa" --semente "$SEMENTE" --saida runtime/cenario_aleatorio.json
    CENARIO=runtime/cenario_aleatorio.json
fi

PIDS=()
NOMES=()
iniciar() {  # nome comando...
    local nome="$1"; shift
    # cada processo em sua própria sessão: o Ctrl+C chega só a este script, que encerra tudo em ordem
    setsid "$@" &
    PIDS+=($!)
    NOMES+=("$nome")
}

encerrar() {
    trap - INT TERM
    echo
    echo "encerrando..."
    for alvo in simulador gestor coletor organizador broker; do
        for i in "${!NOMES[@]}"; do
            if [[ "${NOMES[$i]}" == "$alvo" ]]; then
                kill -TERM "${PIDS[$i]}" 2>/dev/null || true
                wait "${PIDS[$i]}" 2>/dev/null || true
            fi
        done
    done
    "$BIN/limpar_ipc" > /dev/null
    exit 0
}
trap encerrar INT TERM

"$BIN/limpar_ipc" > /dev/null

# ---------------------------------------------------------------- broker MQTT
if (( INTERFACE )); then
    export AMR_MQTT="127.0.0.1:$PORTA_MQTT"
    export AMR_DRIVER=mqtt
    if (( MOSQUITTO )); then
        iniciar broker mosquitto -c config/mosquitto.conf
    else
        iniciar broker node interface/broker.js --porta "$PORTA_MQTT" --porta-ws "$PORTA_WS"
    fi
    for _ in $(seq 50); do (exec 3<>"/dev/tcp/127.0.0.1/$PORTA_MQTT") 2>/dev/null && break; sleep 0.1; done
else
    unset AMR_MQTT
    export AMR_DRIVER=stub
fi

# ---------------------------------------------------------------- núcleo C++
iniciar organizador "$BIN/amr_organizador" --cenario "$CENARIO" --n-max "$N_MAX"
for _ in $(seq 50); do [[ -e /dev/shm/amr_frota ]] && break; sleep 0.1; done
iniciar coletor "$BIN/amr_coletor" --logs logs
iniciar gestor "$BIN/amr_gestor" --cenario "$CENARIO"
sleep 0.5

# ---------------------------------------------------------------- simulador e tela
if (( INTERFACE )); then
    iniciar simulador node interface/simulador.js --cenario "$CENARIO" --robos "$ROBOS" \
        --broker "mqtt://127.0.0.1:$PORTA_MQTT" --porta-http "$PORTA_HTTP" --porta-ws "$PORTA_WS"
    URL="http://localhost:$PORTA_HTTP"
    echo
    echo "  Tela de operação e simulação: $URL"
    echo "  Ctrl+C encerra o sistema."
    echo
    if (( ABRIR )); then
        sleep 1.5
        (command -v xdg-open > /dev/null && xdg-open "$URL" > /dev/null 2>&1) ||
            (command -v wslview > /dev/null && wslview "$URL" > /dev/null 2>&1) ||
            (command -v open > /dev/null && open "$URL" > /dev/null 2>&1) || true
    fi
else
    (( ROBOS > 0 )) && "$BIN/anunciar_robo" --n "$ROBOS" --cenario "$CENARIO" > /dev/null
    echo "núcleo em execução com $ROBOS robô(s) no driver local; acompanhe com $BIN/amr_monitor (Ctrl+C encerra)"
fi

while true; do
    sleep 1 &
    wait $! || true
done
