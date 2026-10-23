# Cloud build workflow

Use the existing isolated checkout; do not create another worktree. Read
AGENTS.md and CODEX_HANDOFF.md before editing. The web reference is READ-ONLY
at `7d6d995aa32a6a3df23d23ca857852f85bdc99c2`.

## Installed workflow

```sh
cd /workspace/ghostsngoal-mvs
bash scripts/bootstrap-ngdevkit-linux.sh
. scripts/activate-cloud-toolchain.sh
make test
bash scripts/run-full-level1-r1.sh --build-only
```

Bootstrap installs under `/workspace/tools` without root. It verifies the
ngdevkit PPA InRelease signature against public key
`DEBA216533CA9B75C79CB44931B8B6160C6BAA4A`, then the package-index and package
SHA256 checksums. Debian dependencies use the system Debian archive keyring.
Signature, artifact checksum and TLS checks stay enabled. Re-running bootstrap
reuses matching packages and refreshes extracted tools and wrappers.

The SDK/toolchain and dependency trees are installation caches, not project
checkouts. Z80 wrappers and autotools need the activation script after
restoring an environment. No persistent server or emulator process is needed.

Versions verified here: ngdevkit 0.5, GCC 15.3.0, MAME 0.276. Python >= 3.12 is
required, with Pillow 12.3.0, ruamel.yaml 0.18.16 and PyYAML 6.0.3.
ngdevkit-examples is pinned to `4e7900b79b1555f9f1737549a49c26fbdb1aa589`.
The preparation script also supports a Homebrew ngdevkit installation.

`GNG_UPSTREAM` defaults to `/workspace/ghostsngoal-reference` after cloud
activation. Existing reference checkouts must be clean and pinned; preparation
never fetches, resets or checks them out. A missing private reference clone can
be created during preparation. `GNG_PYTHON` selects Python; `GNG_INTRO` can
select the user's intro MP3. Local user files take precedence over the pinned
web intro master.

## Build and runtime checks

The launcher prepares a fresh generated project, runs graphics/audio
generation, compiles 68000 and Z80, creates `gngl1r1.zip`, and checks
ELF -> word-swapped P1 -> ZIP/chips -> MAME software-list CRC/SHA1. Its output
is `.ngdevkit-examples/ghostsngoal-mvs/build/verified-rom-sha256.json`.

Run these from the repository after activation:

```sh
python3 scripts/test-mame-level1.py --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-level1.py --scenario checks --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
python3 scripts/test-mame-audio.py --pluginspath /workspace/tools/debian/usr/share/games/mame/plugins
```

Each invocation creates a unique evidence directory under the generated
project's `build/`. JSON records the exact cartridge SHA256 and whether health
assistance was used. The default traversal uses only coin/controller inputs;
it checks checkpoint, all 13 wave flags, boss defeat and stage clear and saves
screenshots at multiple camera positions. `--assist-health` is a separate,
explicit diagnostic mode and is not accepted as release playthrough evidence.

The `checks` scenario explicitly arranges native RAM fixtures for hits, pits
and platform positions, then exercises actual cartridge code. It validates
aimed shots, the ball cap, armor, death/checkpoint, walls/planks, game over and
a second coin/start cycle. Those fixtures do not count as a fair playthrough.

The audio check records MAME's own YM2610 output at cold boot and compares it
against the intro PCM. A Z80 command-0 readiness probe prevents the first intro
request being lost during driver initialization. ADPCM-A sources are 18500 Hz;
vromtool does not resample 22050 Hz sources for the fixed-rate hardware.

MAME runs headlessly for automated checks. The bundled open ngdevkit BIOS
causes expected warnings against SNK BIOS hashes; cartridge hashes are checked
separately. Interactive playback uses `python3 scripts/play-mame.py` with MAME
and a display. Do not present localhost previews.

## Packaging

After committing the tested source, `python3 scripts/package-mame.py` requires
matching passing unassisted traversal, functional checks and audio evidence for
the current cartridge. It checks canonical/generated sources and packages ROM,
software list, open BIOS, licenses, source commit and evidence. Commit the
resulting release ZIP and SHA256 separately. Rebuilding changes the ZIP digest;
rerun validation before packaging a new artifact.

See README.md and CODEX_HANDOFF.md for remaining gameplay/presentation work.
