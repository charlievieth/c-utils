# CC=gcc-11
# Debug (-g creates the *.dSYM directory)
#
CFLAGS=-O3 -std=c11 -flto -mtune=native -g
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
# CFLAGS+=-Wsuggest-attribute=pure
# CFLAGS+=-Wsuggest-attribute=const
# CFLAGS+=-Wsuggest-attribute=noreturn
# CFLAGS+=-Wsuggest-attribute=format
# CFLAGS+=-Wsuggest-attribute=cold
# CFLAGS+=-Wsuggest-attribute=malloc

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
OUT=hashmap
RM=rm -rf

.PHONY: all
all: build

.PHONY: address
address: CFLAGS+=-fsanitize=address
address: build

.PHONY: race
race: CFLAGS+=-fsanitize=thread
race: build

.PHONY: leak
leak: CFLAGS+=-fsanitize=leak
leak: build

# build the binary (we do this so that we generate debug symbols)
hashmap: hashmap.c utils.c utils.h xxhash.c xxhash.h
	$(CC) $(LDFLAGS) $(CFLAGS) -o $(OUT) hashmap.c utils.c

# build: CFLAGS+=-DNDEBUG
.PHONY: build
build: hashmap

.PHONY: debug
debug: CFLAGS+=-fsanitize=address
debug: run clean

.PHONY: run
run: clean address
	./$(OUT)

.PHONY: bench
bench: clean build
	./$(OUT) bench

.PHONY: test
test: build
	./$(OUT)

.PHONE: debug_optimizations
debug_optimizations: CFLAGS+=-fsave-optimization-record -foptimization-record-file=optimizations.yaml
debug_optimizations: clean
debug_optimizations: build

.PHONY: lint
lint:
	clang-tidy hashmap.c utils.c utils.h

.PHONY: clean
clean:
	@$(RM) *.o *.dSYM $(OUT)
