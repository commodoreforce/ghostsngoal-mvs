# Cloud build workflow

Use the existing isolated checkout; do not create another worktree.
Read AGENTS.md and CODEX_HANDOFF.md before editing. The web reference is
read-only, pinned to `7d6d995aa32a6a3df23d23ca857852f85bdc99c2`.

The preparation script supports Linux ngdevkit installations discovered by
pkg-config and macOS Homebrew. Required tools are Python >= 3.12, ngdevkit
0.5 with its 68000/Z80 toolchain, pkg-config, autotools, GNU make, rsync,
zip, ImageMagick, sox and ffmpeg. MAME is required for runtime validation.
For a relocated SDK, set PATH and PKG_CONFIG_PATH to its bin and
lib/pkgconfig directories. Do not disable signature, checksum or TLS checks.

Run from the repository root:

```sh
make test
GNG_UPSTREAM=/workspace/ghostsngoal-reference bash scripts/run-full-level1-r1.sh --build-only
# With MAME/display available, omit --build-only to build, verify and launch.
```

GNG_UPSTREAM can point to an existing clean reference checkout at the pinned
commit. Existing reference checkouts are never fetched/reset/checked out by
preparation. If unset, preparation creates a private reference clone.
GNG_PYTHON can select Python. GNG_INTRO can select the original intro MP3;
otherwise local user files take precedence, then the web intro master.
ngdevkit-examples is pinned to `4e7900b79b1555f9f1737549a49c26fbdb1aa589`.
Preparation rejects an existing examples checkout at a different commit.

The intro and generated WAVs use 18500 Hz, matching ngdevkit's ADPCM-A
convention. The hardware has a fixed playback rate; vromtool encodes PCM
without resampling, so 22050 Hz sources played too slowly and at lower pitch.

The launcher rebuilds a freshly prepared project, checks runtime markers,
compares the cartridge ZIP with every chip and the MAME CRC/SHA1 list, and
recreates P1 from the current ELF to reject mixed/stale artifacts. It writes
`build/verified-rom-sha256.json` only after those checks pass. This verifies
artifact consistency, not gameplay or graphical correctness.

## Current instance evidence (2026-10-23)

- Host core: `make test` passed, 50123 frames. This does not execute mvs/main.c.
- All 15 graphics asset banks generated and converted with tiletool.
  Each C-ROM needs 725248 bytes including the template logo reservation,
  below the 2097152-byte capacity. Palette use: 168/255.
- Seven C/Z80 sound commands and four 512 KiB native ADPCM V-ROMs generated
  using ngdevkit sources at `5106cbd1414a86ba02810ee93ac977be3360de4f`.
- Host C syntax validation of mvs/main.c against those SDK/generated headers
  passed. This is not 68000 compilation or Z80 linking.
- ROM verifier checks passed on synthetic fixtures, including rejection of a
  corrupted chip and a stale P1. These are not MVS runtime tests.
- Full launcher rerun stops at missing ngdevkit pkg-config/toolchain.
- Official PPA metadata was reachable, but retrieval of its signing key from
  keyserver.ubuntu.com returned proxy HTTP 403. No unverified package was used.
  Network destinations were saved in the environment draft; apply/review and
  publish through environment settings before retrying installation.
- MAME is absent. No final cartridge, emulator boot, playthrough, audio
  playback, visual quality, or successful end-to-end ROM verifier run is claimed.

Toolchain installation and complete MAME validation remain required. The
gameplay still differs from the web version (including missing blocks/planks
and a simplified boss); the Level 1 milestone is not complete.
