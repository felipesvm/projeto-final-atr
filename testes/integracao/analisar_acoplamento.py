#!/usr/bin/env python3
"""T-07: agrupa os acionamentos de o_acoplar_e_erguer por equipe (mesmo t_go) e mede a dispersão."""
import re
import sys
from collections import defaultdict

pasta = sys.argv[1]
rx = re.compile(r"\[robo_(\d+)\] T_NAV: acoplar equipe=(\d+) t_go=(\d+) t=(\d+) atraso_ms=([-\d.]+)")
grupos = defaultdict(list)
for linha in open(f"{pasta}/execucao.log", encoding="utf-8", errors="replace"):
    m = rx.search(linha)
    if m:
        robo, eq, tgo, t, _ = m.groups()
        grupos[(int(eq), int(tgo))].append((int(robo), int(t)))

# só equipes completas: o tamanho vem do número de robôs que acionaram com o mesmo t_go
equipes = {k: v for k, v in grupos.items() if len(v) >= 2}
if len(sys.argv) > 2 and sys.argv[2] == "--contar":
    print(len(equipes))
    sys.exit(0)

disp = []
antes = 0
por_k = defaultdict(int)
for (eq, tgo), v in sorted(equipes.items(), key=lambda kv: kv[0][1]):
    ts = [t for _, t in v]
    disp.append((max(ts) - min(ts)) / 1e6)
    antes += sum(1 for t in ts if t < tgo)
    por_k[len(v)] += 1
with open(f"{pasta}/dispersao.csv", "w") as f:
    f.write("equipe;t_go_ns;k;dispersao_ms\n")
    for ((eq, tgo), v), d in zip(sorted(equipes.items(), key=lambda kv: kv[0][1]), disp):
        f.write(f"{eq};{tgo};{len(v)};{d:.3f}\n")

ok = bool(disp) and max(disp) <= 60.0 and antes == 0
print("APROVADO" if ok else "REPROVADO")
if disp:
    s = sorted(disp)
    print(f"{len(disp)} equipes (por tamanho: {dict(sorted(por_k.items()))}); dispersão máxima {max(disp):.2f} ms, "
          f"mediana {s[len(s)//2]:.2f} ms, média {sum(disp)/len(disp):.2f} ms; acionamentos antes de t_go: {antes}")
else:
    print("nenhuma equipe acoplou")
