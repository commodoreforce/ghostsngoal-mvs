#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path
from PIL import Image, ImageDraw
from level1_geometry import read_level, scenery, c_geometry

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
WORLD_W = 262 * 16
X_NUM, X_DEN = 5, 4
CROM_BASE_TILE = 256
GROUND_SPANS = [(0,34),(36,58),(60,96),(98,152),(155,196),(199,262)]

def make_world_ground() -> Image.Image:
    out=Image.new("RGBA",(WORLD_W,32),(0,0,0,0)); d=ImageDraw.Draw(out)
    for a,b in GROUND_SPANS:
        x0,x1=a*16,b*16-1
        d.rectangle((x0,0,x1,15),fill=(0x2a,0x1d,0x14,255)); d.rectangle((x0,0,x1,3),fill=(0x2f,0x6b,0x2a,255)); d.rectangle((x0,4,x1,4),fill=(0x1d,0x4a,0x1c,255))
        for x in range(x0+1,x1+1,4): d.rectangle((x,0,x,1),fill=(0x4a,0x9a,0x3a,255))
        d.rectangle((x0,16,x1,31),fill=(0x2a,0x1d,0x14,255))
        for x in range(x0+19,x1+1,37):
            d.rectangle((x,20,min(x+2,x1),20),fill=(0x3a,0x2a,0x1e,255))
            if x+7<=x1:d.rectangle((x+7,27,x+8,27),fill=(0x3a,0x2a,0x1e,255))
    return out

def fit_background(path:Path,width=528,height=224)->Image.Image:
    im=Image.open(path).convert("RGBA")
    return im if im.size==(width,height) else im.resize((width,height),Image.Resampling.NEAREST)

def frame_sheet(src:Image.Image,fw:int,fh:int,count:int):
    pfw=math.ceil(fw*X_NUM/X_DEN); cell_w=((pfw+15)//16)*16; cell_h=((fh+15)//16)*16
    out=Image.new("RGBA",(cell_w*count,cell_h),(0,0,0,0))
    for i in range(count):
        f=src.crop((i*fw,0,(i+1)*fw,fh)).convert("RGBA"); f=base.clean_frame_components(f); f=f.resize((pfw,fh),Image.Resampling.NEAREST)
        out.alpha_composite(f,(i*cell_w+(cell_w-pfw)//2,cell_h-fh))
    return out,cell_w,cell_h

def quantize_frames(img:Image.Image,count:int,cell_w:int,cell_h:int,slot_first:int):
    bands=max(1,math.ceil(cell_h/32)); out=Image.new("P",img.size,0); out.putpalette(base.neutral_gif_palette()); op=out.load(); pmap=[0]*((img.width//16)*(img.height//16)); palettes=[]; tw=img.width//16
    for fi in range(count):
        x0=fi*cell_w
        for band in range(bands):
            y0=band*32; y1=min(cell_h,y0+32); crop=img.crop((x0,y0,x0+cell_w,y1)); pad=Image.new("RGBA",(cell_w,32),(0,0,0,0)); pad.alpha_composite(crop,(0,0))
            q,alpha,hw=base.quantize_region(pad,True); slot=slot_first+len(palettes)
            if slot>255:raise ValueError(f"palette overflow at frame {fi}")
            palettes.append(hw); qpx=q.load(); apx=alpha.load()
            for y in range(y1-y0):
                for x in range(cell_w):op[x0+x,y0+y]=0 if apx[x,y]<96 else int(qpx[x,y])+1
            tx0=x0//16
            for ty in range(y0//16,math.ceil(y1/16)):
                for tx in range(cell_w//16):pmap[ty*tw+tx0+tx]=slot
    out.info['transparency']=0; return out,palettes,pmap

def c_pal(name,pals):
    rows=["    { "+", ".join(f"0x{x:04x}" for x in p)+" }" for p in pals]; return f"static const unsigned short {name}[{len(pals)}][16] = {{\n"+",\n".join(rows)+"\n};\n"
def c_u8(name,vals):
    vals=list(vals); lines=["    "+", ".join(str(v) for v in vals[i:i+24]) for i in range(0,len(vals),24)]; return f"static const unsigned char {name}[{len(vals)}] = {{\n"+",\n".join(lines)+"\n};\n"

def build(upstream:Path,target:Path):
    public=upstream/'game'/'public'; bgs=public/'backgrounds'; spr=public/'sprites'; ad=target/'assets'; sd=target/'src'; ad.mkdir(parents=True,exist_ok=True); sd.mkdir(parents=True,exist_ok=True)
    level=read_level(upstream)
    manifest=json.loads((spr/'manifest.json').read_text()); enemies=json.loads((spr/'enemies.json').read_text())
    raw=[]
    # 8 regional palettes per painted backdrop: enough colour depth without exhausting 256 hardware slots.
    for name,fn,transparent in [('SKY','l1_sky.png',False),('CITY','l1_cesena.png',True),('STADIUM_BACK','l1_stadio_retro.png',True),('STADIUM_FRONT','l1_stadio_ingresso.png',True),('INSIDE0','l1_inside0.png',False),('INSIDE1','l1_inside1.png',False)]:
        raw.append((name,fit_background(bgs/fn),transparent,(4,2),None))
    # World layers use tiny fixed palettes: they are deliberately authored in only a few colours.
    raw.append(('GROUND',make_world_ground(),True,None,None)); raw.append(('FG_CEMETERY',scenery(level,WORLD_W),True,(8,2),None))
    actor_defs=[('PLAYER',spr/'pl_shpendi_base.png',manifest['shpendi']),('PLAYER_NAKED',spr/'pl_shpendi_naked.png',manifest['shpendi']),('ZOMBIE',spr/f"{enemies['zombie']['sheet']}.png",enemies['zombie']),('BAT',spr/f"{enemies['bat']['sheet']}.png",enemies['bat']),('PUMPKIN',spr/f"{enemies['pumpkin']['sheet']}.png",enemies['pumpkin']),('GHOST',spr/f"{enemies['ghost']['sheet']}.png",enemies['ghost']),('MAZ',spr/f"{enemies['mazapegul']['sheet']}.png",enemies['mazapegul'])]
    actor_meta={}
    for name,path,meta in actor_defs:
        im=Image.open(path).convert('RGBA'); count=len(meta['frames']); fw=int(meta['frameWidth']); fh=int(meta['frameHeight']); sheet,cw,ch=frame_sheet(im,fw,fh,count); actor_meta[name]=(meta,sheet,cw,ch); raw.append((name,sheet,True,None,(count,cw,ch)))
    raw.append(('BALL',base.make_ball(),True,None,None))
    # Native equivalents of the procedural web mazfx/shoe and wave textures.
    shoe=Image.new('RGBA',(18,24));ImageDraw.Draw(shoe).rectangle((3,6,14,17),fill='#8a8aa6')
    wave=Image.new('RGBA',(10,22));wd=ImageDraw.Draw(wave)
    for k in range(3):wd.rectangle((k*3,2+k*2,k*3+1,19-k*2),fill='#ffffff' if k==0 else '#9fe8ff')
    raw.append(('SHOE',shoe,True,None,None));raw.append(('WAVE',wave,True,None,None))
    tile_cursor=CROM_BASE_TILE; pal_cursor=3; # FIX text owns palettes 0, 1 and 2.
    built=[]
    class A:pass
    for name,rgba,transparent,grid,actor in raw:
        if actor is None and name!='BALL':rgba,lw=base.project_rgba(rgba)
        elif name=='BALL':lw=13
        else:lw=(actor[1]*X_DEN//X_NUM)*actor[0]
        if rgba.width%16 or rgba.height%16:
            pad=Image.new('RGBA',(((rgba.width+15)//16)*16,((rgba.height+15)//16)*16),(0,0,0,0)); pad.alpha_composite(rgba,(0,0)); rgba=pad
        if actor:indexed,pals,pmap=quantize_frames(rgba,actor[0],actor[1],actor[2],pal_cursor)
        elif grid:indexed,pals,pmap=base.quantize_grid(rgba,transparent,pal_cursor,*grid)
        else:indexed,pals,pmap=base.quantize_single(rgba,transparent,pal_cursor)
        if pal_cursor+len(pals)>256:raise ValueError(f"palette budget exceeded at {name}: {pal_cursor}+{len(pals)}")
        fn='gng_'+name.lower()+'.gif'; base.save_indexed(indexed,ad/fn,transparent); a=A();a.name=name;a.filename=fn;a.tile_base=tile_cursor;a.cols=rgba.width//16;a.rows=rgba.height//16;a.width=rgba.width;a.height=rgba.height;a.logical_width=lw;a.pal_first=pal_cursor;a.palettes=pals;a.palmap=pmap;built.append(a);tile_cursor+=a.cols*a.rows;pal_cursor+=len(pals)
    h=['#ifndef GNG_LEVEL1_ASSETS_H','#define GNG_LEVEL1_ASSETS_H','',f'#define GNG_LOGICAL_W {LOGICAL_W}',f'#define GNG_SCREEN_W {SCREEN_W}',f'#define GNG_SCREEN_H {SCREEN_H}',f'#define GNG_LEVEL1_WORLD_W {WORLD_W}',f'#define GNG_X_NUM {X_NUM}',f'#define GNG_X_DEN {X_DEN}','']
    for a in built:
        p='GNG_'+a.name;h += [f'#define {p}_TILE {a.tile_base}',f'#define {p}_COLS {a.cols}',f'#define {p}_ROWS {a.rows}',f'#define {p}_W {a.width}',f'#define {p}_H {a.height}',f'#define {p}_PAL_FIRST {a.pal_first}',f'#define {p}_PAL_COUNT {len(a.palettes)}',c_pal(p+'_PALETTES',a.palettes),c_u8(p+'_PALMAP',a.palmap)]
    for name,(meta,sheet,cw,ch) in actor_meta.items():
        h += [f'#define GNG_{name}_FRAME_COUNT {len(meta["frames"])}',f'#define GNG_{name}_CELL_W {cw}',f'#define GNG_{name}_CELL_H {ch}',f'#define GNG_{name}_FRAME_COLS {cw//16}',f'#define GNG_{name}_FRAME_ROWS {ch//16}']
        for i,n in enumerate(meta['frames']):h.append(f'#define GNG_{name}_FRAME_{n.upper()} {i}')
        h.append('')
    h.extend(c_geometry(level));h.append('#endif');(sd/'level1_assets.h').write_text('\n'.join(h)+'\n')
    print('Generated FULL LEVEL 1 MVS assets:')
    for a in built:print(f'  {a.name:14s} {a.width}x{a.height} palettes={len(a.palettes)} tiles={a.cols*a.rows}')
    print(f'  world={WORLD_W} logical px, palette slots used={pal_cursor-1}/255');print('  exact ground spans + zombie/bat/pumpkin/ghost/Mazapegul assets')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('upstream',type=Path);ap.add_argument('target',type=Path);a=ap.parse_args();build(a.upstream.resolve(),a.target.resolve())
if __name__=='__main__':main()
