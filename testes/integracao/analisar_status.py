#!/usr/bin/env python3
"""T-03/T-12: compara o que o amr_monitor leu da tabela da frota com o que cada T_STATUS publicou."""
import re
import sys
from collections import defaultdict

pasta, n = sys.argv[1], int(sys.argv[2])
publicado = {}
rx = re.compile(r"\[robo_(\d+)\] T_STATUS: publicado hb=(\d+) estado=(\S+) pos=\(([-\d.]+),([-\d.]+)\) bat=([-\d.]+)")
for linha in open(f"{pasta}/execucao.log", encoding="utf-8", errors="replace"):
    m = rx.search(linha)
    if m:
        rid, hb, est, x, y, bat = m.groups()
        publicado[(int(rid), int(hb))] = (est, x, y, bat)

lidos = iguais = diferentes = sem_par = 0
ultimo_hb = defaultdict(int)
regressoes = 0
ids = set()
with open(f"{pasta}/monitor.csv", encoding="utf-8") as f:
    next(f)
    for linha in f:
        c = linha.strip().split(";")
        if len(c) < 10:
            continue
        rid, est, x, y, bat, hb = int(c[2]), c[3], c[4], c[5], c[6], int(c[7])
        ids.add(rid)
        lidos += 1
        if hb < ultimo_hb[rid]:
            regressoes += 1
        ultimo_hb[rid] = max(ultimo_hb[rid], hb)
        p = publicado.get((rid, hb))
        if p is None:
            sem_par += 1
        elif p == (est, x, y, bat):
            iguais += 1
        else:
            diferentes += 1

ok = diferentes == 0 and regressoes == 0 and len(ids) == n and iguais > 0
print("APROVADO" if ok else "REPROVADO")
print(f"{lidos} leituras de {len(ids)} robôs; {iguais} idênticas ao publicado, {diferentes} divergentes, "
      f"{sem_par} sem linha de log correspondente; {regressoes} regressões de heartbeat; "
      f"heartbeat final: " + ", ".join(f"{k}:{v}" for k, v in sorted(ultimo_hb.items())))
