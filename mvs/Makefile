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
$(ELF): $(BUILDDIR)/main.o $(BUILDDIR)/src/game.o $(BUILDDIR)/src/levels.o $(BUILDDIR)/src/characters.o
$(PROM1): $(ELF)

# Phase 1 hardware build uses ngdevkit stock FIX/font and placeholder sprite ROMs.
# Real C-ROM assets are introduced in the next graphics milestone.
$(SROM1): $(BUILDDIR)/assets/base-srom-text-shadow.fix
$(CROM1): $(BUILDDIR)/assets/base-crom.c1
$(CROM2): $(BUILDDIR)/assets/base-crom.c2

SOUND_DRIVER=$(BUILDDIR)/assets/base-sound-driver.ihx
$(MROM1): $(SOUND_DRIVER)

CUSTOM_GENERATE_TARGETS=
