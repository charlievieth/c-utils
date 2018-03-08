# CC=gcc-7
#
# Debug (-g creates the *.dSYM directory)
#
CFLAGS=-std=c11 -g -O1
# CFLAGS=-O2 -std=gnu11 -march=native
#
# WARNINGS: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
CFLAGS+=-Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
CFLAGS+=-Wswitch-enum -Wcast-qual -Wpointer-arith
CFLAGS+=-Wstrict-overflow=5 -Wcast-align
#
# Performance:
#
CFLAGS+=-Winline -Wdisabled-optimization # -Wunsafe-loop-optimizations
LDFLAGS=-lyaml
#
# Stack protector flags:
# CFLAGS+=-fstack-protector-strong -fstack-check -fstack-protector
#
# DEPS=linecount.h
OBJ=main.o
OUT=ygrep
RM=rm -rvf

all: build

debug: CFLAGS+=-DDEBUG
debug: run

run: build
	./$(OUT)

%.o: %.c $(DEPS)
	@$(CC) -c -o $@ $< $(CFLAGS)

# Note:
#   $(RM) *.o forces rebuild
#   $(RM) $(OUT).o forces rebuild of target
#
build: $(OBJ)
	@$(CC) -o $(OUT) $^ $(CFLAGS) $(LDFLAGS)
	@$(RM) $(OUT).o

.PHONY: clean
clean:
	$(RM) *.o $(OUT)
	$(RM) *.dSYM $(OUT)
