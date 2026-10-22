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
brew install pkg-config autoconf automake rsync zip imagemagick sox ngdevkit-gngeo make mame

PREFIX="$(brew --prefix)"
export HOMEBREW_PREFIX="$PREFIX"
export PATH="$HOMEBREW_PREFIX/opt/python3/bin:$HOMEBREW_PREFIX/bin:$PATH"

echo
m68k-neogeo-elf-gcc --version | head -n 1
gmake --version | head -n 1
mame -version | head -n 1

echo
echo "Toolchain installata. Ora esegui:"
echo "  ./scripts/prepare-ngdevkit-project.sh"
