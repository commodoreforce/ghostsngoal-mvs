#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/.ngdevkit-examples"
TARGET="$WORK/ghostsngoal-mvs"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew non trovato." >&2
  exit 1
fi

PREFIX="$(brew --prefix)"
PYTHON_BIN="$PREFIX/bin/python3"

if [ ! -x "$PYTHON_BIN" ]; then
  echo "Python Homebrew non trovato in $PYTHON_BIN" >&2
  echo "Esegui: brew install python" >&2
  exit 1
fi

if [ ! -d "$WORK/.git" ]; then
  git clone https://github.com/dciabrin/ngdevkit-examples.git "$WORK"
fi

cd "$WORK"
git submodule update --init --recursive
autoreconf -iv
./configure --with-python="$PYTHON_BIN"

rm -rf "$TARGET"
mkdir -p "$TARGET/src"

cp 00-template/build.mk "$TARGET/build.mk"
cp 00-template/emu.mk "$TARGET/emu.mk"
cp 00-template/rom.mk "$TARGET/rom.mk"
cp config.mk "$TARGET/config.mk"
cp -R 00-template/assets "$TARGET/assets"
cp -R 00-template/setup "$TARGET/setup"

cp "$ROOT/mvs/main.c" "$TARGET/main.c"
cp "$ROOT/src/game.c" "$ROOT/src/game.h" "$ROOT/src/levels.c" "$ROOT/src/levels.h" "$ROOT/src/characters.c" "$ROOT/src/characters.h" "$TARGET/src/"
cp "$ROOT/mvs/Makefile" "$TARGET/Makefile"

rm -rf "$TARGET/build"
gmake -C "$TARGET" generate

echo
printf 'Prepared ngdevkit project at:\n  %s\n' "$TARGET"
printf 'Python used by ngdevkit:\n  %s\n\n' "$PYTHON_BIN"
echo "Build with:"
echo "  cd \"$TARGET\""
echo "  gmake"
echo "  gmake mame-mvs"
