#!/usr/bin/env bash
# T-05 — criação automática de processos até N_max e detecção de término
N_MAX="${N_MAX:-32}"
source "$(dirname "$0")/comum.sh" T-05
iniciar_sistema --robos 0 --n-max "$N_MAX"
"$BIN/anunciar_robo" --n "$N_MAX" --intervalo-ms 1000 > "$EVID/anuncios.txt"
esperar_robos_prontos "$N_MAX"
sleep 2
ps -ef --forest | grep -E "amr_(gestor|robo)" | grep -v grep > "$EVID/ps_forest.txt"
processos=$(grep -c "amr_robo --id" "$EVID/ps_forest.txt" || true)
"$BIN/amr_monitor" --uma-vez --csv > "$EVID/monitor_antes.csv"
slots=$(($(wc -l < "$EVID/monitor_antes.csv") - 1))
"$BIN/anunciar_robo" "$((N_MAX + 1))" >> "$EVID/anuncios.txt"
sleep 1
vitima=$((N_MAX / 2))
pid=$(pid_do_robo "$vitima")
kill -9 "$pid"
sleep 3
"$BIN/amr_monitor" --uma-vez --csv > "$EVID/monitor_depois.csv"
slots_depois=$(($(wc -l < "$EVID/monitor_depois.csv") - 1))
parar_sistema
terminou=$(grep -c "ROBO_TERMINOU $vitima (pid $pid, sinal 9)" "$EVID/execucao.log" || true)
recusado=$(grep -c "frota cheia" "$EVID/execucao.log" || true)
designado_depois=$(awk -v v="$vitima" -F';' '/designada/ {n=split($8,r,","); for(i=1;i<=n;i++) if (r[i]==v) c++} END {print c+0}' "$EVID/cargas.csv")
{
    echo "processos amr_robo: $processos; slots ocupados: $slots"
    echo "kill -9 no robô $vitima (pid $pid): ROBO_TERMINOU registrado: $terminou; slots ocupados depois: $slots_depois"
    echo "anúncio extra com a frota cheia recusado: $recusado"
} | tee "$EVID/resumo.txt"
if (( processos == N_MAX && slots == N_MAX && terminou == 1 && slots_depois == N_MAX - 1 )); then
    resultado "T-05 APROVADO: $N_MAX processos criados, um por slot; término do robô $vitima detectado e slot liberado"
else
    resultado "T-05 REPROVADO: ver $EVID/resumo.txt"
    exit 1
fi
