#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
BASE_PATH = HERE / "build-mvs-visual-assets.py"
spec = importlib.util.spec_from_file_location("gng_v4_base", BASE_PATH)
assert spec and spec.loader
base = importlib.util.module_from_spec(spec)
# Python 3.14 dataclasses resolves postponed annotations through sys.modules.
# Register the dynamically loaded module before executing it, exactly as the
# normal import machinery would do.
sys.modules[spec.name] = base
spec.loader.exec_module(base)

# V5 rule: where the web already has a final separated layer, prefer it.
# Force the base builder to use runtime l1_sky/l1_cesena rather than reinterpreting
# the pre-web master pack for those two layers.
base.read_zip_image = lambda pack, inner: None


def make_sky_webfaithful(sky: Image.Image) -> Image.Image:
    out = sky.convert("RGBA").copy()
    # The persistent left-side "sbordo" is a low cloud mass that leaks into the
    # gameplay band in the MVS composition. Remove only that lower-left leak,
    # leaving the moon and upper cloud bank untouched.
    bg = base.dominant_dark_sky(out)
    d = ImageDraw.Draw(out)
    d.rectangle((0, 146, 31, 223), fill=bg)
    return out


def make_ground_webfaithful(_city: Image.Image) -> Image.Image:
    # Exact colours/geometry from web buildTiles(): grass + dirt.
    W = base.WORLD_W
    out = Image.new("RGBA", (W, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    for ox in range(0, W, 16):
        # grass tile
        d.rectangle((ox, 0, min(ox + 15, W - 1), 15), fill=(0x2a, 0x1d, 0x14, 255))
        d.rectangle((ox, 0, min(ox + 15, W - 1), 3), fill=(0x2f, 0x6b, 0x2a, 255))
        d.rectangle((ox, 4, min(ox + 15, W - 1), 4), fill=(0x1d, 0x4a, 0x1c, 255))
        for x in (1, 5, 9, 13):
            if ox + x < W:
                d.rectangle((ox + x, 0, ox + x, 1), fill=(0x4a, 0x9a, 0x3a, 255))
        if ox + 4 < W:
            d.rectangle((ox + 3, 8, ox + 4, 8), fill=(0x3a, 0x2a, 0x1e, 255))
        if ox + 12 < W:
            d.rectangle((ox + 10, 11, ox + 12, 11), fill=(0x3a, 0x2a, 0x1e, 255))
        # dirt tile
        d.rectangle((ox, 16, min(ox + 15, W - 1), 31), fill=(0x2a, 0x1d, 0x14, 255))
        if ox + 4 < W:
            d.rectangle((ox + 3, 20, ox + 4, 20), fill=(0x3a, 0x2a, 0x1e, 255))
        if ox + 12 < W:
            d.rectangle((ox + 10, 25, ox + 12, 25), fill=(0x3a, 0x2a, 0x1e, 255))
        if ox + 5 < W:
            d.rectangle((ox + 5, 29, ox + 5, 29), fill=(0x3a, 0x2a, 0x1e, 255))
    return out


def make_foreground_webfaithful() -> Image.Image:
    # Rebuild the exact web fg_cemetery motif, but on a wider finite strip so
    # 1.35x foreground parallax can scroll through the whole benchmark without
    # wrap/pop-in.
    W = 640
    im = Image.new("RGBA", (W, 224), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = (0x06, 0x04, 0x0a, 255)

    # Original web motif at authored positions.
    for x in range(60, 120, 8):
        d.rectangle((x, 194, x + 1, 223), fill=c)
        d.rectangle((x - 1, 190, x + 2, 193), fill=c)
        d.rectangle((x, 188, x + 1, 189), fill=c)
    d.rectangle((56, 200, 121, 201), fill=c)

    for k in range(7):
        x = 330 + k * 5
        y = 200 - (k % 3) * 8
        d.rectangle((x, y, x + 1, 223), fill=c)
        x2 = 326 + k * 6
        y2 = 196 - (k % 2) * 6
        d.rectangle((x2, y2, x2 + 5, y2 + 1), fill=c)

    d.rectangle((440, 178, 443, 223), fill=c)
    d.rectangle((432, 186, 451, 189), fill=c)

    # Continue the strip with sparse, unique silhouettes rather than repeating
    # the same fence/cross pattern. This keeps depth without visible tiling.
    for x in range(535, 625, 18):
        d.rectangle((x, 201, x + 1, 223), fill=c)
        d.line((x, 200, x + 4, 195), fill=c)
    return im


base.make_sky = make_sky_webfaithful
base.make_ground = make_ground_webfaithful
base.make_foreground = make_foreground_webfaithful

if __name__ == "__main__":
    print("V5 web-faithful overrides active: runtime sky/city, exact web ground, exact fg_cemetery")
    base.main()
