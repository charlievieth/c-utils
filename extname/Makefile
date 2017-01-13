CC=gcc-6
CFLAGS= -std=gnu11 -Wall -Wextra -pedantic -O3
OUT=extname
OBJ=extname.o
RM=rm -rf

all: build

run: build
	./$(OUT)

build: $(OBJ)
	$(CC) $(CFLAGS) -o $(OUT) extname.c
	$(RM) *.o

clean:
	$(RM) *.o $(OUT)
