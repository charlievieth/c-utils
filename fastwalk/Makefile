CC=gcc
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
OUT=fastwalk
RM=rm -rfv

.PHONY: all
all: build

.PHONY: race
race: CFLAGS+=-fsanitize=thread
race: run

# build the binary (we do this so that we generate debug symbols)
fastwalk:
	@$(CC) $(CFLAGS) -o $(OUT) fastwalk.c

# Note:
#   $(RM) *.o forces rebuild
#   $(RM) $(OUT).o forces rebuild of target
#
# @$(RM) $(OUT).o
.PHONY: build
build: fastwalk

.PHONY: debug
debug: CFLAGS+=-DDEBUG -fsanitize=thread
debug: run clean

.PHONY: run
run: build
	./$(OUT)

.PHONY: clean
clean:
	$(RM) *.o *.dSYM $(OUT)
