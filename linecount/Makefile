# CC=gcc-7
#
# Debug (-g creates the *.dSYM directory)
#
CFLAGS=-O2 -std=gnu11 -g
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
CFLAGS+=-Winline -Wunsafe-loop-optimizations -Wdisabled-optimization
#
# Stack protector flags:
# CFLAGS+=-fstack-protector-strong -fstack-check -fstack-protector
#
DEPS=linecount.h slice.h opts.h
OBJ=linecount.o slice.o opts.o
OUT=linecount
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
	@$(CC) -o $(OUT) $^ $(CFLAGS)
	@$(RM) $(OUT).o

.PHONY: clean
clean:
	$(RM) *.o $(OUT)
	$(RM) *.dSYM $(OUT)
