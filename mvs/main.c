#include <ngdevkit/neogeo.h>
#include <ngdevkit/bios-ram.h>
#include <ngdevkit/ng-fix.h>
#include <ngdevkit/ng-video.h>
#include <stdio.h>
#include "../src/game.h"

#define USER_MODE_GAME 0x2
#define SYSTEM_MVS 0x80

static GngGame game;
static char hud[40];
static u8 prev_pad;

static void init_screen(void) {
    const u16 palette[] = {0x8000, 0x0fff, 0x0555, 0x0bbb};
    ng_cls();
    for (u16 i = 0; i < 4; ++i) MMAP_PALBANK1[i] = palette[i];
    *((volatile u16*)0x401ffe) = 0x0000;
}

static void draw_title(void) {
    ng_center_text_tall(7, 0, "GHOSTS 'N GOALS");
    ng_center_text(11, 0, "CESENA FOOTBALL CLUB");
    ng_center_text(14, 0, "NEO GEO MVS EDITION");
    ng_center_text(21, 0, bios_mvs_flag == SYSTEM_MVS ? "INSERT COIN" : "PRESS START");
    ng_center_text(25, 0, "A JUMP   B KICK");
}

static GngInput read_input(void) {
    const u8 pad = bios_p1current;
    GngInput in = {0};
    in.up = !!(pad & CNT_UP);
    in.down = !!(pad & CNT_DOWN);
    in.left = !!(pad & CNT_LEFT);
    in.right = !!(pad & CNT_RIGHT);
    in.jump = !!((pad & CNT_A) && !(prev_pad & CNT_A));
    in.kick = !!((pad & CNT_B) && !(prev_pad & CNT_B));
    prev_pad = pad;
    return in;
}

static void draw_hud(void) {
    sprintf(hud, "SCORE %06lu", (unsigned long)game.score);
    ng_text(1, 1, 0, hud);
    sprintf(hud, "HI %06lu", (unsigned long)game.high_score);
    ng_text(25, 1, 0, hud);
    sprintf(hud, "STAGE %u  LIFE %u", (unsigned)(game.level + 1), (unsigned)game.player.lives);
    ng_text(1, 27, 0, hud);
}

void player_start(void) {
    bios_user_mode = USER_MODE_GAME;
    bios_player_mod1 = 1;
}

void coin_sound(void) {
    /* ADPCM coin sound will be bound to the Z80 driver in the audio milestone. */
}

static void attract_mode(void) {
    draw_title();
    u16 frames = 60 * 15;
    while (frames-- && bios_user_mode != USER_MODE_GAME) {
        ng_wait_vblank();
    }
}

static void game_mode(void) {
    ng_cls();
    gng_init(&game);
    gng_reset_run(&game);
    game.state = GNG_STATE_PLAY;
    prev_pad = 0;

    while (game.state != GNG_STATE_GAMEOVER && game.state != GNG_STATE_HIGHSCORE) {
        GngInput in = read_input();
        gng_step(&game, in);
        draw_hud();
        ng_wait_vblank();
    }

    bios_player_mod1 = 0;
    ng_cls();
    ng_center_text_tall(13, 0, "GAME OVER");
    sprintf(hud, "SCORE %06lu", (unsigned long)game.score);
    ng_center_text(18, 0, hud);
    for (u16 wait = 180; wait; --wait) ng_wait_vblank();
}

int main(void) {
    init_screen();
    attract_mode();
    if (bios_user_mode == USER_MODE_GAME) game_mode();
    return 0;
}

int main_mvs_title(void) {
    init_screen();
    draw_title();
    while (bios_user_mode != USER_MODE_GAME) ng_wait_vblank();
    game_mode();
    return 0;
}
