/*
 * conta-objetos-paralelo.c - Versao paralela com POSIX Threads.
 *
 * Trabalho de Sistemas Operacionais - PUCRS 2026/II
 * Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)
 *
 * ESTRATEGIA
 *
 * A matriz e dividida em uma grade de blocos retangulares. Os blocos
 * ficam em uma fila dinamica (um contador protegido por mutex) e cada
 * thread retira o proximo bloco livre ate a fila esvaziar. O trabalho
 * acontece em tres fases:
 *
 *   Fase 1 (paralela)   Cada thread rotula os objetos de seus blocos com
 *                       flood fill restrito ao bloco. Os rotulos sao
 *                       locais: 1, 2, 3... dentro de cada bloco.
 *
 *   Intervalo           A thread principal soma o numero de componentes
 *   (sequencial)        de cada bloco (soma de prefixos). Assim o rotulo
 *                       global de um componente e
 *                         deslocamento[bloco] + rotulo_local - 1,
 *                       unico na matriz inteira, e cria a estrutura
 *                       union-find com um conjunto por componente local.
 *
 *   Fase 2 (paralela)   Cada thread examina a borda direita e a borda
 *                       inferior de seus blocos. Para cada celula 1 da
 *                       borda, testa os 3 vizinhos do outro lado
 *                       (reto e duas diagonais). Pares de componentes
 *                       que se tocam sao acumulados em um buffer local
 *                       e depois unidos na union-find compartilhada,
 *                       dentro de uma regiao critica (mutex_uniao).
 *
 *   Contagem final      A thread principal conta os representantes
 *   (sequencial)        (raizes) da union-find: cada raiz e um objeto.
 *
 * SINCRONIZACAO
 *
 *   - Matriz de entrada: somente leitura em todas as fases.
 *   - Rotulos: na fase 1 cada bloco e escrito por uma unica thread
 *     (a que o retirou da fila); na fase 2 sao somente lidos.
 *   - Fila de blocos: proximo_bloco protegido por mutex_fila.
 *   - Union-find: lida e escrita somente com mutex_uniao.
 *   - pthread_join entre as fases funciona como barreira.
 *   - Nenhuma thread segura dois mutexes ao mesmo tempo, logo nao ha
 *     ciclo de espera e nao ha deadlock.
 *
 * Uso:
 *   conta-objetos-paralelo [-t threads] [-b LxC] [-v] arquivo
 *   conta-objetos-paralelo [-t threads] [-b LxC] [-v] -g linhas colunas densidade semente
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "matriz.h"

#define MAX_THREADS      256
#define LIMITE_PARES     4096   /* pares acumulados antes de entrar na regiao critica */

/* ------------------------------------------------------------------ */
/* Estruturas                                                          */
/* ------------------------------------------------------------------ */

/* Estado compartilhado por todas as threads. */
typedef struct {
    const Matriz *m;
    int *rotulos;                 /* rotulo local de cada celula (0 = fundo) */

    /* Grade de blocos */
    long blocos_l, blocos_c, total_blocos;
    long *inicio_linha;           /* blocos_l + 1 posicoes */
    long *inicio_coluna;          /* blocos_c + 1 posicoes */
    long *bloco_da_linha;         /* linha  -> indice do bloco na vertical */
    long *bloco_da_coluna;        /* coluna -> indice do bloco na horizontal */

    /* Resultados locais e consolidacao */
    long *componentes_bloco;      /* fase 1: componentes encontrados em cada bloco */
    long *deslocamento;           /* primeiro rotulo global de cada bloco */
    int  *pai;                    /* union-find sobre os rotulos globais */
    long total_componentes;       /* soma dos componentes locais */

    /* Fila dinamica de blocos */
    long proximo_bloco;
    int  abortar;                 /* sinaliza erro para as demais threads */
    pthread_mutex_t mutex_fila;

    /* Regiao critica da consolidacao */
    pthread_mutex_t mutex_uniao;

    int verboso;
    int imprimir_pares;           /* -v em matriz pequena: lista as equivalencias */

    /* Duracao de cada etapa em ms (exibida no modo -v) */
    double ms_preparo, ms_fase1, ms_prefixos, ms_fase2, ms_contagem;
} Contexto;

/* Estado privado de cada thread trabalhadora. */
typedef struct {
    Contexto *ctx;
    int  id;
    int  falhou;
    long blocos_fase1;
    long blocos_fase2;
    long pares_encontrados;
    long unioes_efetivas;
    int  *pares;                  /* buffer local de pares (a, b) */
    long qtd_pares;
} Trabalhador;

/* ------------------------------------------------------------------ */
/* Fila dinamica de blocos                                             */
/* ------------------------------------------------------------------ */

static void sinalizar_erro(Contexto *ctx)
{
    if (pthread_mutex_lock(&ctx->mutex_fila) == 0) {
        ctx->abortar = 1;
        pthread_mutex_unlock(&ctx->mutex_fila);
    }
}

/* Retira o proximo bloco da fila. Retorna -1 quando nao ha mais blocos. */
static long retirar_bloco(Contexto *ctx, Trabalhador *t)
{
    long bloco;

    if (pthread_mutex_lock(&ctx->mutex_fila) != 0) {
        fprintf(stderr, "erro: thread %d: pthread_mutex_lock (fila)\n", t->id);
        t->falhou = 1;
        return -1;
    }
    if (ctx->abortar || ctx->proximo_bloco >= ctx->total_blocos) {
        bloco = -1;
    } else {
        bloco = ctx->proximo_bloco++;
    }
    if (pthread_mutex_unlock(&ctx->mutex_fila) != 0) {
        fprintf(stderr, "erro: thread %d: pthread_mutex_unlock (fila)\n", t->id);
        t->falhou = 1;
        return -1;
    }
    return bloco;
}

/* ------------------------------------------------------------------ */
/* Fase 1: rotulacao local de cada bloco                               */
/* ------------------------------------------------------------------ */

static int rotular_bloco(Contexto *ctx, long bloco, Pilha *pilha)
{
    const long C = ctx->m->colunas;
    const unsigned char *cel = ctx->m->celulas;
    int *rot = ctx->rotulos;
    long bi = bloco / ctx->blocos_c;
    long bj = bloco % ctx->blocos_c;
    long r0 = ctx->inicio_linha[bi], r1 = ctx->inicio_linha[bi + 1];
    long c0 = ctx->inicio_coluna[bj], c1 = ctx->inicio_coluna[bj + 1];
    long r, c, idx, atual, ar, ac, nr, nc, viz;
    int k;
    int rotulo = 0;

    for (r = r0; r < r1; r++) {
        for (c = c0; c < c1; c++) {
            idx = r * C + c;
            if (cel[idx] == 0 || rot[idx] != 0) {
                continue;
            }
            rotulo++;
            rot[idx] = rotulo;
            pilha->topo = 0;
            if (pilha_empilhar(pilha, idx) != 0) {
                return -1;
            }
            while (pilha->topo > 0) {
                atual = pilha->dados[--pilha->topo];
                ar = atual / C;
                ac = atual % C;
                for (k = 0; k < 8; k++) {
                    nr = ar + VIZINHO_DL[k];
                    nc = ac + VIZINHO_DC[k];
                    /* O flood fill nao sai do bloco: as conexoes que
                     * atravessam a borda sao tratadas na fase 2. */
                    if (nr < r0 || nr >= r1 || nc < c0 || nc >= c1) {
                        continue;
                    }
                    viz = nr * C + nc;
                    if (cel[viz] != 0 && rot[viz] == 0) {
                        rot[viz] = rotulo;
                        if (pilha_empilhar(pilha, viz) != 0) {
                            return -1;
                        }
                    }
                }
            }
        }
    }
    /* Cada posicao e escrita apenas pela thread dona do bloco. */
    ctx->componentes_bloco[bloco] = rotulo;
    return 0;
}

static void *trabalhador_fase1(void *arg)
{
    Trabalhador *t = (Trabalhador *) arg;
    Contexto *ctx = t->ctx;
    Pilha pilha;
    long bloco;

    if (pilha_iniciar(&pilha, 1024) != 0) {
        t->falhou = 1;
        sinalizar_erro(ctx);
        return NULL;
    }
    while ((bloco = retirar_bloco(ctx, t)) >= 0) {
        if (rotular_bloco(ctx, bloco, &pilha) != 0) {
            t->falhou = 1;
            sinalizar_erro(ctx);
            break;
        }
        t->blocos_fase1++;
    }
    pilha_liberar(&pilha);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Fase 2: fronteiras e union-find                                     */
/* ------------------------------------------------------------------ */

/* Rotulo global (0, 1, 2, ...) do componente que contem a celula (r, c). */
static int rotulo_global(const Contexto *ctx, long r, long c)
{
    long bloco = ctx->bloco_da_linha[r] * ctx->blocos_c + ctx->bloco_da_coluna[c];
    return (int) (ctx->deslocamento[bloco] + ctx->rotulos[r * ctx->m->colunas + c] - 1);
}

/* Busca do representante com compressao de caminho (path halving).
 * Chamada somente com mutex_uniao travado. */
static int encontrar(int *pai, int x)
{
    while (pai[x] != x) {
        pai[x] = pai[pai[x]];
        x = pai[x];
    }
    return x;
}

/* Une dois conjuntos. A menor raiz vira o representante, o que torna o
 * resultado independente da ordem das unioes. Retorna 1 se uniu. */
static int unir(int *pai, int a, int b)
{
    int ra = encontrar(pai, a);
    int rb = encontrar(pai, b);

    if (ra == rb) {
        return 0;
    }
    if (ra < rb) {
        pai[rb] = ra;
    } else {
        pai[ra] = rb;
    }
    return 1;
}

/* Aplica os pares acumulados na union-find compartilhada (regiao critica). */
static int descarregar_pares(Trabalhador *t)
{
    Contexto *ctx = t->ctx;
    long i;

    if (t->qtd_pares == 0) {
        return 0;
    }
    if (pthread_mutex_lock(&ctx->mutex_uniao) != 0) {
        fprintf(stderr, "erro: thread %d: pthread_mutex_lock (uniao)\n", t->id);
        return -1;
    }
    for (i = 0; i < t->qtd_pares; i++) {
        if (unir(ctx->pai, t->pares[2 * i], t->pares[2 * i + 1])) {
            t->unioes_efetivas++;
        }
        if (ctx->imprimir_pares) {
            printf("  [thread %d] equivalencia G%d ~ G%d\n",
                   t->id, t->pares[2 * i], t->pares[2 * i + 1]);
        }
    }
    if (pthread_mutex_unlock(&ctx->mutex_uniao) != 0) {
        fprintf(stderr, "erro: thread %d: pthread_mutex_unlock (uniao)\n", t->id);
        return -1;
    }
    t->qtd_pares = 0;
    return 0;
}

static int registrar_par(Trabalhador *t, int a, int b)
{
    /* Descarta repeticao imediata (comum em objetos que encostam na borda
     * em varias celulas seguidas). */
    if (t->qtd_pares > 0 && t->pares[2 * (t->qtd_pares - 1)] == a
                         && t->pares[2 * (t->qtd_pares - 1) + 1] == b) {
        return 0;
    }
    t->pares[2 * t->qtd_pares] = a;
    t->pares[2 * t->qtd_pares + 1] = b;
    t->qtd_pares++;
    t->pares_encontrados++;
    if (t->qtd_pares == LIMITE_PARES) {
        return descarregar_pares(t);
    }
    return 0;
}

/*
 * Examina a borda direita e a borda inferior do bloco. Cada fronteira
 * entre dois blocos vizinhos e examinada uma unica vez (pelo bloco da
 * esquerda ou de cima). Os testes diagonais cobrem tambem o ponto onde
 * quatro blocos se encontram.
 */
static int examinar_fronteiras(Trabalhador *t, long bloco)
{
    Contexto *ctx = t->ctx;
    const long L = ctx->m->linhas;
    const long C = ctx->m->colunas;
    const unsigned char *cel = ctx->m->celulas;
    long bi = bloco / ctx->blocos_c;
    long bj = bloco % ctx->blocos_c;
    long r0 = ctx->inicio_linha[bi], r1 = ctx->inicio_linha[bi + 1];
    long c0 = ctx->inicio_coluna[bj], c1 = ctx->inicio_coluna[bj + 1];
    long r, c, d, nr, nc;
    int a;

    /* Fronteira vertical: coluna c1-1 (este bloco) x coluna c1 (vizinho). */
    if (c1 < C) {
        for (r = r0; r < r1; r++) {
            if (cel[r * C + c1 - 1] == 0) {
                continue;
            }
            a = rotulo_global(ctx, r, c1 - 1);
            for (d = -1; d <= 1; d++) {            /* diagonal, reto, diagonal */
                nr = r + d;
                if (nr < 0 || nr >= L || cel[nr * C + c1] == 0) {
                    continue;
                }
                if (registrar_par(t, a, rotulo_global(ctx, nr, c1)) != 0) {
                    return -1;
                }
            }
        }
    }
    /* Fronteira horizontal: linha r1-1 (este bloco) x linha r1 (vizinho). */
    if (r1 < L) {
        for (c = c0; c < c1; c++) {
            if (cel[(r1 - 1) * C + c] == 0) {
                continue;
            }
            a = rotulo_global(ctx, r1 - 1, c);
            for (d = -1; d <= 1; d++) {
                nc = c + d;
                if (nc < 0 || nc >= C || cel[r1 * C + nc] == 0) {
                    continue;
                }
                if (registrar_par(t, a, rotulo_global(ctx, r1, nc)) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

static void *trabalhador_fase2(void *arg)
{
    Trabalhador *t = (Trabalhador *) arg;
    Contexto *ctx = t->ctx;
    long bloco;

    t->qtd_pares = 0;
    t->pares = (int *) malloc(2 * LIMITE_PARES * sizeof(int));
    if (t->pares == NULL) {
        fprintf(stderr, "erro: thread %d: falta de memoria para pares\n", t->id);
        t->falhou = 1;
        sinalizar_erro(ctx);
        return NULL;
    }
    while ((bloco = retirar_bloco(ctx, t)) >= 0) {
        if (examinar_fronteiras(t, bloco) != 0) {
            t->falhou = 1;
            sinalizar_erro(ctx);
            break;
        }
        t->blocos_fase2++;
    }
    if (!t->falhou && descarregar_pares(t) != 0) {
        t->falhou = 1;
        sinalizar_erro(ctx);
    }
    free(t->pares);
    t->pares = NULL;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Criacao e espera das threads                                        */
/* ------------------------------------------------------------------ */

/* Cria n threads executando 'funcao' e espera todas terminarem.
 * O pthread_join de todas funciona como barreira entre as fases. */
static int executar_fase(Contexto *ctx, Trabalhador *t, int n, void *(*funcao)(void *))
{
    pthread_t ids[MAX_THREADS];
    int criadas = 0;
    int i, ret, status = 0;

    ctx->proximo_bloco = 0;
    for (i = 0; i < n; i++) {
        ret = pthread_create(&ids[i], NULL, funcao, &t[i]);
        if (ret != 0) {
            fprintf(stderr, "erro: pthread_create (thread %d): %s\n", i, strerror(ret));
            sinalizar_erro(ctx);
            status = -1;
            break;
        }
        criadas++;
    }
    /* Mesmo em caso de erro, espera as threads ja criadas. */
    for (i = 0; i < criadas; i++) {
        ret = pthread_join(ids[i], NULL);
        if (ret != 0) {
            fprintf(stderr, "erro: pthread_join (thread %d): %s\n", i, strerror(ret));
            status = -1;
        }
        if (t[i].falhou) {
            status = -1;
        }
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Grade de blocos                                                     */
/* ------------------------------------------------------------------ */

/* Divide 'tamanho' em 'partes' faixas o mais iguais possivel: as sobras
 * ficam distribuidas (a faixa i comeca em i*tamanho/partes). */
static int montar_faixas(long tamanho, long partes, long **inicio, long **dono)
{
    long i, p;

    *inicio = (long *) alocar_zerado((size_t) (partes + 1), sizeof(long), "limites dos blocos");
    *dono = (long *) alocar_zerado((size_t) tamanho, sizeof(long), "mapa de blocos");
    if (*inicio == NULL || *dono == NULL) {
        return -1;
    }
    for (p = 0; p <= partes; p++) {
        (*inicio)[p] = (p * tamanho) / partes;
    }
    for (p = 0; p < partes; p++) {
        for (i = (*inicio)[p]; i < (*inicio)[p + 1]; i++) {
            (*dono)[i] = p;
        }
    }
    return 0;
}

static void liberar_contexto(Contexto *ctx)
{
    free(ctx->rotulos);
    free(ctx->inicio_linha);
    free(ctx->inicio_coluna);
    free(ctx->bloco_da_linha);
    free(ctx->bloco_da_coluna);
    free(ctx->componentes_bloco);
    free(ctx->deslocamento);
    free(ctx->pai);
}

/* ------------------------------------------------------------------ */
/* Saida detalhada (-v)                                                */
/* ------------------------------------------------------------------ */

static void imprimir_rotulos_globais(const Contexto *ctx, int depois_da_uniao)
{
    const Matriz *m = ctx->m;
    long r, c, b;
    int g;

    for (r = 0; r < m->linhas; r++) {
        if (r > 0 && ctx->bloco_da_linha[r] != ctx->bloco_da_linha[r - 1]) {
            for (c = 0; c < m->colunas; c++) {
                printf("%s----", (c > 0 && ctx->bloco_da_coluna[c] != ctx->bloco_da_coluna[c - 1]) ? "-+" : "");
            }
            printf("\n");
        }
        for (c = 0; c < m->colunas; c++) {
            if (c > 0 && ctx->bloco_da_coluna[c] != ctx->bloco_da_coluna[c - 1]) {
                printf(" |");
            }
            if (ctx->rotulos[r * m->colunas + c] == 0) {
                printf("   .");
            } else {
                g = rotulo_global(ctx, r, c);
                if (depois_da_uniao) {
                    g = encontrar(ctx->pai, g);
                }
                printf(" G%-2d", g);
            }
        }
        printf("\n");
    }
    if (!depois_da_uniao) {
        for (b = 0; b < ctx->total_blocos; b++) {
            printf("  bloco (%ld,%ld): linhas %ld-%ld, colunas %ld-%ld, %ld componente(s) locais",
                   b / ctx->blocos_c, b % ctx->blocos_c,
                   ctx->inicio_linha[b / ctx->blocos_c], ctx->inicio_linha[b / ctx->blocos_c + 1] - 1,
                   ctx->inicio_coluna[b % ctx->blocos_c], ctx->inicio_coluna[b % ctx->blocos_c + 1] - 1,
                   ctx->componentes_bloco[b]);
            if (ctx->componentes_bloco[b] > 0) {
                printf(" -> G%ld..G%ld", ctx->deslocamento[b],
                       ctx->deslocamento[b] + ctx->componentes_bloco[b] - 1);
            }
            printf("\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Contagem paralela                                                   */
/* ------------------------------------------------------------------ */

static long contar_objetos_paralelo(Contexto *ctx, Trabalhador *t, int n_threads)
{
    const Matriz *m = ctx->m;
    long b, i, objetos;
    int pequena = (m->linhas * m->colunas <= 4096);
    double t0, t1;

    t0 = tempo_atual_ms();

    /* Grade de blocos e mapas linha/coluna -> bloco (sequencial, O(L + C)). */
    ctx->total_blocos = ctx->blocos_l * ctx->blocos_c;
    if (montar_faixas(m->linhas, ctx->blocos_l, &ctx->inicio_linha, &ctx->bloco_da_linha) != 0 ||
        montar_faixas(m->colunas, ctx->blocos_c, &ctx->inicio_coluna, &ctx->bloco_da_coluna) != 0) {
        return -1;
    }
    ctx->rotulos = (int *) alocar_zerado((size_t) (m->linhas * m->colunas), sizeof(int), "rotulos");
    ctx->componentes_bloco = (long *) alocar_zerado((size_t) ctx->total_blocos, sizeof(long), "componentes por bloco");
    ctx->deslocamento = (long *) alocar_zerado((size_t) ctx->total_blocos, sizeof(long), "deslocamentos");
    if (ctx->rotulos == NULL || ctx->componentes_bloco == NULL || ctx->deslocamento == NULL) {
        return -1;
    }

    t1 = tempo_atual_ms();
    ctx->ms_preparo = t1 - t0;

    /* Fase 1: rotulacao local em paralelo. */
    t0 = t1;
    if (executar_fase(ctx, t, n_threads, trabalhador_fase1) != 0) {
        return -1;
    }
    t1 = tempo_atual_ms();
    ctx->ms_fase1 = t1 - t0;
    t0 = t1;

    /* Soma de prefixos: rotulos globais unicos por bloco. */
    ctx->total_componentes = 0;
    for (b = 0; b < ctx->total_blocos; b++) {
        ctx->deslocamento[b] = ctx->total_componentes;
        ctx->total_componentes += ctx->componentes_bloco[b];
    }
    ctx->pai = (int *) alocar_zerado((size_t) ctx->total_componentes, sizeof(int), "union-find");
    if (ctx->pai == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->total_componentes; i++) {
        ctx->pai[i] = (int) i;
    }
    t1 = tempo_atual_ms();
    ctx->ms_prefixos = t1 - t0;

    if (ctx->verboso) {
        if (pequena) {
            printf("Rotulos globais antes da consolidacao (G = deslocamento + rotulo local - 1):\n");
            imprimir_rotulos_globais(ctx, 0);
        }
        printf("Componentes locais (soma simples): %ld\n", ctx->total_componentes);
        if (pequena) {
            ctx->imprimir_pares = 1;
            printf("Equivalencias registradas na fase 2:\n");
        }
    }

    /* Fase 2: fronteiras e uniao em paralelo. */
    t0 = tempo_atual_ms();
    if (executar_fase(ctx, t, n_threads, trabalhador_fase2) != 0) {
        return -1;
    }
    t1 = tempo_atual_ms();
    ctx->ms_fase2 = t1 - t0;
    t0 = t1;

    /* Contagem final: cada raiz da union-find representa um objeto. */
    objetos = 0;
    for (i = 0; i < ctx->total_componentes; i++) {
        if (ctx->pai[i] == (int) i) {
            objetos++;
        }
    }
    ctx->ms_contagem = tempo_atual_ms() - t0;

    if (ctx->verboso && pequena) {
        printf("Rotulos depois da consolidacao (representante de cada componente):\n");
        imprimir_rotulos_globais(ctx, 1);
    }
    return objetos;
}

/* ------------------------------------------------------------------ */
/* Programa principal                                                  */
/* ------------------------------------------------------------------ */

static void uso(const char *programa)
{
    fprintf(stderr,
            "uso: %s [-t threads] [-b LxC] [-v] arquivo\n"
            "     %s [-t threads] [-b LxC] [-v] -g linhas colunas densidade semente\n"
            "  -t  quantidade de threads (padrao 2, maximo %d)\n"
            "  -b  grade de blocos, ex.: 2x2, 3x3 (padrao: 2T x 2T)\n"
            "  -v  mostra blocos, rotulos, equivalencias e carga por thread\n",
            programa, programa, MAX_THREADS);
}

int main(int argc, char **argv)
{
    Matriz m;
    Contexto ctx;
    Trabalhador trabalhadores[MAX_THREADS];
    int n_threads = 2;
    long blocos_l = 0, blocos_c = 0;
    int verboso = 0;
    int indice = 1;
    int i, ret;
    long objetos;
    char extra;
    double t0, t1;

    /* Opcoes */
    while (indice < argc && argv[indice][0] == '-' && strcmp(argv[indice], "-g") != 0) {
        if (strcmp(argv[indice], "-t") == 0 && indice + 1 < argc) {
            if (sscanf(argv[indice + 1], "%d%c", &n_threads, &extra) != 1 ||
                n_threads < 1 || n_threads > MAX_THREADS) {
                fprintf(stderr, "erro: quantidade de threads invalida '%s' (1 a %d)\n",
                        argv[indice + 1], MAX_THREADS);
                return EXIT_FAILURE;
            }
            indice += 2;
        } else if (strcmp(argv[indice], "-b") == 0 && indice + 1 < argc) {
            if (sscanf(argv[indice + 1], "%ldx%ld%c", &blocos_l, &blocos_c, &extra) != 2 ||
                blocos_l < 1 || blocos_c < 1) {
                fprintf(stderr, "erro: grade de blocos invalida '%s' (ex.: 2x2)\n", argv[indice + 1]);
                return EXIT_FAILURE;
            }
            indice += 2;
        } else if (strcmp(argv[indice], "-v") == 0) {
            verboso = 1;
            indice++;
        } else {
            uso(argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (matriz_ler_entrada(argc, argv, indice, &m) != 0) {
        uso(argv[0]);
        return EXIT_FAILURE;
    }

    /* Grade padrao: 2T x 2T blocos (4T^2 tarefas para a fila dinamica),
     * limitada pelas dimensoes da matriz. */
    if (blocos_l == 0) {
        blocos_l = 2L * n_threads;
        blocos_c = 2L * n_threads;
    }
    if (blocos_l > m.linhas) {
        blocos_l = m.linhas;
    }
    if (blocos_c > m.colunas) {
        blocos_c = m.colunas;
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(trabalhadores, 0, sizeof(trabalhadores));
    ctx.m = &m;
    ctx.blocos_l = blocos_l;
    ctx.blocos_c = blocos_c;
    ctx.verboso = verboso;
    for (i = 0; i < n_threads; i++) {
        trabalhadores[i].ctx = &ctx;
        trabalhadores[i].id = i;
    }

    ret = pthread_mutex_init(&ctx.mutex_fila, NULL);
    if (ret != 0) {
        fprintf(stderr, "erro: pthread_mutex_init (fila): %s\n", strerror(ret));
        matriz_liberar(&m);
        return EXIT_FAILURE;
    }
    ret = pthread_mutex_init(&ctx.mutex_uniao, NULL);
    if (ret != 0) {
        fprintf(stderr, "erro: pthread_mutex_init (uniao): %s\n", strerror(ret));
        pthread_mutex_destroy(&ctx.mutex_fila);
        matriz_liberar(&m);
        return EXIT_FAILURE;
    }

    printf("Versao: paralela (pthreads)\n");
    printf("Matriz: %ld x %ld\n", m.linhas, m.colunas);
    printf("Threads: %d\n", n_threads);
    printf("Blocos: %ldx%ld (%ld)\n", blocos_l, blocos_c, blocos_l * blocos_c);

    /* Trecho medido: alocacoes, fase 1, prefixos, fase 2 e contagem final. */
    t0 = tempo_atual_ms();
    objetos = contar_objetos_paralelo(&ctx, trabalhadores, n_threads);
    t1 = tempo_atual_ms();

    if (objetos >= 0 && verboso) {
        printf("Tempo por etapa (ms): preparo %.3f | fase 1 (paralela) %.3f | prefixos e union-find %.3f"
               " | fase 2 (paralela) %.3f | contagem %.3f\n",
               ctx.ms_preparo, ctx.ms_fase1, ctx.ms_prefixos, ctx.ms_fase2, ctx.ms_contagem);
        printf("Carga por thread:\n");
        for (i = 0; i < n_threads; i++) {
            printf("  thread %d: %ld bloco(s) na fase 1, %ld na fase 2, %ld par(es), %ld uniao(oes)\n",
                   i, trabalhadores[i].blocos_fase1, trabalhadores[i].blocos_fase2,
                   trabalhadores[i].pares_encontrados, trabalhadores[i].unioes_efetivas);
        }
    }
    if (objetos >= 0) {
        printf("Objetos: %ld\n", objetos);
        printf("Tempo (ms): %.3f\n", t1 - t0);
    }

    ret = pthread_mutex_destroy(&ctx.mutex_fila);
    if (ret != 0) {
        fprintf(stderr, "aviso: pthread_mutex_destroy (fila): %s\n", strerror(ret));
    }
    ret = pthread_mutex_destroy(&ctx.mutex_uniao);
    if (ret != 0) {
        fprintf(stderr, "aviso: pthread_mutex_destroy (uniao): %s\n", strerror(ret));
    }
    liberar_contexto(&ctx);
    matriz_liberar(&m);
    return objetos >= 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
