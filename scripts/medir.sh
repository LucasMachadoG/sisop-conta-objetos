#!/bin/sh
# Medicoes de desempenho: versao sequencial x paralela sobre a mesma matriz.
#
# A matriz e gerada em memoria pela opcao -g (mesma semente = mesmos dados
# nas duas versoes). O tempo medido pelos programas exclui a geracao/leitura
# da matriz e inclui alocacao dos rotulos, criacao/join das threads,
# rotulacao, consolidacao e contagem final.
#
# As configuracoes sao INTERCALADAS: cada repeticao executa a sequencial e
# todas as quantidades de threads, em sequencia. Assim, variacoes de carga
# ou de frequencia da maquina afetam todas as configuracoes por igual.
# Antes da primeira repeticao, cada configuracao roda uma vez (aquecimento
# descartado).
#
# Variaveis (opcionais):
#   LINHAS COLUNAS DENSIDADE SEMENTE   matriz (padrao 8000 8000 0.30 2026)
#   REPETICOES                         repeticoes por configuracao (padrao 10)
#   THREADS                            lista de threads (padrao "1 2 4 8")
#   ARQUIVO                            mede um arquivo de matriz em vez de gerar com -g
#   CSV                                arquivo de saida (padrao results/medicoes.csv)
#   AMBIENTE                           descricao do ambiente (padrao results/ambiente.txt)
#   REP_INICIO REP_FIM                 executa so parte das repeticoes; com
#                                      REP_INICIO > 1 as linhas sao acrescentadas
#                                      ao CSV existente (util em sessoes com limite
#                                      de tempo). Ex.: REP_FIM=5 e depois REP_INICIO=6
#
# Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)

set -u
cd "$(dirname "$0")/.." || exit 1

LINHAS=${LINHAS:-8000}
COLUNAS=${COLUNAS:-8000}
DENSIDADE=${DENSIDADE:-0.30}
SEMENTE=${SEMENTE:-2026}
REPETICOES=${REPETICOES:-10}
THREADS=${THREADS:-"1 2 4 8"}
CSV=${CSV:-results/medicoes.csv}
AMBIENTE=${AMBIENTE:-results/ambiente.txt}
REP_INICIO=${REP_INICIO:-1}
REP_FIM=${REP_FIM:-$REPETICOES}
ARQUIVO=${ARQUIVO:-}
if [ -n "$ARQUIVO" ]; then
    NOME=$(basename "$ARQUIVO" .txt)
    GERAR="$ARQUIVO"
    dims=$(grep -v '^#' "$ARQUIVO" | head -n 1)
    LINHAS=$(echo "$dims" | awk '{print $1}')
    COLUNAS=$(echo "$dims" | awk '{print $2}')
    DENSIDADE=-
else
    NOME="aleatoria_${LINHAS}x${COLUNAS}_d${DENSIDADE}"
    GERAR="-g $LINHAS $COLUNAS $DENSIDADE $SEMENTE"
fi

mkdir -p results

campo() { grep "^$1" | sed "s/^$1[^:]*: *//"; }

# Executa uma medicao e acrescenta uma linha ao CSV.
#   $1 = numero de threads (0 = sequencial), $2 = repeticao
medir() {
    if [ "$1" -eq 0 ]; then
        # shellcheck disable=SC2086
        saida=$(./conta-objetos-sequencial $GERAR)
        versao=sequencial; trab=1; blocos=-
    else
        # shellcheck disable=SC2086
        saida=$(./conta-objetos-paralelo -t "$1" $GERAR)
        versao=paralela; trab=$1
        blocos=$(echo "$saida" | campo Blocos | awk '{print $1}')
    fi
    obj=$(echo "$saida" | campo Objetos)
    tempo=$(echo "$saida" | campo Tempo)
    if [ "$obj" = "$REFERENCIA" ]; then ok=true; else ok=false; fi
    echo "$NOME,$LINHAS,$COLUNAS,$DENSIDADE,$versao,$trab,$blocos,$2,$tempo,$obj,$ok" >> "$CSV"
}

if [ "$REP_INICIO" -le 1 ]; then
    {
        echo "data: $(date '+%Y-%m-%d %H:%M:%S')"
        echo "sistema: $(uname -srm)"
        if command -v sysctl >/dev/null 2>&1 && sysctl -n machdep.cpu.brand_string >/dev/null 2>&1; then
            echo "processador: $(sysctl -n machdep.cpu.brand_string)"
            echo "nucleos fisicos: $(sysctl -n hw.physicalcpu)"
            echo "processadores logicos: $(sysctl -n hw.logicalcpu)"
            echo "memoria (bytes): $(sysctl -n hw.memsize)"
        else
            cpu=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed 's/^ //')
            [ -z "$cpu" ] && cpu=$(lscpu 2>/dev/null | grep -m1 -E 'Model name|Vendor ID' | cut -d: -f2 | sed 's/^ *//')
            echo "processador: ${cpu:-desconhecido}"
            echo "processadores logicos: $(getconf _NPROCESSORS_ONLN)"
            echo "memoria: $(grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2 " kB"}')"
        fi
        echo "compilador: $(${CC:-cc} --version 2>&1 | head -n 1)"
        if [ -n "$ARQUIVO" ]; then
            echo "matriz: $ARQUIVO ($LINHAS x $COLUNAS)"
        else
            echo "matriz: $LINHAS x $COLUNAS, densidade $DENSIDADE, semente $SEMENTE"
        fi
        echo "repeticoes: $REPETICOES por configuracao, intercaladas (+1 aquecimento descartado)"
        echo "threads: $THREADS"
    } > "$AMBIENTE"
    cat "$AMBIENTE"
    echo "matriz,linhas,colunas,densidade,versao,trabalhadores,blocos,repeticao,tempo_ms,objetos,resultado_correto" > "$CSV"

    # Aquecimento (descartado) e resultado de referencia da versao sequencial
    # shellcheck disable=SC2086
    REFERENCIA=$(./conta-objetos-sequencial $GERAR | campo Objetos)
    for t in $THREADS; do
        # shellcheck disable=SC2086
        ./conta-objetos-paralelo -t "$t" $GERAR > /dev/null
    done
    echo "$REFERENCIA" > "$CSV.referencia"
else
    if [ ! -f "$CSV.referencia" ]; then
        echo "erro: execute antes a partir da repeticao 1" >&2
        exit 1
    fi
    REFERENCIA=$(cat "$CSV.referencia")
fi

i=$REP_INICIO
while [ "$i" -le "$REP_FIM" ]; do
    echo "repeticao $i de $REPETICOES"
    medir 0 "$i"
    for t in $THREADS; do
        medir "$t" "$i"
    done
    i=$((i + 1))
done

echo "Medicoes gravadas em $CSV (objetos de referencia: $REFERENCIA)"
