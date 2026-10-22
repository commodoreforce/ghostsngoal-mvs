#!/usr/bin/env python3
"""Build the fidelity-first Ghosts'n Goals Neo Geo visual slice.

Principles:
- never rescale the painted Level 1 backgrounds;
- keep painted layers separate (sky, city, stadium rear, stadium entrance, inside);
- quantize local regions independently so the Neo Geo palette model is used properly;
- preserve the original runtime player/enemy pixels 1:1 and pad only to 16x16 tiles.
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
SCREEN_W = 320
SCREEN_H = 224
FRAME_BOX = 64
CROM_BASE_TILE = 256

BG_KEYS = [
    ("SKY", "l1_sky.png", False),
    ("CITY", "l1_cesena.png", True),
    ("RETRO", "l1_stadio_retro.png", True),
    ("INGRESSO", "l1_stadio_ingresso.png", True),
    ("INSIDE", "l1_inside0.png", True),
]

# 10 x 3 local regions per painted layer: up to 30 palettes/layer.
# This gives a large visual improvement while keeping the whole scene safely
# inside the Neo Geo's 256 palette slots together with actors/HUD.
BG_GRID = (10, 3)


@dataclass
class Asset:
    name: str
    filename: str
    tile_base: int
    cols: int
    rows: int
    width: int
    height: int
    pal_first: int
    palettes: list[list[int]]
    palmap: list[int]


def align16(n: int) -> int:
    return (n + 15) & ~15


def neogeo_color(rgb: tuple[int, int, int]) -> int:
    """Match ngdevkit's RGB -> Neo Geo packed colour conversion."""
    r, g, b = [c >> 2 for c in rgb]
    darkbit = 1 if ((r & 1) + (g & 1) + (b & 1)) == 0 else 0
    r, g, b = r >> 1, g >> 1, b >> 1
    lsb = ((r & 1) << 2) | ((g & 1) << 1) | (b & 1)
    r, g, b = r >> 1, g >> 1, b >> 1
    return (darkbit << 15) | (lsb << 12) | (r << 8) | (g << 4) | b


def prepare_native_layer(path: Path) -> Image.Image:
    """Keep original pixels. Only crop/pad vertically to the 224px viewport and pad to tiles."""
    src = Image.open(path).convert("RGBA")
    width = align16(src.width)
    height = SCREEN_H
    out = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    crop_h = min(src.height, SCREEN_H)
    out.alpha_composite(src.crop((0, 0, src.width, crop_h)), (0, 0))
    return out


def neutral_gif_palette() -> list[int]:
    pal: list[int] = []
    for i in range(256):
        v = i if i < 16 else 0
        c = int(round(v * 255 / 15)) if i < 16 else 0
        pal += [c, c, c]
    return pal[:768]


def padded_qpal(q: Image.Image) -> list[int]:
    qpal = list(q.getpalette() or [])[:45]
    qpal += [0] * (45 - len(qpal))
    return qpal


def quantize_grid(
    img: Image.Image,
    transparent: bool,
    slot_first: int,
    grid_cols: int,
    grid_rows: int,
) -> tuple[Image.Image, list[list[int]], list[int]]:
    """Quantize aligned tile regions independently and emit an absolute palette slot per tile."""
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

            q = rgb.quantize(
                colors=15,
                method=Image.Quantize.MEDIANCUT,
                dither=Image.Dither.NONE,
            )
            qpal = padded_qpal(q)
            qpx = q.load()
            apx = alpha.load()

            slot = slot_first + len(palettes)
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
    if transparent:
        out.info["transparency"] = 0
    return out, palettes, palmap


def quantize_single_palette(
    img: Image.Image,
    transparent: bool,
    slot: int,
) -> tuple[Image.Image, list[list[int]], list[int]]:
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
            if transparent and apx[x, y] < 96:
                opx[x, y] = 0
            else:
                opx[x, y] = int(qpx[x, y]) + 1

    out.putpalette(neutral_gif_palette())
    if transparent:
        out.info["transparency"] = 0

    hw = [0x8000]
    for i in range(15):
        off = i * 3
        hw.append(neogeo_color((qpal[off], qpal[off + 1], qpal[off + 2])))

    cols, rows = rgba.width // TILE, rgba.height // TILE
    return out, [hw], [slot] * (cols * rows)


def save_indexed(img: Image.Image, path: Path, transparent: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if transparent:
        img.save(path, format="GIF", transparency=0, optimize=False)
    else:
        img.save(path, format="GIF", optimize=False)


def sheet_from_frames(sheet: Image.Image, fw: int, fh: int, frames: int) -> Image.Image:
    """Copy runtime frames pixel-for-pixel into 64x64 cells. No scaling."""
    if fw > FRAME_BOX or fh > FRAME_BOX:
        raise ValueError(f"frame {fw}x{fh} exceeds {FRAME_BOX}x{FRAME_BOX}")
    out = Image.new("RGBA", (FRAME_BOX * frames, FRAME_BOX), (0, 0, 0, 0))
    src = sheet.convert("RGBA")
    for i in range(frames):
        frame = src.crop((i * fw, 0, (i + 1) * fw, fh))
        x = i * FRAME_BOX + (FRAME_BOX - fw) // 2
        y = FRAME_BOX - fh
        out.alpha_composite(frame, (x, y))
    return out


def make_grass_tile() -> Image.Image:
    """Exact Level 1 grass tile from the web buildTiles() routine."""
    im = Image.new("RGBA", (16, 16), (42, 29, 20, 255))  # #2a1d14
    px = im.load()
    def rect(x0: int, y0: int, w: int, h: int, rgb: tuple[int, int, int]) -> None:
        for yy in range(y0, min(16, y0 + h)):
            for xx in range(x0, min(16, x0 + w)):
                px[xx, yy] = (*rgb, 255)
    rect(0, 0, 16, 4, (47, 107, 42))
    rect(0, 4, 16, 1, (29, 74, 28))
    for x in (1, 5, 9, 13):
        rect(x, 0, 1, 2, (74, 154, 58))
    rect(3, 8, 2, 1, (58, 42, 30))
    rect(10, 11, 3, 1, (58, 42, 30))
    return im


def make_ball() -> Image.Image:
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    px = im.load()
    for y in range(2, 14):
        for x in range(2, 14):
            dx, dy = x - 7.5, y - 7.5
            if dx * dx + dy * dy <= 34:
                px[x, y] = (244, 244, 240, 255)
    for x, y in [(7, 5), (8, 5), (6, 7), (9, 7), (7, 9), (8, 9)]:
        px[x, y] = (20, 20, 24, 255)
    return im


def c_palette_matrix(name: str, palettes: list[list[int]]) -> str:
    rows = []
    for pal in palettes:
        rows.append("    { " + ", ".join(f"0x{v:04x}" for v in pal) + " }")
    return (
        f"static const unsigned short {name}[{len(palettes)}][16] = {{\n"
        + ",\n".join(rows)
        + "\n};\n"
    )


def c_u8_array(name: str, values: Iterable[int]) -> str:
    vals = list(values)
    lines = []
    for i in range(0, len(vals), 24):
        lines.append("    " + ", ".join(str(v) for v in vals[i:i + 24]))
    return (
        f"static const unsigned char {name}[{len(vals)}] = {{\n"
        + ",\n".join(lines)
        + "\n};\n"
    )


def macro_asset(a: Asset) -> str:
    p = f"GNG_{a.name}"
    return (
        f"#define {p}_TILE {a.tile_base}\n"
        f"#define {p}_COLS {a.cols}\n"
        f"#define {p}_ROWS {a.rows}\n"
        f"#define {p}_W {a.width}\n"
        f"#define {p}_H {a.height}\n"
        f"#define {p}_PAL_FIRST {a.pal_first}\n"
        f"#define {p}_PAL_COUNT {len(a.palettes)}\n"
    )


def emit_header(
    path: Path,
    assets: list[Asset],
    player_frames: list[str],
    zombie_frames: list[str],
    total_palettes: list[list[int]],
) -> None:
    by_name = {a.name: a for a in assets}
    max_bg_w = max(by_name[n].width for n in ("SKY", "CITY", "RETRO", "INGRESSO", "INSIDE"))
    demo_world_w = max(960, min(4192, max_bg_w + 320))

    text = "#ifndef GNG_GFX_ASSETS_H\n#define GNG_GFX_ASSETS_H\n\n"
    text += "/* Fidelity-first assets generated from pinned runtime art. */\n"
    text += f"#define GNG_SCREEN_W {SCREEN_W}\n#define GNG_SCREEN_H {SCREEN_H}\n"
    text += f"#define GNG_DEMO_WORLD_W {demo_world_w}\n"
    text += f"#define GNG_PALETTE_COUNT {len(total_palettes)}\n\n"
    for a in assets:
        text += macro_asset(a) + "\n"

    text += f"#define GNG_PLAYER_FRAME_COUNT {len(player_frames)}\n"
    for i, name in enumerate(player_frames):
        text += f"#define GNG_PLAYER_FRAME_{name.upper()} {i}\n"
    text += f"#define GNG_ZOMBIE_FRAME_COUNT {len(zombie_frames)}\n"
    for i, name in enumerate(zombie_frames):
        text += f"#define GNG_ZOMBIE_FRAME_{name.upper()} {i}\n"
    text += "\n"

    text += c_palette_matrix("GNG_PALETTES", total_palettes) + "\n"
    for a in assets:
        text += c_u8_array(f"GNG_{a.name}_PALMAP", a.palmap) + "\n"
    text += "#endif\n"
    path.write_text(text, encoding="utf-8")


def build(upstream: Path, target: Path) -> None:
    public = upstream / "game" / "public"
    backgrounds = public / "backgrounds"
    sprites = public / "sprites"
    assets_dir = target / "assets"
    src_dir = target / "src"
    assets_dir.mkdir(parents=True, exist_ok=True)
    src_dir.mkdir(parents=True, exist_ok=True)

    manifest = json.loads((sprites / "manifest.json").read_text(encoding="utf-8"))
    enemies = json.loads((sprites / "enemies.json").read_text(encoding="utf-8"))
    sh = manifest["shpendi"]
    zo = enemies["zombie"]

    tile_cursor = CROM_BASE_TILE
    palette_cursor = 1
    all_palettes: list[list[int]] = []
    built: list[Asset] = []

    def add_asset(
        name: str,
        filename: str,
        rgba: Image.Image,
        transparent: bool,
        grid: tuple[int, int] | None,
    ) -> Asset:
        nonlocal tile_cursor, palette_cursor

        if rgba.width % TILE or rgba.height % TILE:
            raise ValueError(f"{name} is not tile aligned: {rgba.size}")

        if grid is None:
            indexed, pals, palmap = quantize_single_palette(rgba, transparent, palette_cursor)
        else:
            indexed, pals, palmap = quantize_grid(
                rgba, transparent, palette_cursor, grid[0], grid[1]
            )

        save_indexed(indexed, assets_dir / filename, transparent)
        cols, rows = rgba.width // TILE, rgba.height // TILE
        a = Asset(
            name=name,
            filename=filename,
            tile_base=tile_cursor,
            cols=cols,
            rows=rows,
            width=rgba.width,
            height=rgba.height,
            pal_first=palette_cursor,
            palettes=pals,
            palmap=palmap,
        )
        built.append(a)
        all_palettes.extend(pals)
        tile_cursor += cols * rows
        palette_cursor += len(pals)
        return a

    for key, fn, transparent in BG_KEYS:
        path = backgrounds / fn
        if not path.is_file():
            raise SystemExit(f"Missing painted Level 1 layer: {path}")
        add_asset(
            key,
            f"gng_{key.lower()}.gif",
            prepare_native_layer(path),
            transparent,
            BG_GRID,
        )

    player_path = sprites / "pl_shpendi_base.png"
    zombie_path = sprites / f"{zo['sheet']}.png"

    player = sheet_from_frames(
        Image.open(player_path),
        int(sh["frameWidth"]),
        int(sh["frameHeight"]),
        len(sh["frames"]),
    )
    zombie = sheet_from_frames(
        Image.open(zombie_path),
        int(zo["frameWidth"]),
        int(zo["frameHeight"]),
        len(zo["frames"]),
    )

    add_asset("GROUND", "gng_ground.gif", make_grass_tile(), False, None)
    add_asset("PLAYER", "gng_shpendi.gif", player, True, None)
    add_asset("ZOMBIE", "gng_zombie.gif", zombie, True, None)
    add_asset("BALL", "gng_ball.gif", make_ball(), True, None)

    if palette_cursor > 256:
        raise SystemExit(
            f"Neo Geo palette budget exceeded: slots 0..{palette_cursor - 1} "
            f"({palette_cursor} total). Reduce BG_GRID."
        )

    emit_header(
        src_dir / "gfx_assets.h",
        built,
        list(sh["frames"]),
        list(zo["frames"]),
        all_palettes,
    )

    print("Generated FIDELITY-FIRST Neo Geo assets:")
    for a in built:
        print(
            f"  {a.name:8s} {a.width:4d}x{a.height:<3d} "
            f"tiles={a.cols * a.rows:4d} palettes={len(a.palettes):2d} "
            f"tilebase={a.tile_base}"
        )
    print(f"  palette slots used: {palette_cursor - 1} / 255")
    print("  background scaling: NONE (native pixels preserved)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("upstream", type=Path)
    ap.add_argument("target", type=Path)
    args = ap.parse_args()
    build(args.upstream.resolve(), args.target.resolve())


if __name__ == "__main__":
    main()
