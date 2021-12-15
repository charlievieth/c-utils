# CC=gcc-11
# CC=fake-gcc
# Debug (-g creates the *.dSYM directory)
#
# CFLAGS=-O2 -std=c11 -g -flto
CFLAGS=-O3 -flto -march=native -mtune=native -std=gnu11
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

.PHONY: race
race: CFLAGS+=-fsanitize=thread
race: build

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

.PHONY: clean
clean:
	$(RM) *.o *.dSYM $(OUT)

.PHONY: compile-commands
compile-commands:
	bear fake-gcc $(LDFLAGS) $(CFLAGS) -o $(OUT) cfd.c

.PHONY: test_request
test_request:
	@./cfd --session=46 --status-code=1 -- 1234 test -v cfd request
