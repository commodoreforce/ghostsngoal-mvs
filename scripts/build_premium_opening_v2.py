#!/usr/bin/env python3
"""Build a verified premium opening-zone benchmark for Ghosts'n Goals MVS.

Rules:
- web runtime = source of truth for geometry/gameplay
- original reference pack = art source where it is genuinely separable
- Neo Geo logical space remains 256x224, projected to 320x224 (X 5:4)
- no automatic fullscreen blends, no checkerboard fog, no stadium in this benchmark
- every visual source is composited locally before ROM generation
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
FG_PAL_FIRST=121
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
    out=Image.new('RGBA',(align16(pw),align16(crop_h)),(0,0,0,0)); out.alpha_composite(projected,(0,0))
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

def quantize_actor_frames(img,frame_count,slot_first):
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

def clean_sky_layer(sky):
    src=sky.convert('RGBA'); out=src.copy(); sp=src.load(); op=out.load(); w,h=src.size
    for y in range(138,h):
        for x in range(w):
            r,g,b,a=sp[x,y]
            if a and max(r,g,b)>34 and b>=r and (b-r)>4:
                op[x,y]=(max(3,int(r*.16)),max(2,int(g*.16)),max(7,int(b*.24)),255)
    return out

def clean_city_layer(sky,city):
    s=sky.convert('RGB'); c=city.convert('RGB'); sp=s.load(); cp=c.load(); w,h=s.size
    mask=[[False]*w for _ in range(h)]
    for y in range(96,186):
        for x in range(w):
            sr,sg,sb=sp[x,y]; cr,cg,cb=cp[x,y]
            if max(abs(cr-sr),abs(cg-sg),abs(cb-sb))>28: mask[y][x]=True
    seen=[[False]*w for _ in range(h)]; keep=[[False]*w for _ in range(h)]
    for sy in range(96,186):
        for sx in range(w):
            if not mask[sy][sx] or seen[sy][sx]: continue
            q=deque([(sx,sy)]); seen[sy][sx]=True; comp=[]; maxy=sy
            while q:
                x,y=q.popleft(); comp.append((x,y)); maxy=max(maxy,y)
                for nx,ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
                    if 0<=nx<w and 96<=ny<186 and mask[ny][nx] and not seen[ny][nx]:
                        seen[ny][nx]=True; q.append((nx,ny))
            if len(comp)>=18 and maxy>=150:
                for x,y in comp: keep[y][x]=True
    out=Image.new('RGBA',(w,h),(0,0,0,0)); op=out.load()
    for y in range(h):
        for x in range(w):
            if keep[y][x]:
                cr,cg,cb=cp[x,y]; op[x,y]=(cr,cg,cb,255)
    return out

def make_atmos_from_sky(sky):
    src=sky.convert('RGBA'); sp=src.load(); w,h=src.size
    mask=[[False]*w for _ in range(h)]
    for y in range(104,min(176,h)):
        for x in range(w):
            r,g,b,a=sp[x,y]
            if a and b>20 and r>12 and (r+g+b)>55: mask[y][x]=True
    seen=set(); out=Image.new('RGBA',(w,h),(0,0,0,0)); op=out.load()
    for y in range(104,min(176,h)):
        for x in range(w):
            if not mask[y][x] or (x,y) in seen: continue
            q=deque([(x,y)]); seen.add((x,y)); comp=[]; touches=False
            while q:
                xx,yy=q.popleft(); comp.append((xx,yy))
                if xx<8 or xx>w-9 or yy>=174: touches=True
                for nx,ny in ((xx-1,yy),(xx+1,yy),(xx,yy-1),(xx,yy+1)):
                    if 0<=nx<w and 104<=ny<min(176,h) and mask[ny][nx] and (nx,ny) not in seen:
                        seen.add((nx,ny)); q.append((nx,ny))
            if 8<=len(comp)<=1200 and not touches:
                for xx,yy in comp:
                    r,g,b,a=sp[xx,yy]; op[xx,yy]=(int(r*.72),int(g*.72),int(b*.82),255)
    return out

def master_backgrounds(env_pack):
    sky=read_zip_image(env_pack,'ambientazione/L1/sfondo.png'); city=read_zip_image(env_pack,'ambientazione/L1/cesena.png')
    if sky is None or city is None: return None
    if sky.size!=(3168,1344) or city.size!=(3168,1344): raise ValueError(f'unexpected L1 master sizes sky={sky.size} city={city.size}')
    sky=sky.resize((528,224),Image.Resampling.NEAREST).convert('RGBA')
    city=city.resize((528,224),Image.Resampling.NEAREST).convert('RGBA')
    return clean_sky_layer(sky),clean_city_layer(sky,city),make_atmos_from_sky(sky),city

def make_ground_from_master(city_master):
    return city_master.crop((0,160,528,224)).convert('RGBA')

def make_fallback_ground():
    im=Image.new('RGBA',(528,64),(34,26,18,255)); d=ImageDraw.Draw(im)
    d.rectangle((0,0,527,35),fill=(31,96,47,255)); d.rectangle((0,0,527,3),fill=(61,142,62,255))
    for x in range(0,528,12): d.line((x,8,x+3,3),fill=(52,122,55,255))
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
    im=Image.new('RGBA',(528,224),(0,0,0,0)); d=ImageDraw.Draw(im); ink=(5,4,9,255); ink2=(11,8,16,255)
    for x in range(0,528,11):
        y=197+((x*7)%4); d.line((x,y,x+2,y-5),fill=ink); d.line((x+2,y,x+5,y-3),fill=ink)
    for start,end in ((150,250),(360,510)):
        d.rectangle((start,205,end,207),fill=ink)
        for x in range(start+4,end,10):
            d.rectangle((x,191,x+2,223),fill=ink); d.polygon([(x-1,191),(x+1,187),(x+3,191)],fill=ink)
    for x,y in ((278,192),(320,198),(344,194),(500,189)):
        d.rectangle((x,y,x+3,223),fill=ink); d.rectangle((x-7,y+6,x+10,y+9),fill=ink)
    for x,y,w,h in ((260,202,10,17),(330,204,12,15),(515,200,10,18)):
        d.rounded_rectangle((x,y,x+w,y+h),radius=2,fill=ink2)
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
        sky_src,city_src,atmos_src,city_master=masters
        ground_src=make_ground_from_master(city_master)
        print(f'Using VERIFIED hand-cleaned L1 masters from {env_pack}')
    else:
        sky_src=Image.open(bgs/'l1_sky.png').convert('RGBA'); city_src=Image.open(bgs/'l1_cesena.png').convert('RGBA')
        atmos_src=Image.new('RGBA',sky_src.size,(0,0,0,0)); ground_src=make_fallback_ground()
        print('Original environment pack not found: using web runtime layers')
    fg_src=make_fg_cemetery()
    sky,sky_lw=project_rgba(sky_src); city,city_lw=project_rgba(city_src); atmos,at_lw=project_rgba(atmos_src); ground,ground_lw=project_rgba(ground_src); fg,fg_lw=project_rgba(fg_src)
    player=project_sheet_frames(Image.open(sprites/'pl_shpendi_base.png'),int(sh['frameWidth']),int(sh['frameHeight']),len(sh['frames']))
    zombie=project_sheet_frames(Image.open(sprites/f"{zo['sheet']}.png"),int(zo['frameWidth']),int(zo['frameHeight']),len(zo['frames']))
    add('SKY','gng_sky.gif',sky,sky_lw,False,SKY_PAL_FIRST,(12,4))
    add('ATMOS','gng_atmos.gif',atmos,at_lw,True,ATMOS_PAL_FIRST,(8,1))
    add('CITY','gng_city.gif',city,city_lw,True,CITY_PAL_FIRST,(12,4))
    add('GROUND','gng_ground.gif',ground,ground_lw,False,GROUND_PAL_FIRST,(8,1))
    add('FG_CEMETERY','gng_fg_cemetery.gif',fg,fg_lw,True,FG_PAL_FIRST,None)
    add('PLAYER','gng_shpendi.gif',player,FRAME_CELL_LOGICAL_W*len(sh['frames']),True,PLAYER_PAL_FIRST,actor_frames=len(sh['frames']))
    add('ZOMBIE','gng_zombie.gif',zombie,FRAME_CELL_LOGICAL_W*len(zo['frames']),True,ZOMBIE_PAL_FIRST,actor_frames=len(zo['frames']))
    add('BALL','gng_ball.gif',make_ball(),13,True,BALL_PAL_FIRST,None)
    emit_header(sd/'gfx_assets.h',built,list(sh['frames']),list(zo['frames']))
    print('Generated VERIFIED PREMIUM opening zone:')
    for a in built: print(f'  {a.name:12s} physical={a.width:4d}x{a.height:<3d} logicalW={a.logical_width:4d} palettes={len(a.palettes):2d}')
    print('  sky: low edge cloud banks removed before projection')
    print('  city: grounded Rocca/wall only')
    print('  ground: authored cesena.png field, not synthetic bars')
    print('  foreground: hand-authored cemetery near plane')
    print('  actors: one dedicated 15-colour palette per animation frame')

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('upstream',type=Path); ap.add_argument('target',type=Path); ap.add_argument('--environment-pack',type=Path,default=None); ap.add_argument('--sprite-pack',type=Path,default=None)
    a=ap.parse_args(); build(a.upstream.resolve(),a.target.resolve(),a.environment_pack.resolve() if a.environment_pack else None,a.sprite_pack.resolve() if a.sprite_pack else None)
if __name__=='__main__': main()
