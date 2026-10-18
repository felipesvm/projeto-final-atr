#!/usr/bin/env bash
# T-02 — ausência de condições de corrida: organizador, coletor, gestor e 4 robôs com ThreadSanitizer
DURACAO="${DURACAO:-600}"
BIN=bin-tsan
source "$(dirname "$0")/comum.sh" T-02
make -s tsan > /dev/null
mkdir -p "$EVID/tsan"
export TSAN_OPTIONS="log_path=$RAIZ/$EVID/tsan/relatorio halt_on_error=0 second_deadlock_stack=1"
iniciar_sistema --robos 4 --cenario config/testes_misto.json
sleep "$DURACAO"
parar_sistema
entregues=$(grep -c "entregou caixa" "$EVID/cargas.csv" || true)
avisos=$(cat "$EVID"/tsan/relatorio.* 2>/dev/null | grep -c "WARNING: ThreadSanitizer" || true)
echo "duração: ${DURACAO}s, caixas entregues: $entregues, avisos do ThreadSanitizer: $avisos" > "$EVID/resumo.txt"
if (( avisos == 0 )); then
    resultado "T-02 APROVADO: nenhum aviso do ThreadSanitizer em ${DURACAO}s ($entregues caixas entregues)"
else
    resultado "T-02 REPROVADO: $avisos aviso(s) do ThreadSanitizer (ver $EVID/tsan)"
    exit 1
fi
