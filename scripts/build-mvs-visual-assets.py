#!/usr/bin/env python3
"""Build a fidelity-first Ghosts'n Goals Neo Geo visual slice.

The web game is authored in a 256x224 logical viewport and displayed as 4:3.
Neo Geo is 320x224 and also displayed as 4:3, so preserving the same perceived
geometry requires a 5:4 horizontal projection when art is encoded in C-ROM.
Y is never rescaled. Gameplay/camera stay in the original 256x224 logical space.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image

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

BG_KEYS = [
    ("SKY", "l1_sky.png", False, 1, (8, 3)),
    ("CITY", "l1_cesena.png", True, 33, (8, 4)),
    # Stage variants reuse palette slots 65..128. Only one is visible at once.
    ("RETRO", "l1_stadio_retro.png", True, 65, (12, 4)),
    ("INGRESSO", "l1_stadio_ingresso.png", True, 65, (12, 4)),
    ("INSIDE", "l1_inside0.png", True, 65, (12, 4)),
]

PLAYER_PAL_FIRST = 129
ZOMBIE_PAL_FIRST = 130
BALL_PAL_FIRST = 131
GROUND_PAL_FIRST = 132
FOG_PAL_FIRST = 133


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
    qpal = list(q.getpalette() or [])[:45]
    qpal += [0] * (45 - len(qpal))
    return qpal


def neutral_gif_palette() -> list[int]:
    pal: list[int] = []
    for i in range(256):
        v = i if i < 16 else 0
        c = int(round(v * 255 / 15)) if i < 16 else 0
        pal += [c, c, c]
    return pal[:768]


def project_layer(path: Path) -> tuple[Image.Image, int]:
    src = Image.open(path).convert("RGBA")
    logical_w = src.width
    crop_h = min(src.height, SCREEN_H)
    src = src.crop((0, 0, src.width, crop_h))
    projected_w = project_x(src.width)
    projected = src.resize((projected_w, crop_h), Image.Resampling.NEAREST)
    out = Image.new("RGBA", (align16(projected_w), SCREEN_H), (0, 0, 0, 0))
    out.alpha_composite(projected, (0, 0))
    return out, logical_w


def quantize_grid(img: Image.Image, transparent: bool, slot_first: int,
                  grid_cols: int, grid_rows: int) -> tuple[Image.Image, list[list[int]], list[int]]:
    rgba = img.convert("RGBA")
    tiles_w = rgba.width // TILE
    tiles_h = rgba.height // TILE
    chunk_tw = max(1, math.ceil(tiles_w / grid_cols))
    chunk_th = max(1, math.ceil(tiles_h / grid_rows))
    out = Image.new("P", rgba.size, 0)
    out_px = out.load()
    palmap = [0] * (tiles_w * tiles_h)
    palettes: list[list[int]] = []

    for ty0 in range(0, tiles_h, chunk_th):
        for tx0 in range(0, tiles_w, chunk_tw):
            tx1 = min(tiles_w, tx0 + chunk_tw)
            ty1 = min(tiles_h, ty0 + chunk_th)
            x0, y0 = tx0 * TILE, ty0 * TILE
            x1, y1 = tx1 * TILE, ty1 * TILE
            crop = rgba.crop((x0, y0, x1, y1))
            alpha = crop.getchannel("A")
            rgb = Image.new("RGB", crop.size, (0, 0, 0))
            rgb.paste(crop.convert("RGB"), mask=alpha)
            q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            qpal = padded_qpal(q)
            qpx, apx = q.load(), alpha.load()
            slot = slot_first + len(palettes)
            if slot > 255:
                raise ValueError(f"palette slot overflow for {slot_first=}: {slot}")
            hw = [0x8000]
            for i in range(15):
                off = i * 3
                hw.append(neogeo_color((qpal[off], qpal[off + 1], qpal[off + 2])))
            palettes.append(hw)
            for y in range(crop.height):
                for x in range(crop.width):
                    if transparent and apx[x, y] < 96:
                        out_px[x0 + x, y0 + y] = 0
                    else:
                        out_px[x0 + x, y0 + y] = int(qpx[x, y]) + 1
            for ty in range(ty0, ty1):
                for tx in range(tx0, tx1):
                    palmap[ty * tiles_w + tx] = slot

    out.putpalette(neutral_gif_palette())
    if transparent: out.info["transparency"] = 0
    return out, palettes, palmap


def quantize_single_palette(img: Image.Image, transparent: bool, slot: int) -> tuple[Image.Image, list[list[int]], list[int]]:
    rgba = img.convert("RGBA")
    alpha = rgba.getchannel("A")
    rgb = Image.new("RGB", rgba.size, (0, 0, 0))
    rgb.paste(rgba.convert("RGB"), mask=alpha)
    q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    qpal = padded_qpal(q)
    out = Image.new("P", rgba.size, 0)
    opx, qpx, apx = out.load(), q.load(), alpha.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            if transparent and apx[x, y] < 96: opx[x, y] = 0
            else: opx[x, y] = int(qpx[x, y]) + 1
    out.putpalette(neutral_gif_palette())
    if transparent: out.info["transparency"] = 0
    hw = [0x8000]
    for i in range(15):
        off = i * 3
        hw.append(neogeo_color((qpal[off], qpal[off + 1], qpal[off + 2])))
    cols, rows = rgba.width // TILE, rgba.height // TILE
    return out, [hw], [slot] * (cols * rows)


def save_indexed(img: Image.Image, path: Path, transparent: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if transparent: img.save(path, format="GIF", transparency=0, optimize=False)
    else: img.save(path, format="GIF", optimize=False)


def project_sheet_frames(sheet: Image.Image, fw: int, fh: int, frames: int) -> Image.Image:
    projected_fw = project_x(fw)
    if projected_fw > FRAME_CELL_W or fh > FRAME_CELL_H:
        raise ValueError(f"projected frame {projected_fw}x{fh} exceeds {FRAME_CELL_W}x{FRAME_CELL_H}")
    src = sheet.convert("RGBA")
    out = Image.new("RGBA", (FRAME_CELL_W * frames, FRAME_CELL_H), (0, 0, 0, 0))
    for i in range(frames):
        frame = src.crop((i * fw, 0, (i + 1) * fw, fh))
        frame = frame.resize((projected_fw, fh), Image.Resampling.NEAREST)
        x = i * FRAME_CELL_W + (FRAME_CELL_W - projected_fw) // 2
        y = FRAME_CELL_H - fh
        out.alpha_composite(frame, (x, y))
    return out


def make_grass_tile() -> Image.Image:
    logical = Image.new("RGBA", (16, 16), (42, 29, 20, 255))
    px = logical.load()
    def rect(x0: int, y0: int, w: int, h: int, rgb: tuple[int, int, int]) -> None:
        for yy in range(y0, min(16, y0 + h)):
            for xx in range(x0, min(16, x0 + w)):
                px[xx, yy] = (*rgb, 255)
    rect(0, 0, 16, 4, (47, 107, 42)); rect(0, 4, 16, 1, (29, 74, 28))
    for x in (1, 5, 9, 13): rect(x, 0, 1, 2, (74, 154, 58))
    rect(3, 8, 2, 1, (58, 42, 30)); rect(10, 11, 3, 1, (58, 42, 30))
    return logical


def make_ball() -> Image.Image:
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0)); px = im.load()
    for y in range(4, 12):
        for x in range(3, 13):
            dx = (x - 7.5) / 1.25; dy = y - 7.5
            if dx * dx + dy * dy <= 14: px[x, y] = (244, 244, 240, 255)
    for x, y in ((7, 6), (9, 7), (6, 9), (9, 10)): px[x, y] = (20, 20, 24, 255)
    return im


def make_fog() -> Image.Image:
    im = Image.new("RGBA", (320, 64), (0, 0, 0, 0)); px = im.load()
    for y in range(64):
        for x in range(320):
            band = (x // 4 + y // 3) & 3
            if band in (0, 1) and 5 < y < 60:
                a = 220 if 18 < y < 48 else 150
                px[x, y] = (190, 174, 220, a)
    return im


def c_palette_matrix(name: str, palettes: list[list[int]]) -> str:
    rows = ["    { " + ", ".join(f"0x{v:04x}" for v in pal) + " }" for pal in palettes]
    return f"static const unsigned short {name}[{len(palettes)}][16] = {{\n" + ",\n".join(rows) + "\n};\n"


def c_u8_array(name: str, values: Iterable[int]) -> str:
    vals = list(values)
    lines = ["    " + ", ".join(str(v) for v in vals[i:i + 24]) for i in range(0, len(vals), 24)]
    return f"static const unsigned char {name}[{len(vals)}] = {{\n" + ",\n".join(lines) + "\n};\n"


def macro_asset(a: Asset) -> str:
    p = f"GNG_{a.name}"
    return (f"#define {p}_TILE {a.tile_base}\n#define {p}_COLS {a.cols}\n#define {p}_ROWS {a.rows}\n"
            f"#define {p}_W {a.width}\n#define {p}_H {a.height}\n#define {p}_LOGICAL_W {a.logical_width}\n"
            f"#define {p}_PAL_FIRST {a.pal_first}\n#define {p}_PAL_COUNT {len(a.palettes)}\n")


def emit_header(path: Path, assets: list[Asset], player_frames: list[str], zombie_frames: list[str]) -> None:
    by_name = {a.name: a for a in assets}
    max_logical_bg_w = max(by_name[n].logical_width for n in ("SKY", "CITY", "RETRO", "INGRESSO", "INSIDE"))
    demo_world_w = max(960, min(4192, max_logical_bg_w + LOGICAL_W))
    text = "#ifndef GNG_GFX_ASSETS_H\n#define GNG_GFX_ASSETS_H\n\n"
    text += "/* Web-space 256x224 -> Neo Geo 320x224 projected assets. */\n"
    text += f"#define GNG_LOGICAL_W {LOGICAL_W}\n#define GNG_SCREEN_W {SCREEN_W}\n#define GNG_SCREEN_H {SCREEN_H}\n"
    text += f"#define GNG_X_NUM {X_NUM}\n#define GNG_X_DEN {X_DEN}\n#define GNG_DEMO_WORLD_W {demo_world_w}\n\n"
    for a in assets:
        text += macro_asset(a) + "\n"
        text += c_palette_matrix(f"GNG_{a.name}_PALETTES", a.palettes) + "\n"
        text += c_u8_array(f"GNG_{a.name}_PALMAP", a.palmap) + "\n"
    text += f"#define GNG_PLAYER_FRAME_COUNT {len(player_frames)}\n"
    for i, name in enumerate(player_frames): text += f"#define GNG_PLAYER_FRAME_{name.upper()} {i}\n"
    text += f"#define GNG_ZOMBIE_FRAME_COUNT {len(zombie_frames)}\n"
    for i, name in enumerate(zombie_frames): text += f"#define GNG_ZOMBIE_FRAME_{name.upper()} {i}\n"
    text += f"#define GNG_ACTOR_COLS {FRAME_CELL_W // TILE}\n#define GNG_ACTOR_ROWS {FRAME_CELL_H // TILE}\n"
    text += "#endif\n"; path.write_text(text, encoding="utf-8")


def build(upstream: Path, target: Path) -> None:
    public = upstream / "game" / "public"; backgrounds, sprites = public / "backgrounds", public / "sprites"
    assets_dir, src_dir = target / "assets", target / "src"; assets_dir.mkdir(parents=True, exist_ok=True); src_dir.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((sprites / "manifest.json").read_text(encoding="utf-8")); enemies = json.loads((sprites / "enemies.json").read_text(encoding="utf-8"))
    sh, zo = manifest["shpendi"], enemies["zombie"]; tile_cursor = CROM_BASE_TILE; built: list[Asset] = []

    def add_asset(name: str, filename: str, rgba: Image.Image, logical_w: int, transparent: bool,
                  pal_first: int, grid: tuple[int, int] | None) -> Asset:
        nonlocal tile_cursor
        if rgba.width % TILE or rgba.height % TILE: raise ValueError(f"{name} is not tile aligned: {rgba.size}")
        if grid is None: indexed, pals, palmap = quantize_single_palette(rgba, transparent, pal_first)
        else: indexed, pals, palmap = quantize_grid(rgba, transparent, pal_first, grid[0], grid[1])
        save_indexed(indexed, assets_dir / filename, transparent)
        cols, rows = rgba.width // TILE, rgba.height // TILE
        a = Asset(name, filename, tile_cursor, cols, rows, rgba.width, rgba.height, logical_w, pal_first, pals, palmap)
        built.append(a); tile_cursor += cols * rows; return a

    for key, fn, transparent, pal_first, grid in BG_KEYS:
        path = backgrounds / fn
        if not path.is_file(): raise SystemExit(f"Missing painted Level 1 layer: {path}")
        projected, logical_w = project_layer(path)
        add_asset(key, f"gng_{key.lower()}.gif", projected, logical_w, transparent, pal_first, grid)

    player = project_sheet_frames(Image.open(sprites / "pl_shpendi_base.png"), int(sh["frameWidth"]), int(sh["frameHeight"]), len(sh["frames"]))
    zombie = project_sheet_frames(Image.open(sprites / f"{zo['sheet']}.png"), int(zo["frameWidth"]), int(zo["frameHeight"]), len(zo["frames"]))
    add_asset("GROUND", "gng_ground.gif", make_grass_tile(), 16, False, GROUND_PAL_FIRST, None)
    add_asset("PLAYER", "gng_shpendi.gif", player, FRAME_CELL_LOGICAL_W * len(sh["frames"]), True, PLAYER_PAL_FIRST, None)
    add_asset("ZOMBIE", "gng_zombie.gif", zombie, FRAME_CELL_LOGICAL_W * len(zo["frames"]), True, ZOMBIE_PAL_FIRST, None)
    add_asset("BALL", "gng_ball.gif", make_ball(), 13, True, BALL_PAL_FIRST, None)
    add_asset("FOG", "gng_fog.gif", make_fog(), LOGICAL_W, True, FOG_PAL_FIRST, (5, 1))

    emit_header(src_dir / "gfx_assets.h", built, list(sh["frames"]), list(zo["frames"]))
    print("Generated WEB-GEOMETRY Neo Geo assets:")
    for a in built: print(f"  {a.name:8s} physical={a.width:4d}x{a.height:<3d} logicalW={a.logical_width:4d} palettes={len(a.palettes):2d} tilebase={a.tile_base}")
    print("  geometry: 256x224 logical -> 320x224 C-ROM (X 5:4, Y 1:1)")
    print("  gameplay coordinates remain in original web logical space")


def main() -> None:
    ap = argparse.ArgumentParser(); ap.add_argument("upstream", type=Path); ap.add_argument("target", type=Path)
    args = ap.parse_args(); build(args.upstream.resolve(), args.target.resolve())


if __name__ == "__main__": main()
