#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/.ngdevkit-examples"
TARGET="$WORK/ghostsngoal-mvs"
VENV="$ROOT/.venv-ngdevkit"
UPSTREAM="$ROOT/.ghostsngoal-upstream"
UPSTREAM_SHA="7d6d995aa32a6a3df23d23ca857852f85bdc99c2"

if ! command -v brew >/dev/null 2>&1; then echo "Homebrew non trovato." >&2; exit 1; fi
PYTHON_PREFIX="$(brew --prefix python@3.14 2>/dev/null || true)"
PYTHON_BASE="$PYTHON_PREFIX/bin/python3.14"
NGDEVKIT_PREFIX="$(brew --prefix ngdevkit 2>/dev/null || true)"
NGDK_TOOLSDIR="$NGDEVKIT_PREFIX/libexec/bin"
if [ -z "$PYTHON_PREFIX" ] || [ ! -x "$PYTHON_BASE" ]; then echo "Python 3.14 Homebrew non trovato. Esegui: brew install python@3.14" >&2; exit 1; fi
if [ -z "$NGDEVKIT_PREFIX" ] || [ ! -f "$NGDK_TOOLSDIR/tiletool.py" ]; then echo "Tool Python ngdevkit non trovati: $NGDK_TOOLSDIR" >&2; exit 1; fi

if [ ! -x "$VENV/bin/python" ]; then rm -rf "$VENV"; "$PYTHON_BASE" -m venv "$VENV"; fi
"$VENV/bin/python" -m pip install --quiet --upgrade pip pillow
PYTHON_BIN="$VENV/bin/python"

if [ ! -d "$WORK/.git" ]; then git clone https://github.com/dciabrin/ngdevkit-examples.git "$WORK"; fi
if [ ! -d "$UPSTREAM/.git" ]; then git clone https://github.com/commodoreforce/ghostsngoal.git "$UPSTREAM"; fi
git -C "$UPSTREAM" fetch origin "$UPSTREAM_SHA" --depth 1
git -C "$UPSTREAM" checkout --detach "$UPSTREAM_SHA"

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

# Final Level 1 ground alignment: gameplay floor in the web is y=208.
"$PYTHON_BIN" - "$TARGET/main.c" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])
s=p.read_text()
s=s.replace('camera_x_fp,GROUND_Y-16);draw_layer(SCB_FOREGROUND', 'camera_x_fp,GROUND_Y);draw_layer(SCB_FOREGROUND')
p.write_text(s)
PY

# User-provided title/intro audio. Search common locations automatically.
INTRO=""
for p in "$ROOT/reference-packs/intro.mp3" "$ROOT/intro.mp3" "$HOME/Downloads/intro.mp3"; do
  if [ -f "$p" ]; then INTRO="$p"; break; fi
done
if [ -z "$INTRO" ] && [ -d "$HOME/Downloads" ]; then INTRO="$(find "$HOME/Downloads" -maxdepth 1 -iname 'intro*.mp3' -print -quit 2>/dev/null || true)"; fi
if [ -z "$INTRO" ]; then echo "ERRORE: intro.mp3 non trovato. Mettilo in reference-packs/intro.mp3 o ~/Downloads/intro.mp3" >&2; exit 1; fi

echo "Intro audio: $INTRO"
if command -v ffmpeg >/dev/null 2>&1; then
  ffmpeg -loglevel error -y -i "$INTRO" -ac 1 -ar 22050 -sample_fmt s16 "$TARGET/assets/audio/intro.wav"
elif command -v sox >/dev/null 2>&1; then
  sox "$INTRO" -c 1 -r 22050 -b 16 "$TARGET/assets/audio/intro.wav"
else
  echo "Serve ffmpeg o sox per convertire intro.mp3" >&2; exit 1
fi

# Deterministic original Level 1 loop + gameplay SFX.
"$PYTHON_BIN" - "$TARGET/assets/audio" <<'PY'
import math, wave, struct, random, sys
from pathlib import Path
out=Path(sys.argv[1]); sr=22050
random.seed(0xC35E)

def save(name,samples):
    m=max(1e-6,max(abs(x) for x in samples)); scale=.82/m
    with wave.open(str(out/name),'wb') as w:
        w.setnchannels(1);w.setsampwidth(2);w.setframerate(sr)
        w.writeframes(b''.join(struct.pack('<h',int(max(-1,min(1,x*scale))*32767)) for x in samples))

def tone(freq,dur,kind='square',amp=.5):
    n=int(sr*dur);a=[]
    for i in range(n):
        t=i/sr;s=math.sin(2*math.pi*freq*t)
        if kind=='square':s=1 if s>=0 else -1
        elif kind=='tri':s=2/math.pi*math.asin(s)
        a.append(s*amp*(1-i/max(1,n)*.08))
    return a

def mix(dst,src,start,g=1):
    k=int(start*sr)
    if len(dst)<k+len(src):dst.extend([0.0]*(k+len(src)-len(dst)))
    for i,v in enumerate(src):dst[k+i]+=v*g

dur=15.5;music=[0.0]*int(sr*dur)
notes=[110.00,130.81,164.81,196.00,164.81,130.81,98.00,123.47,146.83,196.00,146.83,123.47];step=.25
for k in range(int(dur/step)):
    f=notes[k%len(notes)];mix(music,tone(f,.19,'square',.20),k*step);mix(music,tone(f*2,.11,'tri',.11),k*step+.125)
    if k%2==0:mix(music,tone(55,.07,'square',.22),k*step)
    if k%4 in (1,3):
        noise=[(random.random()*2-1)*.10*(1-i/(sr*.045)) for i in range(int(sr*.045))];mix(music,noise,k*step)
save('level_loop.wav',music)
save('kick.wav',tone(170,.10,'square',.7)+tone(100,.07,'tri',.4))
save('hit.wav',tone(85,.15,'square',.8))
save('enemy_die.wav',tone(260,.06,'square',.6)+tone(180,.08,'square',.5)+tone(100,.12,'tri',.45))
save('boss_hit.wav',tone(70,.18,'square',.75)+tone(45,.16,'tri',.5))
clear=[]
for i,f in enumerate((261.63,329.63,392.00,523.25)):mix(clear,tone(f,.22,'square',.45),i*.16)
save('stage_clear.wav',clear)
PY

echo "Preflight ngdevkit tools:"
"$PYTHON_BIN" "$NGDK_TOOLSDIR/tiletool.py" --help >/dev/null
"$PYTHON_BIN" -c 'from PIL import Image; import sys; print("  Python",sys.version.split()[0],"+ Pillow OK")'
"$PYTHON_BIN" -m py_compile "$ROOT/scripts/build-mvs-visual-assets.py" "$ROOT/scripts/build_level1_full.py" "$ROOT/scripts/build_level1_full_v2.py"

echo
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
