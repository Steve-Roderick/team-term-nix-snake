# Define the C compiler (gcc for C, g++ for C++)
CC = gcc

# Define compiler flags:
CFLAGS = -Wall -O3 -Wno-unused-result


all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server -lncurses -lpthread


client: client.c
	$(CC) $(CFLAGS) client.c -o client -lncurses -lpthread

# Phony target: targets that do not produce a file named after the target
.PHONY: clean all

# Rule to clean up the directory by removing generated files
clean:
	rm -f server client

