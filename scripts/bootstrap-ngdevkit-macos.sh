#!/usr/bin/env bash
set -euo pipefail

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew non trovato: installalo da https://brew.sh e rilancia." >&2
  exit 1
fi

xcode-select -p >/dev/null 2>&1 || xcode-select --install || true

brew tap dciabrin/ngdevkit
brew install ngdevkit pkg-config autoconf automake rsync zip imagemagick sox make mame || true
brew install ngdevkit-gngeo || true

PREFIX="$(brew --prefix)"
export PATH="$PREFIX/opt/python3/bin:$PREFIX/bin:$PATH"

echo
m68k-neogeo-elf-gcc --version | head -n 1 || true
gmake --version | head -n 1 || true
mame -version | head -n 1 || true

echo
echo "Toolchain base installata. Ora esegui:"
echo "  ./scripts/prepare-ngdevkit-project.sh"
