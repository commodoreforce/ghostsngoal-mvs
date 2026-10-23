# CODEX_HANDOFF.md

## Goal
Take over implementation of the Neo Geo MVS port of Ghosts'n Goals and push it forward autonomously until the current milestone is truly playable in MAME.

Read `AGENTS.md` first and follow it as the project contract.

## Immediate priority
Do not create another partial visual demo.

The next meaningful deliverable is a verified build containing:
- title/start screen
- native intro audio from the user's MP3
- full Level 1
- Shpendi player
- zombie, bat, pumpkin, ghost
- Mazapegul boss
- ball projectile mechanic
- lives, collisions, pits, checkpoint, score, stage clear
- stable scrolling and clean graphics

## Current blocker at handoff
The build has progressed through C compilation and into the Z80/audio link stage.

The latest known error was:

`Undefined Global 'music_ngdevkit_eye_catcher' referenced by module ''`

Root cause identified:
`mvs/user_commands.s` still contained a template reference:

```asm
jp music_ngdevkit_eye_catcher
```

This must not exist because `mvs/music-map.yaml` is currently empty and project audio is defined through `mvs/sample-map.yaml`.

The expected correction is to route that jump-table entry to `snd_command_unused` or otherwise align the table strictly to generated sound commands.

Do not assume this is the final build error. Continue compiling and fixing subsequent errors until a cartridge is actually produced.

## Recent build pipeline improvements already made
- unique cartridge id for this milestone: `gngl1r1`
- one-command launcher: `scripts/run-full-level1-r1.sh`
- Python venv with Pillow + YAML dependencies
- Python 3.14-compatible tool execution
- normalized Start symbol to `CNT_START1`
- removed local SCB register redefinitions from generated runtime
- source verification intended to prevent `CNT_START11` regression
- graphics/audio generation integrated into preparation script

Review these instead of blindly trusting them. Simplify or replace fragile patching if necessary.

## Important architecture warning
The project accumulated too many post-copy source rewrites in `scripts/prepare-ngdevkit-project.sh` during debugging.

Prefer fixing canonical source files directly (`mvs/main.c`, `mvs/Makefile`, `mvs/user_commands.s`, asset generators) rather than continually adding text-replacement patches.

Build scripts should be deterministic and idempotent.

## Reference repos
Writable:
`commodoreforce/ghostsngoal-mvs`

Read-only reference:
`commodoreforce/ghostsngoal`

Reference web commit:
`7d6d995aa32a6a3df23d23ca857852f85bdc99c2`

## Web files to inspect first
- `game/src/levels/level1.js`
- `game/src/entities/Player.js`
- `game/src/entities/Enemies.js`
- `game/src/entities/Mazapegul.js`
- `game/src/scenes/LevelScene.js`
- `game/src/systems/textures.js`
- `game/src/systems/anims.js`
- runtime assets under `game/public`

Use the actual web implementation as the gameplay/visual truth.

## Expected Level 1 progression
World width: 262 * 16 = 4192 px.

Ground spans:
`[0,34],[36,58],[60,96],[98,152],[155,196],[199,262]`

Waves:
`30 bat1, 46 pumpkin1, 62 bat2, 80 pumpkin2, 100 bat2, 132 ghost2, 150 pumpkin2, 162 ghost2, 176 bat2, 192 ghost2, 204 pumpkin2, 216 ghost2, 226 bat2`

Zombie zones:
- 6–118
- 126–238

Checkpoint: tile 121
Boss arena: tile 242
Boss: Mazapegul

## Art strategy
Previous experiments suffered from reconstructed composite layers that generated floating walls, black rectangles, broken seams, duplicated foreground objects and poor web fidelity.

For the full Level 1 implementation:
- prefer final separated runtime web assets whenever they exist
- use original master ZIPs only where they materially improve source quality
- do not infer architecture by naive image differencing
- pre-generate finite world strips for repeated/continuous level scenery when that yields deterministic scrolling
- validate multiple camera positions before declaring scrolling stable

## Audio strategy
User intro MP3 must become native Neo Geo ADPCM/V-ROM content.

Gameplay loop/SFX can be generated or authored as project assets, but the intro supplied by the user should be the actual intro source.

Ensure Z80 command tables and generated command headers agree exactly. Remove ngdevkit template-only music entries if unused.

## How to work
You are expected to operate as an autonomous implementation/debugging agent:

1. Inspect repo state
2. Read `AGENTS.md`
3. Reproduce the build from clean state
4. Fix the current Z80/link issue
5. Continue through every subsequent build error
6. Produce cartridge
7. Run/verify in MAME if environment supports it
8. Check runtime markers so stale ROMs cannot be mistaken for new builds
9. Exercise full Level 1 progression
10. Fix visual/runtime defects
11. Commit working changes with clear messages

Do not stop simply because one compiler/linker error was fixed.

## Quality bar
The user is explicitly deciding whether to continue the project based on the next substantial build.

A build that merely boots is not enough.

Priority order:
1. build reliability
2. full Level 1 functionality
3. scrolling stability / no graphical corruption
4. fidelity to web version
5. sprite/background polish
6. audio polish

## Final reporting
When you finish a work session, report:
- what was actually built and verified
- exact commands/tests run
- MAME runtime result if available
- remaining concrete defects
- commit SHA(s)

Do not describe unverified features as completed.
