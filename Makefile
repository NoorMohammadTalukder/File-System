CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
SRC = src/main.c src/lexer.c src/commands.c src/utils.c
OUT = filesys

all:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT)

run: all
	./$(OUT) fat32.img

clean:
	rm -f $(OUT)
