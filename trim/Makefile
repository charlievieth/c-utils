CC=gcc-6
CFLAGS= -std=gnu11 -Wall -Wextra -pedantic -O3
OUT=trim
OBJ=trim.o
RM=rm -rf

all: build

run: build
	./$(OUT)

build: $(OBJ)
	$(CC) $(CFLAGS) -o $(OUT) trim.c
	$(RM) *.o

clean:
	$(RM) *.o $(OUT)
