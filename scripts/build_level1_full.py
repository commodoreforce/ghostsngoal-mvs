#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageEnhance
from level1_geometry import read_level, scenery, c_geometry
from level1_item_art import item_atlas, score_atlas, checkpoint_flag

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
    # Web pit bands and broken turf edges remain visible instead of transparent holes.
    for (_,end),(start,_) in zip(GROUND_SPANS,GROUND_SPANS[1:]):
        x0,x1=end*16,start*16
        for k,color in enumerate(('#1a1030','#120a22','#0a0614','#050308','#000000')):
            d.rectangle((x0,k*4,x1-1,31),fill=color)
        d.rectangle((x0,0,x0+1,31),fill='#3a2412');d.rectangle((x1-2,0,x1-1,31),fill='#3a2412')
        d.rectangle((x0,0,x0+2,1),fill='#5a8a2a');d.rectangle((x1-3,0,x1-1,1),fill='#5a8a2a')
        d.line((x0+1,2,x0+1,4),fill='#5a8a2a');d.line((x1-2,2,x1-2,4),fill='#5a8a2a')
    return out

def fit_background(path:Path,width=528,height=224)->Image.Image:
    im=Image.open(path).convert("RGBA")
    if im.height != height:
        raise ValueError(f"Unexpected background height: {path}: {im.size}")
    # The stadium interior is 998 px wide, not 528. Never squash its composition.
    return im

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

def opaque_quantize_region(crop, transparent):
    """Allocate all 15 colours to visible pixels, never to the transparent canvas."""
    rgba=crop.convert('RGBA');alpha=rgba.getchannel('A');rgb=rgba.convert('RGB')
    pixels=[c[:3] for c in rgba.getdata() if c[3]>=96] if transparent else list(rgb.getdata())
    sample=Image.new('RGB',(max(1,len(pixels)),1))
    if pixels:sample.putdata(pixels)
    palette=sample.quantize(colors=15,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    colors=base.padded_qpal(palette)
    palette.putpalette(colors+colors[:3]*241)
    q=rgb.quantize(palette=palette,dither=Image.Dither.NONE)
    return q,alpha,[0x8000]+[base.neogeo_color(tuple(colors[i:i+3])) for i in range(0,45,3)]

# The grid and frame quantizers in the imported module use this implementation.
base.quantize_region=opaque_quantize_region


def quantize_animation(img,count,cw,ch,first):
    """One stable palette per 32px band across a complete enemy animation.

    Palette coherence prevents colour flicker between poses and frees the scene
    banks for finer background colour regions. Player frame conversion is kept.
    """
    out=Image.new('P',img.size,0);out.putpalette(base.neutral_gif_palette())
    pmap=[0]*((img.width//16)*(img.height//16));palettes=[]
    for y0 in range(0,ch,32):
        y1=min(ch,y0+32);q,alpha,pal=opaque_quantize_region(img.crop((0,y0,img.width,y1)),True)
        palettes.append(pal);slot=first+len(palettes)-1
        for y in range(y1-y0):
            for x in range(img.width):out.putpixel((x,y+y0),0 if alpha.getpixel((x,y))<96 else q.getpixel((x,y))+1)
        for ty in range(y0//16,math.ceil(y1/16)):
            for tx in range(img.width//16):pmap[ty*(img.width//16)+tx]=slot
    out.info['transparency']=0
    return out,palettes,pmap

def quantize_background_pair(first_image,second_image,slot_first,grid_cols,grid_rows):
    """Keep both crowd drawings on identical palettes, avoiding 1344 palette
    writes on every beat. Only actual tile graphics change at runtime."""
    width,height=first_image.size;tw=width//16;th=height//16
    ctw=math.ceil(tw/grid_cols);cth=math.ceil(th/grid_rows)
    outputs=[Image.new('P',(width,height),0) for _ in range(2)]
    for image in outputs:image.putpalette(base.neutral_gif_palette())
    palettes=[];pmap=[0]*(tw*th)
    for ty in range(0,th,cth):
        for tx in range(0,tw,ctw):
            box=(tx*16,ty*16,min(tw,tx+ctw)*16,min(th,ty+cth)*16)
            w,h=box[2]-box[0],box[3]-box[1]
            pair=Image.new('RGBA',(w,h*2));pair.paste(first_image.crop(box),(0,0));pair.paste(second_image.crop(box),(0,h))
            q,alpha,palette=opaque_quantize_region(pair,False);slot=slot_first+len(palettes);palettes.append(palette)
            for variant in range(2):
                for y in range(h):
                    for x in range(w):outputs[variant].putpixel((box[0]+x,box[1]+y),q.getpixel((x,variant*h+y))+1)
            for row in range(ty,min(th,ty+cth)):
                for col in range(tx,min(tw,tx+ctw)):pmap[row*tw+col]=slot
    return outputs,palettes,pmap

def c_pal(name,pals):
    rows=["    { "+", ".join(f"0x{x:04x}" for x in p)+" }" for p in pals]; return f"static const unsigned short {name}[{len(pals)}][16] = {{\n"+",\n".join(rows)+"\n};\n"
def c_u8(name,vals):
    vals=list(vals); lines=["    "+", ".join(str(v) for v in vals[i:i+24]) for i in range(0,len(vals),24)]; return f"static const unsigned char {name}[{len(vals)}] = {{\n"+",\n".join(lines)+"\n};\n"

def build(upstream:Path,target:Path):
    public=upstream/'game'/'public'; bgs=public/'backgrounds'; spr=public/'sprites'; ad=target/'assets'; sd=target/'src'; ad.mkdir(parents=True,exist_ok=True); sd.mkdir(parents=True,exist_ok=True)
    level=read_level(upstream)
    manifest=json.loads((spr/'manifest.json').read_text()); enemies=json.loads((spr/'enemies.json').read_text())
    raw=[]
    sky=fit_background(bgs/'l1_sky.png')
    city=fit_background(bgs/'l1_cesena.png')
    outdoor=sky.copy();outdoor.alpha_composite(city)
    raw.append(('SKY',sky,False,(8,7),None))
    raw.append(('CITY',outdoor,False,(8,7),None))
    def lightning(image):
        tint=Image.new('RGBA',image.size,(200,192,255,72));result=image.copy();result.alpha_composite(tint);return result
    raw.append(('CITY_FLASH',lightning(outdoor),False,(8,7),None))
    back=fit_background(bgs/'l1_stadio_retro.png')
    front=fit_background(bgs/'l1_stadio_ingresso.png')
    raw.append(('STADIUM_BACK',back,True,(8,7),None))
    raw.append(('STADIUM_FRONT',front,True,(8,7),None))
    # Ordered dissolve replaces architecture in place, without a black rectangle.
    order=((0,8,2,10),(12,4,14,6),(3,11,1,9),(15,7,13,5))
    for step in range(1,4):
        mixed=back.copy();mp=mixed.load();fp=front.load()
        for y in range(mixed.height):
            for x in range(mixed.width):
                if order[y%4][x%4]<step*4:mp[x,y]=fp[x,y]
        raw.append((f'STADIUM_BLEND{step}',mixed,True,(8,7),None))
    for name in ('INSIDE0','INSIDE1'):
        inside=fit_background(bgs/(name.lower().replace('inside','l1_inside')+'.png'))
        # Preserve transparent roof openings: sky and the Rocca remain behind them.
        composite=sky.resize(inside.size,Image.Resampling.NEAREST)
        composite.alpha_composite(city,(20,-30));composite.alpha_composite(inside)
        raw.append((name,composite,False,(12,7),None))
        raw.append((name+'_DARK',ImageEnhance.Brightness(composite).enhance(.32),False,(12,7),None))
        raw.append((name+'_FLASH',lightning(composite),False,(12,7),None))
    for i in range(4):raw.append((f'PEN_BG{i}',fit_background(bgs/f'pen_bg{i}.png'),False,(8,7),None))
    # World layers use tiny fixed palettes: they are deliberately authored in only a few colours.
    raw.append(('GROUND',make_world_ground(),True,(4,1),None)); raw.append(('FG_CEMETERY',scenery(level,WORLD_W),True,(8,2),None))
    actor_defs=[('PLAYER',spr/'pl_shpendi_base.png',manifest['shpendi']),('PLAYER_NAKED',spr/'pl_shpendi_naked.png',manifest['shpendi']),('PLAYER_GOLD',spr/'pl_shpendi_gold.png',manifest['shpendi']),('ZOMBIE',spr/f"{enemies['zombie']['sheet']}.png",enemies['zombie']),('BAT',spr/f"{enemies['bat']['sheet']}.png",enemies['bat']),('PUMPKIN',spr/f"{enemies['pumpkin']['sheet']}.png",enemies['pumpkin']),('GHOST',spr/f"{enemies['ghost']['sheet']}.png",enemies['ghost']),('MAZ',spr/f"{enemies['mazapegul']['sheet']}.png",enemies['mazapegul']),('PEN_KEEPER',spr/'en_pk_ghostkeeper.png',enemies['pk_ghostkeeper']),('PEN_KICKER',spr/'en_pk_shpendi.png',enemies['pk_shpendi'])]
    actor_meta={}
    for name,path,meta in actor_defs:
        im=Image.open(path).convert('RGBA');meta=dict(meta);meta['frames']=list(meta['frames']);fw=int(meta['frameWidth']);fh=int(meta['frameHeight'])
        if name=='GHOST':
            expanded=Image.new('RGBA',(im.width+fw*2,fh));expanded.alpha_composite(im)
            for fi in range(2):
                faded=im.crop((fi*fw,0,(fi+1)*fw,fh))
                for y in range(fh):
                    for x in range(fw):
                        if (x//2+3*(y//2))%4:faded.putpixel((x,y),(0,0,0,0))
                expanded.alpha_composite(faded,(im.width+fi*fw,0))
            im=expanded;meta['frames']+=['fade0','fade1']
        count=len(meta['frames']);sheet,cw,ch=frame_sheet(im,fw,fh,count); actor_meta[name]=(meta,sheet,cw,ch); raw.append((name,sheet,True,None,(count,cw,ch)))
    ball=base.make_ball();spin=Image.new('RGBA',(64,16))
    # Spin the leather patches in logical space; keep the ball's silhouette fixed.
    patches=[(x,y) for y in range(16) for x in range(16) if ball.getpixel((x,y))==(20,20,24,255)]
    for i in range(4):
        frame=ball.copy()
        for x,y in patches:frame.putpixel((x,y),(244,244,240,255))
        for x,y in patches:
            dx,dy=(x-7.5)/1.25,y-7.5
            for _ in range(i):dx,dy=-dy,dx
            px,py=round(dx*1.25+7.5),round(dy+7.5)
            if frame.getpixel((px,py))[3]:frame.putpixel((px,py),(20,20,24,255))
        spin.alpha_composite(frame,(i*16,0))
    raw.append(('BALL',spin,True,None,(4,16,16)))
    fxmeta=enemies['mazfx'];fx,cw,ch=frame_sheet(Image.open(spr/(fxmeta['sheet']+'.png')),28,24,5)
    actor_meta['MAZFX']=(fxmeta,fx,cw,ch)
    raw.append(('MAZFX',fx,True,None,(5,cw,ch)))
    goldball=spin.copy()
    for y in range(16):
        for x in range(64):
            r,g,b,a=goldball.getpixel((x,y))
            if a:goldball.putpixel((x,y),(255,210,63,a) if r>100 else (138,90,10,a))
    largegold=Image.new('RGBA',(128,32))
    for i in range(4):largegold.alpha_composite(goldball.crop((i*16,0,i*16+16,16)).resize((24,24),Image.Resampling.NEAREST),(i*32+4,4))
    raw.append(('GOLD_BALL',largegold,True,None,(4,32,32)))
    raw.append(('ITEMS',item_atlas(spr),True,None,(8,32,32)))
    raw.append(('FLAG',checkpoint_flag(),True,None,None))
    raw.append(('SCOREPOP',score_atlas(upstream),True,None,(16,16,16)))
    # Native equivalents of the procedural web mazfx/shoe and wave textures.
    wave=Image.new('RGBA',(10,22));wd=ImageDraw.Draw(wave)
    for k in range(3):wd.rectangle((k*3,2+k*2,k*3+1,19-k*2),fill='#ffffff' if k==0 else '#9fe8ff')
    raw.append(('WAVE',wave,True,None,None))
    # Native impact/dust flipbook, matching the web's small pixel particles.
    particles=Image.new('RGBA',(128,16))
    pd=ImageDraw.Draw(particles)
    for frame in range(4):
        cx=frame*16+8;radius=2+frame*2
        for dx,dy in ((-1,0),(1,0),(0,-1),(0,1),(-1,-1),(1,-1),(-1,1),(1,1)):
            x=cx+dx*radius;y=8+dy*radius
            pd.rectangle((x,y,x+1,y+1),fill='#fff4c0' if frame<2 else '#e8a84a')
        if frame<2:pd.rectangle((cx-1,7,cx+1,9),fill='#ffffff')
        cx=(frame+4)*16+8
        for dx,dy in ((-1,0),(1,0),(-1,-1),(1,-1),(0,-1)):
            x=cx+dx*(2+frame*2);y=12+dy*(frame+1)
            pd.rectangle((x,y,x+1,y+1),fill='#9a8a7a' if frame<2 else '#615560')
    raw.append(('PARTICLES',particles,True,None,(8,16,16)))
    weather=Image.new('RGBA',(16,16))
    wd=ImageDraw.Draw(weather)
    for k in range(6):wd.point((8-k//3,k+3),fill=(55+k*5,61+k*5,86+k*6,255))
    raw.append(('RAIN',weather,True,None,None))
    # Dithered fog and eyes share the final hardware palette. Weather textures
    # intentionally drift/repeat, as they do in the web weather system.
    fog=Image.new('RGBA',(80,32));fd=ImageDraw.Draw(fog)
    for y in range(32):
        for x in range(64):
            density=max(0,10-abs(y-16))
            if ((x*17+y*29+x*y)%64)<density:
                fd.point((x,y),fill=(56+y//3,52+y//3,72+y//3,255))
    fd.rectangle((68,5,69,6),fill='#ffffff');fd.rectangle((74,5,75,6),fill='#ffffff')
    raw.append(('FOG',fog,True,None,(1,80,32)))
    title=Image.new('RGBA',(256,112));td=ImageDraw.Draw(title)
    for text,y,size,color in (('GHOSTS',4,24,'#f4f4f0'),("'N",42,16,'#ff7a1a'),('GOALS',70,24,'#f4f4f0')):
        font=ImageFont.truetype(str(upstream/'server/lib/fonts/PressStart2P.ttf'),size)
        box=td.textbbox((0,0),text,font=font);x=(256-(box[2]-box[0]))//2
        td.text((x+2,y+2),text,font=font,fill='#160b16',stroke_width=3,stroke_fill='#160b16')
        td.text((x,y),text,font=font,fill=color,stroke_width=2,stroke_fill='#5a1a00')
    raw.append(('TITLE',title,True,None,None))
    raw.append(('PANEL',Image.new('RGBA',(16,16),'#16161c'),True,None,(1,16,16)))
    tile_cursor=CROM_BASE_TILE; pal_cursor=148; # FIX text owns palettes 0, 1 and 2.
    built=[]
    raw_images={name:rgba for name,rgba,_,_,_ in raw}
    paired={}
    class A:pass
    for name,rgba,transparent,grid,actor in raw:
        if actor is None and name not in ('BALL','RAIN'):rgba,lw=base.project_rgba(rgba)
        elif name in ('BALL','RAIN'):lw=13
        else:lw=(actor[1]*X_DEN//X_NUM)*actor[0]
        if rgba.width%16 or rgba.height%16:
            pad=Image.new('RGBA',(((rgba.width+15)//16)*16,((rgba.height+15)//16)*16),(0,0,0,0)); pad.alpha_composite(rgba,(0,0)); rgba=pad
        # Background banks are mutually exclusive and uploaded on scene changes.
        # A: sky/city/interior, slots 3..98. B: stadium, slots 99..147.
        if name in ('SKY','CITY','CITY_FLASH') or name.startswith('INSIDE') or name.startswith('PEN_BG'):first=3
        elif name.startswith('STADIUM_') or name=='PEN_KEEPER':first=99
        elif name=='PEN_KICKER':first=103
        elif name=='PLAYER_GOLD':first=168
        elif name=='ITEMS':first=87
        elif name in ('FLAG','PANEL'):first=89
        elif name=='SCOREPOP':first=95
        elif name=='GOLD_BALL':first=96
        else:first=pal_cursor
        if name in ('FLAG','PANEL'):
            # Reuse the shirt palette, including its white/grey/black entries.
            qp,_,hw=opaque_quantize_region(raw_images['ITEMS'].crop((64,0,96,32)),True)
            q=rgba.convert('RGB').quantize(palette=qp,dither=Image.Dither.NONE)
            indexed=Image.new('P',rgba.size,0);indexed.putpalette(base.neutral_gif_palette())
            for y in range(rgba.height):
                for x in range(rgba.width):
                    if rgba.getpixel((x,y))[3]>=96:indexed.putpixel((x,y),q.getpixel((x,y))+1)
            indexed.info['transparency']=0;pals=[hw];pmap=[first]*(rgba.width//16)*(rgba.height//16)
        elif actor:
            quantizer=quantize_frames if name.startswith('PLAYER') or name=='ITEMS' else quantize_animation
            indexed,pals,pmap=quantizer(rgba,actor[0],actor[1],actor[2],first)
        elif name.startswith('INSIDE'):
            group=name.replace('INSIDE0','INSIDE').replace('INSIDE1','INSIDE')
            if group not in paired:
                a,_=base.project_rgba(raw_images[group.replace('INSIDE','INSIDE0',1)])
                b,_=base.project_rgba(raw_images[group.replace('INSIDE','INSIDE1',1)])
                paired[group]=quantize_background_pair(a,b,first,*grid)
            images,pals,pmap=paired[group];indexed=images[1 if name.startswith('INSIDE1') else 0]
        elif grid:indexed,pals,pmap=base.quantize_grid(rgba,transparent,first,*grid)
        else:indexed,pals,pmap=base.quantize_single(rgba,transparent,first)
        if first+len(pals)>256:raise ValueError(f"palette budget exceeded at {name}: {pal_cursor}+{len(pals)}")
        fn='gng_'+name.lower()+'.gif'; base.save_indexed(indexed,ad/fn,transparent); a=A();a.name=name;a.filename=fn;a.tile_base=tile_cursor;a.cols=rgba.width//16;a.rows=rgba.height//16;a.width=rgba.width;a.height=rgba.height;a.logical_width=lw;a.pal_first=first;a.palettes=pals;a.palmap=pmap;built.append(a);tile_cursor+=a.cols*a.rows
        if first>=148 and name!='PLAYER_GOLD':pal_cursor+=len(pals)
        elif first==3 and len(pals)>84:raise ValueError('Scene bank A overflow')
        elif first==99 and len(pals)>49:raise ValueError('Scene bank B overflow')
    # Each C-ROM pair encodes 128 bytes per tile (64 bytes per chip).
    # Fail before Makefile truncate could silently remove generated graphics.
    if tile_cursor*64 > 2097152:
        raise ValueError(f'C-ROM overflow: {tile_cursor} tiles exceed 2 MiB/chip')
    h=['#ifndef GNG_LEVEL1_ASSETS_H','#define GNG_LEVEL1_ASSETS_H','',f'#define GNG_LOGICAL_W {LOGICAL_W}',f'#define GNG_SCREEN_W {SCREEN_W}',f'#define GNG_SCREEN_H {SCREEN_H}',f'#define GNG_LEVEL1_WORLD_W {WORLD_W}',f'#define GNG_X_NUM {X_NUM}',f'#define GNG_X_DEN {X_DEN}','']
    for a in built:
        p='GNG_'+a.name;h += [f'#define {p}_TILE {a.tile_base}',f'#define {p}_COLS {a.cols}',f'#define {p}_ROWS {a.rows}',f'#define {p}_W {a.width}',f'#define {p}_H {a.height}',f'#define {p}_PAL_FIRST {a.pal_first}',f'#define {p}_PAL_COUNT {len(a.palettes)}',c_pal(p+'_PALETTES',a.palettes),c_u8(p+'_PALMAP',a.palmap)]
    fg=next(a for a in built if a.name=='FG_CEMETERY')
    mask=Image.open(ad/fg.filename)
    tops=[];heights=[]
    for col in range(fg.cols):
        ys=[y for y in range(mask.height) if any(mask.getpixel((col*16+x,y)) for x in range(16))]
        top=min(ys)//16 if ys else 0;bottom=max(ys)//16+1 if ys else 0
        tops.append(top);heights.append(bottom-top)
    h.extend((c_u8('GNG_FG_TOP',tops),c_u8('GNG_FG_HEIGHT',heights)))
    for asset in built:
        if asset.name in ('GROUND','FG_CEMETERY','ZOMBIE','BAT','PUMPKIN','GHOST'):
            dark=[]
            for palette in asset.palettes:
                dark.append([v if i==0 else 0x8000|((((v>>8)&15)*2//5)<<8)|((((v>>4)&15)*2//5)<<4)|((v&15)*2//5) for i,v in enumerate(palette)])
            h.append(c_pal('GNG_'+asset.name+'_DARK_PALETTES',dark))
    for name,(meta,sheet,cw,ch) in actor_meta.items():
        h += [f'#define GNG_{name}_FRAME_COUNT {len(meta["frames"])}',f'#define GNG_{name}_CELL_W {cw}',f'#define GNG_{name}_CELL_H {ch}',f'#define GNG_{name}_FRAME_COLS {cw//16}',f'#define GNG_{name}_FRAME_ROWS {ch//16}']
        for i,n in enumerate(meta['frames']):h.append(f'#define GNG_{name}_FRAME_{n.upper()} {i}')
        boxes=[]
        for fi in range(len(meta['frames'])):
            bbox=sheet.crop((fi*cw,0,(fi+1)*cw,ch)).getchannel('A').getbbox() or (cw//2,0,cw//2+1,1)
            l,t,r,b=bbox;boxes.append(((l-cw//2)*4//5,t,(r-cw//2)*4//5,b))
        h.append('static const short GNG_'+name+'_SHOTBOX[][4]={'+','.join('{%d,%d,%d,%d}'%box for box in boxes)+'};')
        h.append('')
    h.append(c_pal('GNG_MIDAS_PALETTE',[[0x8000]+[base.neogeo_color((min(255,120+i*9),min(230,72+i*10),min(100,10+i*5))) for i in range(15)]]))
    h.append(c_pal('GNG_HIT_PALETTE',[[0x8000]+[base.neogeo_color((255,255,255))]*15]))
    h.extend(c_geometry(level));h.append('#endif');(sd/'level1_assets.h').write_text('\n'.join(h)+'\n')
    # One source of truth for C-ROM concatenation order. Reordering generated
    # scenes must never make the native tile addresses point at another asset.
    (ad/'gng-gfx.mk').write_text('GNG_GFX='+' '.join(a.name.lower() for a in built)+'\n')
    (ad/'graphics-manifest.json').write_text(json.dumps({'base_tile':CROM_BASE_TILE,'total_tiles':tile_cursor,'assets':[{'name':a.name.lower(),'tile_base':a.tile_base,'cols':a.cols,'rows':a.rows,'palette_first':a.pal_first,'palette_count':len(a.palettes)} for a in built]},indent=2)+'\n')
    print('Generated FULL LEVEL 1 MVS assets:')
    for a in built:print(f'  {a.name:14s} {a.width}x{a.height} palettes={len(a.palettes)} tiles={a.cols*a.rows}')
    print(f'  world={WORLD_W} logical px, palette slots used={pal_cursor-1}/255');print('  exact ground spans + zombie/bat/pumpkin/ghost/Mazapegul assets')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('upstream',type=Path);ap.add_argument('target',type=Path);a=ap.parse_args();build(a.upstream.resolve(),a.target.resolve())
if __name__=='__main__':main()
