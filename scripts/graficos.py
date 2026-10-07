#!/usr/bin/env python3
"""Gera o resumo estatistico e os graficos a partir de results/medicoes.csv.

Uso: python3 scripts/graficos.py results/medicoes.csv results [nucleos]

O terceiro argumento (opcional) marca nos graficos a quantidade de nucleos
da maquina; se omitido, usa os.cpu_count().

Saidas:
  results/resumo.csv            mediana, quartis, aceleracao e eficiencia
  results/grafico-tempo.png     tempo (mediana, barras = Q1..Q3)
  results/grafico-aceleracao.png
  results/grafico-eficiencia.png

Medida representativa: mediana das repeticoes (robusta a interferencias
pontuais do sistema). Dispersao: intervalo interquartil (Q1 a Q3).

Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)
"""
import csv
import os
import statistics
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

AZUL = "#2a78d6"
LARANJA = "#eb6834"
TEXTO = "#0b0b0b"
TEXTO_SEC = "#52514e"
GRADE = "#e4e3df"


def quartis(valores):
    v = sorted(valores)
    if len(v) < 4:
        return v[0], v[-1]
    q = statistics.quantiles(v, n=4, method="inclusive")
    return q[0], q[2]


def carregar(caminho):
    grupos = {}
    with open(caminho) as f:
        for linha in csv.DictReader(f):
            chave = (linha["versao"], int(linha["trabalhadores"]))
            g = grupos.setdefault(chave, {"tempos": [], "corretos": True, "blocos": linha["blocos"]})
            g["tempos"].append(float(linha["tempo_ms"]))
            g["corretos"] = g["corretos"] and linha["resultado_correto"] == "true"
    return grupos


def estilo(ax, titulo, xlabel, ylabel):
    ax.set_title(titulo, loc="left", fontsize=12, color=TEXTO, pad=12)
    ax.set_xlabel(xlabel, color=TEXTO_SEC)
    ax.set_ylabel(ylabel, color=TEXTO_SEC)
    ax.grid(axis="y", color=GRADE, linewidth=0.8)
    ax.set_axisbelow(True)
    for lado in ("top", "right"):
        ax.spines[lado].set_visible(False)
    for lado in ("left", "bottom"):
        ax.spines[lado].set_color("#b5b4ae")
    ax.tick_params(colors=TEXTO_SEC)


def formatar_ms(valor, maximo):
    """Casas decimais conforme a escala (matrizes pequenas medem microssegundos)."""
    if maximo >= 100:
        return f"{valor:.0f}"
    if maximo >= 1:
        return f"{valor:.2f}"
    return f"{valor:.3f}"


def marcar_nucleos(ax, nucleos, ps):
    """Linha vertical na quantidade de nucleos disponiveis."""
    if nucleos and min(ps) <= nucleos <= max(ps):
        ax.axvline(nucleos, color="#b5b4ae", linestyle=":", linewidth=1.2)
        ax.annotate(f"{nucleos} núcleos", (nucleos, 0), xytext=(4, 6),
                    textcoords="offset points", fontsize=8.5, color=TEXTO_SEC)


def main():
    entrada, destino = sys.argv[1], sys.argv[2]
    nucleos = int(sys.argv[3]) if len(sys.argv) > 3 else (os.cpu_count() or 0)
    grupos = carregar(entrada)
    seq = grupos[("sequencial", 1)]
    t_seq = statistics.median(seq["tempos"])
    paralelas = sorted(p for (v, p) in grupos if v == "paralela")

    linhas = []
    q1, q3 = quartis(seq["tempos"])
    linhas.append(["sequencial", 1, "-", len(seq["tempos"]), t_seq, q1, q3,
                   min(seq["tempos"]), max(seq["tempos"]), 1.0, 1.0, seq["corretos"]])
    for p in paralelas:
        g = grupos[("paralela", p)]
        med = statistics.median(g["tempos"])
        q1, q3 = quartis(g["tempos"])
        s = t_seq / med
        linhas.append(["paralela", p, g["blocos"], len(g["tempos"]), med, q1, q3,
                       min(g["tempos"]), max(g["tempos"]), s, s / p, g["corretos"]])

    with open(os.path.join(destino, "resumo.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["versao", "trabalhadores", "blocos", "repeticoes", "mediana_ms", "q1_ms",
                    "q3_ms", "min_ms", "max_ms", "aceleracao", "eficiencia", "todos_corretos"])
        for l in linhas:
            w.writerow(l[:4] + [f"{x:.3f}" for x in l[4:9]] + [f"{l[9]:.3f}", f"{l[10]:.3f}", str(l[11]).lower()])

    for l in linhas:
        print(f"{l[0]:<10} p={l[1]:<2} mediana={l[4]:9.1f} ms  IQR=[{l[5]:.1f}, {l[6]:.1f}]  "
              f"S={l[9]:.2f}  E={l[10]:.2f}  corretos={l[11]}")

    plt.rcParams.update({"font.size": 10, "figure.dpi": 150})

    # 1) Tempo
    rotulos = ["Seq."] + [f"Par. p={p}" for p in paralelas]
    medianas = [l[4] for l in linhas]
    erro_inf = [l[4] - l[5] for l in linhas]
    erro_sup = [l[6] - l[4] for l in linhas]
    cores = [LARANJA] + [AZUL] * len(paralelas)
    fig, ax = plt.subplots(figsize=(7, 4.2))
    barras = ax.bar(rotulos, medianas, color=cores, width=0.6,
                    yerr=[erro_inf, erro_sup], capsize=4, ecolor=TEXTO_SEC)
    for b, v, sup in zip(barras, medianas, erro_sup):
        ax.annotate(formatar_ms(v, max(medianas)), (b.get_x() + b.get_width() / 2, v + sup),
                    xytext=(0, 4), textcoords="offset points",
                    ha="center", fontsize=9, color=TEXTO)
    ax.set_ylim(0, max(l[6] for l in linhas) * 1.15)
    estilo(ax, "Tempo de execução (mediana; barras = intervalo interquartil)",
           "Versão e quantidade de threads", "Tempo (ms)")
    fig.tight_layout()
    fig.savefig(os.path.join(destino, "grafico-tempo.png"))
    plt.close(fig)

    # 2) Aceleracao
    ps = paralelas
    acel = [l[9] for l in linhas[1:]]
    fig, ax = plt.subplots(figsize=(7, 4.2))
    ax.plot(ps, ps, color=TEXTO_SEC, linestyle="--", linewidth=1.5, label="Ideal S(p) = p")
    ax.plot(ps, acel, color=AZUL, linewidth=2, marker="o", markersize=7, label="Observada")
    for p, s in zip(ps, acel):
        ax.annotate(f"{s:.2f}", (p, s), xytext=(0, -17), textcoords="offset points",
                    ha="center", fontsize=9, color=TEXTO)
    marcar_nucleos(ax, nucleos, ps)
    ax.set_xticks(ps)
    ax.set_xlim(0.5, max(ps) + 0.5)
    ax.set_ylim(0, max(max(ps), max(acel)) * 1.1)
    estilo(ax, "Aceleração S(p) = T_sequencial / T_paralelo(p)", "Threads (p)", "Aceleração")
    ax.legend(frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(os.path.join(destino, "grafico-aceleracao.png"))
    plt.close(fig)

    # 3) Eficiencia
    efic = [l[10] for l in linhas[1:]]
    fig, ax = plt.subplots(figsize=(7, 4.2))
    ax.axhline(1, color=TEXTO_SEC, linestyle="--", linewidth=1.5, label="Ideal E(p) = 1")
    ax.plot(ps, efic, color=AZUL, linewidth=2, marker="o", markersize=7, label="Observada")
    for p, e in zip(ps, efic):
        ax.annotate(f"{e:.2f}", (p, e), xytext=(0, -17), textcoords="offset points",
                    ha="center", fontsize=9, color=TEXTO)
    marcar_nucleos(ax, nucleos, ps)
    ax.set_xticks(ps)
    ax.set_xlim(0.5, max(ps) + 0.5)
    ax.set_ylim(0, max(1.15, max(efic) * 1.15))
    estilo(ax, "Eficiência E(p) = S(p) / p", "Threads (p)", "Eficiência")
    ax.legend(frameon=False, loc="lower left")
    fig.tight_layout()
    fig.savefig(os.path.join(destino, "grafico-eficiencia.png"))
    plt.close(fig)


if __name__ == "__main__":
    main()
