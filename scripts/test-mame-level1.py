#!/usr/bin/env python3
"""Run the real MVS cartridge with scripted inputs; record new evidence per run."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import time
from level1_geometry import read_level

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--project', type=Path, default=root/'.ngdevkit-examples/ghostsngoal-mvs')
parser.add_argument('--reference', type=Path, default=Path(os.environ.get('GNG_UPSTREAM', root/'.ghostsngoal-upstream')))
parser.add_argument('--assist-health', action='store_true', help='Maintain invulnerability; never modify player position/physics')
parser.add_argument('--scenario', choices=('level1', 'checks', 'content', 'soak'), default='level1')
parser.add_argument('--pluginspath', type=Path)
args = parser.parse_args()
project = args.project.resolve()
subprocess.run([sys.executable, str(root/'scripts/verify-mvs-rom.py'), str(project)], check=True)
symbols = {}
for line in subprocess.check_output(['m68k-neogeo-elf-nm', '--defined-only', str(project/'build/rom.elf')], text=True).splitlines():
    parts = line.split()
    if len(parts) == 3:
        symbols[parts[2]] = int(parts[0], 16)
names = ['player_x', 'player_y', 'player_ground', 'player_x_fp', 'lives', 'frame_counter',
         'stage_clear', 'score', 'checkpoint_reached', 'boss_spawned', 'wave_mask',
         'invuln', 'camera_x', 'player_vy_fp', 'enemies', 'armored', 'balls',
         'hazards', 'player_y_fp', 'player_vx_fp', 'drop_timer', 'bios_user_mode', 'scene_palette', 'stadium_palette', 'gold_palette', 'boost_timer', 'crate_hp', 'pickups', 'ball_kind', 'ball_damage', 'ball_pierce', 'ball_hits', 'ball_counted', 'combo_count', 'combo_timer', 'next_life', 'level_time', 'time_tick', 'took_damage', 'crates_broken', 'pumpkins_collected', 'powers_collected', 'level1_complete', 'penalty_state', 'penalty_aim', 'penalty_power', 'penalty_kick', 'penalty_goals', 'penalty_bonus_score', 'clear_time_bonus', 'clear_accuracy_bonus', 'clear_nohit_bonus', 'zombie_timer', 'charge_timer', 'shot_buffer', 'shots_fired', 'shots_hit', 'death_timer', 'high_score', 'enemy_attack', 'enemy_attack_timer', 'enemy_cooldown', 'enemy_x_fp', 'enemy_y_fp']
level = read_level(args.reference.resolve())
solids = [(b['x']*16, b['row']*16, b['w']*16) for b in level['blocks']]
solids += [(g*16,192,16) for g in level['graves']]
solids += [(c['x']*16,c['row']*16,16) for c in level['crates']]
output = project/'build'/f'mame-{time.time_ns()}'
output.mkdir()
preamble = 'local sym={' + ','.join(f'{n}=0x{symbols[n]:x}' for n in names) + '}\n'
preamble += 'local solids={' + ','.join('{'+','.join(map(str,b))+'}' for b in solids) + '}\n'
preamble += f'local output={json.dumps(str(output))}\nlocal assisted_health={str(args.assist_health).lower()}\n'
script = output/'level.lua'
body = {'level1':'mame-level1.lua','checks':'mame-checks.lua','content':'mame-content.lua','soak':'mame-soak.lua'}[args.scenario]
# Real palette RAM checks catch incorrect scene-bank reuse, not just correct assets.
header=(project/'src/level1_assets.h').read_text()
sets={}
for name in ('SKY','CITY','INSIDE0','INSIDE1','STADIUM_BACK','STADIUM_BLEND1','STADIUM_BLEND2','STADIUM_BLEND3','STADIUM_FRONT','PLAYER','PLAYER_NAKED','PLAYER_GOLD','CITY_FLASH','INSIDE0_DARK','INSIDE1_DARK','INSIDE0_FLASH','INSIDE1_FLASH'):
    prefix='GNG_'+name
    first=int(re.search(r'#define '+prefix+r'_PAL_FIRST (\d+)',header)[1])
    block=re.search(prefix+r'_PALETTES\[.*?= \{(.*?)\n\};',header,re.S)[1]
    colors=[int(x,16) for x in re.findall(r'0x[0-9a-f]+',block)]
    sets[name]=(first,colors)
preamble+='local palette_sets={' + ','.join(name+'={'+str(first)+',{'+','.join(map(str,colors))+'}}' for name,(first,colors) in sets.items()) + '}\n'
preamble+='local crates={' + ','.join('{%d,%d,%d}'%(c['x']*16+8,c['row']*16,{'pumpkinGold':2,'shirt':3,'piadina':4,'seahorse':5}[c['item']]) for c in level['crates']) + '}\n'
script.write_text(preamble + (root/'scripts'/body).read_text())
command = [os.environ.get('MAME', 'mame'), 'neogeo', '-cart1', 'gngl1r1',
           '-hashpath', str(project/'build/rom'), '-rompath', str(project/'build/rom'),
           '-video', 'none', '-sound', 'none', '-nothrottle', '-noautosave', '-skip_gameinfo',
           '-seconds_to_run', '1865' if args.scenario=='soak' else '155', '-autoboot_delay', '0', '-autoboot_script', str(script)]
if args.pluginspath:
    command += ['-pluginspath', str(args.pluginspath.resolve())]
with (output/'mame.log').open('w') as stream:
    result = subprocess.run(command, cwd=output, stdout=stream, stderr=subprocess.STDOUT, timeout=480 if args.scenario=='soak' else 180)
log = (output/'mame.log').read_text()
marker = {'level1':'LEVEL1_PASS','checks':'CHECKS_PASS','content':'CONTENT_PASS','soak':'SOAK_PASS'}[args.scenario]
passed = result.returncode == 0 and marker in log and 'VALIDATION_FAILED' not in log and 'CHECK_FAIL' not in log and 'SOAK_FAIL' not in log
evidence = dict(passed=passed, scenario=args.scenario, assisted_health=args.assist_health, command=command,
                cartridge_sha256=hashlib.sha256((project/'build/rom/gngl1r1.zip').read_bytes()).hexdigest())
(output/'result.json').write_text(json.dumps(evidence, indent=2)+'\n')
print('\n'.join(line for line in log.splitlines() if line.startswith(('STATE', 'LIVES', 'STAGE_CLEAR', 'LEVEL1_', 'GAME_OVER', 'TIMEOUT', 'BOSS_PHASE', 'CHECK', 'CLEAR_BONUS', 'FRAME_CADENCE', 'SOAK', 'CONTENT', 'BONUS', 'PALETTE'))))
print(f'Evidence: {output}')
sys.exit(0 if passed else 1)
