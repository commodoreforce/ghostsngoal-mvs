#include <ngdevkit/neogeo.h>
#include <ngdevkit/bios-ram.h>
#include <ngdevkit/ng-fix.h>
#include <ngdevkit/ng-video.h>
#include <stdio.h>
#include "../src/game.h"

#define USER_MODE_DEMO 0x1
#define USER_MODE_GAME 0x2
#define SYSTEM_MVS 0x80

static GngGame game;
static char str[40];
static u8 prev_pad;

static void set_backdrop(u16 color) {
    *((volatile u16*)0x401ffe) = color;
}

static void init_screen(void) {
    const u16 palette[] = {0x8000, 0x0fff, 0x0555};
    ng_cls();
    for (u16 i = 0; i < 3; ++i) {
        MMAP_PALBANK1[i] = palette[i];
    }
}

static void demo_mode(void) {
    set_backdrop(0x0088);
    ng_center_text_tall(8, 0, "GHOSTS N GOALS");
    ng_center_text(11, 0, "MVS CORE TEST");
    ng_center_text(16, 0, "DEMO MODE");
    ng_center_text(22, 0, bios_mvs_flag == SYSTEM_MVS ? "INSERT COIN" : "PRESS START");

    u16 frames = 60 * 15;
    while (frames-- && bios_user_mode != USER_MODE_GAME) {
        ng_wait_vblank();
    }
}

static void title_mode(void) {
    set_backdrop(0x0800);
    ng_cls();
    ng_center_text_tall(8, 0, "GHOSTS N GOALS");
    ng_center_text(16, 0, "TITLE MODE");
    ng_center_text(22, 0, "PRESS START");

    while (bios_user_mode != USER_MODE_GAME) {
        sprintf(str, "TIMER %02x", bios_compulsion_timer);
        ng_center_text(24, 0, str);
        ng_wait_vblank();
    }
}

static GngInput read_input(void) {
    u8 pad = bios_p1current;
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

static unsigned alive_enemies(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < GNG_MAX_ENEMIES; ++i) {
        if (game.enemies[i].alive) ++n;
    }
    return n;
}

static void draw_core_debug(void) {
    ng_cls();
    ng_center_text_tall(3, 0, "GHOSTS N GOALS");
    ng_center_text(6, 0, "68000 GAME CORE LIVE");

    sprintf(str, "STATE %u  FRAME %lu", (unsigned)game.state, (unsigned long)game.frame);
    ng_text(2, 9, 0, str);

    sprintf(str, "P X%03d Y%03d VX%2d VY%2d", game.player.x, game.player.y, game.player.vx, game.player.vy);
    ng_text(2, 11, 0, str);

    sprintf(str, "BALL X%03d Y%03d VX%2d VY%2d", game.ball.x, game.ball.y, game.ball.vx, game.ball.vy);
    ng_text(2, 13, 0, str);

    sprintf(str, "LIFE %u  SCORE %06lu", (unsigned)game.player.lives, (unsigned long)game.score);
    ng_text(2, 15, 0, str);

    sprintf(str, "LEVEL %u  ENEMIES %u", (unsigned)(game.level + 1), alive_enemies());
    ng_text(2, 17, 0, str);

    ng_text(2, 21, 0, "ARROWS MOVE");
    ng_text(2, 22, 0, "A JUMP   B KICK");
    ng_text(2, 24, 0, "WALK RIGHT TO THE BALL");
}

static void game_over_screen(void) {
    bios_player_mod1 = 0;
    ng_cls();
    set_backdrop(0x0a00);
    ng_center_text_tall(11, 0, "CORE GAME OVER");
    sprintf(str, "SCORE %06lu", (unsigned long)game.score);
    ng_center_text(16, 0, str);
    ng_center_text(20, 0, "RETURNING TO MVS BIOS");
    for (u16 wait = 180; wait; --wait) {
        ng_wait_vblank();
    }
}

static void game_mode(void) {
    set_backdrop(0x8008);
    prev_pad = 0;

    gng_init(&game);
    gng_reset_run(&game);
    game.state = GNG_STATE_PLAY;

    while (game.state != GNG_STATE_GAMEOVER && game.state != GNG_STATE_HIGHSCORE) {
        GngInput in = read_input();
        gng_step(&game, in);
        draw_core_debug();
        ng_wait_vblank();
    }

    game_over_screen();
}

void player_start(void) {
    bios_user_mode = USER_MODE_GAME;
    bios_player_mod1 = 1;
}

void coin_sound(void) {
}

int main(void) {
    init_screen();
    demo_mode();
    if (bios_user_mode == USER_MODE_GAME) {
        game_mode();
    }
    return 0;
}

int main_mvs_title(void) {
    init_screen();
    title_mode();
    game_mode();
    return 0;
}
