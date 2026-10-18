#!/usr/bin/env bash
# T-09 — robôs que morrem segurando o mutex do slot não travam o organizador
REPETICOES="${REPETICOES:-20}"
source "$(dirname "$0")/comum.sh" T-09
# robôs com id >= 100 morrem (SIGKILL) dentro da seção crítica do slot no 20º ciclo da T_STATUS
export AMR_TESTE_MORRER_NO_SLOT="20@100"
iniciar_sistema --robos 2
esperar_robos_prontos 2
for i in $(seq "$REPETICOES"); do
    "$BIN/anunciar_robo" "$((99 + i))" >> "$EVID/anuncios.txt"
    sleep 3
done
hb_antes=$("$BIN/amr_monitor" --uma-vez --csv | awk -F';' '$3==1 {print $8}')
sleep 2
hb_depois=$("$BIN/amr_monitor" --uma-vez --csv | awk -F';' '$3==1 {print $8}')
parar_sistema
mortes=$(grep -cE "robô 1[0-9][0-9] \(pid [0-9]+\) terminou: sinal 9" "$EVID/execucao.log" || true)
eownerdead=$(grep -c "EOWNERDEAD" "$EVID/execucao.log" || true)
resumos=$(grep -c "T_ORG_SUP: resumo" "$EVID/execucao.log" || true)
{
    echo "robôs mortos com a trava: $mortes de $REPETICOES; EOWNERDEAD tratados: $eownerdead"
    echo "heartbeat do robô 1 continuou: $hb_antes -> $hb_depois; ciclos de resumo da supervisão: $resumos"
} | tee "$EVID/resumo.txt"
if (( mortes == REPETICOES && eownerdead >= 1 && hb_depois > hb_antes )); then
    resultado "T-09 APROVADO: $mortes mortes com a trava, $eownerdead EOWNERDEAD tratados, organizador e robôs seguiram"
else
    resultado "T-09 REPROVADO: ver $EVID/resumo.txt"
    exit 1
fi
