# Level 1 — Notte al Manuzzi / Content R4

This cartridge completes the Shpendi Level 1 route through its five-penalty
inter-stage bonus. It is a native MVS adaptation of the pinned web reference
`7d6d995aa32a6a3df23d23ca857852f85bdc99c2`, not the three-level web game or all
five selectable characters. Level 2 and league progression are outside this ROM.

## Play and identify the release

```sh
git pull --ff-only origin main
python3 scripts/play-mame.py
```

Both the title and launcher identify **LEVEL 1 CONTENT R4**. Insert a coin with
`5`, start with `1`, move with arrows, jump with `Ctrl`, shoot with `Alt`.
Up + shoot aims diagonally up; down + shoot in air aims down. Crouch + shoot
hits low crates. Down + jump drops through a plank. Hold shoot for 650 ms,
then release, for a charged shot. During Re Mida, hold shoot for autofire.
In the penalty bonus, shoot locks the moving target, then locks shot power.

## Finite world and authored objects

The full 4192 px world uses the reference's six ground spans, five pits,
11 stone block placements, 16 planks, 13 graves, four large graves, six lamps,
two goals, finite LED boards, checkpoint at tile 121 and boss arena at tile 242.
Coordinates of blocks, planks, crates and pumpkins are parsed from `level1.js`.
No decorative stand-in replaces interactive objects.

All eight crates take three normal hits, show two crack states, block walking,
can be stood on and block the head from below. Charged/gold shots break them
in one hit. Each broken crate awards 100 and releases its actual authored item:

| Tile / row | Contents |
| --- | --- |
| 17 / 12 | Golden pumpkin |
| 52 / 8 | Seahorse |
| 61 / 12 | Shirt |
| 94 / 12 | Shirt |
| 143 / 10 | Shirt |
| 162 / 6 | Seahorse |
| 188 / 12 | Shirt |
| 232 / 12 | Piadina |

All 20 placed pumpkin pickups follow the authored arcs/platform positions.
Dropped bonuses bounce/land; seahorses float. Temporary bonuses blink before
expiry. Collected placed items and broken crates remain consumed through a
life loss, preventing checkpoint score farming. Coin/start resets the content.

## Scoring and lives

| Event | Award / effect |
| --- | --- |
| Zombie / bat | 100 |
| Pumpkin enemy | 200; two hits, cracked poses after the first |
| Ghost | 300; cannot hurt/be hurt during its faded phase |
| Consecutive kills within one second | Combo ×1, ×2, ×3, capped at ×4 |
| Re Mida kill | Kill/combo award ×3 |
| Placed pumpkin | 200 |
| Enemy pumpkin drop | 500 |
| Golden pumpkin | 5000 |
| Shirt while naked | Restores armor |
| Shirt while armored | 1000 |
| Piadina | One extra life |
| Seahorse | Refreshes Re Mida to 11 seconds |
| Score thresholds | Extra life at 20000, 60000, then every 100000 |
| Mazapegùl defeated | 10000 |
| Stage time bonus | Remaining seconds ×10 |
| Stage accuracy bonus | Hit shots / fired shots ×5000, capped at 5000 |
| No-damage bonus | 10000 if no damage/life loss occurred |

Awards happen once. Native floating points, HUD score/lives/armor/pumpkins,
combo and boost countdown provide feedback. The timer starts at 150 seconds,
resets to 180 at the checkpoint, and gives the boss at least 90 seconds.
Timeout costs one life. First contact loses armor; the next loses a life.
Pits cost a life. KO poses play before a synchronized checkpoint/start respawn.
The high score uses ngdevkit's MVS backup-data section, with a format guard;
warm game exits/re-entry are exercised by the cycle test. Power-loss persistence
and other BIOS implementations are not certified.

## Sprites, attacks and audiovisual feedback

- Original Shpendi base, naked and gold sheets, including idle/run/jump/fall,
  shooting/crouching, looking up, hurt, KO and cheer poses. The generic slide
  pose is imported; Shpendi has no Ciofi slide ability in the reference.
- Zombies emerge safely, then walk; selected sliders announce their attack.
  Bats use the source 0/1/2/1 wing cycle and announce dives. Pumpkins breathe,
  compress, hop and crack. Ghosts pursue, attack nearby and become intangible;
  their faded drawings use native stippling instead of frame disappearance.
- Source sprite alpha bounds define projectile hit boxes, including flipped
  poses. Piercing projectiles remember enemy hits and cannot repeatedly damage
  the same living entity. Ground enemies respect scenery/crate sides.
- Mazapegùl retains 32 HP, double-damage head hits, telegraphed horseshoes,
  crouchable laughter, jumps/shockwaves and two nightmare rewinds. His cap flies
  away and his capless pose escapes before the victory tally.
- Three regular balls, charged piercing shots and up to seven larger gold balls.
  Re Mida changes the actual source sprite sheet, grants autofire, +20% running
  speed, piercing gold shots and gold corpses. Expiry/charge have visual cues.
- Native source music note patterns for level/boss, accelerated boost music,
  intro ADPCM, pickup/crate/power/1UP/checkpoint/charge/time/thunder cues.
- Finite painted scenery, original-width interior, crowd animation, in-place
  stadium dissolve, sparse rain/fog, lightning and cycling stadium lights with
  visible enemy eyes. Palette banks preserve player colors during changes.
  Darkness/fog, boss escape and some motion are hardware-specific adaptations,
  not an exact reproduction of browser alpha blending/tweens.

## Ending and penalty bonus

Victory counts time, accuracy and no-damage bonuses individually, then starts
five playable penalties under the Curva Mare. Original penalty backgrounds,
Shpendi and ghost-keeper sheets are used. The keeper learns shot-side history.
Weak/overpowered or saved shots can fail; corner shots, chips, powerful shots
and successive goals affect points. Five goals award 5000 and an extra life.
The final panel shows **LEVEL 1 COMPLETE**, total score and penalty bonus,
then returns to the title/coin loop. There is no fake Level 2 destination.

## Release verification

The package includes exact cartridge/ELF hashes, the graphics packing manifest
and independent MAME evidence. `package-mame.py` requires matching successful:

1. Input-only traversal from coin/start through boss, tally and all five
   penalties, with no health/position writes. Scene/player palette readback
   and game/video frame cadence are checked.
2. Functional input/collision/armor/life/checkpoint/game-over checks.
3. Explicit content RAM fixtures covering every crate/reward, item value,
   controller shots, power/charge/combo/1UP/expiry and enemy attack rules.
4. Continuous 110000-frame soak (over 30 emulated minutes), including at least
   100 coin/start cycles. The first 100 use explicit pickup/pit fixtures;
   subsequent play uses controller inputs. This is not a fair playthrough.
5. Cold-boot intro correlation and a separate recording of all 16 remaining
   native cues/themes, compared to their encoded source WAVs.

Graphics verification checks the converted chips against their declared tile
offsets, as well as ELF/P1, ZIP and MAME CRC/SHA1. Paired crowd frames use shared
palettes; column/actor/hidden-strip/HUD caches and precomputed dark palettes
avoid repeated writes and per-frame palette arithmetic.

These gates establish this MAME milestone, not a universal “perfect” rating,
all-character parity, all-BIOS compatibility or real-hardware certification.
The bundled free BIOS still produces the documented SNK-BIOS hash warnings.
