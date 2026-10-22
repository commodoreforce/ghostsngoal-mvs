#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/.ngdevkit-examples"
TARGET="$WORK/ghostsngoal-mvs"
VENV="$ROOT/.venv-ngdevkit"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew non trovato." >&2
  exit 1
fi

PYTHON_PREFIX="$(brew --prefix python@3.14 2>/dev/null || true)"
PYTHON_BASE="$PYTHON_PREFIX/bin/python3.14"
NGDEVKIT_PREFIX="$(brew --prefix ngdevkit 2>/dev/null || true)"
NGDK_TOOLSDIR="$NGDEVKIT_PREFIX/libexec/bin"

if [ -z "$PYTHON_PREFIX" ] || [ ! -x "$PYTHON_BASE" ]; then
  echo "Python 3.14 Homebrew non trovato." >&2
  echo "Esegui: brew install python@3.14" >&2
  exit 1
fi

if [ -z "$NGDEVKIT_PREFIX" ] || [ ! -f "$NGDK_TOOLSDIR/tiletool.py" ]; then
  echo "Tool Python reali di ngdevkit non trovati in: $NGDK_TOOLSDIR" >&2
  echo "Prova: brew reinstall ngdevkit" >&2
  exit 1
fi

# Dedicated Python environment for ngdevkit tooling. This deliberately avoids
# Apple's /usr/bin/python3, Homebrew's rolling 'python', and Homebrew wrapper
# scripts around ngdevkit's Python tools.
if [ ! -x "$VENV/bin/python" ]; then
  rm -rf "$VENV"
  "$PYTHON_BASE" -m venv "$VENV"
fi
"$VENV/bin/python" -m pip install --quiet --upgrade pip
"$VENV/bin/python" -m pip install --quiet --upgrade pillow
PYTHON_BIN="$VENV/bin/python"

if [ ! -d "$WORK/.git" ]; then
  git clone https://github.com/dciabrin/ngdevkit-examples.git "$WORK"
fi

cd "$WORK"
git submodule update --init --recursive
autoreconf -iv
./configure --with-python="$PYTHON_BIN"

# The binaries under $(pkg-config --variable=bindir ngdevkit) are Homebrew
# shell wrappers. Never feed those wrappers to Python. Point config.mk directly
# at the actual .py sources under the formula's libexec/bin directory.
force_python_tool() {
  key="$1"
  script="$2"
  real_script="$NGDK_TOOLSDIR/$script"
  if [ ! -f "$real_script" ]; then
    echo "ngdevkit tool mancante: $real_script" >&2
    exit 1
  fi
  value="$PYTHON_BIN $real_script"
  if grep -q "^${key}=" config.mk; then
    sed -i.bak "s|^${key}=.*|${key}=${value}|" config.mk
  else
    printf '%s=%s\n' "$key" "$value" >> config.mk
  fi
}

if grep -q '^PYTHON=' config.mk; then
  sed -i.bak "s|^PYTHON=.*|PYTHON=$PYTHON_BIN|" config.mk
else
  printf 'PYTHON=%s\n' "$PYTHON_BIN" >> config.mk
fi

force_python_tool PALTOOL paltool.py
force_python_tool TILETOOL tiletool.py
force_python_tool ADPCMTOOL adpcmtool.py
force_python_tool VROMTOOL vromtool.py
force_python_tool FURTOOL furtool.py
force_python_tool NSSTOOL nsstool.py
force_python_tool SOUNDTOOL soundtool.py
force_python_tool ROMTOOL romtool.py
rm -f config.mk.bak

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

# Fail early using the exact interpreter + real Python tool source that the
# generated project will use.
echo "Preflight ngdevkit tiletool with pinned Python:"
"$PYTHON_BIN" "$NGDK_TOOLSDIR/tiletool.py" --help >/dev/null
"$PYTHON_BIN" -c 'from PIL import Image; import sys; print("  Python", sys.version.split()[0], "+ Pillow OK")'
echo "  TILETOOL=$PYTHON_BIN $NGDK_TOOLSDIR/tiletool.py"

rm -rf "$TARGET/build"
gmake -C "$TARGET" generate

echo
printf 'Prepared ngdevkit project at:\n  %s\n' "$TARGET"
printf 'Python used by ngdevkit:\n  %s\n\n' "$PYTHON_BIN"
echo "Build with:"
echo "  cd \"$TARGET\""
echo "  gmake"
echo "  gmake mame-mvs"
