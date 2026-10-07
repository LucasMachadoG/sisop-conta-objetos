#!/usr/bin/env python3
"""Contagem de referencia independente (BFS em Python, conectividade 8).

Usada apenas para validar as versoes em C: nao compartilha codigo com
elas. Uso: python3 scripts/referencia.py arquivo.txt
"""
import sys
from collections import deque


def ler(caminho):
    tokens = []
    with open(caminho) as f:
        for linha in f:
            linha = linha.split("#", 1)[0]
            for parte in linha.split():
                tokens.append(parte)
    linhas, colunas = int(tokens[0]), int(tokens[1])
    digitos = "".join(tokens[2:])
    assert len(digitos) == linhas * colunas, "quantidade de celulas incorreta"
    return [[int(digitos[r * colunas + c]) for c in range(colunas)] for r in range(linhas)]


def contar(m):
    linhas, colunas = len(m), len(m[0])
    visto = [[False] * colunas for _ in range(linhas)]
    objetos = 0
    for r in range(linhas):
        for c in range(colunas):
            if m[r][c] and not visto[r][c]:
                objetos += 1
                visto[r][c] = True
                fila = deque([(r, c)])
                while fila:
                    a, b = fila.popleft()
                    for da in (-1, 0, 1):
                        for db in (-1, 0, 1):
                            x, y = a + da, b + db
                            if 0 <= x < linhas and 0 <= y < colunas and m[x][y] and not visto[x][y]:
                                visto[x][y] = True
                                fila.append((x, y))
    return objetos


if __name__ == "__main__":
    print(contar(ler(sys.argv[1])))
