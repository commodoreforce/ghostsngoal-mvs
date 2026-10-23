# Stable ROM release gate

The project must not be called a stable MAME ROM until every item below passes.

## Gameplay
- [x] deterministic 60 Hz core architecture
- [x] credits / start state machine
- [x] 3 lives / game-over / high-score loop
- [x] movement, jump and kick
- [x] ball physics and rebounds
- [x] regular-enemy framework
- [x] boss-pattern framework
- [ ] exact parity with the current web game level layouts
- [ ] exact current enemy roster / pickups / character select
- [ ] full stage progression and endings

## Neo Geo MVS
- [ ] m68k/ngdevkit build
- [ ] S-ROM title / HUD / high score
- [ ] C-ROM real sprite conversion
- [ ] animation tables
- [ ] scrolling backgrounds
- [ ] Z80 driver + ADPCM audio
- [ ] MVS coin/start/service inputs
- [ ] backup-RAM persistent high score
- [ ] attract/demo sequence
- [ ] final MAME ROM set packaging

## Stability acceptance
- [x] 50,000-frame host-core fuzz test
- [ ] complete playthrough from title to final clear
- [ ] 100 consecutive coin/start cycles
- [ ] 30-minute MAME soak without lockup
- [ ] no invalid sprite/tile accesses
- [ ] no audio queue overflow
- [ ] no progression deadlocks


## Level 1 Content R4 gate (separate from full-game production readiness)

The checked-in R4 release requires the following for its exact cartridge hash:

- Native title-to-boss-to-tally-to-five-penalties input-only traversal.
- Native functional and all-crate/item/power/score/enemy-rule fixture suites.
- Continuous 110000-video-frame soak, with at least 100 coin/start cycles;
  first 100 cycle endings use explicit fixtures, subsequent input play is natural.
- Recorded cold intro and all 16 other Z80/YM2610 effects/themes.
- Generated sprite packing offsets, C-ROM capacity, palette readback, ELF/P1,
  cartridge/MAME hashes and tested prebuilt launcher.

Read the bundle's validation logs/results for actual counts and scope. Completing
this first-level gate does not certify the absent levels/characters, every BIOS,
real MVS hardware or perfect visual equivalence with browser rendering.
