#!/usr/bin/env python3
"""Generate deterministic native-audio source WAVs (intro remains user audio)."""
import math, wave, struct, random, sys
from pathlib import Path
out=Path(sys.argv[1]); sr=18500
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
