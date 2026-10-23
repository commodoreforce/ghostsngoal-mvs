#!/usr/bin/env python3
"""Record cold-boot YM2610 output and compare it to the encoded intro source."""
import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
import wave

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(__doc__)
p.add_argument('--project', type=Path, default=root/'.ngdevkit-examples/ghostsngoal-mvs')
p.add_argument('--pluginspath', type=Path)
args = p.parse_args()
project = args.project.resolve()
out = project/'build'/f'audio-{time.time_ns()}'
out.mkdir()
command = [os.environ.get('MAME','mame'), 'neogeo', '-cart1', 'gngl1r1',
           '-hashpath', str(project/'build/rom'), '-rompath', str(project/'build/rom'),
           '-video','none','-sound','none','-nothrottle','-noautosave','-skip_gameinfo',
           '-seconds_to_run','4','-wavwrite',str(out/'native.wav')]
if args.pluginspath:
    command += ['-pluginspath', str(args.pluginspath.resolve())]
with (out/'mame.log').open('w') as log:
    subprocess.run(command, cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
# ADPCM-A runs at the fixed ~18518.5 Hz hardware rate; compare raw sample order.
subprocess.run(['ffmpeg','-v','error','-y','-i',str(out/'native.wav'),'-ac','1','-ar','18518',str(out/'mono.wav')],check=True)
def read(path):
    with wave.open(str(path)) as w:
        assert w.getnchannels()==1 and w.getsampwidth()==2
        values=array('h',w.readframes(w.getnframes()))
        if sys.byteorder!='little':values.byteswap()
        return values
a=read(project/'assets/audio/intro.wav')[:55500]
b=read(out/'mono.wav')
assert max(map(abs,b))>100, 'Cold-boot intro is silent'
def envelope(values):
    return [math.sqrt(sum(x*x for x in values[i:i+100])/100) for i in range(0,len(values)-99,100)]
ea,eb=envelope(a),envelope(b)
def similarity(x,y):
    energy=sum(v*v for v in x)*sum(v*v for v in y)
    return sum(u*v for u,v in zip(x,y))/math.sqrt(max(1,energy))
coarse=max(range(len(eb)-len(ea)+1),key=lambda lag:similarity(ea,eb[lag:lag+len(ea)]))*100
small=a[::8]
lag=max(range(max(0,coarse-200),min(len(b)-len(a),coarse+200)+1),
        key=lambda lag:similarity(small,b[lag:lag+len(a):8]))
correlation=similarity(a,b[lag:lag+len(a)])
assert correlation>.8, f'Native audio differs from intro source: correlation={correlation}'
result=dict(passed=True,correlation=correlation,lag_samples=lag,peak=max(map(abs,b)),
            cartridge_sha256=hashlib.sha256((project/'build/rom/gngl1r1.zip').read_bytes()).hexdigest(),command=command)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2));print(f'Evidence: {out}')
