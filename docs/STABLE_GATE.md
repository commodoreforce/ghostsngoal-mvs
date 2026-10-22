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
