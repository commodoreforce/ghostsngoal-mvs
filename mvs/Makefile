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

$(SROM1): $(BUILDDIR)/assets/base-srom-text-shadow.fix

# C-ROM order MUST match scripts/build-mvs-visual-assets.py tile bases.
$(CROM1): $(BUILDDIR)/assets/base-crom-logo.c1
$(CROM2): $(BUILDDIR)/assets/base-crom-logo.c2
$(CROM1): $(BUILDDIR)/assets/gng_sky.c1
$(CROM2): $(BUILDDIR)/assets/gng_sky.c2
$(CROM1): $(BUILDDIR)/assets/gng_city.c1
$(CROM2): $(BUILDDIR)/assets/gng_city.c2
$(CROM1): $(BUILDDIR)/assets/gng_retro.c1
$(CROM2): $(BUILDDIR)/assets/gng_retro.c2
$(CROM1): $(BUILDDIR)/assets/gng_ingresso.c1
$(CROM2): $(BUILDDIR)/assets/gng_ingresso.c2
$(CROM1): $(BUILDDIR)/assets/gng_inside.c1
$(CROM2): $(BUILDDIR)/assets/gng_inside.c2
$(CROM1): $(BUILDDIR)/assets/gng_ground.c1
$(CROM2): $(BUILDDIR)/assets/gng_ground.c2
$(CROM1): $(BUILDDIR)/assets/gng_shpendi.c1
$(CROM2): $(BUILDDIR)/assets/gng_shpendi.c2
$(CROM1): $(BUILDDIR)/assets/gng_zombie.c1
$(CROM2): $(BUILDDIR)/assets/gng_zombie.c2
$(CROM1): $(BUILDDIR)/assets/gng_ball.c1
$(CROM2): $(BUILDDIR)/assets/gng_ball.c2
$(CROM1): $(BUILDDIR)/assets/gng_fog.c1
$(CROM2): $(BUILDDIR)/assets/gng_fog.c2

SOUND_DRIVER=$(BUILDDIR)/assets/base-sound-driver.ihx
$(MROM1): $(SOUND_DRIVER)

CUSTOM_GENERATE_TARGETS=
