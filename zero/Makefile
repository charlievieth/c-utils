# TODO: build null as well

# Debug (-g creates the *.dSYM directory)
#
CFLAGS=-O3 -std=gnu11 -march=native -mtune=native -flto
#
# WARNINGS: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
CFLAGS+=-Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
CFLAGS+=-Wswitch-enum -Wcast-qual -Wpointer-arith
CFLAGS+=-Wstrict-overflow=5 -Wcast-align
#
OBJ=zero.o
OUT=zero
RM=rm -rvf

all: build

debug: CFLAGS+=-DDEBUG
debug: run

run: build
	./$(OUT)

%.o: %.c
	@$(CC) -c -o $@ $< $(CFLAGS)

# Note:
#   $(RM) *.o forces rebuild
#   $(RM) $(OUT).o forces rebuild of target
#
build: $(OBJ)
	@$(CC) -o $(OUT) $^ $(CFLAGS)
	@$(RM) $(OUT).o

.PHONY: clean
clean:
	$(RM) *.o $(OUT)
	$(RM) *.dSYM $(OUT)
