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

#define SCB_BG 0
#define SCB_PLAYER 32
#define SCB_ZOMBIE 36
#define SCB_BALL 40

#define GROUND_PLAYER_Y 140
#define GROUND_BALL_Y 190
#define CAMERA_MAX (GNG_WORLD_W - GNG_SCREEN_W)

static char str[40];
static u8 prev_pad;
static u16 frame_counter;

static s16 player_world_x = 48;
static s16 player_y = GROUND_PLAYER_Y;
static s16 player_vy = 0;
static u8 player_ground = 1;
static u8 player_facing_right = 1;

static s16 zombie_world_x = 372;
static s16 zombie_y = 148;
static u8 zombie_alive = 1;
static u8 zombie_hit_timer = 0;

static s16 ball_world_x = 124;
static s16 ball_y = GROUND_BALL_Y;
static s16 ball_vx = 0;
static s16 ball_vy = 0;

static s16 camera_x = 0;
static u16 score = 0;

static s16 iabs16(s16 v) { return v < 0 ? -v : v; }

static void set_backdrop(u16 color) {
    *((volatile u16*)0x401ffe) = color;
}

static void load_palette(u8 slot, const unsigned short *colors) {
    u16 base = (u16)slot * 16;
    for (u16 i = 0; i < 16; ++i) MMAP_PALBANK1[base + i] = colors[i];
}

static void load_palette_matrix(u8 first, const unsigned short *colors, u16 count) {
    for (u16 p = 0; p < count; ++p) load_palette((u8)(first + p), colors + p * 16);
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
    load_palette_matrix(GNG_BG_PAL_FIRST, &GNG_BG_PALETTES[0][0], GNG_BG_PAL_COUNT);
    load_palette_matrix(GNG_PLAYER_PAL_FIRST, &GNG_PLAYER_PALETTES[0][0], GNG_PLAYER_PAL_COUNT);
    load_palette_matrix(GNG_ZOMBIE_PAL_FIRST, &GNG_ZOMBIE_PALETTES[0][0], GNG_ZOMBIE_PAL_COUNT);
    load_palette_matrix(GNG_BALL_PAL_FIRST, &GNG_BALL_PALETTES[0][0], GNG_BALL_PAL_COUNT);
}

/* Write tile numbers + each tile's own palette attribute into SCB1. */
static void map_chain(u16 first_scb, u16 tile_base, u16 sheet_cols,
                      u16 col0, u8 cols, u8 rows,
                      const unsigned char *palmap) {
    *REG_VRAMMOD = 1;
    for (u16 c = 0; c < cols; ++c) {
        *REG_VRAMADDR = ADDR_SCB1 + ((first_scb + c) * 64);
        u16 tile = tile_base + col0 + c;
        for (u16 r = 0; r < rows; ++r) {
            u16 src_col = col0 + c;
            u16 pal = palmap[r * sheet_cols + src_col];
            *REG_VRAMRW = tile + (r * sheet_cols);
            *REG_VRAMRW = (u16)(pal << 8);
        }
    }
}

/* Explicit X for every 16px strip; this is stable when the background goes offscreen. */
static void position_chain(u16 first_scb, u8 cols, u8 rows, s16 x, s16 screen_y) {
    for (u16 c = 0; c < cols; ++c) {
        *REG_VRAMMOD = 0x200;
        *REG_VRAMADDR = ADDR_SCB2 + first_scb + c;
        *REG_VRAMRW = 0x0fff;
        *REG_VRAMRW = (u16)((-screen_y * 128) + rows);
        *REG_VRAMRW = (u16)((x + (s16)(c * 16)) * 128);
    }
}

static void setup_background(void) {
    map_chain(SCB_BG, GNG_TILE_BG, GNG_BG_COLS, 0,
              GNG_BG_COLS, GNG_BG_ROWS, GNG_BG_PALMAP);
}

static void draw_background(void) {
    position_chain(SCB_BG, GNG_BG_COLS, GNG_BG_ROWS, (s16)-camera_x, 0);
}

static void draw_player(u8 anim_frame) {
    u16 col0 = (u16)(anim_frame * GNG_ACTOR_COLS);
    map_chain(SCB_PLAYER, GNG_TILE_PLAYER, GNG_ACTOR_STRIDE, col0,
              GNG_ACTOR_COLS, GNG_ACTOR_ROWS, GNG_PLAYER_PALMAP);
    position_chain(SCB_PLAYER, GNG_ACTOR_COLS, GNG_ACTOR_ROWS,
                   (s16)(player_world_x - camera_x), player_y);
}

static void draw_zombie(u8 anim_frame) {
    if (!zombie_alive) {
        position_chain(SCB_ZOMBIE, GNG_ACTOR_COLS, GNG_ACTOR_ROWS, 500, 240);
        return;
    }
    u16 col0 = (u16)(anim_frame * GNG_ACTOR_COLS);
    map_chain(SCB_ZOMBIE, GNG_TILE_ZOMBIE, GNG_ACTOR_STRIDE, col0,
              GNG_ACTOR_COLS, GNG_ACTOR_ROWS, GNG_ZOMBIE_PALMAP);
    position_chain(SCB_ZOMBIE, GNG_ACTOR_COLS, GNG_ACTOR_ROWS,
                   (s16)(zombie_world_x - camera_x), zombie_y);
}

static void draw_ball(void) {
    map_chain(SCB_BALL, GNG_TILE_BALL, 1, 0, 1, 1, GNG_BALL_PALMAP);
    position_chain(SCB_BALL, 1, 1, (s16)(ball_world_x - camera_x), ball_y);
}

static void init_screen(void) {
    ng_cls();
    set_backdrop(0x8000);
    init_visual_palettes();
}

static void demo_mode(void) {
    ng_cls();
    set_backdrop(0x0088);
    ng_center_text_tall(8, 0, "GHOSTS N GOALS");
    ng_center_text(12, 0, "NEO GEO MVS");
    ng_center_text(17, 0, "HIGH FIDELITY L1");
    ng_center_text(22, 0, bios_mvs_flag == SYSTEM_MVS ? "INSERT COIN" : "PRESS START");
    u16 frames = 60 * 15;
    while (frames-- && bios_user_mode != USER_MODE_GAME) ng_wait_vblank();
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

static void update_camera(void) {
    s16 desired = player_world_x - 150;
    if (desired < 0) desired = 0;
    if (desired > CAMERA_MAX) desired = CAMERA_MAX;
    camera_x = desired;
}

static void update_player(u8 pad) {
    u8 left = !!(pad & CNT_LEFT);
    u8 right = !!(pad & CNT_RIGHT);
    u8 jump_edge = !!((pad & CNT_A) && !(prev_pad & CNT_A));
    u8 shoot_edge = !!((pad & CNT_B) && !(prev_pad & CNT_B));

    s16 old_x = player_world_x;
    if (left) {
        player_world_x -= 2;
        player_facing_right = 0;
    }
    if (right) {
        player_world_x += 2;
        player_facing_right = 1;
    }
    if (player_world_x < 4) player_world_x = 4;
    if (player_world_x > GNG_WORLD_W - 68) player_world_x = GNG_WORLD_W - 68;

    /* Basic body collision: player cannot simply walk through the zombie. */
    if (zombie_alive && iabs16((player_world_x + 28) - (zombie_world_x + 28)) < 34 && player_y > 115) {
        player_world_x = old_x;
    }

    if (jump_edge && player_ground) {
        player_vy = -8;
        player_ground = 0;
    }
    if (!player_ground) {
        player_vy += 1;
        if (player_vy > 8) player_vy = 8;
        player_y += player_vy;
        if (player_y >= GROUND_PLAYER_Y) {
            player_y = GROUND_PLAYER_Y;
            player_vy = 0;
            player_ground = 1;
        }
    }

    /* B is the football shot: kick the ball only when Shpendi is close enough. */
    if (shoot_edge) {
        s16 dx = ball_world_x - player_world_x;
        if (dx > -18 && dx < 72 && ball_y > player_y + 24) {
            ball_vx = player_facing_right ? 8 : -8;
            ball_vy = -5;
        }
    }
}

static void update_ball(void) {
    ball_vy += 1;
    if (ball_vy > 9) ball_vy = 9;
    ball_world_x += ball_vx;
    ball_y += ball_vy;

    if (ball_y >= GROUND_BALL_Y) {
        ball_y = GROUND_BALL_Y;
        if (ball_vy > 2) ball_vy = -(ball_vy / 2);
        else ball_vy = 0;
        ball_vx = (ball_vx * 7) / 8;
    }
    if (ball_world_x < 2) {
        ball_world_x = 2;
        ball_vx = -ball_vx;
    }
    if (ball_world_x > GNG_WORLD_W - 18) {
        ball_world_x = GNG_WORLD_W - 18;
        ball_vx = -ball_vx;
    }

    if (zombie_alive && iabs16(ball_world_x - (zombie_world_x + 18)) < 28 && iabs16(ball_y - (zombie_y + 32)) < 38 && iabs16(ball_vx) >= 3) {
        zombie_hit_timer = 12;
        zombie_alive = 0;
        score += 100;
        ball_vx = -ball_vx / 2;
        ball_vy = -4;
    }
}

static void game_mode(void) {
    ng_cls();
    set_backdrop(0x8000);
    init_visual_palettes();

    player_world_x = 48;
    player_y = GROUND_PLAYER_Y;
    player_vy = 0;
    player_ground = 1;
    player_facing_right = 1;
    zombie_world_x = 372;
    zombie_y = 148;
    zombie_alive = 1;
    zombie_hit_timer = 0;
    ball_world_x = 124;
    ball_y = GROUND_BALL_Y;
    ball_vx = 0;
    ball_vy = 0;
    camera_x = 0;
    score = 0;
    prev_pad = 0;
    frame_counter = 0;

    setup_background();
    draw_background();
    draw_player(0);
    draw_zombie(0);
    draw_ball();

    ng_text(1, 1, 0, "ARROWS MOVE  A JUMP  B SHOOT");

    for (;;) {
        u8 pad = bios_p1current;
        u8 moving = !!(pad & (CNT_LEFT | CNT_RIGHT));
        u8 player_frame;

        update_player(pad);
        update_ball();
        update_camera();

        if (!player_ground) player_frame = 3;
        else if (moving) player_frame = (u8)(2 + ((frame_counter >> 3) & 1));
        else player_frame = (u8)((frame_counter >> 5) & 1);

        draw_background();
        draw_player(player_frame);
        draw_zombie((u8)((frame_counter >> 4) & 3));
        draw_ball();

        if (zombie_hit_timer) zombie_hit_timer--;
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
