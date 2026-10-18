#!/usr/bin/env bash
# T-08 — despacho tudo-ou-nada, envelhecimento e caixa maior que a frota
source "$(dirname "$0")/comum.sh" T-08
# (a) 5 robôs, uma caixa de tamanho 4 entre muitas de tamanho 1
iniciar_sistema --robos 5 --cenario config/testes_t08a.json
for _ in $(seq 240); do
    grep -q "entregou caixa 2 " logs/ultima/cargas.csv 2>/dev/null && break
    sleep 1
done
sleep 2
parar_sistema
mkdir -p "$EVID/a" && mv "$EVID"/*.csv "$EVID/execucao.log" "$EVID/a/"
t_disp=$(awk -F';' '$9 ~ /^caixa 2 disponibilizada/ {print $10; exit}' "$EVID/a/cargas.csv")
t_entr=$(awk -F';' '$9 ~ /^entregou caixa 2 / {print $10; exit}' "$EVID/a/cargas.csv")
outras=$(grep -c "entregou caixa" "$EVID/a/cargas.csv" || true)
if [[ -n "$t_disp" && -n "$t_entr" ]]; then espera_a=$(( (t_entr - t_disp) / 1000000000 )); else espera_a=-1; fi

# (b) caixa de tamanho 4 com apenas 3 robôs
iniciar_sistema --robos 3 --cenario config/testes_t08b.json
sleep 120
parar_sistema
mkdir -p "$EVID/b" && mv "$EVID"/*.csv "$EVID/execucao.log" "$EVID/b/"
aguarda=$(grep -c "caixa 1 aguardando frota (tamanho 4, 3 operacionais)" "$EVID/b/cargas.csv" || true)
designada=$(grep -c "caixa 1 designada" "$EVID/b/cargas.csv" || true)
entregues_b=$(grep -c "entregou caixa" "$EVID/b/cargas.csv" || true)
{
    echo "(a) caixa de tamanho 4 entregue ${espera_a} s após ficar disponível; $outras caixas entregues no total"
    echo "(b) sinalizada como aguardando frota: $aguarda; designada: $designada; outras caixas entregues: $entregues_b"
} | tee "$EVID/resumo.txt"
if (( espera_a >= 0 && espera_a <= 180 && aguarda >= 1 && designada == 0 && entregues_b > 0 )); then
    resultado "T-08 APROVADO: (a) caixa de 4 entregue em ${espera_a} s; (b) caixa de 4 aguardando frota e $entregues_b outras entregues"
else
    resultado "T-08 REPROVADO: ver $EVID/resumo.txt"
    exit 1
fi
