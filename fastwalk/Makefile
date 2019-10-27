CC=clang
# Debug (-g creates the *.dSYM directory)
#
CFLAGS=-O3 -std=c11 -g -flto
#
# WARNINGS: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
CFLAGS+=-Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
CFLAGS+=-Wswitch-enum -Wcast-qual -Wpointer-arith
CFLAGS+=-Wstrict-overflow=5 -Wcast-align
#
# Performance:
#
# CFLAGS+=-Winline -Wdisabled-optimization
#
# Race detector:
#
# CFLAGS+=-fsanitize=thread
#
# Stack protector flags:
# CFLAGS+=-fstack-protector-strong -fstack-check -fstack-protector
#
OBJ=fastwalk.o
OUT=fastwalk
RM=rm -rf

.PHONY: all
all: build

debug: CFLAGS+=-DDEBUG -fsanitize=thread
debug: run clean

.PHONY: run
run: build
	./$(OUT)

%.o: %.c
	@$(CC) -c -o $@ $< $(CFLAGS)

# Note:
#   $(RM) *.o forces rebuild
#   $(RM) $(OUT).o forces rebuild of target
#
# @$(RM) $(OUT).o
build: $(OBJ)
	@$(CC) -o $(OUT) $^ $(CFLAGS)

.PHONY: clean
clean:
	$(RM) *.o *.dSYM $(OUT)
