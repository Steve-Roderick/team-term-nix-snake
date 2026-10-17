CC = gcc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

all: server client

server: server.c game.c game_view.c protocol.c game.h game_view.h game_config.h protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) server.c game.c game_view.c protocol.c -o $@

client: client.c local.c game.c game_view.c protocol.c local.h game.h game_view.h protocol.h game_config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) client.c local.c game.c game_view.c protocol.c -o $@ -lncurses

tests/test_game: tests/test_game.c game.c protocol.c game.h game_config.h protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. tests/test_game.c game.c protocol.c -o $@

tests/test_local: tests/test_local.c local.c game.c game_view.c protocol.c local.h game.h game_view.h game_config.h protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. tests/test_local.c local.c game.c game_view.c protocol.c -o $@

test-local: client tests/test_local
	./tests/test_local
	python3 tests/test_local_terminal.py

test: all tests/test_game test-local
	./tests/test_game
	python3 tests/test_network.py

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' test

clean:
	rm -f server client tests/test_game tests/test_local

.PHONY: all test test-local sanitize clean
