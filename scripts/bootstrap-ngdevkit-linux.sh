#!/usr/bin/env bash
# Rootless installation for the Debian 13 amd64 cloud image. Package trust is
# checked before extraction; no system package files or application code change.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GNG_TOOLS="${GNG_TOOLS:-/workspace/tools}"
export GNG_TOOLS
mkdir -p "$GNG_TOOLS"
WORK="$(mktemp -d "$GNG_TOOLS/bootstrap.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir "$WORK/gpg"
chmod 700 "$WORK/gpg"
KEY=DEBA216533CA9B75C79CB44931B8B6160C6BAA4A
curl -fsSL "https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x$KEY" -o "$WORK/key.asc"
gpg --homedir "$WORK/gpg" --batch --import "$WORK/key.asc"
gpg --homedir "$WORK/gpg" --with-colons --fingerprint "$KEY" | grep -Fq "$KEY"
PPA=https://ppa.launchpadcontent.net/dciabrin/ngdevkit/ubuntu
curl -fsSL "$PPA/dists/noble/InRelease" -o "$WORK/InRelease"
gpg --homedir "$WORK/gpg" --batch --decrypt "$WORK/InRelease" > "$WORK/Release"
curl -fsSL "$PPA/dists/noble/main/binary-amd64/Packages.gz" -o "$WORK/Packages.gz"
python3 - "$WORK" "$GNG_TOOLS" "$PPA" <<'PY'
import gzip,hashlib,pathlib,subprocess,sys
work,tools=map(pathlib.Path,sys.argv[1:3]);ppa=sys.argv[3]
data=(work/'Packages.gz').read_bytes()
release=(work/'Release').read_text()
entry=next(l.split() for l in release.split('SHA256:\n')[1].splitlines() if l.split()[-1]=='main/binary-amd64/Packages.gz')
assert hashlib.sha256(data).hexdigest()==entry[0] and len(data)==int(entry[1]), 'PPA index checksum mismatch'
found=set()
for block in gzip.decompress(data).decode().split('\n\n'):
    d=dict(l.split(': ',1) for l in block.splitlines() if ': ' in l and not l.startswith(' '))
    if d.get('Package') not in ('ngdevkit','ngdevkit-toolchain'):continue
    found.add(d['Package']);dest=tools/(d['Package']+'.deb')
    if not dest.exists() or hashlib.sha256(dest.read_bytes()).hexdigest()!=d['SHA256']:
        temporary=work/dest.name
        subprocess.run(['curl','-fsSL',ppa+'/'+d['Filename'],'-o',str(temporary)],check=True)
        assert hashlib.sha256(temporary.read_bytes()).hexdigest()==d['SHA256'], 'Package checksum mismatch'
        temporary.replace(dest)
    subprocess.run(['dpkg-deb','-x',str(dest),str(tools/'sdk')],check=True)
assert found=={'ngdevkit','ngdevkit-toolchain'}, 'Missing toolchain packages'
PY

APTROOT="$GNG_TOOLS/apt"
mkdir -p "$APTROOT/lists/partial" "$APTROOT/cache/archives/partial"
printf '%s\n' 'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main' > "$APTROOT/sources.list"
APT=(/usr/bin/apt-get -o "Dir::Etc::sourcelist=$APTROOT/sources.list" -o Dir::Etc::sourceparts=-
     -o "Dir::State::lists=$APTROOT/lists" -o "Dir::Cache=$APTROOT/cache" -o APT::Sandbox::User="$(id -un)" -o Debug::NoLocking=1)
"${APT[@]}" update
"${APT[@]}" --download-only -y --no-install-recommends install autoconf automake m4 rsync sox mame libisl23 libmpc3 libmpfr6 libgmp10 libipt2
for package in "$APTROOT/cache/archives/"*.deb; do dpkg-deb -x "$package" "$GNG_TOOLS/debian"; done

mkdir -p "$GNG_TOOLS/bin" "$GNG_TOOLS/cache" "$GNG_TOOLS/runtime"
chmod 700 "$GNG_TOOLS/runtime"
ln -sf "$GNG_TOOLS/debian/usr/bin/aclocal-1.17" "$GNG_TOOLS/bin/aclocal"
ln -sf "$GNG_TOOLS/debian/usr/bin/automake-1.17" "$GNG_TOOLS/bin/automake"
sed "s|/usr/share/autoconf|$GNG_TOOLS/debian/usr/share/autoconf|g" "$GNG_TOOLS/debian/usr/share/autoconf/autom4te.cfg" > "$GNG_TOOLS/autom4te.cfg"
python3 - "$GNG_TOOLS" <<'PY'
from pathlib import Path
import sys
tools=Path(sys.argv[1]);base=tools/'sdk/usr'
for source in (base/'bin').glob('z80-neogeo-ihx-*'):
    if source.is_file() and source.read_bytes().startswith(b'#!/bin/sh'):
        executable=base/'z80-neogeo-ihx/bin'/source.name.removeprefix('z80-neogeo-ihx-')
        assert executable.is_file(), executable
        wrapper=tools/'bin'/source.name
        wrapper.write_text(f'#!/bin/sh\nexport PATH="{executable.parent}:$PATH"\nexec "{executable}" "$@"\n')
        wrapper.chmod(0o755)
PY
if [ ! -x "$GNG_TOOLS/gng-python/bin/python" ]; then python3 -m venv "$GNG_TOOLS/gng-python"; fi
"$GNG_TOOLS/gng-python/bin/python" -m pip install --quiet 'Pillow==12.3.0' 'ruamel.yaml==0.18.16' 'PyYAML==6.0.3'
. "$ROOT/scripts/activate-cloud-toolchain.sh"
m68k-neogeo-elf-gcc --version | head -1
mame -version
printf 'Activate with: . %s/scripts/activate-cloud-toolchain.sh\n' "$ROOT"
