#!/bin/sh
# Testes funcionais: compara as versoes sequencial e paralela com o valor
# esperado de cada matriz, em varias combinacoes de threads e blocos.
#
# Saidas:
#   results/testes.log            registro completo
#   results/testes-resumo.csv     uma linha por (matriz, threads, blocos)
#
# Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)

set -u
cd "$(dirname "$0")/.." || exit 1

SEQ=./conta-objetos-sequencial
PAR=./conta-objetos-paralelo
LOG=results/testes.log
CSV=results/testes-resumo.csv
THREADS="1 2 3 4 8"
falhas=0
total=0

mkdir -p results
: > "$LOG"
echo "matriz,linhas,colunas,esperado,sequencial,threads,blocos,paralelo,situacao" > "$CSV"

objetos() { grep '^Objetos:' | awk '{print $2}'; }

registrar() {   # mensagem para tela e log
    echo "$1"
    echo "$1" >> "$LOG"
}

registrar "== Testes funcionais - $(date '+%Y-%m-%d %H:%M:%S') - $(uname -sm)"

for arq in tests/obrigatorios/*.txt tests/adicionais/*.txt; do
    nome=$(basename "$arq" .txt)
    esperado=$(grep '^# esperado:' "$arq" | awk '{print $3}')
    dims=$(grep -v '^#' "$arq" | head -n 1)
    L=$(echo "$dims" | awk '{print $1}')
    C=$(echo "$dims" | awk '{print $2}')
    seq=$($SEQ "$arq" | objetos)

    total=$((total + 1))
    if [ "$seq" = "$esperado" ]; then s=OK; else s=FALHOU; falhas=$((falhas + 1)); fi
    registrar "$nome ($L x $C): esperado=$esperado sequencial=$seq [$s]"
    echo "$nome,$L,$C,$esperado,$seq,1,-,-,$s" >> "$CSV"

    # Grades: padrao, 2x2, 3x3, faixas de linhas, faixas de colunas,
    # grade irregular e um bloco por celula (pior caso de fronteiras).
    falhas_arq=0
    for t in $THREADS; do
        for b in padrao 2x2 3x3 4x1 1x4 5x7 "${L}x${C}"; do
            if [ "$b" = padrao ]; then opt=""; else opt="-b $b"; fi
            # shellcheck disable=SC2086
            par=$($PAR -t "$t" $opt "$arq" | objetos)
            total=$((total + 1))
            if [ "$par" = "$esperado" ]; then s=OK; else s=FALHOU; falhas=$((falhas + 1)); fi
            echo "$nome,$L,$C,$esperado,$seq,$t,$b,$par,$s" >> "$CSV"
            if [ "$s" != OK ]; then
                falhas_arq=$((falhas_arq + 1))
                registrar "  FALHA: $nome -t $t -b $b -> $par (esperado $esperado)"
            fi
        done
    done
    if [ "$falhas_arq" -eq 0 ]; then
        registrar "  paralela: threads {$THREADS} x blocos {padrao 2x2 3x3 4x1 1x4 5x7 ${L}x${C}} -> todas = $esperado [OK]"
    else
        registrar "  paralela: $falhas_arq configuracao(oes) com resultado diferente [FALHOU]"
    fi
done

# Determinismo: a mesma configuracao repetida muitas vezes deve dar
# sempre o mesmo resultado, independente do escalonamento das threads.
registrar "== Determinismo (50 repeticoes por configuracao)"
for cfg in "tests/obrigatorios/exemplo5.txt|8|12x12" \
           "tests/obrigatorios/exemplo4.txt|4|3x3" \
           "tests/adicionais/a10-aleatoria-200x300.txt|8|40x40"; do
    arq=$(echo "$cfg" | cut -d'|' -f1)
    t=$(echo "$cfg" | cut -d'|' -f2)
    b=$(echo "$cfg" | cut -d'|' -f3)
    distintos=$(i=0; while [ $i -lt 50 ]; do $PAR -t "$t" -b "$b" "$arq" | objetos; i=$((i + 1)); done | sort -u)
    n=$(echo "$distintos" | wc -l | tr -d ' ')
    total=$((total + 1))
    if [ "$n" = 1 ]; then s=OK; else s=FALHOU; falhas=$((falhas + 1)); fi
    registrar "$(basename "$arq") -t $t -b $b: valores distintos em 50 execucoes = $(echo $distintos) [$s]"
done

# Comparacao aleatoria: matrizes geradas com sementes e densidades
# variadas, sequencial x paralela com configuracoes sorteadas.
registrar "== Comparacao aleatoria sequencial x paralela"
casos=0
for dens in 0.1 0.3 0.45 0.6 0.9; do
    semente=1
    while [ $semente -le 40 ]; do
        L=$(( (semente * 7) % 61 + 1 ))
        C=$(( (semente * 13) % 67 + 1 ))
        t=$(( semente % 6 + 2 ))
        bl=$(( semente % 9 + 1 ))
        bc=$(( (semente * 5) % 11 + 1 ))
        a=$($SEQ -g "$L" "$C" "$dens" "$semente" | objetos)
        p=$($PAR -t "$t" -b "${bl}x${bc}" -g "$L" "$C" "$dens" "$semente" | objetos)
        casos=$((casos + 1))
        total=$((total + 1))
        if [ "$a" != "$p" ]; then
            falhas=$((falhas + 1))
            registrar "  FALHA: -g $L $C $dens $semente -t $t -b ${bl}x${bc}: seq=$a par=$p"
        fi
        semente=$((semente + 1))
    done
done
registrar "$casos matrizes aleatorias (1x1 ate 61x67, densidade 0,1 a 0,9) comparadas"

registrar "== Resultado: $total verificacoes, $falhas falha(s)"
[ "$falhas" -eq 0 ]
