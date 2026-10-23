"""Native atlas from the web's separated pickups and procedural item drawings."""
from PIL import Image, ImageDraw, ImageFont


def item_atlas(sprites):
    frames = []
    for file in ('en_pumpkinItem.png', 'en_pumpkinGold.png'):
        frames.append(Image.open(sprites/file).convert('RGBA'))
    shirt = Image.new('RGBA', (14, 12))
    d = ImageDraw.Draw(shirt)
    for x,y,w,h,c in ((3,0,8,12,'#f4f4f0'),(0,1,4,4,'#f4f4f0'),(10,1,4,4,'#f4f4f0'),(5,0,4,1,'#16161c'),(3,10,8,2,'#c9c9d2'),(6,4,2,4,'#16161c')):
        d.rectangle((x,y,x+w-1,y+h-1),fill=c)
    frames.append(shirt)
    bread = Image.new('RGBA',(14,8));d=ImageDraw.Draw(bread)
    for x,y,w,h,c in ((1,1,12,6,'#f0d9a0'),(0,2,14,4,'#f0d9a0'),(3,3,2,1,'#b07a30'),(8,2,2,1,'#b07a30'),(10,5,2,1,'#b07a30'),(5,5,1,1,'#b07a30')):
        d.rectangle((x,y,x+w-1,y+h-1),fill=c)
    frames.extend((bread,Image.open(sprites/'en_seahorse.png').convert('RGBA')))
    for damage in range(3):
        crate=Image.new('RGBA',(16,16),'#7a4a22');d=ImageDraw.Draw(crate)
        for x,y,w,h,c in ((0,0,16,2,'#9a6a32'),(0,7,16,2,'#5a3212'),(0,14,16,2,'#5a3212'),(1,2,2,12,'#5a3212'),(13,2,2,12,'#5a3212'),(3,3,10,1,'#9a6a32')):
            d.rectangle((x,y,x+w-1,y+h-1),fill=c)
        d.rectangle((0,0,15,15),outline='#24140c')
        if damage:d.line((8,0,7,5,10,8,7,11,8,15),fill='#180e0a')
        if damage>1:d.line((0,8,5,9,7,6,12,12,15,11),fill='#180e0a')
        frames.append(crate)
    sheet=Image.new('RGBA',(32*8,32))
    for i,im in enumerate(frames):
        im=im.resize(((im.width*5+3)//4,im.height),Image.Resampling.NEAREST)
        sheet.alpha_composite(im,(i*32+(32-im.width)//2,(32-im.height)//2))
    return sheet


def score_atlas(upstream):
    sheet=Image.new('RGBA',(16*16,16));d=ImageDraw.Draw(sheet)
    font=ImageFont.truetype(str(upstream/'server/lib/fonts/PressStart2P.ttf'),8)
    for i,c in enumerate('0123456789+xUP! '):
        d.text((i*16,2),c,font=font,fill='#fff4c0',stroke_width=1,stroke_fill='#120a18')
    return sheet


def checkpoint_flag():
    image=Image.new('RGBA',(12,32));d=ImageDraw.Draw(image)
    d.rectangle((1,0,2,31),fill='#c9c9d2');d.rectangle((3,2,11,8),fill='#f4f4f0');d.rectangle((3,5,11,8),fill='#16161c')
    return image
