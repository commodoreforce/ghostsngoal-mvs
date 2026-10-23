#!/usr/bin/env bash
set -euo pipefail

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew non trovato: installalo da https://brew.sh e rilancia." >&2
  exit 1
fi

xcode-select -p >/dev/null 2>&1 || xcode-select --install || true

brew tap dciabrin/ngdevkit
brew trust dciabrin/ngdevkit
brew install ngdevkit
brew install python@3.14 pkg-config autoconf automake rsync zip imagemagick sox ffmpeg ngdevkit-gngeo make mame

PREFIX="$(brew --prefix)"
PYTHON_PREFIX="$(brew --prefix python@3.14)"
PYTHON_BIN="$PYTHON_PREFIX/bin/python3.14"

if [ ! -x "$PYTHON_BIN" ]; then
  echo "Python 3.14 Homebrew non trovato in $PYTHON_BIN" >&2
  exit 1
fi

export HOMEBREW_PREFIX="$PREFIX"
export PATH="$PYTHON_PREFIX/bin:$PREFIX/bin:$PATH"

echo
"$PYTHON_BIN" --version
m68k-neogeo-elf-gcc --version | head -n 1
gmake --version | head -n 1
mame -version | head -n 1
ffmpeg -version | head -n 1

echo
echo "Toolchain installata. Python ngdevkit fissato a: $PYTHON_BIN"
echo "Ora esegui:"
echo "  ./scripts/prepare-ngdevkit-project.sh"
