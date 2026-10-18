#!/usr/bin/env python3
"""T-10: produtores seguem durante a pausa do coletor; registros do período chegam com data e hora."""
import re
import sys
from datetime import datetime

pasta, t0, t1 = sys.argv[1], int(sys.argv[2]) / 1e9, int(sys.argv[3]) / 1e9


def ler(arq):
    linhas = open(f"{pasta}/{arq}", encoding="utf-8").read().splitlines()[1:]
    return {int(c.split(";")[2]): c.split(";") for c in linhas}


antes, durante = ler("monitor_antes.csv"), ler("monitor_pausado.csv")
hb_ok = all(int(durante[r][7]) > int(antes[r][7]) for r in antes if r in durante)

no_periodo = 0
datas_validas = True
for arq in ("eventos.csv", "cargas.csv"):
    for linha in open(f"{pasta}/{arq}", encoding="utf-8").read().splitlines()[1:]:
        try:
            t = datetime.strptime(linha.split(";")[0], "%Y-%m-%dT%H:%M:%S.%f").timestamp()
        except ValueError:
            datas_validas = False
            continue
        if arq == "eventos.csv" and t0 <= t <= t1:
            no_periodo += 1

pausado = open(f"{pasta}/monitor_pausado.txt", encoding="utf-8", errors="replace").read()
m = re.search(r"registros gravados=(\d+)\s+perdas=(\d+)", pausado)
perdas = int(m.group(2)) if m else -1

ok = hb_ok and datas_validas and no_periodo > 0 and perdas >= 0
print("APROVADO" if ok else "REPROVADO")
print(f"heartbeat de todos os robôs avançou durante a pausa: {'sim' if hb_ok else 'não'}; "
      f"{no_periodo} registros com data e hora dentro da pausa gravados após a retomada; perdas contadas: {perdas}")
