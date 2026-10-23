all: cart bios

BUILDDIR=build
SRCDIRS=assets src
CFLAGS=-I$(BUILDDIR) -Isrc -std=c99 -O2 -fomit-frame-pointer
LDFLAGS=
Z80FLAGS=
Z80LDFLAGS=

include config.mk

# Unique cartridge id: never reuse the old ghostngol artifact in MAME.
GAMEROM=gngl1r1
GAMETITLE=Ghosts n Goals - Full Level 1 R1

include rom.mk

# 4 x 512 KiB V-ROM banks. The user intro plus Level 1 loop/SFX are packed
# as native Neo Geo ADPCM data instead of relying on host audio.
VROMTEMPLATE=$(ROM)/$(GAMEROM)-vX.vX
VROM2=$(ROM)/$(GAMEROM)-v2.v2
VROM3=$(ROM)/$(GAMEROM)-v3.v3
VROM4=$(ROM)/$(GAMEROM)-v4.v4

include build.mk
include emu.mk

# MAME is the required emulator. Do not make a missing optional GnGeo
# installation prevent the MVS cartridge and MAME hash from building.
CART_TARGETS=$(CART_ZIP) $(HASH_MAME)

ELF=$(BUILDDIR)/rom.elf
CORE_OBJS=$(BUILDDIR)/src/game.o $(BUILDDIR)/src/levels.o $(BUILDDIR)/src/characters.o
$(CORE_OBJS): | $(BUILDDIR)/src
$(ELF): $(BUILDDIR)/main.o $(CORE_OBJS)
$(PROM1): $(ELF)
$(BUILDDIR)/main.o: $(SOUND_COMMANDS)

$(SROM1): $(BUILDDIR)/assets/base-srom-text-shadow.fix

# FULL LEVEL 1 C-ROM order. MUST match scripts/build_level1_full.py.
GNG_GFX=sky city stadium_back stadium_front inside0 inside1 ground fg_cemetery player player_naked zombie bat pumpkin ghost maz ball shoe wave
$(CROM1): $(BUILDDIR)/assets/base-crom-logo.c1 $(GNG_GFX:%=$(BUILDDIR)/assets/gng_%.c1)
$(CROM2): $(BUILDDIR)/assets/base-crom-logo.c2 $(GNG_GFX:%=$(BUILDDIR)/assets/gng_%.c2)

# Native Neo Geo Z80/nullsound driver + generated ADPCM commands.
SOUND_DRIVER=$(BUILDDIR)/user_commands.ihx
$(MROM1): $(SOUND_DRIVER)
$(SOUND_DRIVER): $(BUILDDIR)/user_commands.rel
$(BUILDDIR)/user_commands.rel: $(Z80_SAMPLES_IN_VROM) $(Z80_SOUND_COMMANDS)

$(VROM1) $(VROM2) $(VROM3) $(VROM4): assets/music-map.yaml assets/sample-map.yaml
$(Z80_SAMPLES_IN_VROM): assets/music-map.yaml assets/sample-map.yaml
$(Z80_SOUND_COMMANDS): assets/music-map.yaml assets/sample-map.yaml
$(SOUND_COMMANDS): assets/music-map.yaml assets/sample-map.yaml

generate-sound: $(Z80_SAMPLES_IN_VROM) $(Z80_SOUND_COMMANDS) $(SOUND_COMMANDS)
.PHONY: generate-sound
CUSTOM_GENERATE_TARGETS=generate-sound
