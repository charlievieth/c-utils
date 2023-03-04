CC=gcc
CFLAGS=-O2 -std=c11 -g
#
# WARNINGS: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
CFLAGS+=-Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
CFLAGS+=-Wswitch-enum -Wcast-qual -Wpointer-arith
CFLAGS+=-Wstrict-overflow=5 -Wcast-align

TEST_EXE=timer_test

.PHONY: all
all: test

timer_test: test.c timer.h
	@$(CC) $(CFLAGS) -o $(TEST_EXE) test.c

.PHONY: test
test: timer_test
	@./$(TEST_EXE)

.PHONY: bench
bench: timer_test
	@./$(TEST_EXE) -bench | column -t

.PHONY: clean
clean:
	rm -rf $(TEST_EXE) *.dSYM *.gch *.plist
