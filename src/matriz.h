/*
 * matriz.h - Estruturas e rotinas comuns as versoes sequencial e paralela.
 *
 * Trabalho de Sistemas Operacionais - PUCRS 2026/II
 * Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)
 *
 * Este modulo concentra o que NAO e algoritmo de contagem: leitura da
 * matriz, geracao deterministica de matrizes grandes, medicao de tempo,
 * pilha dinamica e alocacao com verificacao de erro. Assim as duas
 * versoes processam exatamente os mesmos dados de entrada.
 */
#ifndef MATRIZ_H
#define MATRIZ_H

#include <stddef.h>
#include <stdio.h>

/* Matriz binaria armazenada em um vetor linear (linha por linha). */
typedef struct {
    long linhas;
    long colunas;
    unsigned char *celulas;   /* linhas * colunas valores 0 ou 1 */
} Matriz;

/* Pilha de indices lineares usada pelo flood fill iterativo. */
typedef struct {
    long *dados;
    long topo;                /* quantidade de elementos empilhados */
    long capacidade;
} Pilha;

/* Deslocamentos dos 8 vizinhos (conectividade 8). */
extern const int VIZINHO_DL[8];
extern const int VIZINHO_DC[8];

/* Entrada ------------------------------------------------------------ */
int  matriz_carregar_arquivo(const char *caminho, Matriz *m);
int  matriz_gerar(long linhas, long colunas, double densidade,
                  unsigned long semente, Matriz *m);
int  matriz_ler_entrada(int argc, char **argv, int indice, Matriz *m);
int  matriz_salvar(FILE *saida, const Matriz *m);
void matriz_liberar(Matriz *m);

/* Pilha -------------------------------------------------------------- */
int  pilha_iniciar(Pilha *p, long capacidade_inicial);
int  pilha_empilhar(Pilha *p, long valor);
void pilha_liberar(Pilha *p);

/* Utilitarios -------------------------------------------------------- */
void  *alocar_zerado(size_t quantidade, size_t tamanho, const char *descricao);
double tempo_atual_ms(void);
void   matriz_imprimir_rotulos(const Matriz *m, const int *rotulos);

#endif
