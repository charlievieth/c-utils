CFLAGS=-std=gnu11 -O2 -g
#
# WARNINGS: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
CFLAGS+=-Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
CFLAGS+=-Wswitch-enum -Wcast-qual -Wpointer-arith
CFLAGS+=-Wstrict-overflow=5 -Wcast-align
#
# Uncomment to: BRING THE NOISE
# CFLAGS+=-Weverything
#
# Performance warnings:
#
CFLAGS+=-Winline -Wdisabled-optimization # -Wunsafe-loop-optimizations
#
# Stack protector flags:
# CFLAGS+=-fstack-protector-strong -fstack-check -fstack-protector
#
OBJ=main.o
OUT=list-procs
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
	@$(CC) -o $(OUT) $^ $(CFLAGS) $(LDFLAGS)
	@$(RM) $(OUT).o

.PHONY: clean
clean:
	$(RM) *.o $(OUT)
	$(RM) *.dSYM $(OUT)
