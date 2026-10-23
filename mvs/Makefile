all: cart bios

BUILDDIR=build
SRCDIRS=assets src
CFLAGS=-I$(BUILDDIR) -Isrc -std=c99 -O2 -fomit-frame-pointer
LDFLAGS=
Z80FLAGS=
Z80LDFLAGS=

include config.mk

GAMEROM=ghostngol
GAMETITLE=Ghosts n Goals

include rom.mk
include build.mk
include emu.mk

ELF=$(BUILDDIR)/rom.elf
CORE_OBJS=$(BUILDDIR)/src/game.o $(BUILDDIR)/src/levels.o $(BUILDDIR)/src/characters.o
$(CORE_OBJS): | $(BUILDDIR)/src
$(ELF): $(BUILDDIR)/main.o $(CORE_OBJS)
$(PROM1): $(ELF)
$(BUILDDIR)/main.o: $(SOUND_COMMANDS)

$(SROM1): $(BUILDDIR)/assets/base-srom-text-shadow.fix

# FULL LEVEL 1 C-ROM order. This order MUST match scripts/build_level1_full.py.
GNG_GFX=sky city stadium_back stadium_front inside0 inside1 ground fg_cemetery player zombie bat pumpkin ghost maz ball
$(CROM1): $(BUILDDIR)/assets/base-crom-logo.c1 $(GNG_GFX:%=$(BUILDDIR)/assets/gng_%.c1)
$(CROM2): $(BUILDDIR)/assets/base-crom-logo.c2 $(GNG_GFX:%=$(BUILDDIR)/assets/gng_%.c2)

# Native Neo Geo sound driver + ADPCM samples.
SOUND_DRIVER=$(BUILDDIR)/user_commands.ihx
$(MROM1): $(SOUND_DRIVER)
$(SOUND_DRIVER): $(BUILDDIR)/user_commands.rel
$(BUILDDIR)/user_commands.rel: $(Z80_SAMPLES_IN_VROM) $(Z80_SOUND_COMMANDS)

$(VROM1): assets/music-map.yaml assets/sample-map.yaml
$(Z80_SAMPLES_IN_VROM): assets/music-map.yaml assets/sample-map.yaml
$(Z80_SOUND_COMMANDS): assets/music-map.yaml assets/sample-map.yaml
$(SOUND_COMMANDS): assets/music-map.yaml assets/sample-map.yaml

generate-sound: $(Z80_SAMPLES_IN_VROM) $(Z80_SOUND_COMMANDS) $(SOUND_COMMANDS)
.PHONY: generate-sound

CUSTOM_GENERATE_TARGETS=generate-sound
