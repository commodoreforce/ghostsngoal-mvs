#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/.ngdevkit-examples"
TARGET="$WORK/ghostsngoal-mvs"

if [ ! -d "$WORK/.git" ]; then
  git clone https://github.com/dciabrin/ngdevkit-examples.git "$WORK"
fi

cd "$WORK"
git submodule update --init --recursive
autoreconf -iv
./configure

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

# Force generation of the stock ngdevkit FIX/C-ROM/M-ROM assets now.
# This avoids stale build/.generated markers leaving the project without
# base-sound-driver.ihx or the stock logo/font ROM fragments.
rm -f "$TARGET/build/.generated"
gmake -C "$TARGET" generate

echo
printf 'Prepared ngdevkit project at:\n  %s\n\n' "$TARGET"
echo "Build with:"
echo "  cd \"$TARGET\""
echo "  gmake"
echo "  gmake mame-mvs"
