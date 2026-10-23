#!/usr/bin/env python3
"""Package only a verified cartridge with matching unassisted MAME evidence."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

root=Path(__file__).resolve().parents[1]
project=root/'.ngdevkit-examples/ghostsngoal-mvs'
subprocess.run([sys.executable,str(root/'scripts/verify-mvs-rom.py'),str(project)],check=True)
subprocess.run(['git','diff-index','--quiet','HEAD','--','mvs','src','scripts'],cwd=root,check=True)
for name in ('main.c','Makefile','user_commands.s'):
    assert (project/name).read_bytes()==(root/'mvs'/name).read_bytes(), f'Generated source mismatch: {name}'
cart=project/'build/rom/gngl1r1.zip'
cartsha=hashlib.sha256(cart.read_bytes()).hexdigest()
proofs={}
for file in sorted((project/'build').glob('*/result.json')):
    data=json.loads(file.read_text())
    if data.get('passed') and data.get('cartridge_sha256')==cartsha and not data.get('assisted_health'):
        proofs[data.get('scenario','audio')]=file.parent
assert {'level1','checks','audio'}<=proofs.keys(), 'Current cartridge lacks matching final MAME evidence'
files={name:(project/'build/rom'/name).read_bytes() for name in ('gngl1r1.zip','neogeo.xml','neogeo.zip')}
files['build-info.json']=json.dumps(dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
    cartridge_sha256=cartsha,elf_sha256=hashlib.sha256((project/'build/rom.elf').read_bytes()).hexdigest()),indent=2).encode()+b'\n'
for scenario,directory in proofs.items():
    for name in ('result.json','mame.log','title.png','clear.png','world-1800.png','world-3700.png','armor-lost.png','game-over.png'):
        if (directory/name).is_file():files[f'validation/{scenario}/{name}']=(directory/name).read_bytes()
for name in ('COPYING','COPYING.LESSER'):
    files[name+'.ngdevkit']=(Path('/workspace/tools/ngdevkit')/name).read_bytes()
files['README.txt']=('Ghosts n Goals MVS - Level 1 R1\n\n'
    'Richiede MAME. Controlli: 5 moneta, 1 Start, frecce movimento, Ctrl salto, Alt tiro.\n'
    'Avvio dalla cartella estratta:\n'
    'mame neogeo -cart1 gngl1r1 -hashpath . -rompath . -skip_gameinfo -window\n\n'
    'Il BIOS libero ngdevkit incluso ha hash diversi dai BIOS SNK attesi da MAME: gli avvisi\n'
    'su sp-s2.sp1, sm1.sm1 e sfix.sfix riguardano questo BIOS, non la cartuccia verificata.\n'
    'Sorgenti: https://github.com/commodoreforce/ghostsngoal-mvs\n'
    'SDK e BIOS: https://github.com/dciabrin/ngdevkit\n').encode()
files['bundle-manifest.json']=json.dumps({name:hashlib.sha256(data).hexdigest() for name,data in files.items()},indent=2).encode()+b'\n'
destination=root/'releases/gngl1r1-r1.zip';destination.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
    for name,data in files.items():archive.writestr(name,data)
destination.with_suffix('.zip.sha256').write_text(hashlib.sha256(destination.read_bytes()).hexdigest()+'  '+destination.name+'\n')
print(destination)
