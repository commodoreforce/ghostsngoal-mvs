#!/usr/bin/env python3
"""Run the real MVS cartridge with scripted inputs; record new evidence per run."""
import argparse
import hashlib
import json
import os
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
parser.add_argument('--scenario', choices=('level1', 'checks'), default='level1')
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
         'hazards', 'player_y_fp', 'player_vx_fp', 'drop_timer', 'bios_user_mode']
level = read_level(args.reference.resolve())
solids = [(b['x']*16, b['row']*16, b['w']*16) for b in level['blocks']]
solids += [(g*16,192,16) for g in level['graves']]
output = project/'build'/f'mame-{time.time_ns()}'
output.mkdir()
preamble = 'local sym={' + ','.join(f'{n}=0x{symbols[n]:x}' for n in names) + '}\n'
preamble += 'local solids={' + ','.join('{'+','.join(map(str,b))+'}' for b in solids) + '}\n'
preamble += f'local output={json.dumps(str(output))}\nlocal assisted_health={str(args.assist_health).lower()}\n'
script = output/'level.lua'
body = 'mame-level1.lua' if args.scenario == 'level1' else 'mame-checks.lua'
script.write_text(preamble + (root/'scripts'/body).read_text())
command = [os.environ.get('MAME', 'mame'), 'neogeo', '-cart1', 'gngl1r1',
           '-hashpath', str(project/'build/rom'), '-rompath', str(project/'build/rom'),
           '-video', 'none', '-sound', 'none', '-nothrottle', '-noautosave', '-skip_gameinfo',
           '-seconds_to_run', '155', '-autoboot_delay', '0', '-autoboot_script', str(script)]
if args.pluginspath:
    command += ['-pluginspath', str(args.pluginspath.resolve())]
with (output/'mame.log').open('w') as stream:
    result = subprocess.run(command, cwd=output, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
log = (output/'mame.log').read_text()
marker = 'LEVEL1_PASS' if args.scenario == 'level1' else 'CHECKS_PASS'
passed = result.returncode == 0 and marker in log and 'VALIDATION_FAILED' not in log and 'CHECK_FAIL' not in log
evidence = dict(passed=passed, scenario=args.scenario, assisted_health=args.assist_health, command=command,
                cartridge_sha256=hashlib.sha256((project/'build/rom/gngl1r1.zip').read_bytes()).hexdigest())
(output/'result.json').write_text(json.dumps(evidence, indent=2)+'\n')
print('\n'.join(line for line in log.splitlines() if line.startswith(('STATE', 'LIVES', 'STAGE_CLEAR', 'LEVEL1_', 'GAME_OVER', 'TIMEOUT', 'BOSS_PHASE', 'CHECK'))))
print(f'Evidence: {output}')
sys.exit(0 if passed else 1)
