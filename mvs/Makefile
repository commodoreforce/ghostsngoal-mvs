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

# ngdevkit's generic %.o rule does not itself create nested output folders.
# Make the source build directory an explicit order-only prerequisite so a
# completely clean build can compile build/src/*.o reliably.
$(CORE_OBJS): | $(BUILDDIR)/src

$(ELF): $(BUILDDIR)/main.o $(CORE_OBJS)
$(PROM1): $(ELF)

# Phase 1 hardware build uses ngdevkit stock FIX/font and placeholder sprite ROMs.
# Real C-ROM assets are introduced in the next graphics milestone.
$(SROM1): $(BUILDDIR)/assets/base-srom-text-shadow.fix
$(CROM1): $(BUILDDIR)/assets/base-crom.c1
$(CROM2): $(BUILDDIR)/assets/base-crom.c2

SOUND_DRIVER=$(BUILDDIR)/assets/base-sound-driver.ihx
$(MROM1): $(SOUND_DRIVER)

CUSTOM_GENERATE_TARGETS=
