CFLAGS=-O2 -std=gnu11
CFLAGS+=$(shell pkg-config --cflags --libs sdl2)
# LDFLAGS+=$(shell sdl2-config --libs)
# CFLAGS+=$(shell sdl2-config --cflags)
# CFLAGS+=-isystem /usr/lib/aarch64-linux-gnu
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
OUT=clipboard
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
clipboard:
	$(CC) -o $(OUT) main.c $(CFLAGS)

# Note:
#   $(RM) *.o forces rebuild
#   $(RM) $(OUT).o forces rebuild of target
#
# @$(RM) $(OUT).o
.PHONY: build
build: clean clipboard

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
	bear -- $(LDFLAGS) $(CFLAGS) -o $(OUT) main.c

# .PHONY: test_request
# test_request:
# 	@./clipboard --session=46 --status-code=1 -- 1234 test -v clipboard request
