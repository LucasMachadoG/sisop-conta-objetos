/*
 * matriz.c - Entrada, geracao de dados, pilha e medicao de tempo.
 *
 * Trabalho de Sistemas Operacionais - PUCRS 2026/II
 * Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)
 */
#define _POSIX_C_SOURCE 200112L
#ifdef __APPLE__
/* No macOS, _POSIX_C_SOURCE sozinho esconde clock_gettime/CLOCK_MONOTONIC. */
#define _DARWIN_C_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#include "matriz.h"

/* Ordem dos vizinhos: linha de cima, mesma linha, linha de baixo. */
const int VIZINHO_DL[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };
const int VIZINHO_DC[8] = { -1,  0,  1, -1, 1, -1, 0, 1 };

/* ------------------------------------------------------------------ */
/* Utilitarios                                                         */
/* ------------------------------------------------------------------ */

void *alocar_zerado(size_t quantidade, size_t tamanho, const char *descricao)
{
    void *p;

    if (quantidade == 0) {
        quantidade = 1;
    }
    p = calloc(quantidade, tamanho);
    if (p == NULL) {
        fprintf(stderr, "erro: falta de memoria ao alocar %s (%lu x %lu bytes)\n",
                descricao, (unsigned long) quantidade, (unsigned long) tamanho);
    }
    return p;
}

double tempo_atual_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("erro: clock_gettime");
        return 0.0;
    }
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1.0e6;
}

/* ------------------------------------------------------------------ */
/* Pilha dinamica (substitui a recursao do flood fill)                 */
/* ------------------------------------------------------------------ */

int pilha_iniciar(Pilha *p, long capacidade_inicial)
{
    if (capacidade_inicial < 16) {
        capacidade_inicial = 16;
    }
    p->topo = 0;
    p->capacidade = capacidade_inicial;
    p->dados = (long *) malloc((size_t) capacidade_inicial * sizeof(long));
    if (p->dados == NULL) {
        fprintf(stderr, "erro: falta de memoria ao criar a pilha\n");
        p->capacidade = 0;
        return -1;
    }
    return 0;
}

int pilha_empilhar(Pilha *p, long valor)
{
    long *novo;
    long nova_capacidade;

    if (p->topo == p->capacidade) {
        nova_capacidade = p->capacidade * 2;
        novo = (long *) realloc(p->dados, (size_t) nova_capacidade * sizeof(long));
        if (novo == NULL) {
            fprintf(stderr, "erro: falta de memoria ao aumentar a pilha\n");
            return -1;
        }
        p->dados = novo;
        p->capacidade = nova_capacidade;
    }
    p->dados[p->topo++] = valor;
    return 0;
}

void pilha_liberar(Pilha *p)
{
    free(p->dados);
    p->dados = NULL;
    p->topo = 0;
    p->capacidade = 0;
}

/* ------------------------------------------------------------------ */
/* Leitura de arquivo                                                  */
/*                                                                     */
/* Formato: "linhas colunas" seguido de linhas*colunas digitos 0/1.    */
/* Espacos e quebras de linha sao ignorados; '#' inicia um comentario  */
/* ate o fim da linha.                                                 */
/* ------------------------------------------------------------------ */

static int proximo_caractere_util(FILE *f)
{
    int c;

    for (;;) {
        c = fgetc(f);
        if (c == '#') {
            do {
                c = fgetc(f);
            } while (c != '\n' && c != EOF);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        return c;
    }
}

static int ler_inteiro_positivo(FILE *f, long *valor)
{
    int c;
    long v = 0;

    c = proximo_caractere_util(f);
    if (c < '0' || c > '9') {
        return -1;
    }
    while (c >= '0' && c <= '9') {
        if (v > (LONG_MAX - 9) / 10) {
            return -1;
        }
        v = v * 10 + (c - '0');
        c = fgetc(f);
    }
    if (c != EOF) {
        ungetc(c, f);
    }
    *valor = v;
    return 0;
}

static int dimensoes_validas(long linhas, long colunas)
{
    if (linhas <= 0 || colunas <= 0) {
        fprintf(stderr, "erro: dimensoes invalidas (%ld x %ld)\n", linhas, colunas);
        return 0;
    }
    /* Os rotulos sao int: o total de celulas precisa caber em um int. */
    if (linhas > (long) (INT_MAX - 1) / colunas) {
        fprintf(stderr, "erro: matriz grande demais (%ld x %ld)\n", linhas, colunas);
        return 0;
    }
    return 1;
}

int matriz_carregar_arquivo(const char *caminho, Matriz *m)
{
    FILE *f;
    long linhas, colunas, total, i;
    int c;

    m->linhas = 0;
    m->colunas = 0;
    m->celulas = NULL;

    f = fopen(caminho, "r");
    if (f == NULL) {
        fprintf(stderr, "erro: nao foi possivel abrir '%s': %s\n", caminho, strerror(errno));
        return -1;
    }
    if (ler_inteiro_positivo(f, &linhas) != 0 || ler_inteiro_positivo(f, &colunas) != 0) {
        fprintf(stderr, "erro: '%s' deve iniciar com 'linhas colunas'\n", caminho);
        fclose(f);
        return -1;
    }
    if (!dimensoes_validas(linhas, colunas)) {
        fclose(f);
        return -1;
    }
    total = linhas * colunas;
    m->celulas = (unsigned char *) alocar_zerado((size_t) total, 1, "matriz de entrada");
    if (m->celulas == NULL) {
        fclose(f);
        return -1;
    }
    for (i = 0; i < total; i++) {
        c = proximo_caractere_util(f);
        if (c != '0' && c != '1') {
            if (c == EOF) {
                fprintf(stderr, "erro: '%s' terminou apos %ld de %ld celulas\n",
                        caminho, i, total);
            } else {
                fprintf(stderr, "erro: caractere invalido '%c' na celula %ld de '%s'\n",
                        c, i, caminho);
            }
            free(m->celulas);
            m->celulas = NULL;
            fclose(f);
            return -1;
        }
        m->celulas[i] = (unsigned char) (c - '0');
    }
    if (proximo_caractere_util(f) != EOF) {
        fprintf(stderr, "aviso: dados excedentes ignorados em '%s'\n", caminho);
    }
    if (fclose(f) != 0) {
        perror("aviso: fclose");
    }
    m->linhas = linhas;
    m->colunas = colunas;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Geracao deterministica (xorshift de 32 bits)                        */
/*                                                                     */
/* A mesma semente produz a mesma matriz em qualquer execucao, entao   */
/* as versoes sequencial e paralela medem exatamente os mesmos dados.  */
/* ------------------------------------------------------------------ */

int matriz_gerar(long linhas, long colunas, double densidade,
                 unsigned long semente, Matriz *m)
{
    long total, i;
    unsigned long x;

    m->linhas = 0;
    m->colunas = 0;
    m->celulas = NULL;

    if (!dimensoes_validas(linhas, colunas)) {
        return -1;
    }
    if (densidade < 0.0 || densidade > 1.0) {
        fprintf(stderr, "erro: densidade deve estar entre 0 e 1\n");
        return -1;
    }
    total = linhas * colunas;
    m->celulas = (unsigned char *) alocar_zerado((size_t) total, 1, "matriz gerada");
    if (m->celulas == NULL) {
        return -1;
    }
    x = semente & 0xFFFFFFFFUL;
    if (x == 0) {
        x = 2463534242UL;
    }
    for (i = 0; i < total; i++) {
        x = (x ^ (x << 13)) & 0xFFFFFFFFUL;
        x = x ^ (x >> 17);
        x = (x ^ (x << 5)) & 0xFFFFFFFFUL;
        m->celulas[i] = (unsigned char) (((double) x / 4294967296.0) < densidade);
    }
    m->linhas = linhas;
    m->colunas = colunas;
    return 0;
}

/*
 * Interpreta a entrada a partir de argv[indice]:
 *   arquivo                      le a matriz de um arquivo texto
 *   -g linhas colunas dens sem   gera a matriz em memoria
 */
int matriz_ler_entrada(int argc, char **argv, int indice, Matriz *m)
{
    long linhas, colunas;
    double densidade;
    unsigned long semente;
    char *fim;

    if (indice >= argc) {
        fprintf(stderr, "erro: informe um arquivo ou -g linhas colunas densidade semente\n");
        return -1;
    }
    if (strcmp(argv[indice], "-g") != 0) {
        if (indice + 1 != argc) {
            fprintf(stderr, "erro: argumento inesperado '%s'\n", argv[indice + 1]);
            return -1;
        }
        return matriz_carregar_arquivo(argv[indice], m);
    }
    if (indice + 5 != argc) {
        fprintf(stderr, "erro: uso de -g: -g linhas colunas densidade semente\n");
        return -1;
    }
    errno = 0;
    linhas = strtol(argv[indice + 1], &fim, 10);
    if (errno != 0 || *fim != '\0') {
        fprintf(stderr, "erro: linhas invalidas '%s'\n", argv[indice + 1]);
        return -1;
    }
    colunas = strtol(argv[indice + 2], &fim, 10);
    if (errno != 0 || *fim != '\0') {
        fprintf(stderr, "erro: colunas invalidas '%s'\n", argv[indice + 2]);
        return -1;
    }
    densidade = strtod(argv[indice + 3], &fim);
    if (errno != 0 || *fim != '\0') {
        fprintf(stderr, "erro: densidade invalida '%s'\n", argv[indice + 3]);
        return -1;
    }
    semente = strtoul(argv[indice + 4], &fim, 10);
    if (errno != 0 || *fim != '\0') {
        fprintf(stderr, "erro: semente invalida '%s'\n", argv[indice + 4]);
        return -1;
    }
    return matriz_gerar(linhas, colunas, densidade, semente, m);
}

int matriz_salvar(FILE *saida, const Matriz *m)
{
    long r, c;

    if (fprintf(saida, "%ld %ld\n", m->linhas, m->colunas) < 0) {
        return -1;
    }
    for (r = 0; r < m->linhas; r++) {
        for (c = 0; c < m->colunas; c++) {
            if (putc(m->celulas[r * m->colunas + c] ? '1' : '0', saida) == EOF) {
                return -1;
            }
            if (c + 1 < m->colunas && putc(' ', saida) == EOF) {
                return -1;
            }
        }
        if (putc('\n', saida) == EOF) {
            return -1;
        }
    }
    return 0;
}

void matriz_liberar(Matriz *m)
{
    free(m->celulas);
    m->celulas = NULL;
    m->linhas = 0;
    m->colunas = 0;
}

/* Imprime a matriz de rotulos (usada no modo -v em matrizes pequenas). */
void matriz_imprimir_rotulos(const Matriz *m, const int *rotulos)
{
    long r, c;

    for (r = 0; r < m->linhas; r++) {
        for (c = 0; c < m->colunas; c++) {
            if (rotulos[r * m->colunas + c] == 0) {
                printf("  .");
            } else {
                printf("%3d", rotulos[r * m->colunas + c]);
            }
        }
        printf("\n");
    }
}
