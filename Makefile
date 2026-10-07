# Contagem paralela de objetos em uma matriz binaria
# Lucas Gaelzer Machado (23200069) e Stefan Chagas (23200106)

CC      ?= cc
CFLAGS  ?= -std=c89 -Wall -Wextra -pedantic -O2
LDLIBS  ?=
PTHREAD  = -pthread

SEQ  = conta-objetos-sequencial
PAR  = conta-objetos-paralelo
GER  = gera-matriz

.PHONY: all clean test bench graficos tsan asan

all: $(SEQ) $(PAR) $(GER)

$(SEQ): src/conta-objetos-sequencial.c src/matriz.c src/matriz.h
	$(CC) $(CFLAGS) -o $@ src/conta-objetos-sequencial.c src/matriz.c $(LDLIBS)

$(PAR): src/conta-objetos-paralelo.c src/matriz.c src/matriz.h
	$(CC) $(CFLAGS) $(PTHREAD) -o $@ src/conta-objetos-paralelo.c src/matriz.c $(LDLIBS)

$(GER): src/gera-matriz.c src/matriz.c src/matriz.h
	$(CC) $(CFLAGS) -o $@ src/gera-matriz.c src/matriz.c $(LDLIBS)

# Testes funcionais: obrigatorios, adicionais e comparacao aleatoria
test: all
	./scripts/testar.sh

# Medicoes de desempenho (gera results/medicoes.csv)
bench: all
	./scripts/medir.sh

# Graficos a partir de results/medicoes.csv (requer python3 + matplotlib)
graficos:
	python3 scripts/graficos.py results/medicoes.csv results

# Versao paralela com ThreadSanitizer (deteccao de condicoes de corrida)
tsan:
	$(CC) -std=c89 -Wall -Wextra -pedantic -g -O1 -fsanitize=thread $(PTHREAD) \
	    -o $(PAR)-tsan src/conta-objetos-paralelo.c src/matriz.c

# Versoes com AddressSanitizer/UBSan (memoria e comportamento indefinido)
asan:
	$(CC) -std=c89 -Wall -Wextra -pedantic -g -O1 -fsanitize=address,undefined \
	    -o $(SEQ)-asan src/conta-objetos-sequencial.c src/matriz.c
	$(CC) -std=c89 -Wall -Wextra -pedantic -g -O1 -fsanitize=address,undefined $(PTHREAD) \
	    -o $(PAR)-asan src/conta-objetos-paralelo.c src/matriz.c

clean:
	rm -f $(SEQ) $(PAR) $(GER) $(PAR)-tsan $(SEQ)-asan $(PAR)-asan
