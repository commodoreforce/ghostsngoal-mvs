#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="$ROOT/.ngdevkit-examples/ghostsngoal-mvs"

echo "============================================================"
echo " Ghosts'n Goals MVS - FULL LEVEL 1 R1"
echo " clean prepare -> verify sources -> rebuild ROM -> verify ELF"
echo "============================================================"

cd "$ROOT"

BUILD_ONLY=0
case "${1:-}" in
  --build-only) BUILD_ONLY=1 ;;
  "") ;;
  *) echo "Usage: $0 [--build-only]" >&2; exit 2 ;;
esac

# Always regenerate the ngdevkit project from the current repository.
bash scripts/prepare-ngdevkit-project.sh

fail() { echo; echo "BUILD R1 ABORTED: $*" >&2; exit 1; }

[ -f "$TARGET/main.c" ] || fail "generated main.c missing"
[ -f "$TARGET/Makefile" ] || fail "generated Makefile missing"
[ -f "$TARGET/src/level1_assets.h" ] || fail "full Level 1 asset header missing"

# These markers cannot all exist in the old benchmark build.
grep -q 'MAX_ENEMIES 12' "$TARGET/main.c" || fail "old runtime copied: MAX_ENEMIES marker absent"
grep -q 'static const Wave waves' "$TARGET/main.c" || fail "old runtime copied: 13-wave table absent"
grep -q 'ET_MAZ' "$TARGET/main.c" || fail "old runtime copied: Mazapegul boss absent"
grep -q 'SND_SFX_INTRO' "$TARGET/main.c" || fail "old runtime copied: native intro audio command absent"
grep -q 'GNG_LEVEL1_WORLD_W' "$TARGET/main.c" || fail "old runtime copied: full 4192px world absent"
grep -q '^GAMEROM=gngl1r1$' "$TARGET/Makefile" || fail "wrong cartridge id; expected gngl1r1"
grep -q '#define GNG_LEVEL1_WORLD_W 4192' "$TARGET/src/level1_assets.h" || fail "wrong world asset width; expected 4192"

grep -q 'FULL LEVEL 1 R1' "$TARGET/main.c" || fail "visible R1 stamp was not applied"

echo
echo "Source verification: OK"
echo "  cartridge : gngl1r1"
echo "  world     : 4192 px"
echo "  waves     : 13"
echo "  enemies   : zombie / bat / pumpkin / ghost"
echo "  boss      : Mazapegul"
echo "  intro     : native ADPCM command present"

echo
echo "Rebuilding cartridge from zero..."
cd "$TARGET"
gmake

[ -f "build/rom.elf" ] || fail "build/rom.elf missing after compile"
[ -f "build/rom/gngl1r1.zip" ] || fail "new gngl1r1.zip cartridge missing"
[ -f "build/rom/neogeo.xml" ] || fail "MAME hash file missing"

# Verify that the exact freshly compiled ELF carries the R1 stamp and gameplay.
if command -v strings >/dev/null 2>&1; then
  strings build/rom.elf > build/rom.strings
  grep -q 'FULL LEVEL 1 R1' build/rom.strings || fail "ELF is not R1: visible build stamp absent"
  grep -q 'MAZAPEGUL' build/rom.strings || fail "ELF is missing boss/runtime strings"
fi

echo
python3 "$ROOT/scripts/verify-mvs-rom.py" "$PWD"
echo "ROM verification: OK"
echo "  $(pwd)/build/rom/gngl1r1.zip"
echo
if [ "$BUILD_ONLY" = 1 ]; then exit 0; fi

echo "Launching ONLY the verified R1 cartridge..."
exec gmake mame-mvs
