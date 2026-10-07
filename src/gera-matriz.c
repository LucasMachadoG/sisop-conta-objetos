/*
 * gera-matriz.c - Gera uma matriz binaria aleatoria em formato texto.
 *
 * Trabalho de Sistemas Operacionais - PUCRS 2026/II
 * Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)
 *
 * Usa o mesmo gerador da opcao -g dos programas de contagem, entao
 *   gera-matriz L C D S > arq.txt   e   -g L C D S
 * produzem exatamente a mesma matriz.
 *
 * Uso: gera-matriz linhas colunas densidade semente > arquivo.txt
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>

#include "matriz.h"

int main(int argc, char **argv)
{
    Matriz m;
    int status = EXIT_SUCCESS;
    char opcao_g[] = "-g";
    char *argumentos[5];

    if (argc != 5) {
        fprintf(stderr, "uso: %s linhas colunas densidade semente > arquivo.txt\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* Reaproveita o interpretador da opcao -g. */
    argumentos[0] = opcao_g;
    argumentos[1] = argv[1];
    argumentos[2] = argv[2];
    argumentos[3] = argv[3];
    argumentos[4] = argv[4];
    if (matriz_ler_entrada(5, argumentos, 0, &m) != 0) {
        return EXIT_FAILURE;
    }
    if (matriz_salvar(stdout, &m) != 0 || fflush(stdout) != 0) {
        perror("erro: escrita da matriz");
        status = EXIT_FAILURE;
    }
    matriz_liberar(&m);
    return status;
}
