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

#define VISIBLE_STRIPS 21

#define SCB_SKY      0
#define SCB_CITY     21
#define SCB_STAGE    42
#define SCB_GROUND   63
#define SCB_PLAYER   83
#define SCB_ZOMBIE   87
#define SCB_BALL     91

#define LEVEL1_WORLD_W (262 * 16)
#define LEVEL1_MAX_SCROLL (LEVEL1_WORLD_W - GNG_SCREEN_W)
#define LEVEL1_CHECKPOINT 121
#define SIGN_X 262
#define STADIUM_FROM 570
#define INSIDE_BEGIN ((LEVEL1_CHECKPOINT - 8) * 16)

#define GROUND_Y 208
#define PLAYER_GROUND_Y 140
#define BALL_GROUND_Y 190
#define CAMERA_MAX (GNG_DEMO_WORLD_W - GNG_SCREEN_W)

static char str[40];
static u8 prev_pad;
static u16 frame_counter;

static s16 player_world_x;
static s16 player_y;
static s16 player_vy;
static u8 player_ground;
static u8 player_facing_right;
static u8 kick_timer;

static s16 ball_world_x;
static s16 ball_y;
static s16 ball_vx;
static s16 ball_vy;

static s16 zombie_world_x;
static s16 zombie_y;
static u8 zombie_state;
static u8 zombie_die_timer;

static s16 camera_x;
static u16 score;

static s16 iabs16(s16 v) { return v < 0 ? -v : v; }

static s16 clamp16(s16 v, s16 lo, s16 hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void set_backdrop(u16 color) {
    *((volatile u16*)0x401ffe) = color;
}

static void load_palette(u8 slot, const unsigned short *colors) {
    u16 base = (u16)slot * 16;
    u16 i;
    for (i = 0; i < 16; ++i) MMAP_PALBANK1[base + i] = colors[i];
}

static void init_fix_palette(void) {
    const u16 fix[] = {
        0x8000, 0x0fff, 0x0555, 0x0bbb,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    load_palette(0, fix);
}

static void init_visual_palettes(void) {
    u16 p;
    init_fix_palette();
    for (p = 0; p < GNG_PALETTE_COUNT; ++p) {
        load_palette((u8)(p + 1), &GNG_PALETTES[p][0]);
    }
}

static void position_strip(u16 scb, u8 rows, s16 x, s16 y) {
    *REG_VRAMMOD = 0x200;
    *REG_VRAMADDR = ADDR_SCB2 + scb;
    *REG_VRAMRW = 0x0fff;
    *REG_VRAMRW = (u16)((-y * 128) + rows);
    *REG_VRAMRW = (u16)(x * 128);
}

static void hide_strip(u16 scb) {
    position_strip(scb, 1, 500, 240);
}

static void draw_layer_window(
    u16 first_scb,
    u16 tile_base,
    u16 sheet_cols,
    u8 rows,
    const unsigned char *palmap,
    s16 image_x,
    s16 image_y
) {
    s16 hidden = 0;
    u16 start_col = 0;
    s16 base_x = image_x;
    u16 c, r;

    if (image_x < 0) {
        hidden = (s16)(-image_x);
        start_col = (u16)(hidden >> 4);
        base_x = (s16)(-(hidden & 15));
    }

    for (c = 0; c < VISIBLE_STRIPS; ++c) {
        u16 scb = first_scb + c;
        u16 src_col = start_col + c;
        s16 x = (s16)(base_x + (s16)(c * 16));

        if (src_col >= sheet_cols || x >= GNG_SCREEN_W || x <= -16) {
            hide_strip(scb);
            continue;
        }

        *REG_VRAMMOD = 1;
        *REG_VRAMADDR = ADDR_SCB1 + (scb * 64);
        for (r = 0; r < rows; ++r) {
            u16 idx = (u16)(r * sheet_cols + src_col);
            u16 tile = (u16)(tile_base + idx);
            u16 pal = palmap[idx];
            *REG_VRAMRW = tile;
            *REG_VRAMRW = (u16)(pal << 8);
        }
        position_strip(scb, rows, x, image_y);
    }
}

static void draw_repeated_ground(void) {
    u16 c;
    u16 pal = GNG_GROUND_PALMAP[0];
    for (c = 0; c < 20; ++c) {
        u16 scb = SCB_GROUND + c;
        *REG_VRAMMOD = 1;
        *REG_VRAMADDR = ADDR_SCB1 + (scb * 64);
        *REG_VRAMRW = GNG_GROUND_TILE;
        *REG_VRAMRW = (u16)(pal << 8);
        position_strip(scb, 1, (s16)(c * 16), GROUND_Y);
    }
}

static void draw_actor(
    u16 first_scb,
    u16 tile_base,
    u16 sheet_cols,
    const unsigned char *palmap,
    u8 frame,
    s16 x,
    s16 y,
    u8 flip_x
) {
    u16 col0 = (u16)frame * 4;
    u16 c, r;
    for (c = 0; c < 4; ++c) {
        u16 scb = first_scb + c;
        u16 src_col = col0 + c;
        *REG_VRAMMOD = 1;
        *REG_VRAMADDR = ADDR_SCB1 + (scb * 64);
        for (r = 0; r < 4; ++r) {
            u16 idx = (u16)(r * sheet_cols + src_col);
            u16 attr = (u16)(palmap[idx] << 8);
            if (flip_x) attr |= 0x0001;
            *REG_VRAMRW = (u16)(tile_base + idx);
            *REG_VRAMRW = attr;
        }
        if (!flip_x) position_strip(scb, 4, (s16)(x + c * 16), y);
        else position_strip(scb, 4, (s16)(x + (3 - c) * 16), y);
    }
}

static void draw_ball(void) {
    u16 pal = GNG_BALL_PALMAP[0];
    *REG_VRAMMOD = 1;
    *REG_VRAMADDR = ADDR_SCB1 + (SCB_BALL * 64);
    *REG_VRAMRW = GNG_BALL_TILE;
    *REG_VRAMRW = (u16)(pal << 8);
    position_strip(SCB_BALL, 1, (s16)(ball_world_x - camera_x), ball_y);
}

static s16 demo_to_web_scroll(void) {
    s32 denom = (s32)GNG_DEMO_WORLD_W - GNG_SCREEN_W;
    if (denom <= 0) return 0;
    return (s16)(((s32)camera_x * LEVEL1_MAX_SCROLL) / denom);
}

static s16 layer_scroll(s16 web_sx, s16 width, s16 num, s16 den) {
    s16 max_scroll = (s16)(width - GNG_SCREEN_W);
    s32 v;
    if (max_scroll <= 0) return 0;
    v = ((s32)web_sx * num) / den;
    if (v < 0) v = 0;
    if (v > max_scroll) v = max_scroll;
    return (s16)v;
}

static void draw_scenery(void) {
    s16 web_sx = demo_to_web_scroll();
    s16 sky_scroll = 0;
    s16 city_scroll = 0;
    s16 stadium_x;
    s16 min_stage_x;
    s16 stage_delta;
    u8 show_inside = (web_sx > INSIDE_BEGIN);

    if (GNG_SKY_W > GNG_SCREEN_W) {
        sky_scroll = (s16)(((s32)web_sx * (GNG_SKY_W - GNG_SCREEN_W)) / LEVEL1_MAX_SCROLL);
    }
    city_scroll = layer_scroll(web_sx, GNG_CITY_W, 15, 100);

    draw_layer_window(
        SCB_SKY, GNG_SKY_TILE, GNG_SKY_COLS, GNG_SKY_ROWS,
        GNG_SKY_PALMAP, (s16)-sky_scroll, 0
    );
    draw_layer_window(
        SCB_CITY, GNG_CITY_TILE, GNG_CITY_COLS, GNG_CITY_ROWS,
        GNG_CITY_PALMAP, (s16)-city_scroll, 0
    );

    if (show_inside) {
        s16 d = (s16)(web_sx - INSIDE_BEGIN);
        s16 inside_scroll = layer_scroll(d, GNG_INSIDE_W, 295, 1000);
        draw_layer_window(
            SCB_STAGE, GNG_INSIDE_TILE, GNG_INSIDE_COLS, GNG_INSIDE_ROWS,
            GNG_INSIDE_PALMAP, (s16)-inside_scroll, 0
        );
    } else {
        stage_delta = (s16)(web_sx - STADIUM_FROM);
        stadium_x = (s16)(GNG_SCREEN_W - ((s32)stage_delta * 35) / 100);
        min_stage_x = (s16)(GNG_SCREEN_W - GNG_RETRO_W);
        stadium_x = clamp16(stadium_x, min_stage_x, GNG_SCREEN_W);

        if (web_sx < 1346) {
            draw_layer_window(
                SCB_STAGE, GNG_RETRO_TILE, GNG_RETRO_COLS, GNG_RETRO_ROWS,
                GNG_RETRO_PALMAP, stadium_x, 0
            );
        } else {
            draw_layer_window(
                SCB_STAGE, GNG_INGRESSO_TILE, GNG_INGRESSO_COLS, GNG_INGRESSO_ROWS,
                GNG_INGRESSO_PALMAP, stadium_x, 0
            );
        }
    }

    draw_repeated_ground();
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
    ng_center_text(17, 0, "FIDELITY BUILD");
    ng_center_text(22, 0, bios_mvs_flag == SYSTEM_MVS ? "INSERT COIN" : "PRESS START");
    {
        u16 frames = 60 * 15;
        while (frames-- && bios_user_mode != USER_MODE_GAME) ng_wait_vblank();
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

static void update_camera(void) {
    s16 desired = (s16)(player_world_x - 144);
    camera_x = clamp16(desired, 0, CAMERA_MAX);
}

static void kick_ball(void) {
    s16 dx = (s16)(ball_world_x - player_world_x);
    if (dx > -20 && dx < 72 && ball_y > player_y + 24) {
        ball_vx = player_facing_right ? 9 : -9;
        ball_vy = -5;
    }
}

static void update_player(u8 pad) {
    u8 left = !!(pad & CNT_LEFT);
    u8 right = !!(pad & CNT_RIGHT);
    u8 jump_edge = !!((pad & CNT_A) && !(prev_pad & CNT_A));
    u8 kick_edge = !!((pad & CNT_B) && !(prev_pad & CNT_B));
    s16 old_x = player_world_x;

    if (left) {
        player_world_x -= 2;
        player_facing_right = 0;
    }
    if (right) {
        player_world_x += 2;
        player_facing_right = 1;
    }

    player_world_x = clamp16(player_world_x, 4, GNG_DEMO_WORLD_W - 68);

    if (zombie_state == 0 &&
        iabs16((player_world_x + 28) - (zombie_world_x + 28)) < 34 &&
        player_y > 115) {
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
        if (player_y >= PLAYER_GROUND_Y) {
            player_y = PLAYER_GROUND_Y;
            player_vy = 0;
            player_ground = 1;
        }
    }

    if (kick_edge) {
        kick_timer = 10;
        kick_ball();
    }
}

static void update_ball(void) {
    ball_vy += 1;
    if (ball_vy > 9) ball_vy = 9;
    ball_world_x += ball_vx;
    ball_y += ball_vy;

    if (ball_y >= BALL_GROUND_Y) {
        ball_y = BALL_GROUND_Y;
        if (ball_vy > 2) ball_vy = -(ball_vy / 2);
        else ball_vy = 0;
        ball_vx = (ball_vx * 7) / 8;
    }

    if (ball_world_x < 2) {
        ball_world_x = 2;
        ball_vx = -ball_vx;
    }
    if (ball_world_x > GNG_DEMO_WORLD_W - 18) {
        ball_world_x = GNG_DEMO_WORLD_W - 18;
        ball_vx = -ball_vx;
    }

    if (zombie_state == 0 &&
        iabs16(ball_world_x - (zombie_world_x + 18)) < 28 &&
        iabs16(ball_y - (zombie_y + 32)) < 38 &&
        iabs16(ball_vx) >= 3) {
        zombie_state = 1;
        zombie_die_timer = 28;
        score += 100;
        ball_vx = (s16)(-ball_vx / 2);
        ball_vy = -4;
    }
}

static void update_zombie(void) {
    if (zombie_state != 0) {
        if (zombie_die_timer) zombie_die_timer--;
        else zombie_state = 2;
        return;
    }

    if ((frame_counter & 1) == 0 && zombie_world_x > player_world_x + 72) {
        zombie_world_x--;
    }
}

static u8 player_frame_for(u8 moving) {
    if (kick_timer) return GNG_PLAYER_FRAME_KICK;
    if (!player_ground) {
        return player_vy < 0 ? GNG_PLAYER_FRAME_JUMP : GNG_PLAYER_FRAME_FALL;
    }
    if (moving) {
        return (u8)(GNG_PLAYER_FRAME_RUN0 + ((frame_counter >> 2) & 3));
    }
    return (u8)(GNG_PLAYER_FRAME_IDLE0 + ((frame_counter >> 5) & 1));
}

static void draw_characters(u8 player_frame) {
    draw_actor(
        SCB_PLAYER, GNG_PLAYER_TILE, GNG_PLAYER_COLS, GNG_PLAYER_PALMAP,
        player_frame, (s16)(player_world_x - camera_x), player_y,
        player_facing_right ? 0 : 1
    );

    if (zombie_state == 2) {
        hide_strip(SCB_ZOMBIE);
        hide_strip(SCB_ZOMBIE + 1);
        hide_strip(SCB_ZOMBIE + 2);
        hide_strip(SCB_ZOMBIE + 3);
    } else {
        u8 zf;
        if (zombie_state == 1) {
            zf = zombie_die_timer > 14 ? GNG_ZOMBIE_FRAME_DIE0 : GNG_ZOMBIE_FRAME_DIE1;
        } else {
            zf = (u8)(GNG_ZOMBIE_FRAME_WALK0 + ((frame_counter >> 4) & 3));
        }
        draw_actor(
            SCB_ZOMBIE, GNG_ZOMBIE_TILE, GNG_ZOMBIE_COLS, GNG_ZOMBIE_PALMAP,
            zf, (s16)(zombie_world_x - camera_x), zombie_y, 1
        );
    }

    draw_ball();
}

static void game_mode(void) {
    ng_cls();
    set_backdrop(0x8000);
    init_visual_palettes();

    player_world_x = 48;
    player_y = PLAYER_GROUND_Y;
    player_vy = 0;
    player_ground = 1;
    player_facing_right = 1;
    kick_timer = 0;

    ball_world_x = 124;
    ball_y = BALL_GROUND_Y;
    ball_vx = 0;
    ball_vy = 0;

    zombie_world_x = 520;
    zombie_y = 144;
    zombie_state = 0;
    zombie_die_timer = 0;

    camera_x = 0;
    score = 0;
    prev_pad = 0;
    frame_counter = 0;

    for (;;) {
        u8 pad = bios_p1current;
        u8 moving = !!(pad & (CNT_LEFT | CNT_RIGHT));
        u8 pf;

        update_player(pad);
        update_ball();
        update_zombie();
        update_camera();

        pf = player_frame_for(moving);

        draw_scenery();
        draw_characters(pf);

        if (kick_timer) kick_timer--;
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
