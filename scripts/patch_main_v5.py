#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_main_v5.py <main.c>")

p = Path(sys.argv[1])
s = p.read_text()
repls = {
    '(camera_x_fp*3)/100,0);': '(camera_x_fp*7)/100,0);',
    'GNG_GROUND_PALMAP,camera_x_fp,160);': 'GNG_GROUND_PALMAP,camera_x_fp,208);',
    'GNG_FG_CEMETERY_PALMAP,camera_x_fp,0);': 'GNG_FG_CEMETERY_PALMAP,(camera_x_fp*135)/100,0);',
    'PREMIUM OPENING V4': 'PREMIUM OPENING V5',
    'OPENING ZONE V4': 'OPENING ZONE V5',
}
for old, new in repls.items():
    if old not in s:
        raise SystemExit(f'expected V4 renderer fragment not found: {old}')
    s = s.replace(old, new, 1)
p.write_text(s)
print('Patched renderer for V5: sky 7%, ground@208, foreground 135%, V5 labels')
