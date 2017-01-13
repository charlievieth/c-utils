CC=gcc-6
CFLAGS= -std=c11 -Wall -Wextra
# CFLAGS= -std=c11 -Wall
OUT=cutn
RM=rm -rf

all: build

run: build
	./$(OUT)

build: cutn.o
	$(CC) $(CFLAGS) -o $(OUT) cutn.c
	$(RM) *.o

cutn: cutn.c

clean:
	$(RM) *.o $(OUT)
