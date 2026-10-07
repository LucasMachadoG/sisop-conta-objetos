/*
 * conta-objetos-sequencial.c - Versao sequencial de referencia.
 *
 * Trabalho de Sistemas Operacionais - PUCRS 2026/II
 * Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)
 *
 * Conta os componentes conexos de celulas 1 usando conectividade 8.
 * Percorre a matriz em ordem de linhas; ao encontrar uma celula 1 ainda
 * sem rotulo, inicia um flood fill que marca todo o objeto com um novo
 * rotulo. O numero de flood fills iniciados e o numero de objetos.
 *
 * O flood fill usa uma pilha explicita em vez de recursao: em matrizes
 * grandes um unico objeto pode ter milhoes de celulas, o que estouraria
 * a pilha de chamadas do processo.
 *
 * Uso:
 *   conta-objetos-sequencial [-v] arquivo
 *   conta-objetos-sequencial [-v] -g linhas colunas densidade semente
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "matriz.h"

/*
 * Rotula todos os objetos da matriz. rotulos[i] == 0 significa "fundo ou
 * ainda nao visitado"; rotulos[i] == k (k >= 1) indica o objeto k.
 * Retorna a quantidade de objetos ou -1 em caso de erro.
 */
static long contar_objetos_sequencial(const Matriz *m, int *rotulos)
{
    const long L = m->linhas;
    const long C = m->colunas;
    const unsigned char *cel = m->celulas;
    Pilha pilha;
    long total = L * C;
    long inicio, atual, r, c, nr, nc, vizinho;
    int k;
    int objetos = 0;

    if (pilha_iniciar(&pilha, 1024) != 0) {
        return -1;
    }

    for (inicio = 0; inicio < total; inicio++) {
        if (cel[inicio] == 0 || rotulos[inicio] != 0) {
            continue;
        }
        /* Celula 1 nao visitada: inicio de um novo objeto. */
        objetos++;
        rotulos[inicio] = objetos;
        if (pilha_empilhar(&pilha, inicio) != 0) {
            pilha_liberar(&pilha);
            return -1;
        }
        while (pilha.topo > 0) {
            atual = pilha.dados[--pilha.topo];
            r = atual / C;
            c = atual % C;
            /* Verifica os 8 vizinhos: horizontais, verticais e diagonais. */
            for (k = 0; k < 8; k++) {
                nr = r + VIZINHO_DL[k];
                nc = c + VIZINHO_DC[k];
                if (nr < 0 || nr >= L || nc < 0 || nc >= C) {
                    continue;
                }
                vizinho = nr * C + nc;
                /* Marca ao empilhar: cada celula entra na pilha uma unica vez. */
                if (cel[vizinho] != 0 && rotulos[vizinho] == 0) {
                    rotulos[vizinho] = objetos;
                    if (pilha_empilhar(&pilha, vizinho) != 0) {
                        pilha_liberar(&pilha);
                        return -1;
                    }
                }
            }
        }
    }

    pilha_liberar(&pilha);
    return objetos;
}

static void uso(const char *programa)
{
    fprintf(stderr,
            "uso: %s [-v] arquivo\n"
            "     %s [-v] -g linhas colunas densidade semente\n",
            programa, programa);
}

int main(int argc, char **argv)
{
    Matriz m;
    int *rotulos;
    int verboso = 0;
    int indice = 1;
    long objetos;
    double t0, t1;

    if (indice < argc && strcmp(argv[indice], "-v") == 0) {
        verboso = 1;
        indice++;
    }
    if (indice >= argc || strcmp(argv[indice], "-h") == 0) {
        uso(argv[0]);
        return EXIT_FAILURE;
    }
    if (matriz_ler_entrada(argc, argv, indice, &m) != 0) {
        uso(argv[0]);
        return EXIT_FAILURE;
    }

    /* Trecho medido: alocacao dos rotulos + rotulacao/contagem. */
    t0 = tempo_atual_ms();
    rotulos = (int *) alocar_zerado((size_t) (m.linhas * m.colunas), sizeof(int), "rotulos");
    if (rotulos == NULL) {
        matriz_liberar(&m);
        return EXIT_FAILURE;
    }
    objetos = contar_objetos_sequencial(&m, rotulos);
    t1 = tempo_atual_ms();

    if (objetos < 0) {
        free(rotulos);
        matriz_liberar(&m);
        return EXIT_FAILURE;
    }

    printf("Versao: sequencial\n");
    printf("Matriz: %ld x %ld\n", m.linhas, m.colunas);
    if (verboso) {
        if (m.linhas * m.colunas <= 4096) {
            printf("Rotulos:\n");
            matriz_imprimir_rotulos(&m, rotulos);
        } else {
            printf("Rotulos: matriz grande demais para impressao\n");
        }
    }
    printf("Objetos: %ld\n", objetos);
    printf("Tempo (ms): %.3f\n", t1 - t0);

    free(rotulos);
    matriz_liberar(&m);
    return EXIT_SUCCESS;
}
