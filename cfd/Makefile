# CC=gcc-11
# CC=fake-gcc
# Debug (-g creates the *.dSYM directory)

# TODO: build with -DNDEBUG

CFLAGS=-O2 -g -std=c11 -mtune=native

# Add -march=native for non-arm64 platforms
UNAME_M := $(shell uname -m)
ifneq ($(UNAME_M),arm64)
	CFLAGS+=-march=native
endif

# Support glibc
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	CFLAGS+=-D_GNU_SOURCE
endif

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
OUT=cfd
RM=rm -rfv

.PHONY: all
all: clean build

.PHONY: address
address: CFLAGS+=-fsanitize=address
address: build

.PHONY: undefined
undefined: CFLAGS+=-fsanitize=undefined
undefined: build

.PHONY: race
race: CFLAGS+=-fsanitize=thread
race: build

.PHONY: profile
profile: CFLAGS+=-DNDEBUG
profile: build

.PHONY: release
release: CFLAGS+=-DNDEBUG
release: build

# build the binary (we do this so that we generate debug symbols)
cfd:
	@$(CC) $(LDFLAGS) $(CFLAGS) -o $(OUT) main.c

# Note:
#   $(RM) *.o forces rebuild
#   $(RM) $(OUT).o forces rebuild of target
#
# @$(RM) $(OUT).o
.PHONY: build
build: clean cfd

.PHONY: debug
debug: CFLAGS+=-DDEBUG -fsanitize=thread
debug: run clean

.PHONY: run
run: clean build
	./$(OUT)

.PHONY: test
test:
	@./scripts/test.bash

.PHONY: bench
bench:
	@./scripts/bench.bash

.PHONY: clean
clean:
	$(RM) *.o *.dSYM $(OUT)
