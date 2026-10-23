"""Read the literal Level 1 geometry from the pinned, read-only web source."""
import ast
import re
from PIL import Image, ImageDraw, ImageFont


def read_level(upstream):
    source = (upstream / 'game/src/levels/level1.js').read_text()
    result = {}
    for name in ('blocks', 'planks', 'crates'):
        literal = re.search(r'\b' + name + r':\s*(\[.*?\n  \])', source, re.S)[1]
        literal = re.sub(r'\b(x|row|w|item):', r'"\1":', literal)
        result[name] = ast.literal_eval(literal)
    result['pumpkins']=ast.literal_eval(re.search(r'\bpumpkins:\s*(\[.*?\n  \])',source,re.S)[1])
    for name in ('graves', 'bigGraves', 'lamps', 'goals'):
        result[name] = ast.literal_eval(re.search(r'\b' + name + r':\s*(\[[^\]]*\])', source)[1])
    for name in ('ledFrom','ledTo','ledStep','checkpoint'):
        result[name] = int(re.search(r'\b'+name+r':\s*(\d+)',source)[1])
    return result


def scenery(level, width):
    image = Image.new('RGBA', (width, 224))
    d = ImageDraw.Draw(image)
    def rect(x, y, w, h, color):
        d.rectangle((x, y, x+w-1, y+h-1), fill=color)
    # Same pixel primitives, scale and finite positions as web buildTiles().
    for tile in level['graves']:
        x, y = tile*16+1, 190
        for dx,dy,w,h,c in ((1,3,12,15,'#6a6a78'),(3,1,8,3,'#6a6a78'),
                           (1,3,12,1,'#8a8a98'),(6,5,2,7,'#3a3a46'),
                           (4,7,6,2,'#3a3a46'),(1,16,12,2,'#4a4a56')):
            rect(x+dx,y+dy,w,h,c)
    for tile in level['bigGraves']:
        x,y=tile*16-1,186
        for dx,dy,w,h,c in ((2,4,14,18,'#5a5a66'),(4,1,10,4,'#5a5a66'),
                           (6,0,6,2,'#5a5a66'),(5,8,8,1,'#2e2e38'),
                           (5,11,8,1,'#2e2e38'),(5,14,6,1,'#2e2e38')):
            rect(x+dx,y+dy,w,h,c)
    for tile in level['lamps']:
        x,y=tile*16-5,144
        for dx,dy,w,h,c in ((4,8,2,56,'#2a2a34'),(1,2,8,7,'#2a2a34'),
                           (2,3,6,5,'#ffe7a0'),(0,0,10,2,'#2a2a34'),(3,60,4,4,'#2a2a34')):
            rect(x+dx,y+dy,w,h,c)
    for block in level['blocks']:
        for x in range(block['x']*16, (block['x']+block['w'])*16,16):
            for y in range(block['row']*16,208,16):
                for dx,dy,w,h,c in ((0,0,16,16,'#4a4a56'),(0,0,16,1,'#6a6a78'),
                                   (0,7,16,1,'#2e2e38'),(7,0,1,7,'#2e2e38'),
                                   (3,8,1,8,'#2e2e38'),(12,8,1,8,'#2e2e38')):
                    rect(x+dx,y+dy,w,h,c)
    for plank in level['planks']:
        x,y,w=plank['x']*16,plank['row']*16,plank['w']*16
        rect(x+3,y+6,2,208-y-6,'#2a1a0e');rect(x+w-5,y+6,2,208-y-6,'#2a1a0e')
        rect(x,y,w,6,'#6a4422');rect(x,y,w,1,'#8a6432')
        for dx in range(4,w,8):rect(x+dx,y+1,1,5,'#4a2a12')
    for tile in level['goals']:
        x,y=tile*16-17,168
        rect(x,y,34,3,'#f4f4f0');rect(x,y,3,40,'#f4f4f0');rect(x+31,y,3,40,'#f4f4f0')
        for dx in range(4,31,4):rect(x+dx,y+3,1,37,'#55555a')
        for dy in range(6,40,4):rect(x+3,y+dy,28,1,'#55555a')
    # Actual finite LED placements from the web, not repeating cemetery tiles.
    font=ImageFont.load_default(size=8)
    labels=('SPONSOR 1','SPONSOR 2','DAIBURDEL','SPONSOR 3')
    for i,tile in enumerate(range(level['ledFrom'],level['ledTo'],level['ledStep'])):
        x,y=tile*16-48,171
        rect(x,y,96,14,'#05050a');rect(x,y,96,1,'#2a2a34');rect(x,y+13,96,1,'#2a2a34')
        text=labels[i%4];box=d.textbbox((0,0),text,font=font)
        d.text((x+(96-(box[2]-box[0]))//2,y+1),text,font=font,fill='#ff7a1a')
        for dy in range(2,13,2):rect(x+1,y+dy,94,1,'#100b0c')
    x=level['checkpoint']*16-6
    rect(x+1,176,2,32,'#505058');rect(x+3,178,9,7,'#626268');rect(x+3,181,9,4,'#09090c')
    return image


def c_geometry(level):
    lines = []
    level = dict(level)
    level['solids'] = level['blocks'] + [dict(x=g, row=12, w=1) for g in level['graves']]
    for name in ('blocks','planks','solids'):
        prefix = 'GNG_LEVEL1_' + name.upper()
        lines.append(f'#define {prefix}_COUNT {len(level[name])}')
        rows = ','.join('{%d,%d,%d}' % (b['x']*16,b['row']*16,b['w']*16) for b in level[name])
        lines.append(f'static const short {prefix}[][3] = {{{rows}}};')
    kinds={'pumpkinGold':2,'shirt':3,'piadina':4,'seahorse':5}
    rows=','.join('{%d,%d,%d}'%(c['x']*16+8,c['row']*16,kinds[c['item']]) for c in level['crates'])
    lines.append(f'#define GNG_CRATE_COUNT {len(level["crates"])}')
    lines.append('static const short GNG_CRATES[][3]={'+rows+'};')
    rows=','.join('{%d,%d}'%(x*16+8,y*16+8) for x,y in level['pumpkins'])
    lines.append(f'#define GNG_PUMPKIN_COUNT {len(level["pumpkins"])}')
    lines.append('static const short GNG_PUMPKINS[][2]={'+rows+'};')
    return lines
