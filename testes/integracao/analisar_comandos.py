#!/usr/bin/env python3
"""T-04: confere ordem de recepção, perdas e duplicações na fila de comandos do robô.

Cada rodada é uma execução da ferramenta rajada_comandos (que numera as próprias mensagens a partir de 1)
seguida de um RETOMAR; as recepções da T_NAV são separadas em rodadas pelo RETOMAR."""
import re
import sys

pasta = sys.argv[1]
rodadas = []
for linha in open(f"{pasta}/envios.txt", encoding="utf-8"):
    if linha.startswith("=="):
        rodadas.append([])
    m = re.match(r"enviado (\S+) seq=(\d+)", linha)
    if m:
        rodadas[-1].append((m.group(1), int(m.group(2))))

recebidas = [[]]
rx = re.compile(r"\[robo_1\] T_NAV: comando (\S+) seq=(\d+) prio=(\d+)")
for linha in open(f"{pasta}/execucao.log", encoding="utf-8", errors="replace"):
    m = rx.search(linha)
    if not m:
        continue
    if m.group(1) == "RETOMAR":
        recebidas.append([])
    elif m.group(1) in ("MISSAO_EQUIPE", "PARAR"):
        recebidas[-1].append((m.group(1), int(m.group(2))))
recebidas = [r for r in recebidas if r]

enviadas = sum(len(r) for r in rodadas)
total_recebidas = sum(len(r) for r in recebidas)
perdidas = duplicadas = 0
ultrapassou = 0
for env, rec in zip(rodadas, recebidas):
    seqs = [s for _, s in rec]
    duplicadas += len(seqs) - len(set(seqs))
    perdidas += len({s for _, s in env} - set(seqs))
    seq_parar = next(s for t, s in env if t == "PARAR")
    antes = {s for t, s in env if t != "PARAR" and s < seq_parar}
    if seq_parar in seqs:
        pos = seqs.index(seq_parar)
        if all(seqs.index(s) > pos for s in antes if s in seqs):
            ultrapassou += 1
perdidas += sum(len(r) for r in rodadas[len(recebidas):])

ok = duplicadas == 0 and perdidas == 0 and ultrapassou == len(rodadas) and len(recebidas) == len(rodadas)
print("APROVADO" if ok else "REPROVADO")
print(f"{len(rodadas)} rodadas, {enviadas} mensagens enviadas, {total_recebidas} recebidas; {perdidas} perdidas, "
      f"{duplicadas} duplicadas; PARAR recebido antes das missões enviadas antes dele em {ultrapassou} de {len(rodadas)} rodadas")
