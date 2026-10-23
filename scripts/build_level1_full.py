#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
BASE_PATH = HERE / "build-mvs-visual-assets.py"
spec = importlib.util.spec_from_file_location("gng_asset_base", BASE_PATH)
base = importlib.util.module_from_spec(spec)
assert spec and spec.loader
sys.modules[spec.name] = base
spec.loader.exec_module(base)

TILE = 16
LOGICAL_W = 256
SCREEN_W = 320
SCREEN_H = 224
WORLD_W = 262 * 16  # exact Level 1 world: 4192 logical px
X_NUM, X_DEN = 5, 4
CROM_BASE_TILE = 256

GROUND_SPANS = [(0,34),(36,58),(60,96),(98,152),(155,196),(199,262)]

# Exact foreground motif from the web buildTiles/buildBackground implementation.
def cemetery_motif() -> Image.Image:
    W = 512
    im = Image.new("RGBA", (W, 224), (0,0,0,0))
    d = ImageDraw.Draw(im)
    c = (0x06,0x04,0x0a,255)
    for x in range(60,120,8):
        d.rectangle((x,194,x+1,223), fill=c)
        d.rectangle((x-1,190,x+2,193), fill=c)
        d.rectangle((x,188,x+1,189), fill=c)
    d.rectangle((56,200,121,201), fill=c)
    for k in range(7):
        x=330+k*5; y=200-(k%3)*8
        d.rectangle((x,y,x+1,223), fill=c)
        x2=326+k*6; y2=196-(k%2)*6
        d.rectangle((x2,y2,x2+5,y2+1), fill=c)
    d.rectangle((440,178,443,223), fill=c)
    d.rectangle((432,186,451,189), fill=c)
    return im


def make_world_foreground() -> Image.Image:
    motif = cemetery_motif()
    out = Image.new("RGBA", (WORLD_W,224), (0,0,0,0))
    # Phaser tileSprite repeats the 512px motif. Bake that repetition once into
    # the world so the MVS renderer never wraps or creates scenery on the fly.
    for x in range(0,WORLD_W,512):
        out.alpha_composite(motif,(x,0))
    return out


def make_world_ground() -> Image.Image:
    out = Image.new("RGBA", (WORLD_W,32), (0,0,0,0))
    d = ImageDraw.Draw(out)
    for a,b in GROUND_SPANS:
        x0,x1=a*16,b*16-1
        d.rectangle((x0,0,x1,15), fill=(0x2a,0x1d,0x14,255))
        d.rectangle((x0,0,x1,3), fill=(0x2f,0x6b,0x2a,255))
        d.rectangle((x0,4,x1,4), fill=(0x1d,0x4a,0x1c,255))
        for x in range(x0+1,x1+1,4):
            d.rectangle((x,0,x,1), fill=(0x4a,0x9a,0x3a,255))
        d.rectangle((x0,16,x1,31), fill=(0x2a,0x1d,0x14,255))
        for x in range(x0+19,x1+1,37):
            d.rectangle((x,20,min(x+2,x1),20), fill=(0x3a,0x2a,0x1e,255))
            if x+7 <= x1: d.rectangle((x+7,27,x+8,27), fill=(0x3a,0x2a,0x1e,255))
    return out


def fit_background(path: Path, width=528, height=224) -> Image.Image:
    im = Image.open(path).convert("RGBA")
    # Runtime backgrounds are already the final web art. Preserve alpha and
    # pixel shapes, only normalize their logical canvas to the web benchmark.
    if im.size != (width,height):
        im = im.resize((width,height), Image.Resampling.NEAREST)
    return im


def frame_sheet(src: Image.Image, fw:int, fh:int, count:int):
    pfw = math.ceil(fw * X_NUM / X_DEN)
    cell_w = ((pfw + 15)//16)*16
    cell_h = ((fh + 15)//16)*16
    out = Image.new("RGBA", (cell_w*count,cell_h), (0,0,0,0))
    for i in range(count):
        f = src.crop((i*fw,0,(i+1)*fw,fh)).convert("RGBA")
        f = base.clean_frame_components(f)
        f = f.resize((pfw,fh), Image.Resampling.NEAREST)
        out.alpha_composite(f,(i*cell_w+(cell_w-pfw)//2,cell_h-fh))
    return out, cell_w, cell_h


def quantize_frames(img:Image.Image, count:int, cell_w:int, cell_h:int, slot_first:int):
    # One independent 15-colour palette for every 32px-high band of every frame.
    bands=max(1,math.ceil(cell_h/32))
    out=Image.new("P",img.size,0); out.putpalette(base.neutral_gif_palette())
    op=out.load(); pmap=[0]*((img.width//16)*(img.height//16)); palettes=[]
    tw=img.width//16
    for fi in range(count):
        x0=fi*cell_w
        for band in range(bands):
            y0=band*32; y1=min(cell_h,y0+32)
            crop=img.crop((x0,y0,x0+cell_w,y1))
            # pad to 32 high for tile mapping if last band is shorter
            pad=Image.new("RGBA",(cell_w,32),(0,0,0,0)); pad.alpha_composite(crop,(0,0))
            q,alpha,hw=base.quantize_region(pad,True)
            slot=slot_first+len(palettes)
            if slot>255: raise ValueError(f"palette overflow at frame {fi}")
            palettes.append(hw); qpx=q.load(); apx=alpha.load()
            for y in range(y1-y0):
                for x in range(cell_w):
                    op[x0+x,y0+y]=0 if apx[x,y]<96 else int(qpx[x,y])+1
            tx0=x0//16
            for ty in range(y0//16, math.ceil(y1/16)):
                for tx in range(cell_w//16):
                    pmap[ty*tw+tx0+tx]=slot
    out.info['transparency']=0
    return out,palettes,pmap


def c_pal(name,pals):
    rows=["    { "+", ".join(f"0x{x:04x}" for x in p)+" }" for p in pals]
    return f"static const unsigned short {name}[{len(pals)}][16] = {{\n"+",\n".join(rows)+"\n};\n"

def c_u8(name,vals):
    vals=list(vals); lines=["    "+", ".join(str(v) for v in vals[i:i+24]) for i in range(0,len(vals),24)]
    return f"static const unsigned char {name}[{len(vals)}] = {{\n"+",\n".join(lines)+"\n};\n"


def build(upstream:Path,target:Path):
    public=upstream/'game'/'public'; bgs=public/'backgrounds'; spr=public/'sprites'
    ad=target/'assets'; sd=target/'src'; ad.mkdir(parents=True,exist_ok=True); sd.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((spr/'manifest.json').read_text()); enemies=json.loads((spr/'enemies.json').read_text())

    # Final web background layers: no master reconstruction here.
    raw_assets=[]
    for name,fn,transparent,grid in [
        ('SKY','l1_sky.png',False,(16,4)),
        ('CITY','l1_cesena.png',True,(12,4)),
        ('STADIUM_BACK','l1_stadio_retro.png',True,(12,4)),
        ('STADIUM_FRONT','l1_stadio_ingresso.png',True,(12,4)),
        ('INSIDE0','l1_inside0.png',False,(16,4)),
        ('INSIDE1','l1_inside1.png',False,(16,4)),
    ]:
        raw_assets.append((name,fit_background(bgs/fn),transparent,grid,None))

    raw_assets.append(('GROUND',make_world_ground(),True,(24,2),None))
    raw_assets.append(('FG_CEMETERY',make_world_foreground(),True,(24,4),None))

    actor_defs=[
        ('PLAYER',spr/'pl_shpendi_base.png',manifest['shpendi']),
        ('ZOMBIE',spr/f"{enemies['zombie']['sheet']}.png",enemies['zombie']),
        ('BAT',spr/f"{enemies['bat']['sheet']}.png",enemies['bat']),
        ('PUMPKIN',spr/f"{enemies['pumpkin']['sheet']}.png",enemies['pumpkin']),
        ('GHOST',spr/f"{enemies['ghost']['sheet']}.png",enemies['ghost']),
        ('MAZ',spr/f"{enemies['mazapegul']['sheet']}.png",enemies['mazapegul']),
    ]
    actor_meta={}
    for name,path,meta in actor_defs:
        im=Image.open(path).convert('RGBA'); count=len(meta['frames']); fw=int(meta['frameWidth']); fh=int(meta['frameHeight'])
        sheet,cw,ch=frame_sheet(im,fw,fh,count)
        actor_meta[name]=(meta,sheet,cw,ch)
        raw_assets.append((name,sheet,True,None,(count,cw,ch)))

    # Ball projectile.
    raw_assets.append(('BALL',base.make_ball(),True,None,None))

    tile_cursor=CROM_BASE_TILE; pal_cursor=1; built=[]
    class A: pass
    for name,rgba,transparent,grid,actor in raw_assets:
        # Project backgrounds/world layers here. Actor sheets are already projected per frame.
        if actor is None and name!='BALL': rgba,lw=base.project_rgba(rgba)
        elif name=='BALL': lw=13
        else: lw=(actor[1]*X_DEN//X_NUM)*actor[0]
        if rgba.width%16 or rgba.height%16:
            pad=Image.new('RGBA',(((rgba.width+15)//16)*16,((rgba.height+15)//16)*16),(0,0,0,0)); pad.alpha_composite(rgba,(0,0)); rgba=pad
        if actor:
            indexed,pals,pmap=quantize_frames(rgba,actor[0],actor[1],actor[2],pal_cursor)
        elif grid:
            indexed,pals,pmap=base.quantize_grid(rgba,transparent,pal_cursor,*grid)
        else:
            indexed,pals,pmap=base.quantize_single(rgba,transparent,pal_cursor)
        if pal_cursor+len(pals)>256: raise ValueError(f"palette budget exceeded at {name}: {pal_cursor}+{len(pals)}")
        fn='gng_'+name.lower()+'.gif'; base.save_indexed(indexed,ad/fn,transparent)
        a=A(); a.name=name; a.filename=fn; a.tile_base=tile_cursor; a.cols=rgba.width//16; a.rows=rgba.height//16; a.width=rgba.width; a.height=rgba.height; a.logical_width=lw; a.pal_first=pal_cursor; a.palettes=pals; a.palmap=pmap
        built.append(a); tile_cursor+=a.cols*a.rows; pal_cursor+=len(pals)

    # Emit self-contained header.
    h=['#ifndef GNG_LEVEL1_ASSETS_H','#define GNG_LEVEL1_ASSETS_H','',f'#define GNG_LOGICAL_W {LOGICAL_W}',f'#define GNG_SCREEN_W {SCREEN_W}',f'#define GNG_SCREEN_H {SCREEN_H}',f'#define GNG_LEVEL1_WORLD_W {WORLD_W}',f'#define GNG_X_NUM {X_NUM}',f'#define GNG_X_DEN {X_DEN}','']
    for a in built:
        p='GNG_'+a.name
        h += [f'#define {p}_TILE {a.tile_base}',f'#define {p}_COLS {a.cols}',f'#define {p}_ROWS {a.rows}',f'#define {p}_W {a.width}',f'#define {p}_H {a.height}',f'#define {p}_PAL_FIRST {a.pal_first}',f'#define {p}_PAL_COUNT {len(a.palettes)}',c_pal(p+'_PALETTES',a.palettes),c_u8(p+'_PALMAP',a.palmap)]
    for name,(meta,sheet,cw,ch) in actor_meta.items():
        h += [f'#define GNG_{name}_FRAME_COUNT {len(meta["frames"])}',f'#define GNG_{name}_CELL_W {cw}',f'#define GNG_{name}_CELL_H {ch}',f'#define GNG_{name}_FRAME_COLS {cw//16}',f'#define GNG_{name}_FRAME_ROWS {ch//16}']
        for i,n in enumerate(meta['frames']): h.append(f'#define GNG_{name}_FRAME_{n.upper()} {i}')
        h.append('')
    h.append('#endif')
    (sd/'level1_assets.h').write_text('\n'.join(h)+'\n')

    print('Generated FULL LEVEL 1 MVS assets:')
    for a in built: print(f'  {a.name:14s} {a.width}x{a.height} palettes={len(a.palettes)} tiles={a.cols*a.rows}')
    print(f'  world={WORLD_W} logical px, exact ground spans={GROUND_SPANS}')
    print(f'  palette slots used={pal_cursor-1}/255')
    print('  enemies: zombie, bat, pumpkin, ghost, Mazapegul boss')


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('upstream',type=Path); ap.add_argument('target',type=Path); a=ap.parse_args()
    build(a.upstream.resolve(),a.target.resolve())
if __name__=='__main__': main()
