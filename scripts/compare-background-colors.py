#!/usr/bin/env python3
"""Compare converted palette/tile data against the pinned source artwork.

RGB reconstruction uses the packed Neo Geo six-bit approximation, not a CRT
simulation. Use the same decoder for both revisions; MAME remains the runtime
check. --baseline expects GIFs and level1_assets.h from the older build.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from PIL import Image


def converted(directory, name):
    header = (directory/'level1_assets.h').read_text()
    prefix = 'GNG_'+name
    first = int(re.search(r'#define '+prefix+r'_PAL_FIRST (\d+)', header)[1])
    block = re.search(prefix+r'_PALETTES\[.*?= \{(.*?)\n\};', header, re.S)[1]
    values = [int(x, 16) for x in re.findall(r'0x[0-9a-f]+', block)]
    colors = []
    for v in values:
        dark = 0 if v & 0x8000 else 1
        colors.append(tuple((((v >> shift) & 15)*4+((v >> low) & 1)*2+dark)*255//63
                            for shift, low in ((8, 14), (4, 13), (0, 12))))
    block = re.search(prefix+r'_PALMAP\[.*?= \{(.*?)\n\};', header, re.S)[1]
    mapping = list(map(int, re.findall(r'\d+', block)))
    tiles = Image.open(directory/('gng_'+name.lower()+'.gif'))
    rgb = Image.new('RGB', tiles.size)
    for y in range(tiles.height):
        for x in range(tiles.width):
            slot = mapping[(y//16)*(tiles.width//16)+x//16]-first
            rgb.putpixel((x, y), colors[slot*16+tiles.getpixel((x, y))])
    return rgb, len(values)//16


def compare(source, rendered):
    source = source.resize(((source.width*5+3)//4, source.height), Image.Resampling.NEAREST)
    error = samples = 0
    distinct = set()
    for y in range(source.height):
        for x in range(source.width):
            pixel = source.getpixel((x, y))
            if pixel[3] < 96:
                continue
            rgb = rendered.getpixel((x, y))
            error += sum((a-b)**2 for a, b in zip(pixel[:3], rgb))
            distinct.add(rgb)
            samples += 3
    return dict(rgb_mse=round(error/samples, 3), visible_colors=len(distinct))


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--current', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    bg = args.reference/'game/public/backgrounds'
    sky = Image.open(bg/'l1_sky.png').convert('RGBA')
    city = Image.open(bg/'l1_cesena.png').convert('RGBA')
    outdoor = sky.copy()
    outdoor.alpha_composite(city)
    sources = {'CITY': outdoor}
    for name in ('STADIUM_BACK', 'STADIUM_FRONT'):
        file = 'l1_stadio_retro.png' if name.endswith('BACK') else 'l1_stadio_ingresso.png'
        sources[name] = Image.open(bg/file).convert('RGBA')
    for name in ('INSIDE0', 'INSIDE1'):
        inside = Image.open(bg/('l1_'+name.lower()+'.png')).convert('RGBA')
        composite = sky.resize(inside.size, Image.Resampling.NEAREST)
        composite.alpha_composite(city, (20, -30))
        composite.alpha_composite(inside)
        sources[name] = composite
    report = {'decoder': 'packed Neo Geo six-bit RGB approximation', 'assets': {}}
    for name, source in sources.items():
        old, old_count = converted(args.baseline, name)
        new, new_count = converted(args.current, name)
        before, after = compare(source, old), compare(source, new)
        before['palettes'] = old_count
        after['palettes'] = new_count
        report['assets'][name] = {'revision2': before, 'revision3': after}
        if after['rgb_mse'] >= before['rgb_mse']:
            raise ValueError(f'{name}: conversion did not improve source colour error')
    report['baseline_header_sha256'] = hashlib.sha256((args.baseline/'level1_assets.h').read_bytes()).hexdigest()
    report['current_header_sha256'] = hashlib.sha256((args.current/'level1_assets.h').read_bytes()).hexdigest()
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
