# Ghosts 'n Goals — Neo Geo MVS

Native 68000/Z80 port of `commodoreforce/ghostsngoal`. The web repository is a
read-only gameplay and art reference, pinned to
`7d6d995aa32a6a3df23d23ca857852f85bdc99c2`.

## Play the prebuilt milestone

Install MAME (on macOS: `brew install mame`), then run from this checkout:

```sh
python3 scripts/play-mame.py
```

The launcher verifies and extracts `releases/gngl1r1-r1.zip`. Development tools
are not needed to play it. Controls with MAME's default keyboard mapping:

- `5`: insert coin; `1`: start.
- Arrow keys: move; `Ctrl`: jump; `Alt`: shoot.
- Up + shoot: diagonal upward; down + shoot in the air: diagonal downward.
- Down: crouch; down + jump on a plank: drop through.

The included free ngdevkit BIOS differs from the SNK BIOS images in MAME's
built-in hashes. MAME reports warnings about **BIOS** files `sp-s2.sp1`,
`sm1.sm1` and `sfix.sfix`; the cartridge has its own checked software-list
CRC/SHA1 hashes. The bundle includes BIOS source/license references.

## Current: visual revision 3

Update with `git pull --ff-only origin main`, then launch as above. The title
and launcher identify **VISUAL REVISION 3**. The checked-in bundle is rebuilt
and validated; no local toolchain or compilation is necessary.

- Background conversion now uses 49 local palettes per exterior and 84 per
  interior, rather than eight. Two mutually exclusive scene banks keep within
  the Neo Geo's 256 palettes, with players always resident. Visible interior
  colour counts rise from 92/82 to 310/293 in the converted artwork.
- Transparent pixels no longer consume actor colours or darken opaque edges.
  Enemy animation bands share palettes across poses to avoid colour pumping.
- Bats use the source 0/1/2/1 wing cycle; zombie, ghost and boss walking cadence
  follow the source animation rates more closely. Player movement is unchanged.
- Rotating ball, native source shoe/laugh effects, impact sparks and landing
  dust add motion and hit/landing feedback without changing collision rules.
- Immutable scenery columns are cached; fine scrolling updates position only.
  This removes repeated full tilemap uploads and leaves time for scene palettes.

[Actual MAME animation](releases/visual-revision3.gif) ·
[Colour comparison against the source](docs/validation/visual-revision3-colors.json).
The colour comparison uses the same approximate hardware RGB decoder for both
revisions; it measures conversion fidelity, not artistic quality or CRT output.
MAME validation also reads back every scene palette and resident player palette.

This is still a graphics improvement milestone. Fog/lightning, full foreground
composition, interior-entry transition and source animation/AI parity need more
work. The revision reuses source animation drawings; it does not add new poses.

## Previous visual revision 2

The prebuilt release now shows **VISUAL REVISION 2** on the title screen.
It includes the original Press Start font as native title graphics, animated
menu ghosts, gradual rear/front stadium replacement, corrected sign placement,
finite parallax and sparse native rain. The stadium interior retains its full
998-pixel source width instead of being squeezed to 528 pixels; its scrolling
is clamped and its roof openings preserve the distant sky/city. Finite LED
boards, checkpoint flag and shaded pit edges follow the web Level 1 data.

This is a visual improvement milestone, not full web parity: interior entry
still uses a cut, sky/city behind the architecture are precomposited to limit
sprite cost, and dynamic fog/lightning and foreground weather remain simplified.
Character and enemy gameplay/art are unchanged in this revision.

## Build

On macOS, install prerequisites with `scripts/bootstrap-ngdevkit-macos.sh`.
The Linux cloud image supports a rootless, signature-verified setup:

```sh
bash scripts/bootstrap-ngdevkit-linux.sh
. scripts/activate-cloud-toolchain.sh
```

Then build the cartridge from canonical sources and pinned web assets:

```sh
make test
bash scripts/run-full-level1-r1.sh --build-only
```

Omit `--build-only` to build, verify and launch MAME. `GNG_UPSTREAM` may point
to an existing clean reference checkout at the pinned commit. See
[cloud setup](docs/CLOUD_SETUP.md) for tool activation and validation.

## R1 scope

The native runtime includes the title, original intro as YM2610 ADPCM-A,
4192-pixel world, finite scenery, stone blocks, one-way planks, Shpendi with
armor loss, zombie/bat/pumpkin/ghost waves, three ball projectiles, rebounds,
lives, pits, checkpoint and score. Mazapegùl has 32 HP, telegraphed throws,
laugh waves, jumps/shockwaves and two rewind phases. Defeat triggers stage
clear and a walk toward the final world section.

The release includes MAME evidence for an input-only playthrough, targeted
native gameplay checks and a cold-boot recording compared with the intro
source. Artifact validation compares the ELF-derived P1, ROM chips, cartridge
ZIP and MAME hashes; generated SHA256 manifests identify the exact build.

This is a playable Level 1 milestone, not final web parity. Collectible items,
breakable crates, powers, weather/fog/fade effects and parts of enemy AI still
differ or are absent. The release has not completed the production soak and
all gates in [STABLE_GATE.md](docs/STABLE_GATE.md).
