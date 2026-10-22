CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror

.PHONY: all test clean

all: test

build:
	mkdir -p build

build/test_game: build src/game.c src/game.h tests/test_game.c
	$(CC) $(CFLAGS) -Isrc src/game.c tests/test_game.c -o $@

test: build/test_game
	./build/test_game

clean:
	rm -rf build
