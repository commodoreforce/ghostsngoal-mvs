# Current handoff — native Level 1 R1

Read AGENTS.md first. Work in the existing MVS checkout, commit on main as
already authorized, and never modify `commodoreforce/ghostsngoal`.

## Current state

The original stale `music_ngdevkit_eye_catcher` reference was already removed
before this session. Linux toolchain setup, 68000/Z80 compilation, ROM packing
and MAME execution now work. Do not send the user back to unclear cloud panels.

Canonical sources are in `mvs/`; preparation copies them without regex source
rewrites. The cartridge ID remains `gngl1r1`, visibly marked FULL LEVEL 1 R1.
Artifact verification recreates P1 from the current ELF and compares chip/ZIP
contents and MAME hashes. The checked-in prebuilt package is launched with:

```sh
python3 scripts/play-mame.py
```

It requires MAME, not ngdevkit. The package records its source commit, exact
cartridge SHA256 and validation evidence. Any later rebuild must be validated
again before replacing the release. `package-mame.py` enforces this.

## Native changes

- MVS coin/Start handshake sets BIOS user mode and player state correctly;
  the optional title entry point also runs gameplay.
- Sprite coordinates account for MAME's cropped top 16 scanlines; HUD uses
  visible FIX rows. Positioned finite layers clip source columns correctly.
- Web Level 1 blocks, planks, graves, lamps and goals are generated at finite
  world coordinates. The old synthetic repeating cemetery motif is unused.
- Swept feet collisions handle ground, blocks and one-way planks; down+A drops
  through planks. Jump buffering/coyote timing follow the web concept.
- Armor takes the first hit and switches to the native naked Shpendi sheet;
  the second hit loses a life. Pits bypass armor. Respawn synchronizes position,
  clears projectiles/hazards and respects checkpoint tile 121.
- Ball projectiles use fixed-point 226 px/s movement, aimed shots, three-ball
  cap, solid/grave rebounds, lifetime and enemy/boss hits.
- Enemy waves use camera-right triggers; alternate ghosts enter from the left.
  Zombies emerge, offscreen actors retire, ground-bound actors fall into pits.
- Mazapegùl: 32 HP, head-shot bonus, three phases, announced shoes, laugh waves
  avoided by crouching, jumps with shockwaves, and rewind/invulnerability at
  two HP thresholds. Defeat scores once and enters stage clear.
- Stage clear keeps native audio playing and walks toward the final world area.
  Title re-entry hides sprites left by the previous game.
- Cold-boot intro audio initially failed despite a successful link. The MVS
  driver now exposes command 0 as a processed readiness reply (0x7e); the 68000
  waits for that reply after reset before playback. Do not remove this handshake
  or rely only on the NMI queue ACK. Native audio is checked against source PCM.

## Reproduce in this cloud workspace

```sh
bash scripts/bootstrap-ngdevkit-linux.sh
. scripts/activate-cloud-toolchain.sh
make test
bash scripts/run-full-level1-r1.sh --build-only
python3 scripts/test-mame-level1.py --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-level1.py --scenario checks --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-audio.py --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
```

See docs/CLOUD_SETUP.md for installation trust, retained tools, process startup
and evidence details. SDK dependencies are under `/workspace/tools`; they are
not project repositories. Reference assets remain at web commit
`7d6d995aa32a6a3df23d23ca857852f85bdc99c2`.

## Validation scope and remaining work

Read the release's `validation/*/result.json` and logs for exact outcomes and
cartridge hashes. Traversal must pass without `--assist-health`. The separate
functional scenario arranges RAM fixtures and must not be represented as an
input-only playthrough. Audio uses real MAME recording, not host playback.

MAME's warnings about sp-s2.sp1, sm1.sm1 and sfix.sfix are caused by the open
ngdevkit BIOS differing from SNK BIOS hashes. Do not change expected checksums
or disable verification to hide them.

The build is not complete web parity or a production-stable release. Remaining
work includes collectibles, breakable crates/powers, more faithful enemy AI,
weather/fog/fade composition, audio polish, extended soak/balance testing, and
validation with other BIOS/hardware. The current deterministic boss attacks and
rewind are native approximations of web behavior. Follow docs/STABLE_GATE.md
before claiming production readiness or a 10/10 fidelity score.

## Visual revision 2 (2026-10-23)

The prebuilt bundle is refreshed on the same gngl1r1 cartridge ID. The new
visible marker VISUAL REVISION 2 is now mandatory in verify-mvs-rom.py.

- Preserve original backdrop widths: inside0/1 are **998x224**, not 528x224.
  fit_background() must never resize these backdrops to one common width.
- Background render has two 21-strip planes. City+sky are precomposited;
  stadium replaces them at the web-derived coordinates. Three ordered dissolve
  assets replace rear/front architecture in place. Inside uses 0.295 scrolling
  from sx=1808, clamped, with 25-frame crowd alternation.
- Transparent interior openings are composited over sky/city, never black.
- Original upstream PressStart2P.ttf generates native title C-ROM graphics;
  four animated ghosts reuse native actor sheets.
- LEDs and checkpoint flag use literal finite web placements; ground includes
  pit bands/edge turf. Eight foreground rain strips use SCB 276..283; keep them
  separate from hazard slots 212..275 and hide at game-loop exit.
- Palette usage is 252/255, C-ROM budget checked before Makefile truncation.
  Do not add per-asset palettes without checking the hardware limit.

Verified clean build, 50,123-frame host suite, all 16 fixture checks, default
input-only MAME traversal (stage clear score 12,900; 2 lives; all 13 waves), and
cold intro audio correlation 0.97888. Evidence packaged against exact ZIP SHA.
MAME screenshots inspected at title, entrance and inside/arena. These are
sampled frames, not proof of zero graphics defects on every frame/hardware.

Remaining: interior entry cut; baked sky/city relative parallax approximation;
no full fog/lightning/foreground silhouette treatment; existing pickup/crate/
power and enemy-AI parity gaps. Shpendi/enemy sheets were not redrawn. Continue
native visual polish and sprite/scanline profiling before claiming stability.
