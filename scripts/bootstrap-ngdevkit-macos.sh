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
brew install python pkg-config autoconf automake rsync zip imagemagick sox ngdevkit-gngeo make mame

PREFIX="$(brew --prefix)"
PYTHON_BIN="$PREFIX/bin/python3"

if [ ! -x "$PYTHON_BIN" ]; then
  echo "Python Homebrew non trovato in $PYTHON_BIN" >&2
  exit 1
fi

export HOMEBREW_PREFIX="$PREFIX"
export PATH="$PREFIX/bin:$PATH"

echo
"$PYTHON_BIN" --version
m68k-neogeo-elf-gcc --version | head -n 1
gmake --version | head -n 1
mame -version | head -n 1

echo
echo "Toolchain installata. Python ngdevkit: $PYTHON_BIN"
echo "Ora esegui:"
echo "  ./scripts/prepare-ngdevkit-project.sh"
