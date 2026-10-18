#!/usr/bin/env python3
"""T-11: injeção (T_IO) -> detecção (T_STATUS) -> congelamento do setpoint (T_NAV), por robô."""
import re
import sys

pasta = sys.argv[1]
rx = re.compile(r"\[robo_(\d+)\] (T_IO: defeito injetado|T_STATUS: defeito detectado|T_NAV: setpoint congelado \(defeito\)) t_mono=(\d+)")
pend = {}
amostras = []
for linha in open(f"{pasta}/execucao.log", encoding="utf-8", errors="replace"):
    m = rx.search(linha)
    if not m:
        continue
    robo, evento, t = int(m.group(1)), m.group(2), int(m.group(3))
    if "injetado" in evento:
        pend[robo] = {"inj": t}
    elif robo in pend:
        pend[robo]["det" if "detectado" in evento else "cong"] = t
        if "det" in pend[robo] and "cong" in pend[robo]:
            p = pend.pop(robo)
            amostras.append(((p["det"] - p["inj"]) / 1e6, (p["cong"] - p["det"]) / 1e6, (p["cong"] - p["inj"]) / 1e6))

with open(f"{pasta}/tempos.csv", "w") as f:
    f.write("injecao_deteccao_ms;deteccao_congelamento_ms;total_ms\n")
    for a in amostras:
        f.write(";".join(f"{x:.3f}" for x in a) + "\n")

if not amostras:
    print("REPROVADO")
    print("nenhuma injeção completa encontrada")
    sys.exit(0)
det = [a[0] for a in amostras]
cong = [a[1] for a in amostras]
tot = sorted(a[2] for a in amostras)
ok = max(cong) <= 100.0
print("APROVADO" if ok else "REPROVADO")
print(f"{len(amostras)} injeções; injeção->detecção máx {max(det):.1f} ms; detecção->congelamento máx {max(cong):.1f} ms "
      f"(limite de dois ciclos da T_NAV = 100 ms); total máx {max(tot):.1f} ms, p99 {tot[int(0.99 * (len(tot) - 1))]:.1f} ms")
