/*
 * Ghosts 'n Goals — Neo Geo MVS hardware adapter (ngdevkit)
 *
 * This translation unit is only built by the Neo Geo toolchain. The gameplay
 * core stays platform independent and receives one GngInput sample per VBlank.
 */
#ifdef GNG_NGDEVKIT

#include <ngdevkit/neogeo.h>
#include <ngdevkit/bios-ram.h>
#include <ngdevkit/bios-calls.h>
#include <ngdevkit/ng-fix.h>
#include <ngdevkit/ng-video.h>
#include "../game.h"
#include "../characters.h"
#include "../levels.h"
#include <stdio.h>

extern u8 bios_p1current;

static GngGame game;
static uint8_t prev_pad;
static char hud[40];

static uint8_t edge(uint8_t now, uint8_t mask) {
    return (uint8_t)((now & mask) && !(prev_pad & mask));
}

static GngInput read_input(void) {
    const uint8_t pad = bios_p1current;
    GngInput in = {0};
    in.left  = (pad & CNT_LEFT)  != 0;
    in.right = (pad & CNT_RIGHT) != 0;
    in.up    = (pad & CNT_UP)    != 0;
    in.down  = (pad & CNT_DOWN)  != 0;
    in.jump  = edge(pad, CNT_A);
    in.kick  = edge(pad, CNT_B);
    /* START/coin are ultimately synchronized with BIOS callbacks. During the
       hardware bring-up START is sampled here as well for AES/MVS debug runs. */
    in.start = edge(pad, CNT_START);
    prev_pad = pad;
    return in;
}

static void draw_fix(void) {
    ng_cls();
    if (game.state == GNG_STATE_BOOT || game.state == GNG_STATE_ATTRACT || game.state == GNG_STATE_TITLE) {
        ng_center_text_tall(6, 0, "GHOSTS 'N GOALS");
        ng_center_text(10, 0, "CESENA FOOTBALL CLUB");
        ng_center_text(14, 0, "NEO GEO MVS");
        ng_center_text(20, 0, game.credits ? "PRESS START" : "INSERT COIN");
        return;
    }

    sprintf(hud, "1UP %06lu", (unsigned long)game.score);
    ng_text(1, 1, 0, hud);
    sprintf(hud, "HI %06lu", (unsigned long)game.high_score);
    ng_text(14, 1, 0, hud);
    sprintf(hud, "STAGE %u  LIFE %u", (unsigned)(game.level + 1), (unsigned)game.player.lives);
    ng_text(1, 27, 0, hud);
}

static void render_gameplay(void) {
    /* Phase HW-1 deliberately renders FIX HUD first. C-ROM sprite binding is
       isolated here so tile/animation data can be swapped without touching
       gameplay. The next asset pass assigns SCB entries for player, balls,
       enemies, pickups and parallax strips. */
    draw_fix();
}

void player_start(void) {
    /* BIOS callback used by MVS. The core still owns credit consumption. */
    if (game.credits < 9) game.credits++;
}

void coin_sound(void) {
    /* Z80/ADPCM hook. Sound bank arrives with the audio milestone. */
}

int main(void) {
    gng_init(&game);
    for (;;) {
        GngInput in = read_input();
        gng_step(&game, in);
        render_gameplay();
        ng_wait_vblank();
    }
    return 0;
}

int main_mvs_title(void) {
    return main();
}

#endif
