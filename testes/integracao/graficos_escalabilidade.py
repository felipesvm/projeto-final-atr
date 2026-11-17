#!/usr/bin/env python3
"""Gráficos da rampa de escalabilidade: lê resumo.csv e grava os PNG na mesma pasta.

Uso: python3 testes/integracao/graficos_escalabilidade.py testes/resultados/escalabilidade
"""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

T_NAV, T_STATUS = 50.0, 100.0
TINTA, CINZA, GRADE = "#1C2127", "#5B6774", "#E1E5EA"
COR_NAV, COR_STATUS, COR_LIMITE = "#2F6FB0", "#2FA88F", "#C0392B"

pasta = Path(sys.argv[1] if len(sys.argv) > 1 else "testes/resultados/escalabilidade")
linhas = list(csv.DictReader(open(pasta / "resumo.csv"), delimiter=";"))
por_n = defaultdict(list)
for l in linhas:
    por_n[int(l["n"])].append({k: float(v) for k, v in l.items()})
ns = sorted(por_n)
nucleos = 2
for linha in open(pasta / "plataforma.txt"):
    if linha.startswith("nucleos:"):
        nucleos = int(linha.split(":")[1])


def media(campo):
    return [statistics.mean(r[campo] for r in por_n[n]) for n in ns]


def pior(campo):
    return [max(r[campo] for r in por_n[n]) for n in ns]


def estilo(ax, titulo, ylabel):
    ax.set_title(titulo, loc="left", fontsize=12, fontweight="bold", color=TINTA)
    ax.set_xlabel("robôs na frota (N)", color=CINZA)
    ax.set_ylabel(ylabel, color=CINZA)
    ax.grid(axis="y", color=GRADE)
    ax.set_axisbelow(True)
    for lado in ("top", "right"):
        ax.spines[lado].set_visible(False)
    ax.spines["left"].set_color(GRADE)
    ax.spines["bottom"].set_color(GRADE)
    ax.tick_params(colors=CINZA)
    ax.set_xticks(ns)


plt.rcParams.update({"font.size": 10, "font.family": "DejaVu Sans"})

# 1. tempo de resposta da T_NAV e da T_STATUS contra os prazos
fig, ax = plt.subplots(figsize=(7.2, 3.8))
ax.plot(ns, pior("nav_resp_max_ms"), "o-", color=COR_NAV, label="T_NAV: pior tempo de resposta")
ax.plot(ns, pior("status_resp_max_ms"), "s-", color=COR_STATUS, label="T_STATUS: pior tempo de resposta")
ax.axhline(T_NAV, color=COR_NAV, ls="--", lw=1)
ax.axhline(T_STATUS, color=COR_STATUS, ls="--", lw=1)
ax.text(ns[0], T_NAV + 2, "prazo da T_NAV (50 ms)", color=COR_NAV, fontsize=9)
ax.text(ns[0], T_STATUS + 2, "prazo da T_STATUS (100 ms)", color=COR_STATUS, fontsize=9)
ax.set_ylim(0, max(T_STATUS * 1.15, max(pior("status_resp_max_ms") + pior("nav_resp_max_ms")) * 1.1))
estilo(ax, "Pior tempo de resposta por tamanho de frota", "ms")
ax.legend(frameon=False, loc="center right")
fig.tight_layout()
fig.savefig(pasta / "grafico_resposta.png", dpi=200)

# 2. tempo de execução médio e máximo da T_NAV
fig, ax = plt.subplots(figsize=(7.2, 3.4))
ax.plot(ns, media("nav_c_med_ms"), "o-", color=COR_NAV, label="T_NAV: execução média")
ax.plot(ns, pior("nav_c_max_ms"), "o--", color=COR_NAV, alpha=0.55, label="T_NAV: execução máxima")
ax.plot(ns, media("status_c_med_ms"), "s-", color=COR_STATUS, label="T_STATUS: execução média")
ax.set_yscale("log")
estilo(ax, "Tempo de execução de um ciclo", "ms (escala log)")
ax.legend(frameon=False)
fig.tight_layout()
fig.savefig(pasta / "grafico_execucao.png", dpi=200)

# 3. utilização estimada pelas tarefas críticas e limite de Liu e Layland
u_med = [n * (c / T_NAV + s / T_STATUS) / nucleos for n, c, s in zip(ns, media("nav_c_med_ms"), media("status_c_med_ms"))]
u_pior = [n * (c / T_NAV + s / T_STATUS) / nucleos for n, c, s in zip(ns, pior("nav_c_max_ms"), pior("status_c_max_ms"))]
fig, ax = plt.subplots(figsize=(7.2, 3.4))
ax.plot(ns, u_med, "o-", color=COR_NAV, label="com o tempo médio de execução")
ax.plot(ns, u_pior, "o--", color=COR_NAV, alpha=0.55, label="com o tempo máximo de execução")
ax.axhline(0.693, color=COR_LIMITE, ls="--", lw=1)
ax.text(ns[0], 0.72, "limite suficiente de Liu e Layland (ln 2)", color=COR_LIMITE, fontsize=9)
estilo(ax, f"Utilização por núcleo das tarefas críticas ({nucleos} núcleos)", "utilização")
ax.legend(frameon=False, loc="upper left", bbox_to_anchor=(0, 0.9))
fig.tight_layout()
fig.savefig(pasta / "grafico_utilizacao.png", dpi=200)

# 4. vazão e carga de CPU
fig, ax = plt.subplots(figsize=(7.2, 3.4))
dur = 1.0
for linha in open(pasta / "plataforma.txt"):
    if "duracao:" in linha:
        dur = float(linha.split("duracao:")[1].split("s")[0])
vazao = [statistics.mean(r["entregues"] for r in por_n[n]) * 60.0 / dur for n in ns]
ax.bar(ns, vazao, width=[max(1.0, n * 0.25) for n in ns], color="#F5B82E", label="entregas por minuto")
estilo(ax, "Entregas por minuto e uso de CPU", "entregas por minuto")
ax2 = ax.twinx()
ax2.plot(ns, media("cpu_pct"), "o-", color=TINTA, label="CPU ocupada (%)")
ax2.set_ylim(0, 100)
ax2.set_ylabel("CPU ocupada (%)", color=CINZA)
ax2.spines["top"].set_visible(False)
h1, l1 = ax.get_legend_handles_labels()
h2, l2 = ax2.get_legend_handles_labels()
ax.legend(h1 + h2, l1 + l2, frameon=False, loc="upper left")
fig.tight_layout()
fig.savefig(pasta / "grafico_vazao.png", dpi=200)

# tabela consolidada para o relatório
with open(pasta / "consolidado.csv", "w") as f:
    f.write("n;repeticoes;prontos;entregas_min;perdas;nav_c_med_ms;nav_c_max_ms;nav_resp_max_ms;status_resp_max_ms;cpu_pct;u_med;u_pior\n")
    for i, n in enumerate(ns):
        rs = por_n[n]
        f.write(
            f"{n};{len(rs)};{min(r['prontos'] for r in rs):.0f};{vazao[i]:.2f};{sum(r['nav_perdas'] + r['status_perdas'] for r in rs):.0f};"
            f"{media('nav_c_med_ms')[i]:.3f};{pior('nav_c_max_ms')[i]:.2f};{pior('nav_resp_max_ms')[i]:.2f};"
            f"{pior('status_resp_max_ms')[i]:.2f};{media('cpu_pct')[i]:.1f};{u_med[i]:.3f};{u_pior[i]:.3f}\n"
        )
print("gráficos gravados em", pasta)
