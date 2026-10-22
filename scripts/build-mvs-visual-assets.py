#!/usr/bin/env python3
"""Build the premium opening-zone benchmark for Ghosts'n Goals MVS.

Goal: one beautiful, playable first scene before attempting the whole level.
The web game remains the visual source of truth. 256x224 logical space is
projected to Neo Geo 320x224 using X 5:4 and Y 1:1.

Phase 1 deliberately contains only:
  SKY / CITY / ATMOS / GROUND / FG_CEMETERY / PLAYER / ZOMBIE / BALL
No stadium transition, no beams, no full-screen dither overlay.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw

TILE = 16
LOGICAL_W = 256
SCREEN_W = 320
SCREEN_H = 224
X_NUM = 5
X_DEN = 4
FRAME_CELL_LOGICAL_W = 64
FRAME_CELL_W = 80
FRAME_CELL_H = 64
CROM_BASE_TILE = 256
BENCHMARK_WORLD_W = 1024

# Palette plan: generous budget for painted art, compact actors/effects.
SKY_PAL_FIRST = 1
CITY_PAL_FIRST = 65
ATMOS_PAL_FIRST = 129
GROUND_PAL_FIRST = 137
FG_PAL_FIRST = 138
PLAYER_PAL_FIRST = 139
ZOMBIE_PAL_FIRST = 140
BALL_PAL_FIRST = 141


@dataclass
class Asset:
    name: str
    filename: str
    tile_base: int
    cols: int
    rows: int
    width: int
    height: int
    logical_width: int
    pal_first: int
    palettes: list[list[int]]
    palmap: list[int]


def align16(n: int) -> int:
    return (n + 15) & ~15


def project_x(n: int) -> int:
    return (n * X_NUM + X_DEN - 1) // X_DEN


def neogeo_color(rgb: tuple[int, int, int]) -> int:
    r, g, b = [c >> 2 for c in rgb]
    darkbit = 1 if ((r & 1) + (g & 1) + (b & 1)) == 0 else 0
    r, g, b = r >> 1, g >> 1, b >> 1
    lsb = ((r & 1) << 2) | ((g & 1) << 1) | (b & 1)
    r, g, b = r >> 1, g >> 1, b >> 1
    return (darkbit << 15) | (lsb << 12) | (r << 8) | (g << 4) | b


def padded_qpal(q: Image.Image) -> list[int]:
    p = list(q.getpalette() or [])[:45]
    p += [0] * (45 - len(p))
    return p


def neutral_gif_palette() -> list[int]:
    p: list[int] = []
    for i in range(256):
        c = int(round(i * 255 / 15)) if i < 16 else 0
        p += [c, c, c]
    return p[:768]


def project_rgba(src: Image.Image) -> tuple[Image.Image, int]:
    src = src.convert("RGBA")
    logical_w = src.width
    crop_h = min(src.height, SCREEN_H)
    src = src.crop((0, 0, src.width, crop_h))
    pw = project_x(src.width)
    projected = src.resize((pw, crop_h), Image.Resampling.NEAREST)
    out = Image.new("RGBA", (align16(pw), SCREEN_H), (0, 0, 0, 0))
    out.alpha_composite(projected, (0, 0))
    return out, logical_w


def project_file(path: Path) -> tuple[Image.Image, int]:
    return project_rgba(Image.open(path))


def quantize_grid(img: Image.Image, transparent: bool, slot_first: int,
                  grid_cols: int, grid_rows: int) -> tuple[Image.Image, list[list[int]], list[int]]:
    rgba = img.convert("RGBA")
    tw, th = rgba.width // TILE, rgba.height // TILE
    chunk_tw = max(1, math.ceil(tw / grid_cols))
    chunk_th = max(1, math.ceil(th / grid_rows))
    out = Image.new("P", rgba.size, 0)
    out_px = out.load()
    palmap = [0] * (tw * th)
    palettes: list[list[int]] = []

    for ty0 in range(0, th, chunk_th):
        for tx0 in range(0, tw, chunk_tw):
            tx1, ty1 = min(tw, tx0 + chunk_tw), min(th, ty0 + chunk_th)
            x0, y0, x1, y1 = tx0*TILE, ty0*TILE, tx1*TILE, ty1*TILE
            crop = rgba.crop((x0, y0, x1, y1))
            alpha = crop.getchannel("A")
            rgb = Image.new("RGB", crop.size, (0, 0, 0))
            rgb.paste(crop.convert("RGB"), mask=alpha)
            q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            qpal = padded_qpal(q)
            qpx, apx = q.load(), alpha.load()
            slot = slot_first + len(palettes)
            if slot > 255:
                raise ValueError(f"palette slot overflow: {slot}")
            hw = [0x8000]
            for i in range(15):
                off = i*3
                hw.append(neogeo_color((qpal[off], qpal[off+1], qpal[off+2])))
            palettes.append(hw)
            for y in range(crop.height):
                for x in range(crop.width):
                    if transparent and apx[x, y] < 96:
                        out_px[x0+x, y0+y] = 0
                    else:
                        out_px[x0+x, y0+y] = int(qpx[x, y]) + 1
            for ty in range(ty0, ty1):
                for tx in range(tx0, tx1):
                    palmap[ty*tw + tx] = slot

    out.putpalette(neutral_gif_palette())
    if transparent: out.info["transparency"] = 0
    return out, palettes, palmap


def quantize_single(img: Image.Image, transparent: bool, slot: int):
    rgba = img.convert("RGBA")
    alpha = rgba.getchannel("A")
    rgb = Image.new("RGB", rgba.size, (0,0,0)); rgb.paste(rgba.convert("RGB"), mask=alpha)
    q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    qpal = padded_qpal(q)
    out = Image.new("P", rgba.size, 0); opx, qpx, apx = out.load(), q.load(), alpha.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            opx[x,y] = 0 if transparent and apx[x,y] < 96 else int(qpx[x,y]) + 1
    out.putpalette(neutral_gif_palette())
    if transparent: out.info["transparency"] = 0
    hw = [0x8000]
    for i in range(15):
        off=i*3; hw.append(neogeo_color((qpal[off],qpal[off+1],qpal[off+2])))
    cols, rows = rgba.width//TILE, rgba.height//TILE
    return out, [hw], [slot]*(cols*rows)


def save_indexed(img: Image.Image, path: Path, transparent: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if transparent: img.save(path, format="GIF", transparency=0, optimize=False)
    else: img.save(path, format="GIF", optimize=False)


def project_sheet_frames(sheet: Image.Image, fw: int, fh: int, frames: int) -> Image.Image:
    pfw = project_x(fw)
    if pfw > FRAME_CELL_W or fh > FRAME_CELL_H:
        raise ValueError(f"projected frame {pfw}x{fh} exceeds cell")
    src = sheet.convert("RGBA")
    out = Image.new("RGBA", (FRAME_CELL_W*frames, FRAME_CELL_H), (0,0,0,0))
    for i in range(frames):
        f = src.crop((i*fw,0,(i+1)*fw,fh)).resize((pfw,fh), Image.Resampling.NEAREST)
        out.alpha_composite(f, (i*FRAME_CELL_W + (FRAME_CELL_W-pfw)//2, FRAME_CELL_H-fh))
    return out


def make_ground() -> Image.Image:
    # Web grass/dirt language, but one clean 32x16 repeating tile.
    im = Image.new("RGBA", (32,16), (42,29,20,255)); d=ImageDraw.Draw(im)
    d.rectangle((0,0,31,3), fill=(47,107,42,255))
    d.rectangle((0,4,31,4), fill=(29,74,28,255))
    for x in (1,5,9,13,17,21,25,29): d.rectangle((x,0,x,1), fill=(74,154,58,255))
    d.rectangle((3,8,5,8), fill=(58,42,30,255)); d.rectangle((19,11,22,11), fill=(58,42,30,255))
    return im


def make_ball() -> Image.Image:
    im=Image.new("RGBA",(16,16),(0,0,0,0)); px=im.load()
    for y in range(4,12):
        for x in range(3,13):
            dx=(x-7.5)/1.25; dy=y-7.5
            if dx*dx+dy*dy<=14: px[x,y]=(244,244,240,255)
    for x,y in ((7,6),(9,7),(6,9),(9,10)): px[x,y]=(20,20,24,255)
    return im


def make_atmos() -> Image.Image:
    """Opaque low-contrast mist wisps on transparent background.

    No checkerboard/dither and no white pixels. This layer is rendered BEHIND
    actors. It is intentionally subtle so silhouettes remain readable.
    """
    w,h=512,224
    im=Image.new("RGBA",(w,h),(0,0,0,0)); d=ImageDraw.Draw(im)
    dark=(49,40,73,255); mid=(66,54,91,255); hi=(82,68,108,255)
    # Horizontal nocturnal haze banks, all above ground and below the skyline.
    wisps=[(10,132,150,8,dark),(82,146,220,7,mid),(250,126,430,8,dark),
           (330,151,505,6,mid),(0,166,110,5,hi),(175,174,360,5,dark)]
    for x0,y0,x1,hh,c in wisps:
        # pixel-art taper: 3 nested bars, no alpha blending required
        d.rectangle((x0+8,y0,x1-8,y0+hh-1), fill=c)
        d.rectangle((x0+22,y0-2,x1-22,y0-1), fill=c)
        d.rectangle((x0+30,y0+hh,x1-30,y0+hh+1), fill=c)
    # sparse tiny stars/haze glints, dark lavender only
    for x,y in ((72,119),(184,137),(292,116),(406,142),(470,121)):
        d.point((x,y), fill=hi)
    return im


def make_fg_cemetery() -> Image.Image:
    """Clean near silhouettes. No solid band across the player."""
    w=512; im=Image.new("RGBA",(w,224),(0,0,0,0)); d=ImageDraw.Draw(im); c=(6,4,10,255)
    # Short iron fence segments with intentional gaps.
    for base in (44, 360):
        for x in range(base, base+72, 8):
            d.rectangle((x,198,x+1,223),fill=c)
            d.rectangle((x-1,194,x+2,197),fill=c)
            d.rectangle((x,192,x+1,193),fill=c)
        d.rectangle((base-4,204,base+76,205),fill=c)
    # Dry shrub and crooked cross, positioned away from starting player.
    for k in range(6):
        x=270+k*6; d.rectangle((x,202-(k%3)*7,x+1,223),fill=c)
        d.rectangle((x-4,198-(k%2)*6,x+4,199-(k%2)*6),fill=c)
    d.rectangle((470,180,473,223),fill=c); d.rectangle((462,188,481,191),fill=c)
    return im


def c_palette_matrix(name: str, palettes: list[list[int]]) -> str:
    rows=["    { "+", ".join(f"0x{v:04x}" for v in p)+" }" for p in palettes]
    return f"static const unsigned short {name}[{len(palettes)}][16] = {{\n"+",\n".join(rows)+"\n};\n"


def c_u8_array(name: str, values: Iterable[int]) -> str:
    vals=list(values); lines=["    "+", ".join(str(v) for v in vals[i:i+24]) for i in range(0,len(vals),24)]
    return f"static const unsigned char {name}[{len(vals)}] = {{\n"+",\n".join(lines)+"\n};\n"


def macro_asset(a: Asset) -> str:
    p=f"GNG_{a.name}"
    return (f"#define {p}_TILE {a.tile_base}\n#define {p}_COLS {a.cols}\n#define {p}_ROWS {a.rows}\n"
            f"#define {p}_W {a.width}\n#define {p}_H {a.height}\n#define {p}_LOGICAL_W {a.logical_width}\n"
            f"#define {p}_PAL_FIRST {a.pal_first}\n#define {p}_PAL_COUNT {len(a.palettes)}\n")


def emit_header(path: Path, assets: list[Asset], player_frames: list[str], zombie_frames: list[str]) -> None:
    text="#ifndef GNG_GFX_ASSETS_H\n#define GNG_GFX_ASSETS_H\n\n"
    text += "/* Premium opening-zone benchmark. */\n"
    text += f"#define GNG_LOGICAL_W {LOGICAL_W}\n#define GNG_SCREEN_W {SCREEN_W}\n#define GNG_SCREEN_H {SCREEN_H}\n"
    text += f"#define GNG_X_NUM {X_NUM}\n#define GNG_X_DEN {X_DEN}\n#define GNG_BENCHMARK_WORLD_W {BENCHMARK_WORLD_W}\n\n"
    for a in assets:
        text += macro_asset(a)+"\n"+c_palette_matrix(f"GNG_{a.name}_PALETTES",a.palettes)+"\n"+c_u8_array(f"GNG_{a.name}_PALMAP",a.palmap)+"\n"
    text += f"#define GNG_PLAYER_FRAME_COUNT {len(player_frames)}\n"
    for i,n in enumerate(player_frames): text += f"#define GNG_PLAYER_FRAME_{n.upper()} {i}\n"
    text += f"#define GNG_ZOMBIE_FRAME_COUNT {len(zombie_frames)}\n"
    for i,n in enumerate(zombie_frames): text += f"#define GNG_ZOMBIE_FRAME_{n.upper()} {i}\n"
    text += f"#define GNG_ACTOR_COLS {FRAME_CELL_W//TILE}\n#define GNG_ACTOR_ROWS {FRAME_CELL_H//TILE}\n#endif\n"
    path.write_text(text,encoding="utf-8")


def build(upstream: Path, target: Path) -> None:
    public=upstream/"game"/"public"; bgs=public/"backgrounds"; sprites=public/"sprites"
    ad=target/"assets"; sd=target/"src"; ad.mkdir(parents=True,exist_ok=True); sd.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((sprites/"manifest.json").read_text()); enemies=json.loads((sprites/"enemies.json").read_text())
    sh,zo=manifest["shpendi"],enemies["zombie"]; tile_cursor=CROM_BASE_TILE; built:list[Asset]=[]

    def add(name,fn,rgba,lw,transparent,pfirst,grid=None):
        nonlocal tile_cursor
        if rgba.width%16 or rgba.height%16: raise ValueError(f"{name} not tile aligned {rgba.size}")
        indexed,pals,pmap=(quantize_single(rgba,transparent,pfirst) if grid is None else quantize_grid(rgba,transparent,pfirst,*grid))
        save_indexed(indexed,ad/fn,transparent); cols,rows=rgba.width//16,rgba.height//16
        a=Asset(name,fn,tile_cursor,cols,rows,rgba.width,rgba.height,lw,pfirst,pals,pmap); built.append(a); tile_cursor += cols*rows; return a

    sky,sky_lw=project_file(bgs/"l1_sky.png")
    city,city_lw=project_file(bgs/"l1_cesena.png")
    atmos,at_lw=project_rgba(make_atmos())
    fg,fg_lw=project_rgba(make_fg_cemetery())
    player=project_sheet_frames(Image.open(sprites/"pl_shpendi_base.png"),int(sh["frameWidth"]),int(sh["frameHeight"]),len(sh["frames"]))
    zombie=project_sheet_frames(Image.open(sprites/f"{zo['sheet']}.png"),int(zo["frameWidth"]),int(zo["frameHeight"]),len(zo["frames"]))

    add("SKY","gng_sky.gif",sky,sky_lw,False,SKY_PAL_FIRST,(16,4))
    add("CITY","gng_city.gif",city,city_lw,True,CITY_PAL_FIRST,(16,4))
    add("ATMOS","gng_atmos.gif",atmos,at_lw,True,ATMOS_PAL_FIRST,(8,1))
    add("GROUND","gng_ground.gif",make_ground(),32,False,GROUND_PAL_FIRST,None)
    add("FG_CEMETERY","gng_fg_cemetery.gif",fg,fg_lw,True,FG_PAL_FIRST,None)
    add("PLAYER","gng_shpendi.gif",player,FRAME_CELL_LOGICAL_W*len(sh["frames"]),True,PLAYER_PAL_FIRST,None)
    add("ZOMBIE","gng_zombie.gif",zombie,FRAME_CELL_LOGICAL_W*len(zo["frames"]),True,ZOMBIE_PAL_FIRST,None)
    add("BALL","gng_ball.gif",make_ball(),13,True,BALL_PAL_FIRST,None)
    emit_header(sd/"gfx_assets.h",built,list(sh["frames"]),list(zo["frames"]))

    print("Generated PREMIUM OPENING-ZONE assets:")
    for a in built: print(f"  {a.name:12s} physical={a.width:4d}x{a.height:<3d} logicalW={a.logical_width:4d} palettes={len(a.palettes):2d}")
    print("  NO stadium / NO beams / NO fullscreen dither")
    print("  atmosphere renders behind actors; foreground has deliberate player gaps")
    print("  geometry: 256x224 logical -> 320x224 physical (X 5:4)")


def main():
    ap=argparse.ArgumentParser(); ap.add_argument("upstream",type=Path); ap.add_argument("target",type=Path)
    a=ap.parse_args(); build(a.upstream.resolve(),a.target.resolve())

if __name__=="__main__": main()
