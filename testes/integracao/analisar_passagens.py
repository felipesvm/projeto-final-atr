#!/usr/bin/env python3
"""T-06: reconstrói a ocupação de cada passagem a partir dos registros de aquisição e liberação."""
import json
import re
import sys
from collections import defaultdict

pasta = sys.argv[1]
cap = {p["nome"]: p["capacidade"] for p in json.load(open("config/cenario.json"))["passagens"]}
eventos = []
rx = re.compile(r"(adquiriu|liberou) (\d+) vaga\(s\) em (P\d+)")
for linha in open(f"{pasta}/eventos.csv", encoding="utf-8"):
    c = linha.rstrip("\n").split(";")
    if len(c) < 10:
        continue
    m = rx.search(c[8])
    if m:
        eventos.append((int(c[9]), m.group(1), int(m.group(2)), m.group(3), c[3]))
eventos.sort()

ocup = defaultdict(int)
maximo = defaultdict(int)
entradas = defaultdict(int)
por_k = defaultdict(int)
violacoes = 0
for _, acao, k, p, _robo in eventos:
    if acao == "adquiriu":
        ocup[p] += k
        entradas[p] += 1
        por_k[k] += 1
    else:
        ocup[p] -= k
    maximo[p] = max(maximo[p], ocup[p])
    if ocup[p] > cap[p] or ocup[p] < 0:
        violacoes += 1

espelho_max = defaultdict(int)
for linha in open(f"{pasta}/espelho.txt", encoding="utf-8"):
    m = re.match(r"\s+(P\d+)\s+capacidade (\d+)\s+ocupação (-?\d+) \(máx (\d+)\)", linha)
    if m:
        espelho_max[m.group(1)] = max(espelho_max[m.group(1)], int(m.group(4)))

ok = violacoes == 0 and all(maximo[p] <= cap[p] for p in cap) and all(espelho_max[p] <= cap[p] for p in cap)
print("APROVADO" if ok else "REPROVADO")
print("; ".join(f"{p}: capacidade {cap[p]}, ocupação máxima {maximo[p]}, {entradas[p]} travessias" for p in sorted(cap))
      + f"; aquisições por tamanho de grupo: {dict(sorted(por_k.items()))}; violações: {violacoes}")
