# CC=gcc-11
# CC=fake-gcc
# Debug (-g creates the *.dSYM directory)
#
CFLAGS=-Og -std=gnu11 -g -ggdb3
# CFLAGS=-O3 -flto -std=gnu11
#
# WARNINGS: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
CFLAGS+=-Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
CFLAGS+=-Wswitch-enum -Wcast-qual -Wpointer-arith
CFLAGS+=-Wstrict-overflow=5 -Wcast-align
CFLAGS+=-Wno-gnu-zero-variadic-macro-arguments
# CFLAGS+=-Wbool-compare
#
# Performance:
#
CFLAGS+=-Winline -Wdisabled-optimization
#
# Experimental
#
CFLAGS+=-Wstring-compare
CFLAGS+=-Wsuggest-attribute=pure
CFLAGS+=-Wsuggest-attribute=const
CFLAGS+=-Wsuggest-attribute=noreturn
CFLAGS+=-Wsuggest-attribute=format
CFLAGS+=-Wsuggest-attribute=cold
CFLAGS+=-Wsuggest-attribute=malloc

# This looks useful
CFLAGS+=-Wunused-macros
# CFLAGS+=-Wrestrict
CFLAGS+=-Wnull-dereference

#
# Memory sanitizer:
#
# CFLAGS+=-fsanitize=address
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
OUT=hmap
RM=rm -rfv

.PHONY: all
all: clean build

.PHONY: address
address: CFLAGS+=-fsanitize=address
address: build

.PHONY: race
race: CFLAGS+=-fsanitize=thread
race: build

# build the binary (we do this so that we generate debug symbols)
hmap:
	@$(CC) $(LDFLAGS) $(CFLAGS) -o $(OUT) hmap.c

# Note:
#   $(RM) *.o forces rebuild
#   $(RM) $(OUT).o forces rebuild of target
#
# @$(RM) $(OUT).o
.PHONY: build
build: CFLAGS+=-DNDEBUG
build: clean hmap

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
	bear fake-gcc $(LDFLAGS) $(CFLAGS) -o $(OUT) cfd.c
