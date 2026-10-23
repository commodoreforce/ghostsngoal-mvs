#!/usr/bin/env python3
from PIL import Image, ImageDraw
import build_premium_opening_v2 as v2


def make_ground_from_master(city_master):
    """Verified clean field crop: authored grass starts at logical y=176.
    Extend it to a 64px gameplay strip with a restrained dark-earth base."""
    grass = city_master.crop((0, 176, 528, 224)).convert("RGBA")  # 528x48
    out = Image.new("RGBA", (528, 64), (33, 24, 17, 255))
    out.alpha_composite(grass, (0, 0))
    d = ImageDraw.Draw(out)
    d.rectangle((0, 48, 527, 63), fill=(39, 28, 19, 255))
    d.rectangle((0, 48, 527, 49), fill=(25, 76, 34, 255))
    for x in range(8, 528, 31):
        y = 55 + ((x * 3) % 6)
        d.rectangle((x, y, min(x + 3, 527), y), fill=(56, 40, 27, 255))
    return out


v2.make_ground_from_master = make_ground_from_master


def main():
    v2.main()


if __name__ == "__main__":
    main()
