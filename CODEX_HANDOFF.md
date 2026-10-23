# Current handoff — Level 1 Content R4

Read AGENTS.md in full. Work in the existing checkout on main as authorized.
The web repository is read-only, pinned to
`7d6d995aa32a6a3df23d23ca857852f85bdc99c2`.

## Current target and release

The native ROM now implements the Shpendi Level 1 route, all authored crates
and pumpkins, all Level 1 reward types, Re Mida/charged shots/scoring/lives,
native enemy behaviors, boss defeat/tally and five playable bonus penalties.
See docs/LEVEL1_CONTENT.md for the exact scope, placements, rules and limits.
This is not the full three-level/all-character game or a universal 10/10 claim.

Cart ID remains `gngl1r1`; mandatory visible marker is **LEVEL 1 CONTENT R4**.
The launcher prints this label and the source commit from the verified bundle.

For the user, on their Mac in their local checkout:

```sh
git pull --ff-only origin main
python3 scripts/play-mame.py
```

No SDK build is needed to play. Do not send the user to a cloud panel or give
`/workspace/...` as their Mac path. **Always end each step with the update/play
commands and the expected visible build marker**, per the user's instruction.

## Canonical implementation

- mvs/main.c: title, movement, combat, renderer, AI, boss, tally and main loop.
- mvs/level1_content.h: fixed pools for crates/items, power, scoring, timer,
  high-score backup, atlas renderer, floating points and readable result panel.
- mvs/level1_penalty.h: five-penalty bonus, learning keeper, goal/streak awards,
  perfect-bonus life, final Level 1 completion and return to title.
- scripts/level1_geometry.py: literal web geometry/crate/pumpkin data parser.
- scripts/level1_item_art.py: real separated pickup art and exact procedural
  shirt/piadina/crate/flag drawings adapted from the web fallback textures.
- scripts/build_level1_full.py: all native C-ROM assets, scene palettes,
  per-pose shot bounds, occupancy bounds, shared crowd palettes, precomputed
  dark palettes and graphics packing metadata.
- scripts/build-mvs-audio.py: intro stays the original MP3 conversion; level
  and boss note patterns are read from web TRACKS, including boost tempo.
  Generated level1_audio.h uses MVS video/ADPCM timing for music restarts.

Preparation copies canonical C/headers without regex source rewrites. Changes
to either content header must be copied to generated src/ and rebuilt. The
Makefile lists these headers as main.o dependencies. Packaging compares the
canonical headers with the compiled project's copies.

## Graphics invariants

The original interior is **998x224**, never squash it to 528px. Outside artwork
is 528px wide. Finite world strips remain deterministic, without duplicated
cemetery objects or scenery modulo repetition. Fog/rain intentionally repeat
as in the web weather system.

All 256 hardware palette slots are allocated:
- 0..2 FIX text.
- 3..86: mutually exclusive SKY/CITY/interior/lightning/dark/penalty scene bank.
- 87..94: item/crate frames; FLAG and PANEL reuse shirt slot89 exactly.
- 95 floating score font;96 gold balls;97 hit-white;98 Midas statue tint.
- 99..147: stadium plane, or penalty keeper/kicker during the bonus.
- 148..167 ground/scenery;168..203 base/gold player bank;204..239 naked player.
- 240..255 enemies, balls, boss FX, particles, rain, fog/eyes and title.

Do not upload every background at init: scene banks overlap deliberately.
Paired crowd drawings have identical regional palettes. Scene selection skips
palette uploads between the paired variants; switching light mode uploads the
appropriate bank. Dark resident palettes are generated offline, avoiding costly
per-channel integer arithmetic while the game runs. Player palettes stay bright.

Sprite ranges: layers0..83, player84..88 with flag90..91, enemies92..187,
balls190..205, hazards212..275, weather276..283, particles284..291,
visible content292..323, score popups324..355, fog356..371, eyes372..379.
The result panel reuses276..293 after gameplay. All inactive strips are hidden.

Keep layer_columns, actor_columns and hidden-strip/HUD caches: full redundant
VRAM uploads caused lost frames in experimental builds. Invalidate caches when
BIOS/title transitions or other writes replace their VRAM contents. Hit/tint
rendering invalidates the actor tile cache for the subsequent untinted pose.

The generator writes assets/gng-gfx.mk and graphics-manifest.json. The Makefile
**includes that order**, never maintain a separate handwritten GNG_GFX list.
verify-mvs-rom.py reconstructs both C-ROMs from every converted asset at its
declared tile offset. ELF/chip/ZIP hashes alone cannot detect wrong tile order.
The generator also rejects C-ROM/palette overflow before Makefile truncation.

## Native gameplay invariants

Player base movement remains established; power adds the source +20% speed.
Down+A drops only through planks; crates have sides/top/underside collisions.
First hit removes armor, next loses a life; pits bypass armor. KO then respawn
synchronizes fixed/integer position and camera, clears attacks, and preserves
collected/broken map content. Coin/start resets the content and score thresholds.

Eight crates and twenty pumpkins are exact imported placements. Every crate
contains its actual specified bonus. Item expiry, shirt recovery/spare-shirt
points, piadina lives, 11-second Re Mida, seven gold balls, gold pierce/damage,
650ms charged release, combo cap4, score lives20k/60k/+100k and final bonuses
are implemented and tested on the native cartridge.

Do not replace scripted input-only traversal with health/position writes.
Functional/content/soak fixtures explicitly arrange RAM and are separate tests.
For any fixture teleport, write **both** integer and fixed-point player fields;
otherwise old_foot/old_x swept collisions correctly see an inconsistent state.
Enemy/projectile fixture state must be cleared between cases to prevent stray
old bouncing projectiles from contaminating subsequent attack checks.

High score/guard use ngdevkit's _backup_ram section, not guessed raw BRAM
addresses. The cycle test exercises record retention on warm ROM exits/re-entry;
other BIOSes and loss of power during a game are not certified.

## Audio invariant

The original silent-cold-boot Z80 issue is fixed. Command0 replies0x7e only
when the driver command loop processed the readiness probe. The 68000 resets,
waits for that reply, then plays ADPCM. Never remove the handshake or mistake
NMI enqueue ACK for driver readiness. No host playback stands in for ROM audio.

## Reproduce and release

```sh
. scripts/activate-cloud-toolchain.sh
make test
bash scripts/run-full-level1-r1.sh --build-only
python3 scripts/test-mame-level1.py --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-level1.py --scenario checks --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-level1.py --scenario content --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-level1.py --scenario soak --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-audio.py --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-audio-cues.py --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
```

Retained rootless tools are under /workspace/tools. If absent, run the verified
bootstrap described in docs/CLOUD_SETUP.md. No persistent service is required.

The fair traversal now handles low enemies with crouching and flying enemies
with aimed shots. It continues through all five penalties and final tally,
checks scene/player palette RAM and logs measured game/video frame cadence.
The fixture suites cover the original16 functional checks plus63 content steps
(with multiple assertions in some steps). The soak requires110000 video frames
(over30 emulated minutes) and at least100 coin/start cycles. Its first100 cycles
use explicit pickup/pit fixtures, then controller-only play. Never call it a fair
full playthrough. Audio checks record cold intro and all16 other native cues.

Commit source on main, then run scripts/package-mame.py. It requires matching
passed level1/checks/content/soak/audio/audio_cues evidence for the exact ZIP.
Commit the refreshed releases/gngl1r1-r1.zip and checksum, then push main.
The bundle includes source commit, hashes, graphics manifest and validation.
Test the actual package with scripts/play-mame.py before asking the user to play.

MAME's sp-s2.sp1/sm1.sm1/sfix.sfix warnings concern the free ngdevkit BIOS versus
SNK hashes. Do not change expected checksums or disable verification to hide them.
Native darkness/fog/boss motion are adaptations. Real hardware, other BIOSes,
other characters/levels, league progression and exact browser alpha blending
remain outside this release's certification. See docs/STABLE_GATE.md before
claiming production readiness or a perfect artistic score.
