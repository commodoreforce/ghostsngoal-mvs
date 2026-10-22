CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror

CORE_SRC = src/game.c src/characters.c src/levels.c
CORE_HDR = src/game.h src/characters.h src/levels.h

.PHONY: all test clean

all: test

build:
	mkdir -p build

build/test_game: build $(CORE_SRC) $(CORE_HDR) tests/test_game.c
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/test_game.c -o $@

test: build/test_game
	./build/test_game

clean:
	rm -rf build
