#!/usr/bin/env python3
"""Build the hand-authored premium opening-zone benchmark for Ghosts'n Goals MVS.

Rules:
- web runtime = source of truth for geometry/gameplay
- original reference pack = highest-quality art source when available
- Neo Geo logical space remains 256x224, projected to 320x224 (X 5:4)
- no automatic full-screen blends, no checkerboard fog, no stadium in this benchmark
"""
from __future__ import annotations
import argparse, io, json, math, zipfile
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from PIL import Image, ImageDraw

TILE=16; LOGICAL_W=256; SCREEN_W=320; SCREEN_H=224; X_NUM=5; X_DEN=4
FRAME_CELL_LOGICAL_W=64; FRAME_CELL_W=80; FRAME_CELL_H=64; CROM_BASE_TILE=256
BENCHMARK_WORLD_W=1024

SKY_PAL_FIRST=1
CITY_PAL_FIRST=49
ATMOS_PAL_FIRST=105
GROUND_PAL_FIRST=113
FG_PAL_FIRST=114
PLAYER_PAL_FIRST=128
ZOMBIE_PAL_FIRST=160
BALL_PAL_FIRST=192

@dataclass
class Asset:
    name:str; filename:str; tile_base:int; cols:int; rows:int; width:int; height:int
    logical_width:int; pal_first:int; palettes:list[list[int]]; palmap:list[int]

def align16(n): return (n+15)&~15
def project_x(n): return (n*X_NUM+X_DEN-1)//X_DEN

def neogeo_color(rgb):
    r,g,b=[c>>2 for c in rgb]
    dark=1 if ((r&1)+(g&1)+(b&1))==0 else 0
    r,g,b=r>>1,g>>1,b>>1
    lsb=((r&1)<<2)|((g&1)<<1)|(b&1)
    r,g,b=r>>1,g>>1,b>>1
    return (dark<<15)|(lsb<<12)|(r<<8)|(g<<4)|b

def padded_qpal(q):
    p=list(q.getpalette() or [])[:45]
    return p+[0]*(45-len(p))

def neutral_gif_palette():
    p=[]
    for i in range(256):
        c=int(round(i*255/15)) if i<16 else 0
        p += [c,c,c]
    return p[:768]

def project_rgba(src):
    src=src.convert('RGBA'); lw=src.width
    crop_h=min(src.height,SCREEN_H); src=src.crop((0,0,src.width,crop_h))
    pw=project_x(src.width)
    projected=src.resize((pw,crop_h),Image.Resampling.NEAREST)
    out=Image.new('RGBA',(align16(pw),SCREEN_H),(0,0,0,0)); out.alpha_composite(projected,(0,0))
    return out,lw

def quantize_grid(img,transparent,slot_first,grid_cols,grid_rows):
    rgba=img.convert('RGBA'); tw,th=rgba.width//TILE,rgba.height//TILE
    ctw=max(1,math.ceil(tw/grid_cols)); cth=max(1,math.ceil(th/grid_rows))
    out=Image.new('P',rgba.size,0); op=out.load(); pmap=[0]*(tw*th); pals=[]
    for ty0 in range(0,th,cth):
        for tx0 in range(0,tw,ctw):
            tx1,ty1=min(tw,tx0+ctw),min(th,ty0+cth)
            x0,y0,x1,y1=tx0*TILE,ty0*TILE,tx1*TILE,ty1*TILE
            crop=rgba.crop((x0,y0,x1,y1)); alpha=crop.getchannel('A')
            rgb=Image.new('RGB',crop.size,(0,0,0)); rgb.paste(crop.convert('RGB'),mask=alpha)
            q=rgb.quantize(colors=15,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
            qp=padded_qpal(q); qpx,apx=q.load(),alpha.load(); slot=slot_first+len(pals)
            if slot>255: raise ValueError(f'palette slot overflow: {slot}')
            hw=[0x8000]
            for i in range(15):
                o=i*3; hw.append(neogeo_color((qp[o],qp[o+1],qp[o+2])))
            pals.append(hw)
            for y in range(crop.height):
                for x in range(crop.width):
                    op[x0+x,y0+y]=0 if transparent and apx[x,y]<96 else int(qpx[x,y])+1
            for ty in range(ty0,ty1):
                for tx in range(tx0,tx1): pmap[ty*tw+tx]=slot
    out.putpalette(neutral_gif_palette())
    if transparent: out.info['transparency']=0
    return out,pals,pmap

def quantize_single(img,transparent,slot):
    rgba=img.convert('RGBA'); alpha=rgba.getchannel('A')
    rgb=Image.new('RGB',rgba.size,(0,0,0)); rgb.paste(rgba.convert('RGB'),mask=alpha)
    q=rgb.quantize(colors=15,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE); qp=padded_qpal(q)
    out=Image.new('P',rgba.size,0); op,qpx,apx=out.load(),q.load(),alpha.load()
    for y in range(rgba.height):
        for x in range(rgba.width): op[x,y]=0 if transparent and apx[x,y]<96 else int(qpx[x,y])+1
    out.putpalette(neutral_gif_palette())
    if transparent: out.info['transparency']=0
    hw=[0x8000]
    for i in range(15):
        o=i*3; hw.append(neogeo_color((qp[o],qp[o+1],qp[o+2])))
    return out,[hw],[slot]*((rgba.width//TILE)*(rgba.height//TILE))

def quantize_actor_frames(img, frame_count, slot_first):
    """One Neo Geo palette per animation frame. Preserves skin/kit shading far better
    than forcing the whole animation sheet through one shared 15-colour palette."""
    rgba=img.convert('RGBA'); out=Image.new('P',rgba.size,0); out.putpalette(neutral_gif_palette())
    op=out.load(); tw,th=rgba.width//TILE,rgba.height//TILE; pmap=[0]*(tw*th); pals=[]
    for fi in range(frame_count):
        x0=fi*FRAME_CELL_W; crop=rgba.crop((x0,0,x0+FRAME_CELL_W,FRAME_CELL_H)); alpha=crop.getchannel('A')
        rgb=Image.new('RGB',crop.size,(0,0,0)); rgb.paste(crop.convert('RGB'),mask=alpha)
        q=rgb.quantize(colors=15,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE); qp=padded_qpal(q)
        slot=slot_first+fi
        if slot>255: raise ValueError(f'actor palette slot overflow: {slot}')
        hw=[0x8000]
        for i in range(15):
            o=i*3; hw.append(neogeo_color((qp[o],qp[o+1],qp[o+2])))
        pals.append(hw); qpx,apx=q.load(),alpha.load()
        for y in range(FRAME_CELL_H):
            for x in range(FRAME_CELL_W): op[x0+x,y]=0 if apx[x,y]<96 else int(qpx[x,y])+1
        tx0=x0//TILE
        for ty in range(FRAME_CELL_H//TILE):
            for tx in range(FRAME_CELL_W//TILE): pmap[ty*tw+tx0+tx]=slot
    out.info['transparency']=0
    return out,pals,pmap

def save_indexed(img,path,transparent):
    path.parent.mkdir(parents=True,exist_ok=True)
    if transparent: img.save(path,format='GIF',transparency=0,optimize=False)
    else: img.save(path,format='GIF',optimize=False)

def project_sheet_frames(sheet,fw,fh,frames):
    pfw=project_x(fw); src=sheet.convert('RGBA')
    out=Image.new('RGBA',(FRAME_CELL_W*frames,FRAME_CELL_H),(0,0,0,0))
    for i in range(frames):
        f=src.crop((i*fw,0,(i+1)*fw,fh)).resize((pfw,fh),Image.Resampling.NEAREST)
        out.alpha_composite(f,(i*FRAME_CELL_W+(FRAME_CELL_W-pfw)//2,FRAME_CELL_H-fh))
    return out

def read_zip_image(pack,inner):
    if not pack or not pack.is_file(): return None
    with zipfile.ZipFile(pack) as z:
        try: data=z.read(inner)
        except KeyError: return None
    return Image.open(io.BytesIO(data)).convert('RGBA')

def clean_city_layer(sky,city):
    """Extract only grounded architecture/terrain from the Cesena master.
    Floating warm-cloud differences are deliberately discarded."""
    s=sky.convert('RGB'); c=city.convert('RGB'); sp=s.load(); cp=c.load(); w,h=s.size
    mask=[[False]*w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            sr,sg,sb=sp[x,y]; cr,cg,cb=cp[x,y]
            if max(abs(cr-sr),abs(cg-sg),abs(cb-sb))>28: mask[y][x]=True
    seen=[[False]*w for _ in range(h)]; keep=[[False]*w for _ in range(h)]
    for sy in range(h):
        for sx in range(w):
            if not mask[sy][sx] or seen[sy][sx]: continue
            q=deque([(sx,sy)]); seen[sy][sx]=True; comp=[]; maxy=sy; sumy=0
            while q:
                x,y=q.popleft(); comp.append((x,y)); maxy=max(maxy,y); sumy+=y
                for nx,ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
                    if 0<=nx<w and 0<=ny<h and mask[ny][nx] and not seen[ny][nx]:
                        seen[ny][nx]=True; q.append((nx,ny))
            if len(comp)>=18 and (maxy>=145 or (sumy//len(comp))>=132):
                for x,y in comp: keep[y][x]=True
    out=Image.new('RGBA',(w,h),(0,0,0,0)); op=out.load()
    for y in range(h):
        for x in range(w):
            if keep[y][x]:
                cr,cg,cb=cp[x,y]; op[x,y]=(cr,cg,cb,255)
    return out

def master_backgrounds(env_pack):
    sky=read_zip_image(env_pack,'ambientazione/L1/sfondo.png'); city=read_zip_image(env_pack,'ambientazione/L1/cesena.png')
    if sky is None or city is None: return None
    if sky.size!=(3168,1344) or city.size!=(3168,1344): raise ValueError(f'unexpected L1 master sizes sky={sky.size} city={city.size}')
    # Masters are exactly 6x the authored 528x224 layer. Nearest preserves the original pixel blocks.
    sky=sky.resize((528,224),Image.Resampling.NEAREST).convert('RGBA')
    city=city.resize((528,224),Image.Resampling.NEAREST).convert('RGBA')
    return sky,clean_city_layer(sky,city)

def make_atmos_from_sky(sky_src):
    """Real cloud material from the master, not generated bars/dither.
    Keep only a few lower, dark purple wisps and darken them slightly."""
    base=sky_src.convert('RGBA'); w,h=base.size; out=Image.new('RGBA',(w,h),(0,0,0,0)); bp=base.load(); op=out.load()
    for y in range(112,min(190,h)):
        for x in range(w):
            r,g,b,a=bp[x,y]
            lum=max(r,g,b)
            if a>0 and lum>24 and b>=r and b>=g and r<110:
                op[x,y]=(max(8,int(r*.58)),max(6,int(g*.58)),max(12,int(b*.68)),255)
    return out

def make_ground():
    im=Image.new('RGBA',(32,16),(42,29,20,255)); d=ImageDraw.Draw(im)
    d.rectangle((0,0,31,3),fill=(47,107,42,255)); d.rectangle((0,4,31,4),fill=(29,74,28,255))
    for x in (1,5,9,13,17,21,25,29): d.rectangle((x,0,x,1),fill=(74,154,58,255))
    d.rectangle((3,8,5,8),fill=(58,42,30,255)); d.rectangle((19,11,22,11),fill=(58,42,30,255))
    return im

def make_ball():
    im=Image.new('RGBA',(16,16),(0,0,0,0)); p=im.load()
    for y in range(4,12):
        for x in range(3,13):
            dx=(x-7.5)/1.25; dy=y-7.5
            if dx*dx+dy*dy<=14: p[x,y]=(244,244,240,255)
    for x,y in ((7,6),(9,7),(6,9),(9,10)): p[x,y]=(20,20,24,255)
    return im

def make_fg_cemetery():
    im=Image.new('RGBA',(512,224),(0,0,0,0)); d=ImageDraw.Draw(im); c=(6,4,10,255)
    # Spawn area 0..145 intentionally stays clean.
    for base in (176,392):
        for x in range(base,base+64,8):
            d.rectangle((x,198,x+1,223),fill=c); d.rectangle((x-1,194,x+2,197),fill=c); d.rectangle((x,192,x+1,193),fill=c)
        d.rectangle((base-4,204,base+68,205),fill=c)
    d.rectangle((472,180,475,223),fill=c); d.rectangle((464,188,483,191),fill=c)
    return im

def c_palette_matrix(name,palettes):
    rows=['    { '+', '.join(f'0x{v:04x}' for v in p)+' }' for p in palettes]
    return f'static const unsigned short {name}[{len(palettes)}][16] = {{\n'+',\n'.join(rows)+'\n};\n'
def c_u8_array(name,values:Iterable[int]):
    vals=list(values); lines=['    '+', '.join(str(v) for v in vals[i:i+24]) for i in range(0,len(vals),24)]
    return f'static const unsigned char {name}[{len(vals)}] = {{\n'+',\n'.join(lines)+'\n};\n'
def macro_asset(a):
    p=f'GNG_{a.name}'
    return f'#define {p}_TILE {a.tile_base}\n#define {p}_COLS {a.cols}\n#define {p}_ROWS {a.rows}\n#define {p}_W {a.width}\n#define {p}_H {a.height}\n#define {p}_LOGICAL_W {a.logical_width}\n#define {p}_PAL_FIRST {a.pal_first}\n#define {p}_PAL_COUNT {len(a.palettes)}\n'
def emit_header(path,assets,pframes,zframes):
    t='#ifndef GNG_GFX_ASSETS_H\n#define GNG_GFX_ASSETS_H\n\n'
    t+=f'#define GNG_LOGICAL_W {LOGICAL_W}\n#define GNG_SCREEN_W {SCREEN_W}\n#define GNG_SCREEN_H {SCREEN_H}\n#define GNG_X_NUM {X_NUM}\n#define GNG_X_DEN {X_DEN}\n#define GNG_BENCHMARK_WORLD_W {BENCHMARK_WORLD_W}\n\n'
    for a in assets: t+=macro_asset(a)+'\n'+c_palette_matrix(f'GNG_{a.name}_PALETTES',a.palettes)+'\n'+c_u8_array(f'GNG_{a.name}_PALMAP',a.palmap)+'\n'
    t+=f'#define GNG_PLAYER_FRAME_COUNT {len(pframes)}\n'
    for i,n in enumerate(pframes): t+=f'#define GNG_PLAYER_FRAME_{n.upper()} {i}\n'
    t+=f'#define GNG_ZOMBIE_FRAME_COUNT {len(zframes)}\n'
    for i,n in enumerate(zframes): t+=f'#define GNG_ZOMBIE_FRAME_{n.upper()} {i}\n'
    t+=f'#define GNG_ACTOR_COLS {FRAME_CELL_W//TILE}\n#define GNG_ACTOR_ROWS {FRAME_CELL_H//TILE}\n#endif\n'; path.write_text(t)

def build(upstream,target,env_pack=None,sprite_pack=None):
    public=upstream/'game'/'public'; bgs=public/'backgrounds'; sprites=public/'sprites'
    ad=target/'assets'; sd=target/'src'; ad.mkdir(parents=True,exist_ok=True); sd.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((sprites/'manifest.json').read_text()); enemies=json.loads((sprites/'enemies.json').read_text()); sh,zo=manifest['shpendi'],enemies['zombie']
    cursor=CROM_BASE_TILE; built=[]
    def add(name,fn,rgba,lw,transparent,pfirst,grid=None,actor_frames=None):
        nonlocal cursor
        if rgba.width%16 or rgba.height%16: raise ValueError(f'{name} not tile aligned: {rgba.size}')
        if actor_frames is not None: indexed,pals,pmap=quantize_actor_frames(rgba,actor_frames,pfirst)
        elif grid is None: indexed,pals,pmap=quantize_single(rgba,transparent,pfirst)
        else: indexed,pals,pmap=quantize_grid(rgba,transparent,pfirst,*grid)
        save_indexed(indexed,ad/fn,transparent); cols,rows=rgba.width//16,rgba.height//16
        a=Asset(name,fn,cursor,cols,rows,rgba.width,rgba.height,lw,pfirst,pals,pmap); built.append(a); cursor+=cols*rows
    masters=master_backgrounds(env_pack)
    if masters:
        sky_src,city_src=masters; print(f'Using HAND-CLEANED original L1 environment masters from {env_pack}')
    else:
        sky_src=Image.open(bgs/'l1_sky.png').convert('RGBA'); city_src=Image.open(bgs/'l1_cesena.png').convert('RGBA'); print('Original environment pack not found: using web runtime layers')
    atmos_src=make_atmos_from_sky(sky_src)
    sky,sky_lw=project_rgba(sky_src); city,city_lw=project_rgba(city_src); atmos,at_lw=project_rgba(atmos_src); fg,fg_lw=project_rgba(make_fg_cemetery())
    player=project_sheet_frames(Image.open(sprites/'pl_shpendi_base.png'),int(sh['frameWidth']),int(sh['frameHeight']),len(sh['frames']))
    zombie=project_sheet_frames(Image.open(sprites/f"{zo['sheet']}.png"),int(zo['frameWidth']),int(zo['frameHeight']),len(zo['frames']))
    add('SKY','gng_sky.gif',sky,sky_lw,False,SKY_PAL_FIRST,(12,4))
    add('ATMOS','gng_atmos.gif',atmos,at_lw,True,ATMOS_PAL_FIRST,(8,1))
    add('CITY','gng_city.gif',city,city_lw,True,CITY_PAL_FIRST,(12,4))
    add('GROUND','gng_ground.gif',make_ground(),32,False,GROUND_PAL_FIRST,None)
    add('FG_CEMETERY','gng_fg_cemetery.gif',fg,fg_lw,True,FG_PAL_FIRST,None)
    add('PLAYER','gng_shpendi.gif',player,FRAME_CELL_LOGICAL_W*len(sh['frames']),True,PLAYER_PAL_FIRST,actor_frames=len(sh['frames']))
    add('ZOMBIE','gng_zombie.gif',zombie,FRAME_CELL_LOGICAL_W*len(zo['frames']),True,ZOMBIE_PAL_FIRST,actor_frames=len(zo['frames']))
    add('BALL','gng_ball.gif',make_ball(),13,True,BALL_PAL_FIRST,None)
    emit_header(sd/'gfx_assets.h',built,list(sh['frames']),list(zo['frames']))
    print('Generated LAST-CHANCE HAND-AUTHORED opening zone:')
    for a in built: print(f'  {a.name:12s} physical={a.width:4d}x{a.height:<3d} logicalW={a.logical_width:4d} palettes={len(a.palettes):2d}')
    print('  city extraction: connected grounded architecture only; floating cloud artifacts discarded')
    print('  actors: one dedicated 15-colour palette per animation frame')
    print('  atmosphere: real master-cloud pixels, behind actors; no bars, no checkerboard')

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('upstream',type=Path); ap.add_argument('target',type=Path); ap.add_argument('--environment-pack',type=Path,default=None); ap.add_argument('--sprite-pack',type=Path,default=None)
    a=ap.parse_args(); build(a.upstream.resolve(),a.target.resolve(),a.environment_pack.resolve() if a.environment_pack else None,a.sprite_pack.resolve() if a.sprite_pack else None)
if __name__=='__main__': main()
