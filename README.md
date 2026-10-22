# Ghosts 'n Goals — Neo Geo MVS / MAME Port

Dedicated arcade port of **Ghosts 'n Goals** for **Neo Geo MVS**, with the final goal of producing a stable, fully playable ROM set for MAME.

## Repository policy

- `commodoreforce/ghostsngoal` = web version / reference source only
- `commodoreforce/ghostsngoal-mvs` = writable Neo Geo / MAME port
- The web repository is never modified by this project.

## Target

A real early-1990s-style coin-op game, not a browser wrapper:

- Motorola 68000 gameplay code
- Neo Geo C-ROM sprite/tile graphics
- S-ROM FIX layer for HUD/title/high scores
- Z80 + ADPCM audio
- MVS coin/start/service flow
- attract mode
- persistent high score
- MAME validation and soak testing

## Arcade design direction

The ball is the weapon and the identity of the game. The core loop is:

**MOVE → CREATE SPACE → KICK → REBOUND → KILL → ADVANCE → BOSS**

Controls are intentionally arcade-first:

- Joystick: movement
- A: jump
- B: kick

Difficulty must come from enemy combinations and timing, not slippery controls.

## Current milestone

`Arcade Core v0.8`

Implemented in the hardware-independent core:

- boot / attract / title / play / boss / clear / game-over / high-score state machine
- MVS-style credits
- 3 lives
- deterministic 60 Hz gameplay model
- movement, jump and kick
- ball physics and rebounds
- zombie / bat / pumpkin behaviour framework
- Mazapegùl boss pattern
- Azdora boss pattern
- two-stage progression framework
- score / high score
- long-run fuzz test

The next milestone is the **Neo Geo hardware adapter** and exact parity with the current web game's content/assets.

## Stable release definition

The project is not considered finished until it passes the checklist in `docs/STABLE_GATE.md`, including a complete playthrough, repeated credit/start cycles and a long MAME soak test.
