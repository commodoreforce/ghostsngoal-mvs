#!/usr/bin/env python3
from __future__ import annotations
import importlib.util, sys
from pathlib import Path
from PIL import Image

HERE=Path(__file__).resolve().parent
P=HERE/'build_level1_full.py'
spec=importlib.util.spec_from_file_location('gng_level1_full',P)
mod=importlib.util.module_from_spec(spec)
assert spec and spec.loader
sys.modules[spec.name]=mod
spec.loader.exec_module(mod)

def preserve_native_background(path:Path,width=528,height=224):
    im=Image.open(path).convert('RGBA')
    if im.height==height:
        return im
    nw=round(im.width*height/im.height)
    return im.resize((nw,height),Image.Resampling.NEAREST)

mod.fit_background=preserve_native_background
if __name__=='__main__':
    mod.main()
