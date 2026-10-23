#!/usr/bin/env python3
"""Generate deterministic native-audio source WAVs (intro remains user audio)."""
import math, wave, struct, random, sys, ast, re
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
# Read the actual web note patterns; do not replace the game's themes with a
# generic test tune. The waveforms/percussion are rendered offline for YM2610 A.
reference=Path(sys.argv[2])
source=(reference/'game/src/systems/sfx.js').read_text()
def track(name,multiplier=1,cycles=2):
    block=re.search(r'\b'+name+r':\s*\{(.*?)\n  \}',source,re.S)[1]
    bpm=float(re.search(r'bpm:\s*([\d.]+)',block)[1])*multiplier
    lead=ast.literal_eval(re.search(r'lead:\s*(\[.*?\])',block,re.S)[1])
    bass=ast.literal_eval(re.search(r'bass:\s*(\[.*?\])',block,re.S)[1])
    drums=re.search(r"drum:\s*'([^']+)'",block)[1]
    step=60/bpm/4;duration=len(lead)*cycles*step;music=[0.0]*int(sr*duration)
    note=lambda n:440*2**((n-69)/12)
    for i in range(len(lead)*cycles):
        at=i*step;ln=lead[i%len(lead)];bn=bass[i%len(bass)]
        if ln:mix(music,tone(note(ln),step*.9,'square',.22),at)
        if bn:mix(music,tone(note(bn),step*.95,'tri',.35),at)
        drum=drums[i%len(drums)]
        if drum=='k':mix(music,[math.sin(2*math.pi*(150*t/sr-55*(t/sr)**2/.09))*.3*(1-t/(sr*.09)) for t in range(int(sr*.09))],at)
        elif drum in ('s','h'):
            length=.08 if drum=='s' else .025;gain=.18 if drum=='s' else .06
            mix(music,[(random.random()*2-1)*gain*(1-t/(sr*length)) for t in range(int(sr*length))],at)
    return music[:int(sr*duration)]
tracks={'level_loop':track('level1'),'boss_loop':track('boss',cycles=4),'boost_loop':track('level1',1.15)}
frames={}
for name,music in tracks.items():
    save(name+'.wav',music);frames[name]=round(len(music)/sr*59.1856*18500/18518.5)
with wave.open(str(out/'intro.wav')) as intro:frames['intro']=round(intro.getnframes()/intro.getframerate()*59.1856*18500/18518.5)
header=out.parents[1]/'src/level1_audio.h'
header.write_text('// Generated from pinned web TRACKS at MVS video/audio clock rates.\n'+''.join('#define GNG_'+name.upper()+'_FRAMES '+str(count)+'\n' for name,count in frames.items()))
save('kick.wav',tone(170,.10,'square',.7)+tone(100,.07,'tri',.4))
save('hit.wav',tone(85,.15,'square',.8))
save('enemy_die.wav',tone(260,.06,'square',.6)+tone(180,.08,'square',.5)+tone(100,.12,'tri',.45))
save('boss_hit.wav',tone(70,.18,'square',.75)+tone(45,.16,'tri',.5))
clear=[]
for i,f in enumerate((261.63,329.63,392.00,523.25)):mix(clear,tone(f,.22,'square',.45),i*.16)
save('stage_clear.wav',clear)

# Short native cues for complete Level 1 item/score feedback.
save('pickup.wav',tone(880,.035,'tri',.5)+tone(1320,.07,'tri',.45))
save('crate.wav',[(random.random()*2-1)*.6*(1-i/(sr*.1)) for i in range(int(sr*.1))])
save('power.wav',sum((tone(f,.06,'square',.4) for f in (330,440,550,660,880,1100)),[]))
save('one_up.wav',sum((tone(f,.08,'square',.4) for f in (523,659,784,1046)),[]))
save('checkpoint.wav',sum((tone(f,.07,'tri',.5) for f in (392,523,784)),[]))
save('charge.wav',tone(440,.08,'tri',.35)+tone(880,.12,'tri',.45))
save('time_tick.wav',tone(1200,.035,'square',.3))

save('thunder.wav',[(random.random()*2-1)*.45*(1-t/(sr*.6))**2 for t in range(int(sr*.6))])
