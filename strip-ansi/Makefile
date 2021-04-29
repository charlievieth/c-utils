# CC=fake-gcc
# Debug (-g creates the *.dSYM directory)
#
CFLAGS=-O3 -march=native -mtune=native -flto -std=c11 -g
#
# WARNINGS: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
CFLAGS+=-Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
CFLAGS+=-Wswitch-enum -Wcast-qual -Wpointer-arith
CFLAGS+=-Wstrict-overflow=5 -Wcast-align
CFLAGS+=-Wno-gnu-zero-variadic-macro-arguments
#
# Performance:
#
# CFLAGS+=-Winline -Wdisabled-optimization
#
# Memory sanitizer:
#
# CFLAGS+=-fsanitize=address -g
#
# UB sanitizer:
#
# CFLAGS+=-fsanitize=undefined
#
# Race detector:
#
# CFLAGS+=-fsanitize=thread
#
# Stack protector flags:
# CFLAGS+=-fstack-protector-strong -fstack-check -fstack-protector
#
OUT=strip-ansi
RM=rm -rfv

.PHONY: all
all: clean build

.PHONY: address
address: CFLAGS+=-fsanitize=address
address: run

.PHONY: race
race: CFLAGS+=-fsanitize=thread
race: run

# build the binary (we do this so that we generate debug symbols)
main:
	@$(CC) $(LDFLAGS) $(CFLAGS) -o $(OUT) main.c

# Note:
#   $(RM) *.o forces rebuild
#   $(RM) $(OUT).o forces rebuild of target
#
# @$(RM) $(OUT).o
.PHONY: build
build: main

.PHONY: debug
debug: CFLAGS+=-DDEBUG -fsanitize=thread
debug: run clean

.PHONY: run
run: clean build
	./$(OUT)

.PHONY: clean
clean:
	$(RM) *.o *.dSYM $(OUT)

.PHONY: compile-commands
compile-commands:
	bear fake-gcc $(LDFLAGS) $(CFLAGS) -o $(OUT) main.c

install: build
	mkdir -p ${PWD}/bin
	ln -sf ${PWD}/bin/strip-ansi ${HOME}/bin/.
