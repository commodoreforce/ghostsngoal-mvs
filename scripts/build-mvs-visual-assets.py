#!/usr/bin/env python3
"""Build the verified Ghosts'n Goals MVS opening benchmark.

The opening zone is a finite 528x224 logical world. Nothing in the scene is
repeated while scrolling: sky, architecture, ground and cemetery foreground
are authored once and clamped to the camera. This removes the pop-in and
strip duplication seen in the earlier prototypes.
"""
from __future__ import annotations

import argparse
import io
import json
import math
import zipfile
from collections import deque, Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw

TILE = 16
LOGICAL_W = 256
SCREEN_W = 320
SCREEN_H = 224
WORLD_W = 528
X_NUM = 5
X_DEN = 4
CROM_BASE_TILE = 256
FRAME_LOGICAL_W = 64
FRAME_W = 80
FRAME_H = 64


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


def neogeo_color(rgb):
    # Same bit packing used by ngdevkit/paltool.
    r, g, b = [c >> 2 for c in rgb]
    dark = 1 if ((r & 1) + (g & 1) + (b & 1)) == 0 else 0
    r, g, b = r >> 1, g >> 1, b >> 1
    lsb = ((r & 1) << 2) | ((g & 1) << 1) | (b & 1)
    r, g, b = r >> 1, g >> 1, b >> 1
    return (dark << 15) | (lsb << 12) | (r << 8) | (g << 4) | b


def neutral_gif_palette():
    p = []
    for i in range(256):
        c = int(round(i * 255 / 15)) if i < 16 else 0
        p += [c, c, c]
    return p[:768]


def padded_qpal(q):
    p = list(q.getpalette() or [])[:45]
    return p + [0] * (45 - len(p))


def project_rgba(src: Image.Image):
    src = src.convert("RGBA")
    logical_width = src.width
    projected = src.resize((project_x(src.width), src.height), Image.Resampling.NEAREST)
    out = Image.new("RGBA", (align16(projected.width), align16(projected.height)), (0, 0, 0, 0))
    out.alpha_composite(projected, (0, 0))
    return out, logical_width


def quantize_region(crop: Image.Image, transparent: bool):
    rgba = crop.convert("RGBA")
    alpha = rgba.getchannel("A")
    rgb = Image.new("RGB", rgba.size, (0, 0, 0))
    rgb.paste(rgba.convert("RGB"), mask=alpha)
    q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    qp = padded_qpal(q)
    hw = [0x8000]
    for i in range(15):
        o = i * 3
        hw.append(neogeo_color((qp[o], qp[o + 1], qp[o + 2])))
    return q, alpha, hw


def quantize_grid(img, transparent, slot_first, grid_cols, grid_rows):
    rgba = img.convert("RGBA")
    tw, th = rgba.width // TILE, rgba.height // TILE
    ctw = max(1, math.ceil(tw / grid_cols))
    cth = max(1, math.ceil(th / grid_rows))
    out = Image.new("P", rgba.size, 0)
    out.putpalette(neutral_gif_palette())
    op = out.load()
    pmap = [0] * (tw * th)
    palettes = []

    for ty0 in range(0, th, cth):
        for tx0 in range(0, tw, ctw):
            tx1, ty1 = min(tw, tx0 + ctw), min(th, ty0 + cth)
            x0, y0, x1, y1 = tx0 * TILE, ty0 * TILE, tx1 * TILE, ty1 * TILE
            crop = rgba.crop((x0, y0, x1, y1))
            q, alpha, hw = quantize_region(crop, transparent)
            slot = slot_first + len(palettes)
            if slot > 255:
                raise ValueError(f"palette slot overflow: {slot}")
            palettes.append(hw)
            qpx, apx = q.load(), alpha.load()
            for y in range(crop.height):
                for x in range(crop.width):
                    op[x0 + x, y0 + y] = 0 if transparent and apx[x, y] < 96 else int(qpx[x, y]) + 1
            for ty in range(ty0, ty1):
                for tx in range(tx0, tx1):
                    pmap[ty * tw + tx] = slot

    if transparent:
        out.info["transparency"] = 0
    return out, palettes, pmap


def quantize_single(img, transparent, slot):
    rgba = img.convert("RGBA")
    q, alpha, hw = quantize_region(rgba, transparent)
    out = Image.new("P", rgba.size, 0)
    out.putpalette(neutral_gif_palette())
    op, qpx, apx = out.load(), q.load(), alpha.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            op[x, y] = 0 if transparent and apx[x, y] < 96 else int(qpx[x, y]) + 1
    if transparent:
        out.info["transparency"] = 0
    return out, [hw], [slot] * ((rgba.width // TILE) * (rgba.height // TILE))


def quantize_actor_halves(img, frame_count, slot_first):
    """Two palettes per actor frame: head/torso and shorts/legs.

    Neo Geo can select a palette per 16x16 tile. Splitting every 64px frame in
    two horizontal palette zones keeps skin/hair shading independent from the
    black/white kit and is visibly cleaner than one 15-colour palette/frame.
    """
    rgba = img.convert("RGBA")
    out = Image.new("P", rgba.size, 0)
    out.putpalette(neutral_gif_palette())
    op = out.load()
    tw, th = rgba.width // TILE, rgba.height // TILE
    pmap = [0] * (tw * th)
    palettes = []

    for fi in range(frame_count):
        x0 = fi * FRAME_W
        for half in range(2):
            y0 = half * 32
            crop = rgba.crop((x0, y0, x0 + FRAME_W, y0 + 32))
            q, alpha, hw = quantize_region(crop, True)
            slot = slot_first + len(palettes)
            if slot > 255:
                raise ValueError(f"actor palette slot overflow: {slot}")
            palettes.append(hw)
            qpx, apx = q.load(), alpha.load()
            for y in range(32):
                for x in range(FRAME_W):
                    op[x0 + x, y0 + y] = 0 if apx[x, y] < 96 else int(qpx[x, y]) + 1
            tx0 = x0 // TILE
            for ty in range(y0 // TILE, (y0 + 32) // TILE):
                for tx in range(FRAME_W // TILE):
                    pmap[ty * tw + tx0 + tx] = slot

    out.info["transparency"] = 0
    return out, palettes, pmap


def save_indexed(img, path, transparent):
    path.parent.mkdir(parents=True, exist_ok=True)
    if transparent:
        img.save(path, format="GIF", transparency=0, optimize=False)
    else:
        img.save(path, format="GIF", optimize=False)


def read_zip_image(pack, inner):
    if not pack or not pack.is_file():
        return None
    with zipfile.ZipFile(pack) as z:
        try:
            data = z.read(inner)
        except KeyError:
            return None
    return Image.open(io.BytesIO(data)).convert("RGBA")


def dominant_dark_sky(sky):
    values = []
    for y in range(140, 175):
        for x in range(sky.width):
            r, g, b, a = sky.getpixel((x, y))
            if a and max(r, g, b) < 48:
                values.append((r, g, b, 255))
    return Counter(values).most_common(1)[0][0] if values else (2, 1, 12, 255)


def make_sky(sky):
    """Keep the authored moon/top clouds; remove low cloud masses that intruded
    into gameplay in the previous build."""
    out = sky.copy().convert("RGBA")
    bg = dominant_dark_sky(sky)
    ImageDraw.Draw(out).rectangle((0, 150, WORLD_W - 1, 223), fill=bg)
    return out


def make_architecture(sky, city):
    """Manual production mask for Rocca/mura.

    Difference extraction is only allowed inside known architecture zones, so
    orange cloud variations can never leak into the city layer.
    """
    out = Image.new("RGBA", (WORLD_W, 224), (0, 0, 0, 0))
    op, sp, cp = out.load(), sky.load(), city.load()
    zones = [
        (0, 132, 528, 184),       # continuous city wall
        (34, 108, 94, 184),       # left tower
        (128, 108, 190, 184),     # middle tower
        (270, 70, 338, 184),      # main keep
        (315, 78, 474, 184),      # Rocca body
        (442, 126, 528, 184),     # right arches
    ]
    for x0, y0, x1, y1 in zones:
        for y in range(y0, y1):
            for x in range(x0, x1):
                cr, cg, cb, ca = cp[x, y]
                sr, sg, sb, sa = sp[x, y]
                if max(abs(cr - sr), abs(cg - sg), abs(cb - sb)) > 12:
                    op[x, y] = (cr, cg, cb, 255)
    return out


def make_ground(city):
    """Verified clean master crop: grass begins at y=176.
    Darken it for the night scene and add a restrained earth strip underneath.
    """
    grass = city.crop((0, 176, WORLD_W, 224)).convert("RGBA")
    gp = grass.load()
    for y in range(grass.height):
        for x in range(grass.width):
            r, g, b, a = gp[x, y]
            gp[x, y] = (int(r * .72), int(g * .72), int(b * .72), 255)
    out = Image.new("RGBA", (WORLD_W, 64), (32, 23, 17, 255))
    out.alpha_composite(grass, (0, 0))
    d = ImageDraw.Draw(out)
    d.rectangle((0, 48, WORLD_W - 1, 63), fill=(34, 24, 18, 255))
    d.rectangle((0, 48, WORLD_W - 1, 49), fill=(16, 13, 15, 255))
    for x in range(12, WORLD_W, 37):
        y = 55 + ((x * 5) % 5)
        d.rectangle((x, y, min(x + 3, WORLD_W - 1), y), fill=(54, 39, 28, 255))
    return out


def make_foreground():
    """World-authored cemetery foreground. It never repeats."""
    im = Image.new("RGBA", (WORLD_W, 224), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    ink = (5, 4, 9, 255)
    ink2 = (11, 8, 16, 255)

    # Sparse grass silhouettes.
    for x in range(6, WORLD_W, 19):
        if 28 <= x <= 132:  # keep spawn readable
            continue
        base = 207 + ((x * 7) % 3)
        d.line((x, base, x + 3, base - 6), fill=ink)
        d.line((x + 5, base, x + 8, base - 4), fill=ink)

    # Two finite fence sections, authored in world coordinates.
    for start, end in ((150, 248), (355, 510)):
        d.rectangle((start, 203, end, 205), fill=ink)
        for x in range(start + 5, end, 11):
            d.rectangle((x, 188, x + 2, 223), fill=ink)
            d.polygon([(x - 1, 188), (x + 1, 184), (x + 3, 188)], fill=ink)

    # Crosses and a few low tombstones.
    for x, y in ((270, 188), (320, 194), (338, 190), (495, 187)):
        d.rectangle((x, y, x + 3, 223), fill=ink)
        d.rectangle((x - 7, y + 7, x + 10, y + 10), fill=ink)
    for x, y, w, h in ((258, 204, 10, 16), (329, 205, 11, 15), (514, 201, 10, 18)):
        d.rounded_rectangle((x, y, x + w, y + h), radius=2, fill=ink2)
    return im


def clean_frame_components(frame):
    """Remove tiny isolated opaque components that become black sparkle pixels
    after C-ROM quantisation, while keeping every meaningful sprite component."""
    rgba = frame.convert("RGBA")
    a = rgba.getchannel("A")
    ap = a.load()
    w, h = rgba.size
    seen = set()
    remove = []
    for sy in range(h):
        for sx in range(w):
            if ap[sx, sy] < 64 or (sx, sy) in seen:
                continue
            q = deque([(sx, sy)])
            seen.add((sx, sy))
            comp = []
            while q:
                x, y = q.popleft()
                comp.append((x, y))
                for nx, ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
                    if 0 <= nx < w and 0 <= ny < h and ap[nx, ny] >= 64 and (nx, ny) not in seen:
                        seen.add((nx, ny)); q.append((nx, ny))
            if len(comp) <= 2:
                remove.extend(comp)
    rp = rgba.load()
    for x, y in remove:
        rp[x, y] = (0, 0, 0, 0)
    return rgba


def project_sheet_frames(sheet, fw, fh, frames):
    pfw = project_x(fw)
    src = sheet.convert("RGBA")
    out = Image.new("RGBA", (FRAME_W * frames, FRAME_H), (0, 0, 0, 0))
    for i in range(frames):
        f = src.crop((i * fw, 0, (i + 1) * fw, fh))
        f = clean_frame_components(f)
        f = f.resize((pfw, fh), Image.Resampling.NEAREST)
        out.alpha_composite(f, (i * FRAME_W + (FRAME_W - pfw) // 2, FRAME_H - fh))
    return out


def make_ball():
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    p = im.load()
    for y in range(4, 12):
        for x in range(3, 13):
            dx, dy = (x - 7.5) / 1.25, y - 7.5
            if dx * dx + dy * dy <= 14:
                p[x, y] = (244, 244, 240, 255)
    for x, y in ((7, 6), (9, 7), (6, 9), (9, 10)):
        p[x, y] = (20, 20, 24, 255)
    return im


def c_palette_matrix(name, palettes):
    rows = ["    { " + ", ".join(f"0x{v:04x}" for v in p) + " }" for p in palettes]
    return f"static const unsigned short {name}[{len(palettes)}][16] = {{\n" + ",\n".join(rows) + "\n};\n"


def c_u8_array(name, values: Iterable[int]):
    vals = list(values)
    lines = ["    " + ", ".join(str(v) for v in vals[i:i+24]) for i in range(0, len(vals), 24)]
    return f"static const unsigned char {name}[{len(vals)}] = {{\n" + ",\n".join(lines) + "\n};\n"


def macro_asset(a):
    p = f"GNG_{a.name}"
    return (f"#define {p}_TILE {a.tile_base}\n#define {p}_COLS {a.cols}\n#define {p}_ROWS {a.rows}\n"
            f"#define {p}_W {a.width}\n#define {p}_H {a.height}\n#define {p}_LOGICAL_W {a.logical_width}\n"
            f"#define {p}_PAL_FIRST {a.pal_first}\n#define {p}_PAL_COUNT {len(a.palettes)}\n")


def emit_header(path, assets, pframes, zframes):
    t = "#ifndef GNG_GFX_ASSETS_H\n#define GNG_GFX_ASSETS_H\n\n"
    t += (f"#define GNG_LOGICAL_W {LOGICAL_W}\n#define GNG_SCREEN_W {SCREEN_W}\n#define GNG_SCREEN_H {SCREEN_H}\n"
          f"#define GNG_X_NUM {X_NUM}\n#define GNG_X_DEN {X_DEN}\n#define GNG_BENCHMARK_WORLD_W {WORLD_W}\n\n")
    for a in assets:
        t += macro_asset(a) + "\n"
        t += c_palette_matrix(f"GNG_{a.name}_PALETTES", a.palettes) + "\n"
        t += c_u8_array(f"GNG_{a.name}_PALMAP", a.palmap) + "\n"
    t += f"#define GNG_PLAYER_FRAME_COUNT {len(pframes)}\n"
    for i, n in enumerate(pframes):
        t += f"#define GNG_PLAYER_FRAME_{n.upper()} {i}\n"
    t += f"#define GNG_ZOMBIE_FRAME_COUNT {len(zframes)}\n"
    for i, n in enumerate(zframes):
        t += f"#define GNG_ZOMBIE_FRAME_{n.upper()} {i}\n"
    t += f"#define GNG_ACTOR_COLS {FRAME_W // TILE}\n#define GNG_ACTOR_ROWS {FRAME_H // TILE}\n#endif\n"
    path.write_text(t)


def build(upstream, target, env_pack=None, sprite_pack=None):
    public = upstream / "game" / "public"
    bgs = public / "backgrounds"
    sprites = public / "sprites"
    ad, sd = target / "assets", target / "src"
    ad.mkdir(parents=True, exist_ok=True)
    sd.mkdir(parents=True, exist_ok=True)

    manifest = json.loads((sprites / "manifest.json").read_text())
    enemies = json.loads((sprites / "enemies.json").read_text())
    sh, zo = manifest["shpendi"], enemies["zombie"]

    master_sky = read_zip_image(env_pack, "ambientazione/L1/sfondo.png")
    master_city = read_zip_image(env_pack, "ambientazione/L1/cesena.png")
    if master_sky is not None and master_city is not None:
        if master_sky.size != (3168, 1344) or master_city.size != (3168, 1344):
            raise ValueError(f"unexpected L1 master sizes: sky={master_sky.size} city={master_city.size}")
        master_sky = master_sky.resize((WORLD_W, 224), Image.Resampling.NEAREST)
        master_city = master_city.resize((WORLD_W, 224), Image.Resampling.NEAREST)
        print(f"Using verified original L1 masters from {env_pack}")
    else:
        master_sky = Image.open(bgs / "l1_sky.png").convert("RGBA").resize((WORLD_W,224), Image.Resampling.NEAREST)
        master_city = Image.open(bgs / "l1_cesena.png").convert("RGBA").resize((WORLD_W,224), Image.Resampling.NEAREST)
        print("Original environment pack missing: using web runtime fallback")

    sky_src = make_sky(master_sky)
    city_src = make_architecture(master_sky, master_city)
    ground_src = make_ground(master_city)
    fg_src = make_foreground()

    sky, sky_lw = project_rgba(sky_src)
    city, city_lw = project_rgba(city_src)
    ground, ground_lw = project_rgba(ground_src)
    fg, fg_lw = project_rgba(fg_src)

    player = project_sheet_frames(Image.open(sprites / "pl_shpendi_base.png"), int(sh["frameWidth"]), int(sh["frameHeight"]), len(sh["frames"]))
    zombie = project_sheet_frames(Image.open(sprites / f"{zo['sheet']}.png"), int(zo["frameWidth"]), int(zo["frameHeight"]), len(zo["frames"]))

    tile_cursor = CROM_BASE_TILE
    palette_cursor = 1
    built = []

    def add(name, fn, rgba, logical_w, transparent, grid=None, actor_frames=None):
        nonlocal tile_cursor, palette_cursor
        if rgba.width % TILE or rgba.height % TILE:
            raise ValueError(f"{name} not tile aligned: {rgba.size}")
        if actor_frames is not None:
            indexed, pals, pmap = quantize_actor_halves(rgba, actor_frames, palette_cursor)
        elif grid is None:
            indexed, pals, pmap = quantize_single(rgba, transparent, palette_cursor)
        else:
            indexed, pals, pmap = quantize_grid(rgba, transparent, palette_cursor, *grid)
        if palette_cursor + len(pals) > 256:
            raise ValueError(f"palette budget exceeded by {name}: {palette_cursor}+{len(pals)}")
        save_indexed(indexed, ad / fn, transparent)
        cols, rows = rgba.width // TILE, rgba.height // TILE
        built.append(Asset(name, fn, tile_cursor, cols, rows, rgba.width, rgba.height, logical_w, palette_cursor, pals, pmap))
        tile_cursor += cols * rows
        palette_cursor += len(pals)

    # Palette-rich painted layers first. Actor palettes are then isolated per frame/half.
    add("SKY", "gng_sky.gif", sky, sky_lw, False, grid=(16, 4))
    add("CITY", "gng_city.gif", city, city_lw, True, grid=(12, 4))
    add("GROUND", "gng_ground.gif", ground, ground_lw, False, grid=(8, 2))
    add("FG_CEMETERY", "gng_fg_cemetery.gif", fg, fg_lw, True)
    add("PLAYER", "gng_shpendi.gif", player, FRAME_LOGICAL_W * len(sh["frames"]), True, actor_frames=len(sh["frames"]))
    add("ZOMBIE", "gng_zombie.gif", zombie, FRAME_LOGICAL_W * len(zo["frames"]), True, actor_frames=len(zo["frames"]))
    add("BALL", "gng_ball.gif", make_ball(), 13, True)

    emit_header(sd / "gfx_assets.h", built, list(sh["frames"]), list(zo["frames"]))

    print("Generated PREMIUM OPENING V4 finite-world assets:")
    for a in built:
        print(f"  {a.name:12s} physical={a.width:4d}x{a.height:<3d} logicalW={a.logical_width:4d} palettes={len(a.palettes):3d}")
    print(f"  palette slots used: {palette_cursor - 1} / 255")
    print("  world: 528 logical px; camera max 272; NO repeating scenery layers")
    print("  parallax: sky 3%, architecture 15%, ground/foreground 100%")
    print("  actor quality: two independent Neo Geo palettes per animation frame")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("upstream", type=Path)
    ap.add_argument("target", type=Path)
    ap.add_argument("--environment-pack", type=Path, default=None)
    ap.add_argument("--sprite-pack", type=Path, default=None)
    a = ap.parse_args()
    build(a.upstream.resolve(), a.target.resolve(),
          a.environment_pack.resolve() if a.environment_pack else None,
          a.sprite_pack.resolve() if a.sprite_pack else None)


if __name__ == "__main__":
    main()
