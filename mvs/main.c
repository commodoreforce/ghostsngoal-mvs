#include <ngdevkit/neogeo.h>
#include <ngdevkit/bios-ram.h>
#include <ngdevkit/ng-fix.h>
#include <ngdevkit/ng-video.h>
#include <stdio.h>
#include "src/gfx_assets.h"

#define USER_MODE_GAME 0x2
#define SYSTEM_MVS 0x80

#define ADDR_SCB1 0x0000
#define ADDR_SCB2 0x8000
#define ADDR_SCB3 0x8200

#define SCB_BG 0
#define SCB_PLAYER 20
#define SCB_ZOMBIE 24
#define SCB_BALL 28

static char str[32];
static u8 prev_pad;
static u16 frame_counter;

static s16 player_x = 42;
static s16 player_y = 140;
static s16 player_vy = 0;
static u8 player_ground = 1;

static s16 zombie_x = 226;
static s16 zombie_y = 148;

static s16 ball_x = 118;
static s16 ball_y = 190;
static s16 ball_vx = 0;
static s16 ball_vy = 0;

static void set_backdrop(u16 color) {
    *((volatile u16*)0x401ffe) = color;
}

static void load_palette(u8 slot, const unsigned short *colors) {
    u16 base = (u16)slot * 16;
    for (u16 i = 0; i < 16; ++i) {
        MMAP_PALBANK1[base + i] = colors[i];
    }
}

static void init_fix_palette(void) {
    const u16 fix[] = {
        0x8000, 0x0fff, 0x0555, 0x0bbb,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    load_palette(0, fix);
}

static void init_visual_palettes(void) {
    init_fix_palette();
    load_palette(1, GNG_PAL_BG);
    load_palette(2, GNG_PAL_PLAYER);
    load_palette(3, GNG_PAL_ZOMBIE);
    load_palette(4, GNG_PAL_BALL);
}

/*
 * Neo Geo sprite objects are vertical 16x16 tile strips. A wide image is a
 * chain of sticky strips. sheet_cols is the source sheet tile stride; col0
 * lets us select one 64x64 animation frame from the 256x64 actor sheets.
 */
static void draw_chain(u16 first_scb, u16 tile_base, u16 sheet_cols,
                       u16 col0, u8 cols, u8 rows, u8 palette,
                       s16 x, s16 screen_y) {
    u16 attr = (u16)palette << 8;

    *REG_VRAMMOD = 1;
    for (u16 c = 0; c < cols; ++c) {
        *REG_VRAMADDR = ADDR_SCB1 + ((first_scb + c) * 64);
        u16 tile = tile_base + col0 + c;
        for (u16 r = 0; r < rows; ++r) {
            *REG_VRAMRW = tile + (r * sheet_cols);
            *REG_VRAMRW = attr;
        }
    }

    *REG_VRAMMOD = 0x200;
    *REG_VRAMADDR = ADDR_SCB2 + first_scb;
    *REG_VRAMRW = 0x0fff;
    *REG_VRAMRW = (u16)((-screen_y * 128) + rows);
    *REG_VRAMRW = (u16)(x * 128);

    for (u16 c = 1; c < cols; ++c) {
        *REG_VRAMADDR = ADDR_SCB2 + first_scb + c;
        *REG_VRAMRW = 0x0fff;
        *REG_VRAMRW = 1 << 6;
    }
}

static void draw_background(void) {
    draw_chain(SCB_BG, GNG_TILE_BG, GNG_BG_COLS, 0,
               GNG_BG_COLS, GNG_BG_ROWS, 1, 0, 0);
}

static void draw_player(u8 anim_frame) {
    draw_chain(SCB_PLAYER, GNG_TILE_PLAYER, GNG_ACTOR_STRIDE,
               (u16)(anim_frame * GNG_ACTOR_COLS),
               GNG_ACTOR_COLS, GNG_ACTOR_ROWS, 2, player_x, player_y);
}

static void draw_zombie(u8 anim_frame) {
    draw_chain(SCB_ZOMBIE, GNG_TILE_ZOMBIE, GNG_ACTOR_STRIDE,
               (u16)(anim_frame * GNG_ACTOR_COLS),
               GNG_ACTOR_COLS, GNG_ACTOR_ROWS, 3, zombie_x, zombie_y);
}

static void draw_ball(void) {
    draw_chain(SCB_BALL, GNG_TILE_BALL, 1, 0, 1, 1, 4, ball_x, ball_y);
}

static void init_screen(void) {
    ng_cls();
    set_backdrop(0x0000);
    init_visual_palettes();
}

static void demo_mode(void) {
    ng_cls();
    set_backdrop(0x0088);
    ng_center_text_tall(8, 0, "GHOSTS N GOALS");
    ng_center_text(12, 0, "NEO GEO MVS");
    ng_center_text(17, 0, "FIRST VISUAL SLICE");
    ng_center_text(22, 0, bios_mvs_flag == SYSTEM_MVS ? "INSERT COIN" : "PRESS START");

    u16 frames = 60 * 15;
    while (frames-- && bios_user_mode != USER_MODE_GAME) {
        ng_wait_vblank();
    }
}

static void title_mode(void) {
    ng_cls();
    set_backdrop(0x0800);
    ng_center_text_tall(8, 0, "GHOSTS N GOALS");
    ng_center_text(16, 0, "LEVEL 1 - MANUZZI");
    ng_center_text(22, 0, "PRESS START");
    while (bios_user_mode != USER_MODE_GAME) {
        sprintf(str, "TIMER %02x", bios_compulsion_timer);
        ng_center_text(24, 0, str);
        ng_wait_vblank();
    }
}

static void update_player(u8 pad) {
    u8 left = !!(pad & CNT_LEFT);
    u8 right = !!(pad & CNT_RIGHT);
    u8 jump_edge = !!((pad & CNT_A) && !(prev_pad & CNT_A));
    u8 kick_edge = !!((pad & CNT_B) && !(prev_pad & CNT_B));

    if (left && player_x > 4) player_x -= 2;
    if (right && player_x < 252) player_x += 2;

    if (jump_edge && player_ground) {
        player_vy = -8;
        player_ground = 0;
    }
    if (!player_ground) {
        player_vy += 1;
        if (player_vy > 8) player_vy = 8;
        player_y += player_vy;
        if (player_y >= 140) {
            player_y = 140;
            player_vy = 0;
            player_ground = 1;
        }
    }

    if (kick_edge) {
        s16 dx = ball_x - player_x;
        if (dx > -20 && dx < 70 && ball_y > player_y + 25) {
            ball_vx = right ? 7 : (left ? -7 : 7);
            ball_vy = -5;
        }
    }
}

static void update_ball(void) {
    ball_vy += 1;
    if (ball_vy > 9) ball_vy = 9;
    ball_x += ball_vx;
    ball_y += ball_vy;

    if (ball_y >= 190) {
        ball_y = 190;
        if (ball_vy > 2) ball_vy = -(ball_vy / 2);
        else ball_vy = 0;
        ball_vx = (ball_vx * 7) / 8;
    }
    if (ball_x < 2) {
        ball_x = 2;
        ball_vx = -ball_vx;
    }
    if (ball_x > 302) {
        ball_x = 302;
        ball_vx = -ball_vx;
    }
}

static void game_mode(void) {
    ng_cls();
    set_backdrop(0x0000);
    init_visual_palettes();

    player_x = 42;
    player_y = 140;
    player_vy = 0;
    player_ground = 1;
    zombie_x = 226;
    zombie_y = 148;
    ball_x = 118;
    ball_y = 190;
    ball_vx = 0;
    ball_vy = 0;
    prev_pad = 0;
    frame_counter = 0;

    draw_background();
    draw_player(0);
    draw_zombie(0);
    draw_ball();

    /* Static FIX text only: never clear/redraw it every frame. */
    ng_text(1, 1, 0, "L1 MANUZZI  A:JUMP B:KICK");

    for (;;) {
        u8 pad = bios_p1current;
        u8 moving = !!(pad & (CNT_LEFT | CNT_RIGHT));
        u8 player_frame;

        update_player(pad);
        update_ball();

        if (!player_ground) {
            player_frame = 3;
        } else if (moving) {
            player_frame = (u8)(2 + ((frame_counter >> 3) & 1));
        } else {
            player_frame = (u8)((frame_counter >> 5) & 1);
        }

        draw_player(player_frame);
        draw_zombie((u8)((frame_counter >> 4) & 3));
        draw_ball();

        prev_pad = pad;
        frame_counter++;
        ng_wait_vblank();
    }
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
    if (bios_user_mode == USER_MODE_GAME) game_mode();
    return 0;
}

int main_mvs_title(void) {
    init_screen();
    title_mode();
    game_mode();
    return 0;
}
