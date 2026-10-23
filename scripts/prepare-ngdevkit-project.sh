#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/.ngdevkit-examples"
TARGET="$WORK/ghostsngoal-mvs"
VENV="$ROOT/.venv-ngdevkit"
UPSTREAM="${GNG_UPSTREAM:-$ROOT/.ghostsngoal-upstream}"
UPSTREAM_SHA="7d6d995aa32a6a3df23d23ca857852f85bdc99c2"
EXAMPLES_SHA="4e7900b79b1555f9f1737549a49c26fbdb1aa589"

# Use a native ngdevkit installation on Linux; Homebrew is optional on macOS.
PYTHON_BASE="${GNG_PYTHON:-$(command -v python3)}"
if command -v brew >/dev/null 2>&1; then
  NGDEVKIT_PREFIX="$(brew --prefix ngdevkit)"
  export PKG_CONFIG_PATH="$NGDEVKIT_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
fi
if ! pkg-config --exists ngdevkit; then
  echo "ngdevkit non disponibile in pkg-config. Installare la toolchain ufficiale e impostare PATH/PKG_CONFIG_PATH." >&2
  exit 1
fi
NGDK_TOOLSDIR="$(pkg-config --variable=bindir ngdevkit)"
if [ -d "${NGDEVKIT_PREFIX:-}/libexec/bin" ]; then NGDK_TOOLSDIR="$NGDEVKIT_PREFIX/libexec/bin"; fi
for tool in m68k-neogeo-elf-gcc z80-neogeo-ihx-sdasz80 z80-neogeo-ihx-sdldz80 autoreconf ffmpeg; do
  command -v "$tool" >/dev/null || { echo "Prerequisito mancante: $tool" >&2; exit 1; }
done
"$PYTHON_BASE" -c 'import sys; assert sys.version_info >= (3,12), "Python >= 3.12 richiesto"'
if [ ! -x "$VENV/bin/python" ]; then "$PYTHON_BASE" -m venv "$VENV"; fi
"$VENV/bin/python" -m pip install --quiet 'Pillow==12.3.0' 'ruamel.yaml==0.18.16' 'PyYAML==6.0.3'
PYTHON_BIN="$VENV/bin/python"

if [ ! -d "$WORK/.git" ]; then
  git clone https://github.com/dciabrin/ngdevkit-examples.git "$WORK"
  git -C "$WORK" checkout --detach "$EXAMPLES_SHA"
fi
if [ "$(git -C "$WORK" rev-parse HEAD)" != "$EXAMPLES_SHA" ]; then
  echo "ngdevkit-examples deve essere al commit $EXAMPLES_SHA; nessun checkout esistente viene sovrascritto." >&2; exit 1
fi
if [ ! -d "$UPSTREAM/.git" ]; then
  git clone https://github.com/commodoreforce/ghostsngoal.git "$UPSTREAM"
  git -C "$UPSTREAM" checkout --detach "$UPSTREAM_SHA"
fi
# Never fetch, reset, or rewrite an existing reference checkout.
if [ "$(git -C "$UPSTREAM" rev-parse HEAD)" != "$UPSTREAM_SHA" ] || [ -n "$(git -C "$UPSTREAM" status --porcelain)" ]; then
  echo "Il riferimento READ-ONLY deve essere pulito al commit $UPSTREAM_SHA." >&2; exit 1
fi

cd "$WORK"
git submodule update --init --recursive
autoreconf -iv
./configure --with-python="$PYTHON_BIN"

force_python_tool() {
  key="$1"; script="$2"; real_script="$NGDK_TOOLSDIR/$script"
  if [ ! -f "$real_script" ]; then echo "ngdevkit tool mancante: $real_script" >&2; exit 1; fi
  value="$PYTHON_BIN $real_script"
  if grep -q "^${key}=" config.mk; then sed -i.bak "s|^${key}=.*|${key}=${value}|" config.mk; else printf '%s=%s\n' "$key" "$value" >> config.mk; fi
}
if grep -q '^PYTHON=' config.mk; then sed -i.bak "s|^PYTHON=.*|PYTHON=$PYTHON_BIN|" config.mk; else printf 'PYTHON=%s\n' "$PYTHON_BIN" >> config.mk; fi
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
mkdir -p "$TARGET/src" "$TARGET/assets/audio"
cp 00-template/build.mk "$TARGET/build.mk"
cp 00-template/emu.mk "$TARGET/emu.mk"
cp 00-template/rom.mk "$TARGET/rom.mk"
cp config.mk "$TARGET/config.mk"
cp -R 00-template/assets/. "$TARGET/assets/"
cp -R 00-template/setup "$TARGET/setup"
cp "$ROOT/mvs/main.c" "$TARGET/main.c"
cp "$ROOT/mvs/Makefile" "$TARGET/Makefile"
cp "$ROOT/mvs/user_commands.s" "$TARGET/user_commands.s"
cp "$ROOT/mvs/sample-map.yaml" "$TARGET/assets/sample-map.yaml"
cp "$ROOT/mvs/music-map.yaml" "$TARGET/assets/music-map.yaml"
cp "$ROOT/src/game.c" "$ROOT/src/game.h" "$ROOT/src/levels.c" "$ROOT/src/levels.h" "$ROOT/src/characters.c" "$ROOT/src/characters.h" "$TARGET/src/"

# User-provided title/intro audio. Search common locations automatically.
INTRO=""
for p in "${GNG_INTRO:-/nonexistent}" "$ROOT/reference-packs/intro.mp3" "$ROOT/intro.mp3" "$HOME/Downloads/intro.mp3" "$UPSTREAM/assets/audio/intro_master.mp3" "$UPSTREAM/game/public/audio/intro.mp3"; do
  if [ -f "$p" ]; then INTRO="$p"; break; fi
done
if [ -z "$INTRO" ] && [ -d "$HOME/Downloads" ]; then INTRO="$(find "$HOME/Downloads" -maxdepth 1 -iname 'intro*.mp3' -print -quit 2>/dev/null || true)"; fi
if [ -z "$INTRO" ]; then echo "ERRORE: intro.mp3 non trovato. Mettilo in reference-packs/intro.mp3 o ~/Downloads/intro.mp3" >&2; exit 1; fi

echo "Intro audio: $INTRO"
if command -v ffmpeg >/dev/null 2>&1; then
  ffmpeg -loglevel error -y -i "$INTRO" -ac 1 -ar 18500 -sample_fmt s16 "$TARGET/assets/audio/intro.wav"
elif command -v sox >/dev/null 2>&1; then
  sox "$INTRO" -c 1 -r 18500 -b 16 "$TARGET/assets/audio/intro.wav"
else
  echo "Serve ffmpeg o sox per convertire intro.mp3" >&2; exit 1
fi

"$PYTHON_BIN" "$ROOT/scripts/build-mvs-audio.py" "$TARGET/assets/audio" "$UPSTREAM"
echo "Preflight ngdevkit tools:"
"$PYTHON_BIN" "$NGDK_TOOLSDIR/tiletool.py" --help >/dev/null
"$PYTHON_BIN" -c 'import PIL, ruamel.yaml, yaml, sys; print("  Python",sys.version.split()[0],"+ Pillow + ruamel.yaml + PyYAML OK")'
"$PYTHON_BIN" -m py_compile "$ROOT/scripts/build-mvs-visual-assets.py" "$ROOT/scripts/build_level1_full.py" "$ROOT/scripts/build_level1_full_v2.py"

echo
cp "$ROOT/mvs/level1_content.h" "$ROOT/mvs/level1_penalty.h" "$TARGET/src/"
echo "Building Ghosts n Goals FULL LEVEL 1 assets from web reference $UPSTREAM_SHA"
"$PYTHON_BIN" "$ROOT/scripts/build_level1_full_v2.py" "$UPSTREAM" "$TARGET"

rm -rf "$TARGET/build"
gmake -C "$TARGET" generate

echo
printf 'Prepared FULL LEVEL 1 ngdevkit project at:\n  %s\n' "$TARGET"
printf 'Intro converted to native sound source:\n  %s\n' "$TARGET/assets/audio/intro.wav"
printf 'Web reference commit:\n  %s\n' "$UPSTREAM_SHA"
echo
echo "Build and run:"
echo "  cd \"$TARGET\""
echo "  gmake"
echo "  gmake mame-mvs"
