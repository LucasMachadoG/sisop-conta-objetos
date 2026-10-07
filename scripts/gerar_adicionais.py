#!/usr/bin/env python3
"""Gera as matrizes de teste adicionais em tests/adicionais/.

Cada arquivo traz um comentario '# esperado: N' com o valor calculado
de forma independente por scripts/referencia.py (BFS em Python), que
nao compartilha codigo com as versoes em C.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from referencia import contar  # noqa: E402

DESTINO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "adicionais")


def salvar(nome, descricao, matriz):
    linhas, colunas = len(matriz), len(matriz[0])
    esperado = contar(matriz)
    caminho = os.path.join(DESTINO, nome)
    with open(caminho, "w") as f:
        f.write(f"# {descricao}\n# esperado: {esperado}\n{linhas} {colunas}\n")
        for linha in matriz:
            f.write(" ".join(str(v) for v in linha) + "\n")
    print(f"{nome}: {linhas}x{colunas}, esperado {esperado}")


def main():
    os.makedirs(DESTINO, exist_ok=True)

    # A1: somente zeros
    salvar("a01-vazia.txt", "A1 - Matriz somente com zeros", [[0] * 10 for _ in range(10)])

    # A2: um unico objeto em serpentina que passa por todos os blocos
    n = 15
    m = [[0] * n for _ in range(n)]
    for r in range(0, n, 2):
        for c in range(n):
            m[r][c] = 1
    for r in range(1, n, 2):
        m[r][n - 1 if (r // 2) % 2 == 0 else 0] = 1
    salvar("a02-serpentina.txt", "A2 - Um unico objeto em serpentina ocupando todas as regioes", m)

    # A3: tabuleiro de xadrez - celulas ligadas somente pelas diagonais
    salvar("a03-xadrez.txt", "A3 - Tabuleiro de xadrez: conexoes somente diagonais",
           [[1 if (r + c) % 2 == 0 else 0 for c in range(9)] for r in range(9)])

    # A4: diagonais paralelas separadas (cada uma e um objeto)
    n = 16
    m = [[0] * n for _ in range(n)]
    for inicio in range(-n + 1, n, 3):
        for r in range(n):
            c = r + inicio
            if 0 <= c < n:
                m[r][c] = 1
    salvar("a04-diagonais.txt", "A4 - Diagonais paralelas separadas por duas celulas", m)

    # A5: matriz cheia (um objeto) e casos degenerados de dimensao
    salvar("a05-cheia.txt", "A5 - Matriz totalmente preenchida", [[1] * 13 for _ in range(11)])
    salvar("a06-uma-linha.txt", "A6 - Uma unica linha (mais blocos pedidos do que linhas)",
           [[1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1]])
    salvar("a07-uma-coluna.txt", "A7 - Uma unica coluna",
           [[v] for v in [1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1]])
    salvar("a08-celula-unica.txt", "A8 - Matriz 1x1 com valor 1", [[1]])

    # A9: X gigante cruzando o centro (encontro de quatro blocos so pela diagonal)
    n = 20
    m = [[1 if (r == c or r + c == n - 1) else 0 for c in range(n)] for r in range(n)]
    salvar("a09-xis.txt", "A9 - Dois tracos diagonais em X cruzando o centro", m)

    # A10: matriz aleatoria media (usada tambem para comparar com o oraculo)
    random.seed(2026)
    m = [[1 if random.random() < 0.35 else 0 for _ in range(300)] for _ in range(200)]
    salvar("a10-aleatoria-200x300.txt", "A10 - Aleatoria 200x300, densidade 0,35", m)


if __name__ == "__main__":
    main()
