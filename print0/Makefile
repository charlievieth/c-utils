CFLAGS= -std=c11 -Wall -Wextra -pedantic -O3 -march=native
OUT=print0
OBJ=print0.o
RM=rm -rf

all: build

run: build
	./$(OUT)

build: $(OBJ)
	$(CC) $(CFLAGS) -o $(OUT) print0.c
	$(RM) *.o

clean:
	$(RM) *.o $(OUT)
