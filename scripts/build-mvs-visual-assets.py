#!/usr/bin/env python3
"""Build a higher-fidelity Ghosts'n Goals Neo Geo visual slice.

The source art is read from a pinned, read-only checkout of the web game.
Runtime Phaser assets are converted to Neo Geo 16x16 sprite tiles. Instead
of forcing an entire image through one 16-colour palette, each aligned region
gets its own 15-visible-colour palette and every tile records the palette slot
it must use. This matches the Neo Geo hardware model much more closely.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageOps

TILE = 16
SCREEN_W = 320
SCREEN_H = 224
WORLD_W = 480
PLAYER_FRAME = (56, 54)
ZOMBIE_FRAME = (50, 48)
FRAME_BOX = (64, 64)
FRAMES = 4

# ngdevkit's stock C-ROM logo occupies tiles 0..255.
BG_TILE = 256
BG_COLS = WORLD_W // TILE
BG_ROWS = SCREEN_H // TILE
BG_TILES = BG_COLS * BG_ROWS
PLAYER_TILE = BG_TILE + BG_TILES
ACTOR_SHEET_COLS = (FRAMES * FRAME_BOX[0]) // TILE
ACTOR_ROWS = FRAME_BOX[1] // TILE
PLAYER_TILES = ACTOR_SHEET_COLS * ACTOR_ROWS
ZOMBIE_TILE = PLAYER_TILE + PLAYER_TILES
ZOMBIE_TILES = PLAYER_TILES
BALL_TILE = ZOMBIE_TILE + ZOMBIE_TILES

# Palette 0 is kept for FIX text. Background uses 32x32 regions (2x2 tiles),
# actors also use 32x32 regions, giving four local palettes per animation frame.
BG_PAL_FIRST = 1
BG_REGION = (32, 32)
BG_PAL_COUNT = (WORLD_W // BG_REGION[0]) * (SCREEN_H // BG_REGION[1])
PLAYER_PAL_FIRST = BG_PAL_FIRST + BG_PAL_COUNT
ACTOR_REGION = (32, 32)
PLAYER_PAL_COUNT = ((FRAMES * FRAME_BOX[0]) // ACTOR_REGION[0]) * (FRAME_BOX[1] // ACTOR_REGION[1])
ZOMBIE_PAL_FIRST = PLAYER_PAL_FIRST + PLAYER_PAL_COUNT
ZOMBIE_PAL_COUNT = PLAYER_PAL_COUNT
BALL_PAL_FIRST = ZOMBIE_PAL_FIRST + ZOMBIE_PAL_COUNT
BALL_PAL_COUNT = 1


def neogeo_color(rgb: tuple[int, int, int]) -> int:
    """Match ngdevkit paltool.py's 24-bit RGB -> Neo Geo packed colour."""
    r, g, b = [c >> 2 for c in rgb]
    darkbit = 1 if ((r & 1) + (g & 1) + (b & 1)) == 0 else 0
    r, g, b = r >> 1, g >> 1, b >> 1
    lsb = ((r & 1) << 2) | ((g & 1) << 1) | (b & 1)
    r, g, b = r >> 1, g >> 1, b >> 1
    return (darkbit << 15) | (lsb << 12) | (r << 8) | (g << 4) | b


def padded_quant_palette(q: Image.Image) -> list[int]:
    qpal = list(q.getpalette() or [])[: 15 * 3]
    qpal += [0] * ((15 * 3) - len(qpal))
    return qpal


def regional_indexed(
    img: Image.Image,
    region: tuple[int, int],
    transparent: bool,
    slot_first: int,
) -> tuple[Image.Image, list[list[int]], list[int]]:
    """Quantize aligned regions independently and return per-tile palette slots."""
    rgba = img.convert("RGBA")
    rw, rh = region
    if rgba.width % rw or rgba.height % rh or rw % TILE or rh % TILE:
        raise ValueError(f"Image {rgba.size} is not aligned to region {region} / tile {TILE}")

    tiles_w = rgba.width // TILE
    tiles_h = rgba.height // TILE
    out = Image.new("P", rgba.size, 0)
    out_px = out.load()
    palettes: list[list[int]] = []
    palmap = [0] * (tiles_w * tiles_h)

    for ry in range(0, rgba.height, rh):
        for rx in range(0, rgba.width, rw):
            crop = rgba.crop((rx, ry, rx + rw, ry + rh))
            alpha = crop.getchannel("A")
            rgb = Image.new("RGB", crop.size, (0, 0, 0))
            rgb.paste(crop.convert("RGB"), mask=alpha)
            q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            qpal = padded_quant_palette(q)
            qpx = q.load()
            apx = alpha.load()

            slot = slot_first + len(palettes)
            hw = [0x8000]
            for i in range(15):
                off = i * 3
                hw.append(neogeo_color((qpal[off], qpal[off + 1], qpal[off + 2])))
            palettes.append(hw)

            for y in range(rh):
                for x in range(rw):
                    if transparent and apx[x, y] < 96:
                        out_px[rx + x, ry + y] = 0
                    else:
                        out_px[rx + x, ry + y] = int(qpx[x, y]) + 1

            tx0, ty0 = rx // TILE, ry // TILE
            for ty in range(ty0, ty0 + rh // TILE):
                for tx in range(tx0, tx0 + rw // TILE):
                    palmap[ty * tiles_w + tx] = slot

    # tiletool consumes pixel indices directly; the GIF's display palette is not
    # the hardware palette. Give indices 0..15 a neutral preview palette.
    gif_pal: list[int] = []
    for i in range(256):
        v = i if i < 16 else 0
        c = int(round(v * 255 / 15)) if i < 16 else 0
        gif_pal += [c, c, c]
    out.putpalette(gif_pal[:768])
    if transparent:
        out.info["transparency"] = 0
    return out, palettes, palmap


def save_regional(
    img: Image.Image,
    path: Path,
    region: tuple[int, int],
    transparent: bool,
    slot_first: int,
) -> tuple[list[list[int]], list[int]]:
    indexed, palettes, palmap = regional_indexed(img, region, transparent, slot_first)
    path.parent.mkdir(parents=True, exist_ok=True)
    if transparent:
        indexed.save(path, format="GIF", transparency=0, optimize=False)
    else:
        indexed.save(path, format="GIF", optimize=False)
    return palettes, palmap


def crop_frames(sheet: Image.Image, frame_size: tuple[int, int], count: int) -> Image.Image:
    fw, fh = frame_size
    bw, bh = FRAME_BOX
    out = Image.new("RGBA", (bw * count, bh), (0, 0, 0, 0))
    for i in range(count):
        frame = sheet.crop((i * fw, 0, (i + 1) * fw, fh)).convert("RGBA")
        x = i * bw + (bw - fw) // 2
        y = bh - fh
        out.alpha_composite(frame, (x, y))
    return out


def make_ball() -> Image.Image:
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    px = im.load()
    for y in range(2, 14):
        for x in range(2, 14):
            dx, dy = x - 7.5, y - 7.5
            if dx * dx + dy * dy <= 34:
                px[x, y] = (242, 242, 242, 255)
    for x, y in [(7, 5), (8, 5), (6, 7), (9, 7), (7, 9), (8, 9)]:
        px[x, y] = (24, 24, 24, 255)
    return im


def c_palette_matrix(name: str, palettes: list[list[int]]) -> str:
    rows = []
    for pal in palettes:
        rows.append("    { " + ", ".join(f"0x{v:04x}" for v in pal) + " }")
    return f"static const unsigned short {name}[{len(palettes)}][16] = {{\n" + ",\n".join(rows) + "\n};\n"


def c_u8_array(name: str, values: Iterable[int]) -> str:
    vals = list(values)
    lines = []
    for i in range(0, len(vals), 20):
        lines.append("    " + ", ".join(str(v) for v in vals[i:i + 20]))
    return f"static const unsigned char {name}[{len(vals)}] = {{\n" + ",\n".join(lines) + "\n};\n"


def emit_header(
    path: Path,
    bg_pals: list[list[int]], bg_map: list[int],
    player_pals: list[list[int]], player_map: list[int],
    zombie_pals: list[list[int]], zombie_map: list[int],
    ball_pals: list[list[int]], ball_map: list[int],
) -> None:
    text = "#ifndef GNG_GFX_ASSETS_H\n#define GNG_GFX_ASSETS_H\n\n"
    text += "/* Generated from pinned Ghosts'n Goals runtime assets. */\n"
    text += f"#define GNG_WORLD_W {WORLD_W}\n#define GNG_SCREEN_W {SCREEN_W}\n#define GNG_SCREEN_H {SCREEN_H}\n"
    text += f"#define GNG_TILE_BG {BG_TILE}\n#define GNG_BG_COLS {BG_COLS}\n#define GNG_BG_ROWS {BG_ROWS}\n"
    text += f"#define GNG_TILE_PLAYER {PLAYER_TILE}\n#define GNG_TILE_ZOMBIE {ZOMBIE_TILE}\n#define GNG_TILE_BALL {BALL_TILE}\n"
    text += f"#define GNG_ACTOR_COLS {FRAME_BOX[0] // TILE}\n#define GNG_ACTOR_ROWS {ACTOR_ROWS}\n#define GNG_ACTOR_STRIDE {ACTOR_SHEET_COLS}\n\n"

    text += f"#define GNG_BG_PAL_FIRST {BG_PAL_FIRST}\n#define GNG_BG_PAL_COUNT {len(bg_pals)}\n"
    text += f"#define GNG_PLAYER_PAL_FIRST {PLAYER_PAL_FIRST}\n#define GNG_PLAYER_PAL_COUNT {len(player_pals)}\n"
    text += f"#define GNG_ZOMBIE_PAL_FIRST {ZOMBIE_PAL_FIRST}\n#define GNG_ZOMBIE_PAL_COUNT {len(zombie_pals)}\n"
    text += f"#define GNG_BALL_PAL_FIRST {BALL_PAL_FIRST}\n#define GNG_BALL_PAL_COUNT {len(ball_pals)}\n\n"

    text += c_palette_matrix("GNG_BG_PALETTES", bg_pals) + "\n"
    text += c_palette_matrix("GNG_PLAYER_PALETTES", player_pals) + "\n"
    text += c_palette_matrix("GNG_ZOMBIE_PALETTES", zombie_pals) + "\n"
    text += c_palette_matrix("GNG_BALL_PALETTES", ball_pals) + "\n"
    text += c_u8_array("GNG_BG_PALMAP", bg_map) + "\n"
    text += c_u8_array("GNG_PLAYER_PALMAP", player_map) + "\n"
    text += c_u8_array("GNG_ZOMBIE_PALMAP", zombie_map) + "\n"
    text += c_u8_array("GNG_BALL_PALMAP", ball_map) + "\n"
    text += "#endif\n"
    path.write_text(text, encoding="utf-8")


def build(upstream: Path, target: Path) -> None:
    public = upstream / "game" / "public"
    retro_path = public / "backgrounds" / "l1_stadio_retro.png"
    ingresso_path = public / "backgrounds" / "l1_stadio_ingresso.png"
    player_path = public / "sprites" / "pl_shpendi_base.png"
    zombie_path = public / "sprites" / "en_zombie.png"

    for p in (retro_path, ingresso_path, player_path, zombie_path):
        if not p.is_file():
            raise SystemExit(f"Missing upstream runtime asset: {p}")

    assets = target / "assets"
    src = target / "src"
    assets.mkdir(parents=True, exist_ok=True)
    src.mkdir(parents=True, exist_ok=True)

    # 480px test world: one full real Manuzzi runtime panel plus half of the
    # adjacent real panel. This is enough to validate actual hardware scrolling.
    retro = ImageOps.fit(Image.open(retro_path).convert("RGBA"), (320, SCREEN_H), method=Image.Resampling.LANCZOS)
    ingresso = ImageOps.fit(Image.open(ingresso_path).convert("RGBA"), (320, SCREEN_H), method=Image.Resampling.LANCZOS)
    bg = Image.new("RGBA", (WORLD_W, SCREEN_H), (0, 0, 0, 255))
    bg.alpha_composite(retro, (0, 0))
    bg.alpha_composite(ingresso.crop((0, 0, 160, SCREEN_H)), (320, 0))

    player_sheet = crop_frames(Image.open(player_path), PLAYER_FRAME, FRAMES)
    zombie_sheet = crop_frames(Image.open(zombie_path), ZOMBIE_FRAME, FRAMES)
    ball = make_ball()

    bg_pals, bg_map = save_regional(bg, assets / "gng_l1_bg.gif", BG_REGION, False, BG_PAL_FIRST)
    player_pals, player_map = save_regional(player_sheet, assets / "gng_shpendi.gif", ACTOR_REGION, True, PLAYER_PAL_FIRST)
    zombie_pals, zombie_map = save_regional(zombie_sheet, assets / "gng_zombie.gif", ACTOR_REGION, True, ZOMBIE_PAL_FIRST)
    ball_pals, ball_map = save_regional(ball, assets / "gng_ball.gif", (16, 16), True, BALL_PAL_FIRST)

    emit_header(src / "gfx_assets.h", bg_pals, bg_map, player_pals, player_map, zombie_pals, zombie_map, ball_pals, ball_map)

    total_palettes = len(bg_pals) + len(player_pals) + len(zombie_pals) + len(ball_pals)
    if BALL_PAL_FIRST + len(ball_pals) > 256:
        raise SystemExit(f"Palette budget exceeded: last slot {BALL_PAL_FIRST + len(ball_pals) - 1}")

    print("Generated high-fidelity MVS visual slice:")
    print(f"  background : {assets / 'gng_l1_bg.gif'} ({WORLD_W}x{SCREEN_H}, {len(bg_pals)} local palettes)")
    print(f"  Shpendi    : {assets / 'gng_shpendi.gif'} ({len(player_pals)} local palettes)")
    print(f"  zombie     : {assets / 'gng_zombie.gif'} ({len(zombie_pals)} local palettes)")
    print(f"  ball       : {assets / 'gng_ball.gif'} ({len(ball_pals)} local palette)")
    print(f"  palette slots used by graphics: {total_palettes} / 255")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("upstream", type=Path)
    ap.add_argument("target", type=Path)
    args = ap.parse_args()
    build(args.upstream.resolve(), args.target.resolve())


if __name__ == "__main__":
    main()
