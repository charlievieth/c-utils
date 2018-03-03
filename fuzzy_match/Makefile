CC=gcc-5
CFLAGS= -Wall -Wextra -std=c11
RM=rm -rf
OUT=repl-9

all: build

run: build
	./$(OUT)

build: repl-9.o
	$(CC) $(CFLAGS) -o $(OUT) repl-9.c
	$(RM) *.o

debug: CFLAGS+=-DDEBUG_ON
debug: build

repl-9.o: repl-9.c

clean:
	$(RM) *.o $(OUT)
