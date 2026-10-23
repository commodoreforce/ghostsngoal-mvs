#!/usr/bin/env python3
"""Exercise each native Z80 cue and compare MAME's YM2610 recording to its WAV.

This is a direct sound-command fixture, separate from gameplay and cold intro.
No host audio substitutes for the cartridge's output.
"""
import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import wave

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(__doc__)
p.add_argument('--project',type=Path,default=root/'.ngdevkit-examples/ghostsngoal-mvs')
p.add_argument('--pluginspath',type=Path)
args=p.parse_args()
project=args.project.resolve()
output=project/'build'/f'audio-cues-{time.time_ns()}'
output.mkdir()
names=('kick','hit','enemy_die','boss_hit','stage_clear','pickup','crate','power',
       'one_up','checkpoint','charge','time_tick','thunder','level_loop','boss_loop','boost_loop')
header=(project/'build/snd_commands.h').read_text()
base=int(re.search(r'#define _SND_CMD_BASE_GENERATED (\d+)',header)[1])
commands={name:base+int(re.search(r'#define\s+SND_SFX_'+name.upper()+r'\s+\(_SND_CMD_BASE_GENERATED \+ (\d+)\)',header)[1]) for name in names}
lua='''local frame=0
local m=manager.machine.devices[':maincpu'].spaces.program
local commands={COMMANDS}
emu.register_frame_done(function()
 frame=frame+1
 if frame>=180 then
  local index=math.floor((frame-180)/90)+1
  local phase=(frame-180)%90
  if not commands[index] then manager.machine:exit();return end
  if phase==0 then m:write_u8(0x320000,3)
  elseif phase==3 then m:write_u8(0x320000,0)
  elseif phase==6 then
   if m:read_u8(0x320000)~=0x7e then print('AUDIO_CUE_FAILED ready',index);manager.machine:exit();return end
   m:write_u8(0x320000,commands[index]);print('AUDIO_CUE',index,frame)
  end
 end
end)
'''.replace('COMMANDS',','.join(str(commands[n]) for n in names))
(output/'cues.lua').write_text(lua)
command=[os.environ.get('MAME','mame'),'neogeo','-cart1','gngl1r1',
         '-hashpath',str(project/'build/rom'),'-rompath',str(project/'build/rom'),
         '-video','none','-sound','none','-nothrottle','-noautosave','-skip_gameinfo',
         '-seconds_to_run','35','-autoboot_delay','0','-autoboot_script',str(output/'cues.lua'),
         '-wavwrite',str(output/'native.wav')]
if args.pluginspath:command+=['-pluginspath',str(args.pluginspath.resolve())]
with (output/'mame.log').open('w') as stream:
    subprocess.run(command,cwd=output,stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=90)
log=(output/'mame.log').read_text()
assert 'AUDIO_CUE_FAILED' not in log
assert len(re.findall(r'AUDIO_CUE\s+\d+\s+\d+',log))==len(names)
subprocess.run(['ffmpeg','-v','error','-y','-i',str(output/'native.wav'),'-ac','1','-ar','18518',str(output/'mono.wav')],check=True)

def read(file):
    with wave.open(str(file)) as w:
        assert w.getnchannels()==1 and w.getsampwidth()==2
        data=array('h',w.readframes(w.getnframes()))
        if sys.byteorder!='little':data.byteswap()
        return data

def similarity(a,b):
    energy=sum(x*x for x in a)*sum(y*y for y in b)
    return sum(x*y for x,y in zip(a,b))/math.sqrt(max(1,energy))

recording=read(output/'mono.wav')
results={}
for i,name in enumerate(names):
    source=read(project/'assets/audio'/(name+'.wav'))[:9250]
    expected=int((186+i*90)/59.1856*18518)
    start=max(0,expected-2400);end=min(len(recording)-len(source),expected+2400)
    # Short broadband noise has a one-sample correlation peak: an eight-sample
    # lag grid can miss it entirely. Search every lag for that cue.
    stride=4 if name=='crate' else 24
    lag_step=1 if name=='crate' else 8
    coarse=max(range(start,end,lag_step),key=lambda lag:similarity(source[::stride],recording[lag:lag+len(source):stride]))
    lag=max(range(max(start,coarse-8),min(end,coarse+8)),key=lambda lag:similarity(source[::8],recording[lag:lag+len(source):8]))
    correlation=similarity(source,recording[lag:lag+len(source)])
    threshold=.35 if name in ('crate','thunder') else .65
    results[name]={'correlation':correlation,'lag_samples':lag,'passed':correlation>threshold}
    print(name,round(correlation,4),'PASS' if correlation>threshold else 'FAIL')
passed=all(result['passed'] for result in results.values())
evidence=dict(passed=passed,scenario='audio_cues',cues=results,command=command,
              cartridge_sha256=hashlib.sha256((project/'build/rom/gngl1r1.zip').read_bytes()).hexdigest())
(output/'result.json').write_text(json.dumps(evidence,indent=2)+'\n')
print('Evidence:',output)
sys.exit(0 if passed else 1)
