#!/usr/bin/env python3
"""Launch the checked-in MVS milestone; only Python and MAME are required."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

root=Path(__file__).resolve().parents[1]
bundle=root/'releases/gngl1r1-r1.zip'
expected=(bundle.with_suffix('.zip.sha256')).read_text().split()[0]
if hashlib.sha256(bundle.read_bytes()).hexdigest()!=expected:
    sys.exit('Checksum del pacchetto non valido: aggiorna il repository.')
destination=root/'build/prebuilt-gngl1r1'
with zipfile.ZipFile(bundle) as archive:
    manifest=json.loads(archive.read('bundle-manifest.json'))
    for name,digest in manifest.items():
        # Extract only checked regular files into this private output directory.
        path=Path(name)
        if path.is_absolute() or '..' in path.parts:
            sys.exit('Percorso non valido nel pacchetto.')
        data=archive.read(name)
        if hashlib.sha256(data).hexdigest()!=digest:
            sys.exit(f'Checksum non valido: {name}')
        target=destination/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
info=json.loads((destination/'build-info.json').read_text())
label=info.get('milestone',f"VISUAL REVISION {info.get('visual_revision',1)}")
print(f"Cartuccia gngl1r1 | {label} | sorgenti {info['source_commit'][:8]}",flush=True)
mame=os.environ.get('MAME','mame')
if not shutil.which(mame):
    sys.exit('MAME non trovato. Su Mac: brew install mame')
print('5: moneta | 1: Start | Frecce: movimento | Ctrl: salto | Alt: tiro',flush=True)
command=[mame,'neogeo','-cart1','gngl1r1','-hashpath',str(destination),'-rompath',str(destination),
         '-noautosave','-skip_gameinfo','-window','-resolution','960x672']+sys.argv[1:]
sys.exit(subprocess.call(command,cwd=destination))
