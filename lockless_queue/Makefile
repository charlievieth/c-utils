# Debug (-g creates the *.dSYM directory)
#
CFLAGS=-O0 -std=gnu11 -g3
#
# WARNINGS: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
CFLAGS+=-Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
CFLAGS+=-Wswitch-enum -Wcast-qual -Wpointer-arith
CFLAGS+=-Wstrict-overflow=5 -Wcast-align
#
# Performance:
#
CFLAGS+=-Winline -Wdisabled-optimization
#
# Race detector:
#
CFLAGS+=-fsanitize=thread -fexceptions
#
# Stack protector flags:
# CFLAGS+=-fstack-protector-strong -fstack-check -fstack-protector
#
DEPS=lockless_queue.h
OBJ=lockless_queue.o
OUT=lockless_queue
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

# $(RM) *.dSYM $(OUT)
.PHONY: clean
clean:
	$(RM) *.o $(OUT)
