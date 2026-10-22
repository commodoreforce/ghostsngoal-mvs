#!/usr/bin/env python3
"""Build the first real Ghosts'n Goals Neo Geo visual slice.

The source art is read from a pinned, read-only checkout of the web game.
Nothing is modified in the upstream repository. Runtime Phaser assets are
converted into Neo Geo friendly 16x16-tile GIF sheets with one 16-color
palette per visual object (color 0 reserved for transparency).
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageOps

SCREEN = (320, 224)
TILE = 16
PLAYER_FRAME = (56, 54)
ZOMBIE_FRAME = (50, 48)
FRAME_BOX = (64, 64)
FRAMES = 4

# ngdevkit's stock C-ROM occupies tiles 0..255.
BG_TILE = 256
BG_TILES = (SCREEN[0] // TILE) * (SCREEN[1] // TILE)  # 280
PLAYER_TILE = BG_TILE + BG_TILES                       # 536
PLAYER_TILES = (FRAMES * FRAME_BOX[0] // TILE) * (FRAME_BOX[1] // TILE)  # 64
ZOMBIE_TILE = PLAYER_TILE + PLAYER_TILES               # 600
ZOMBIE_TILES = PLAYER_TILES                            # 64
BALL_TILE = ZOMBIE_TILE + ZOMBIE_TILES                 # 664


def neogeo_color(rgb: tuple[int, int, int]) -> int:
    """Convert 8-bit RGB to the simple 4:4:4 format used by ngdevkit examples."""
    r, g, b = rgb
    r4 = int(round(r * 15 / 255)) & 0xF
    g4 = int(round(g * 15 / 255)) & 0xF
    b4 = int(round(b * 15 / 255)) & 0xF
    return (r4 << 8) | (g4 << 4) | b4


def quantize_15(img: Image.Image, transparent: bool) -> tuple[Image.Image, list[int]]:
    """Return indexed image using indices 1..15; index 0 is transparent/reserved."""
    rgba = img.convert("RGBA")
    alpha = rgba.getchannel("A")
    rgb = Image.new("RGB", rgba.size, (0, 0, 0))
    rgb.paste(rgba.convert("RGB"), mask=alpha)

    # Keep one hardware color free for transparency.
    q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)

    # Pillow may return only the entries actually used by a low-color image
    # (the football is a good example), rather than a full 15-color table.
    # Pad to exactly 15 RGB entries so both the GIF and Neo Geo hardware
    # palette generation can always address indices 1..15 safely.
    qpal = list(q.getpalette() or [])[: 15 * 3]
    qpal += [0] * ((15 * 3) - len(qpal))

    src = q.load()
    a = alpha.load()

    out = Image.new("P", rgba.size, 0)
    dst = out.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            if transparent and a[x, y] < 96:
                dst[x, y] = 0
            else:
                dst[x, y] = int(src[x, y]) + 1

    # GIF palette: index 0 is transparent black; source palette shifts to 1..15.
    pal = [0, 0, 0] + qpal
    pal += [0] * (768 - len(pal))
    out.putpalette(pal)
    out.info["transparency"] = 0

    hw = [0x8000]
    for i in range(15):
        off = i * 3
        hw.append(neogeo_color((qpal[off], qpal[off + 1], qpal[off + 2])))
    return out, hw


def save_indexed(img: Image.Image, path: Path, transparent: bool) -> list[int]:
    indexed, palette = quantize_15(img, transparent=transparent)
    path.parent.mkdir(parents=True, exist_ok=True)
    indexed.save(path, format="GIF", transparency=0, optimize=False)
    return palette


def crop_frames(sheet: Image.Image, frame_size: tuple[int, int], count: int) -> Image.Image:
    fw, fh = frame_size
    bw, bh = FRAME_BOX
    out = Image.new("RGBA", (bw * count, bh), (0, 0, 0, 0))
    for i in range(count):
        frame = sheet.crop((i * fw, 0, (i + 1) * fw, fh)).convert("RGBA")
        # Preserve the original runtime pixels exactly: no resampling. Only pad to tiles.
        x = i * bw + (bw - fw) // 2
        y = bh - fh
        out.alpha_composite(frame, (x, y))
    return out


def make_ball() -> Image.Image:
    # A simple 16x16 football placeholder for the first hardware slice.
    # It is deliberately tiny and will be replaced by the exact runtime ball treatment later.
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    px = im.load()
    for y in range(2, 14):
        for x in range(2, 14):
            dx, dy = x - 7.5, y - 7.5
            if dx * dx + dy * dy <= 34:
                px[x, y] = (240, 240, 240, 255)
    for x, y in [(7, 5), (8, 5), (6, 7), (9, 7), (7, 9), (8, 9)]:
        px[x, y] = (25, 25, 25, 255)
    return im


def emit_header(path: Path, palettes: dict[str, Iterable[int]]) -> None:
    def arr(name: str, values: Iterable[int]) -> str:
        vals = list(values)
        body = ", ".join(f"0x{v:04x}" for v in vals)
        return f"static const unsigned short {name}[16] = {{ {body} }};\n"

    text = "#ifndef GNG_GFX_ASSETS_H\n#define GNG_GFX_ASSETS_H\n\n"
    text += "/* Generated from the pinned runtime assets of commodoreforce/ghostsngoal. */\n"
    text += f"#define GNG_TILE_BG {BG_TILE}\n"
    text += f"#define GNG_TILE_PLAYER {PLAYER_TILE}\n"
    text += f"#define GNG_TILE_ZOMBIE {ZOMBIE_TILE}\n"
    text += f"#define GNG_TILE_BALL {BALL_TILE}\n"
    text += "#define GNG_BG_COLS 20\n#define GNG_BG_ROWS 14\n"
    text += "#define GNG_ACTOR_COLS 4\n#define GNG_ACTOR_ROWS 4\n#define GNG_ACTOR_STRIDE 16\n\n"
    text += arr("GNG_PAL_BG", palettes["bg"])
    text += arr("GNG_PAL_PLAYER", palettes["player"])
    text += arr("GNG_PAL_ZOMBIE", palettes["zombie"])
    text += arr("GNG_PAL_BALL", palettes["ball"])
    text += "\n#endif\n"
    path.write_text(text, encoding="utf-8")


def build(upstream: Path, target: Path) -> None:
    public = upstream / "game" / "public"
    bg_path = public / "backgrounds" / "l1_stadio_retro.png"
    player_path = public / "sprites" / "pl_shpendi_base.png"
    zombie_path = public / "sprites" / "en_zombie.png"

    for p in (bg_path, player_path, zombie_path):
        if not p.is_file():
            raise SystemExit(f"Missing upstream runtime asset: {p}")

    assets = target / "assets"
    src = target / "src"
    assets.mkdir(parents=True, exist_ok=True)
    src.mkdir(parents=True, exist_ok=True)

    # First visible hardware slice: use the real painted Manuzzi layer, fit to the
    # Neo Geo viewport. Full multi-plane parallax is the next milestone.
    bg_src = Image.open(bg_path).convert("RGBA")
    bg = ImageOps.fit(bg_src, SCREEN, method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))

    player_sheet = crop_frames(Image.open(player_path), PLAYER_FRAME, FRAMES)
    zombie_sheet = crop_frames(Image.open(zombie_path), ZOMBIE_FRAME, FRAMES)
    ball = make_ball()

    palettes = {
        "bg": save_indexed(bg, assets / "gng_l1_bg.gif", transparent=False),
        "player": save_indexed(player_sheet, assets / "gng_shpendi.gif", transparent=True),
        "zombie": save_indexed(zombie_sheet, assets / "gng_zombie.gif", transparent=True),
        "ball": save_indexed(ball, assets / "gng_ball.gif", transparent=True),
    }
    emit_header(src / "gfx_assets.h", palettes)

    print("Generated MVS visual slice:")
    print(f"  background : {assets / 'gng_l1_bg.gif'} (320x224, tile {BG_TILE})")
    print(f"  Shpendi    : {assets / 'gng_shpendi.gif'} (4 exact runtime frames, tile {PLAYER_TILE})")
    print(f"  zombie     : {assets / 'gng_zombie.gif'} (4 exact runtime frames, tile {ZOMBIE_TILE})")
    print(f"  ball       : {assets / 'gng_ball.gif'} (tile {BALL_TILE})")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("upstream", type=Path)
    ap.add_argument("target", type=Path)
    args = ap.parse_args()
    build(args.upstream.resolve(), args.target.resolve())


if __name__ == "__main__":
    main()
