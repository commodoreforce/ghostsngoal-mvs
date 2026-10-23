# AGENTS.md — Ghosts'n Goals Neo Geo MVS Port

## Mission
Turn `commodoreforce/ghostsngoal-mvs` into a real, stable Neo Geo MVS game that runs correctly in MAME and faithfully reproduces the web game in `commodoreforce/ghostsngoal`.

This is not a mockup project. The expected output is a working MVS ROM build, with a complete Level 1 vertical slice, native Neo Geo graphics/audio, stable scrolling, correct gameplay, and high visual fidelity.

## Repositories
- Writable implementation repo: `commodoreforce/ghostsngoal-mvs`
- Read-only upstream/reference repo: `commodoreforce/ghostsngoal`
- NEVER modify the upstream web repo unless explicitly instructed by the user.

Upstream reference commit currently used:
`7d6d995aa32a6a3df23d23ca857852f85bdc99c2`

## User expectations
The user wants fast autonomous progress with minimal back-and-forth.

Do not stop at the first compiler/linker/runtime error. Diagnose it, fix it, rebuild, and continue until the requested milestone is actually working.

Do not claim a milestone is complete unless it has been verified by build/test/runtime evidence.

Do not ask the user to perform repetitive diagnostic steps when the environment allows you to do them yourself.

## Current required milestone
Produce a verified MVS build with at least:
1. Title / start screen
2. User-provided intro audio integrated as native Neo Geo audio
3. Complete Level 1
4. Player sprite and animation
5. Level 1 enemies: zombie, bat, pumpkin, ghost
6. Mazapegul boss
7. Correct ball-projectile mechanic
8. Stable collisions, lives, death, checkpoint, score, boss defeat, stage clear
9. Stable scrolling without pop-in, duplicated cemetery pieces, seams, floating layers, black holes, or random garbage tiles
10. High visual fidelity to the web version

## Critical quality rule
The web game is the final reference for:
- composition
- scale
- camera
- parallax
- gameplay
- timing
- level progression
- atmosphere
- visual hierarchy

Original art packs are production sources, not the final composition reference.

Where the web repo already contains a separated runtime asset, prefer that asset rather than trying to infer/separate layers again from composite master artwork.

## User-provided art packs
The user has two large ZIP reference packs locally:
- `ambientazione.zip`
- `sprite.zip`

Expected common location in the MVS repo:
- `reference-packs/ambientazione.zip`
- `reference-packs/sprite.zip`

They contain original/master artwork used before adaptation into the web game.

Use them when they provide cleaner source material, but preserve the web game's final proportions/composition.

## Intro audio
The user supplied an intro MP3 containing the `DAI BURDEL` vocal / laugh / arcade-style intro music.

The build scripts currently search common locations including:
- `reference-packs/intro.mp3`
- repo root `intro.mp3`
- `~/Downloads/intro.mp3`

Audio must be converted and included in native Neo Geo V-ROM/ADPCM output. Do not fake audio playback externally.

## Neo Geo toolchain
Primary toolchain:
- ngdevkit
- MAME
- macOS/Homebrew local environment has been used by the user

Current Homebrew-era ngdevkit behavior observed:
- Python 3.14 may be used
- `tiletool.py`, `vromtool.py`, `soundtool.py`, etc. are Python tools
- YAML dependencies may require `ruamel.yaml` and/or `PyYAML`
- Start button symbol is `CNT_START1`, not `CNT_START`
- `ADDR_SCB1` / `ADDR_SCB2` are already provided by ngdevkit headers

Do not reintroduce template-only symbols such as:
- `music_ngdevkit_eye_catcher`

## Build discipline
Before asking the user to test:
1. Validate scripts syntactically
2. Run host tests
3. Run all available asset generators
4. Build the MVS project if the environment permits
5. Verify the produced cartridge/ELF contains the intended runtime markers
6. Ensure a stale older ROM cannot be confused with the new build

The project currently uses a unique cartridge id for the complete Level 1 work:
`gngl1r1`

Do not silently revert to older cartridge IDs such as `ghostngol` for new milestone builds.

## Build launcher
Prefer maintaining a single top-level launcher script that:
- cleans stale generated project output
- runs preparation
- generates graphics/audio
- builds the cartridge
- validates the ROM
- launches MAME

The user should ideally run one command rather than manually chaining setup/build steps.

## Current Level 1 reference data
Web level file: `game/src/levels/level1.js`

Important known data:
- tile size: 16
- tilesW: 262
- world width: 4192 px
- ground row: 13
- ground spans:
  - [0,34]
  - [36,58]
  - [60,96]
  - [98,152]
  - [155,196]
  - [199,262]
- goals: [124,236]
- checkpoint: 121
- arena: 242
- boss: Mazapegul

Waves:
- 30 bat x1
- 46 pumpkin x1
- 62 bat x2
- 80 pumpkin x2
- 100 bat x2
- 132 ghost x2
- 150 pumpkin x2
- 162 ghost x2
- 176 bat x2
- 192 ghost x2
- 204 pumpkin x2
- 216 ghost x2
- 226 bat x2

Zombie zones:
- tiles 6–118
- tiles 126–238

## Player reference
Current target character for Level 1 vertical slice: Shpendi.

From web tuning:
- run speed: 110 px/s equivalent for Shpendi
- jump velocity: about -303 equivalent web tuning
- ball speed: 226
- ball damage: 1
- max balls: 3
- boost duration: 11 seconds equivalent

Gameplay behavior must match the web concept:
- B shoots/launches a ball projectile
- UP+B = upward diagonal
- DOWN+B in air = downward diagonal

Do not turn the mechanic into a grounded soccer kick interaction.

## Rendering quality requirements
Previous failed experiments produced:
- floating walls
- black rectangles
- broken layer seams
- cemetery objects appearing/disappearing during scrolling
- duplicated crosses/fences
- excessive synthetic green ground
- visual fidelity far below the web game

Avoid fragile runtime layer synthesis.

Prefer:
- pre-generated deterministic world strips
- explicit finite world coordinates
- runtime assets already separated by the web game
- deterministic parallax
- no modulo/repeat unless the web design genuinely repeats
- validation at multiple camera positions before considering the renderer done

## User scoring rubric
The user evaluates builds with:
- Sprite player /10
- Sprite enemies /10
- Background /10
- Parallax /10
- Scrolling /10
- Ground/foreground /10
- Gameplay /10
- Shooting /10
- Camera /10
- Glitches/technical cleanliness /10
- Web fidelity /10
- Neo Geo quality /10

The target is 10/10 quality, not merely “works”.

Recent approximate baseline before full Level 1 work:
- Player sprite: 6/10
- Enemy sprites: 5/10
- Background: 6/10
- Parallax: 5/10
- Scrolling: 6/10 after some fixes
- Ground/foreground: 3/10
- Shooting: 7/10
- Glitch/cleanliness: 5/10
- Web fidelity: 2/10

The biggest artistic gap remains web fidelity and lower-scene/foreground quality.

## Git rules
Direct writes to `main` are currently authorized by the user for this repo.

Do not modify `commodoreforce/ghostsngoal`.

Keep commits focused and descriptive.

Do not leave the repo in a half-broken state after a fix if you can continue to the next build error autonomously.

## Known prior toolchain failures — do not repeat
Previously encountered and fixed/identified:
- missing nested `build/src`
- Python 3.9 missing `typing.Self`
- bad tiletool invocation / wrapper confusion
- `ruamel.yaml` / `yaml` missing for `vromtool.py`
- `CNT_START` should be `CNT_START1`
- accidental patch corruption producing `CNT_START11`
- redundant `ADDR_SCB1` / `ADDR_SCB2` defines
- stale ngdevkit template symbol `music_ngdevkit_eye_catcher`
- stale ROM/artifact confusion caused by reusing the same cartridge id

When touching build scripts, make transformations idempotent.

## Autonomy rule
When you encounter an error:
1. Inspect the exact source/tool generating it
2. Fix root cause in the repo
3. Re-run the failing step
4. Continue until the next real blocker
5. Do not stop merely to report an error unless the environment truly prevents further progress

## Definition of done for the current milestone
Do not call the Level 1 milestone complete until all of these are true:
- cartridge builds successfully
- MAME boots the expected new cartridge
- unique build marker/title confirms correct ROM
- intro audio plays
- start transitions to gameplay
- Level 1 scrolls through the complete 4192 px world
- ground gaps behave correctly
- enemies spawn according to level data
- ball projectiles work
- collisions work
- player can die/restart
- checkpoint works
- Mazapegul appears and can be defeated
- stage clear works
- no obvious garbage tiles, black rectangles, floating architecture, broken seams, or scenery pop-in
- visual result is materially closer to the web version than previous prototypes
