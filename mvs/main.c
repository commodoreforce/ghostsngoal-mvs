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
#define SCB_SKY        0
#define SCB_CITY       21
#define SCB_STAGE      42
#define SCB_PITCH      63
#define SCB_GROUND     84
#define SCB_PLAYER     104
#define SCB_ZOMBIE     109
#define SCB_BALL0      114
#define SCB_FOG        117
#define SCB_FOREGROUND 138

#define LEVEL1_WORLD_W (262 * 16)
#define LEVEL1_MAX_SCROLL (LEVEL1_WORLD_W - GNG_LOGICAL_W)
#define LEVEL1_CHECKPOINT 121
#define SIGN_X 262
#define SIGN_SCROLL ((LEVEL1_CHECKPOINT * 16) - (GNG_LOGICAL_W / 2))
#define STADIUM_FROM 694
#define STAGE_BLEND_START (SIGN_SCROLL - 560)
#define STAGE_BLEND_END (STAGE_BLEND_START + 260)
#define INSIDE_BEGIN ((LEVEL1_CHECKPOINT - 8) * 16)

#define GROUND_Y 208
#define PLAYER_GROUND_Y 144
#define CAMERA_MAX (LEVEL1_WORLD_W - GNG_LOGICAL_W)
#define MAX_BALLS 3
#define BALL_LIFE 49

/* Web physics at 60 Hz, represented in 8.8 fixed point. */
#define FP_SHIFT 8
#define FP_ONE (1L << FP_SHIFT)
#define RUN_SPEED_FP 469       /* 110 px/s */
#define GROUND_ACCEL_FP 156    /* 2200 px/s^2 */
#define AIR_ACCEL_FP 92        /* 1300 px/s^2 */
#define JUMP_VEL_FP (-1293)    /* -303 px/s */
#define GRAVITY_FP 60          /* 850 px/s^2 */
#define MAX_FALL_FP 2219       /* 520 px/s */
#define RELEASE_CUT_FP (-469)  /* -110 px/s */

#define STAGE_RETRO 0
#define STAGE_BLEND25 1
#define STAGE_BLEND50 2
#define STAGE_BLEND75 3
#define STAGE_INGRESSO 4
#define STAGE_INSIDE 5

typedef struct {
    s16 x, y;
    s16 vx, vy;
    u8 life;
    u8 bounces;
    u8 active;
} Ball;

static char str[40];
static u8 prev_pad;
static u16 frame_counter;

static s32 player_x_fp;
static s32 player_y_fp;
static s32 player_vx_fp;
static s32 player_vy_fp;
static s16 player_x;
static s16 player_y;
static u8 player_ground;
static u8 player_facing_right;
static u8 kick_timer;

static s16 zombie_x;
static s16 zombie_y;
static u8 zombie_state;
static u8 zombie_die_timer;

static s32 camera_x_fp;
static s16 camera_x;
static Ball balls[MAX_BALLS];
static u8 stage_kind = 255;

static s16 iabs16(s16 v) { return v < 0 ? -v : v; }
static s16 clamp16(s16 v, s16 lo, s16 hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }
static s16 px(s16 logical_x) { return (s16)(((s32)logical_x * GNG_X_NUM) / GNG_X_DEN); }
static s32 approach32(s32 value, s32 target, s32 amount) {
    if (value < target) { value += amount; if (value > target) value = target; }
    else if (value > target) { value -= amount; if (value < target) value = target; }
    return value;
}

static void sync_player_xy(void) {
    player_x = (s16)(player_x_fp >> FP_SHIFT);
    player_y = (s16)(player_y_fp >> FP_SHIFT);
}

static void set_backdrop(u16 color) { *((volatile u16*)0x401ffe) = color; }

static void load_palette(u8 slot, const unsigned short *colors) {
    u16 base = (u16)slot * 16, i;
    for (i = 0; i < 16; ++i) MMAP_PALBANK1[base + i] = colors[i];
}

static void load_palette_set(u8 first, const unsigned short *pals, u16 count) {
    u16 i;
    for (i = 0; i < count; ++i) load_palette((u8)(first + i), pals + i * 16);
}

static void init_fix_palette(void) {
    const u16 fix[] = { 0x8000, 0x0fff, 0x0555, 0x0bbb, 0,0,0,0,0,0,0,0,0,0,0,0 };
    load_palette(0, fix);
}

static void load_static_palettes(void) {
    init_fix_palette();
    load_palette_set(GNG_SKY_PAL_FIRST, &GNG_SKY_PALETTES[0][0], GNG_SKY_PAL_COUNT);
    load_palette_set(GNG_CITY_PAL_FIRST, &GNG_CITY_PALETTES[0][0], GNG_CITY_PAL_COUNT);
    load_palette_set(GNG_PLAYER_PAL_FIRST, &GNG_PLAYER_PALETTES[0][0], GNG_PLAYER_PAL_COUNT);
    load_palette_set(GNG_ZOMBIE_PAL_FIRST, &GNG_ZOMBIE_PALETTES[0][0], GNG_ZOMBIE_PAL_COUNT);
    load_palette_set(GNG_BALL_PAL_FIRST, &GNG_BALL_PALETTES[0][0], GNG_BALL_PAL_COUNT);
    load_palette_set(GNG_GROUND_PAL_FIRST, &GNG_GROUND_PALETTES[0][0], GNG_GROUND_PAL_COUNT);
    load_palette_set(GNG_FOG_PAL_FIRST, &GNG_FOG_PALETTES[0][0], GNG_FOG_PAL_COUNT);
    load_palette_set(GNG_PITCH_PAL_FIRST, &GNG_PITCH_PALETTES[0][0], GNG_PITCH_PAL_COUNT);
    load_palette_set(GNG_FG_CEMETERY_PAL_FIRST, &GNG_FG_CEMETERY_PALETTES[0][0], GNG_FG_CEMETERY_PAL_COUNT);
    load_palette_set(GNG_FG_PITCH_PAL_FIRST, &GNG_FG_PITCH_PALETTES[0][0], GNG_FG_PITCH_PAL_COUNT);
}

static void load_stage_palettes(u8 kind) {
    if (kind == stage_kind) return;
    stage_kind = kind;
    if (kind == STAGE_RETRO) load_palette_set(GNG_RETRO_PAL_FIRST, &GNG_RETRO_PALETTES[0][0], GNG_RETRO_PAL_COUNT);
    else if (kind == STAGE_BLEND25) load_palette_set(GNG_BLEND25_PAL_FIRST, &GNG_BLEND25_PALETTES[0][0], GNG_BLEND25_PAL_COUNT);
    else if (kind == STAGE_BLEND50) load_palette_set(GNG_BLEND50_PAL_FIRST, &GNG_BLEND50_PALETTES[0][0], GNG_BLEND50_PAL_COUNT);
    else if (kind == STAGE_BLEND75) load_palette_set(GNG_BLEND75_PAL_FIRST, &GNG_BLEND75_PALETTES[0][0], GNG_BLEND75_PAL_COUNT);
    else if (kind == STAGE_INGRESSO) load_palette_set(GNG_INGRESSO_PAL_FIRST, &GNG_INGRESSO_PALETTES[0][0], GNG_INGRESSO_PAL_COUNT);
    else load_palette_set(GNG_INSIDE_PAL_FIRST, &GNG_INSIDE_PALETTES[0][0], GNG_INSIDE_PAL_COUNT);
}

static void position_strip(u16 scb, u8 rows, s16 x, s16 y) {
    *REG_VRAMMOD = 0x200;
    *REG_VRAMADDR = ADDR_SCB2 + scb;
    *REG_VRAMRW = 0x0fff;
    *REG_VRAMRW = (u16)((-y * 128) + rows);
    *REG_VRAMRW = (u16)(x * 128);
}

static void hide_strip(u16 scb) { position_strip(scb, 1, 500, 240); }
static void hide_range(u16 first, u16 count) { u16 i; for (i = 0; i < count; ++i) hide_strip(first + i); }

static void draw_layer_window(u16 first_scb, u16 tile_base, u16 sheet_cols, u8 rows,
                              const unsigned char *palmap, s16 image_x, s16 image_y) {
    s16 hidden = 0, base_x = image_x;
    u16 start_col = 0, c, r;
    if (image_x < 0) {
        hidden = (s16)(-image_x);
        start_col = (u16)(hidden >> 4);
        base_x = (s16)(-(hidden & 15));
    }
    for (c = 0; c < VISIBLE_STRIPS; ++c) {
        u16 scb = first_scb + c, src_col = start_col + c;
        s16 x = (s16)(base_x + c * 16);
        if (src_col >= sheet_cols || x >= GNG_SCREEN_W || x <= -16) { hide_strip(scb); continue; }
        *REG_VRAMMOD = 1;
        *REG_VRAMADDR = ADDR_SCB1 + (scb * 64);
        for (r = 0; r < rows; ++r) {
            u16 idx = (u16)(r * sheet_cols + src_col);
            *REG_VRAMRW = (u16)(tile_base + idx);
            *REG_VRAMRW = (u16)(palmap[idx] << 8);
        }
        position_strip(scb, rows, x, image_y);
    }
}

static void draw_repeating_layer(u16 first_scb, u16 tile_base, u16 sheet_cols, u8 rows,
                                 const unsigned char *palmap, s16 logical_scroll, s16 image_y) {
    s32 physical_scroll = ((s32)logical_scroll * GNG_X_NUM) / GNG_X_DEN;
    s32 width = (s32)sheet_cols * 16;
    u16 c, r;
    s16 base_x;
    u16 start_col;
    while (physical_scroll < 0) physical_scroll += width;
    if (width > 0) physical_scroll %= width;
    start_col = (u16)(physical_scroll >> 4);
    base_x = (s16)(-(physical_scroll & 15));
    for (c = 0; c < VISIBLE_STRIPS; ++c) {
        u16 scb = first_scb + c;
        u16 src_col = (u16)((start_col + c) % sheet_cols);
        s16 x = (s16)(base_x + c * 16);
        *REG_VRAMMOD = 1;
        *REG_VRAMADDR = ADDR_SCB1 + (scb * 64);
        for (r = 0; r < rows; ++r) {
            u16 idx = (u16)(r * sheet_cols + src_col);
            *REG_VRAMRW = (u16)(tile_base + idx);
            *REG_VRAMRW = (u16)(palmap[idx] << 8);
        }
        position_strip(scb, rows, x, image_y);
    }
}

static void draw_repeated_ground(void) {
    u16 c, pal = GNG_GROUND_PALMAP[0];
    for (c = 0; c < 20; ++c) {
        u16 scb = SCB_GROUND + c;
        *REG_VRAMMOD = 1; *REG_VRAMADDR = ADDR_SCB1 + scb * 64;
        *REG_VRAMRW = GNG_GROUND_TILE; *REG_VRAMRW = (u16)(pal << 8);
        position_strip(scb, 1, (s16)(c * 16), GROUND_Y);
    }
}

static void draw_actor(u16 first_scb, u16 tile_base, u16 sheet_cols, const unsigned char *palmap,
                       u8 frame, s16 logical_x, s16 y, u8 flip_x) {
    u16 col0 = (u16)frame * GNG_ACTOR_COLS, c, r;
    s16 x = px(logical_x);
    for (c = 0; c < GNG_ACTOR_COLS; ++c) {
        u16 scb = first_scb + c, src_col = col0 + c;
        *REG_VRAMMOD = 1; *REG_VRAMADDR = ADDR_SCB1 + scb * 64;
        for (r = 0; r < GNG_ACTOR_ROWS; ++r) {
            u16 idx = (u16)(r * sheet_cols + src_col), attr = (u16)(palmap[idx] << 8);
            if (flip_x) attr |= 0x0001;
            *REG_VRAMRW = (u16)(tile_base + idx); *REG_VRAMRW = attr;
        }
        if (!flip_x) position_strip(scb, GNG_ACTOR_ROWS, (s16)(x + c * 16), y);
        else position_strip(scb, GNG_ACTOR_ROWS, (s16)(x + (GNG_ACTOR_COLS - 1 - c) * 16), y);
    }
}

static void draw_ball_slot(u8 n) {
    u16 scb = SCB_BALL0 + n, pal = GNG_BALL_PALMAP[0];
    Ball *b = &balls[n];
    if (!b->active) { hide_strip(scb); return; }
    *REG_VRAMMOD = 1; *REG_VRAMADDR = ADDR_SCB1 + scb * 64;
    *REG_VRAMRW = GNG_BALL_TILE; *REG_VRAMRW = (u16)(pal << 8);
    position_strip(scb, 1, px((s16)(b->x - camera_x)), b->y);
}

static s16 layer_scroll(s16 sx, s16 logical_width, s16 num, s16 den) {
    s16 max_scroll = (s16)(logical_width - GNG_LOGICAL_W);
    s32 v;
    if (max_scroll <= 0) return 0;
    v = ((s32)sx * num) / den;
    if (v < 0) v = 0;
    if (v > max_scroll) v = max_scroll;
    return (s16)v;
}

static u8 transition_surge(s16 sx, s16 start, s16 end) {
    s16 mid, half, d;
    if (sx <= start || sx >= end) return 0;
    mid = (s16)((start + end) / 2);
    half = (s16)((end - start) / 2);
    d = iabs16((s16)(sx - mid));
    return (u8)(255 - ((s32)d * 255) / half);
}

static u8 outside_stage_kind(s16 sx) {
    s32 c;
    if (sx <= STAGE_BLEND_START) return STAGE_RETRO;
    if (sx >= STAGE_BLEND_END) return STAGE_INGRESSO;
    c = ((s32)(sx - STAGE_BLEND_START) * 255) / (STAGE_BLEND_END - STAGE_BLEND_START);
    if (c < 51) return STAGE_RETRO;
    if (c < 103) return STAGE_BLEND25;
    if (c < 154) return STAGE_BLEND50;
    if (c < 205) return STAGE_BLEND75;
    return STAGE_INGRESSO;
}

static void draw_stage_variant(u8 stage, s16 x) {
    if (stage == STAGE_RETRO)
        draw_layer_window(SCB_STAGE, GNG_RETRO_TILE, GNG_RETRO_COLS, GNG_RETRO_ROWS, GNG_RETRO_PALMAP, x, 0);
    else if (stage == STAGE_BLEND25)
        draw_layer_window(SCB_STAGE, GNG_BLEND25_TILE, GNG_BLEND25_COLS, GNG_BLEND25_ROWS, GNG_BLEND25_PALMAP, x, 0);
    else if (stage == STAGE_BLEND50)
        draw_layer_window(SCB_STAGE, GNG_BLEND50_TILE, GNG_BLEND50_COLS, GNG_BLEND50_ROWS, GNG_BLEND50_PALMAP, x, 0);
    else if (stage == STAGE_BLEND75)
        draw_layer_window(SCB_STAGE, GNG_BLEND75_TILE, GNG_BLEND75_COLS, GNG_BLEND75_ROWS, GNG_BLEND75_PALMAP, x, 0);
    else if (stage == STAGE_INGRESSO)
        draw_layer_window(SCB_STAGE, GNG_INGRESSO_TILE, GNG_INGRESSO_COLS, GNG_INGRESSO_ROWS, GNG_INGRESSO_PALMAP, x, 0);
    else
        draw_layer_window(SCB_STAGE, GNG_INSIDE_TILE, GNG_INSIDE_COLS, GNG_INSIDE_ROWS, GNG_INSIDE_PALMAP, x, 0);
}

static void draw_fog(u8 surge) {
    s16 y = (s16)(150 - ((s32)surge * 40) / 255);
    s16 scroll = (s16)(camera_x / 2 + (frame_counter / 5));
    draw_repeating_layer(SCB_FOG, GNG_FOG_TILE, GNG_FOG_COLS, GNG_FOG_ROWS,
                         GNG_FOG_PALMAP, scroll, y);
}

static void draw_scenery(void) {
    s16 sx = camera_x;
    s16 sky_scroll = 0, city_scroll, stadium_x, min_stage_x, d, inside_scroll;
    u8 stage, surge_a, surge_b, surge;

    /* Same web-space camera coordinates: no compressed mapping across the level. */
    if (GNG_SKY_LOGICAL_W > GNG_LOGICAL_W)
        sky_scroll = (s16)(((s32)sx * (GNG_SKY_LOGICAL_W - GNG_LOGICAL_W)) / LEVEL1_MAX_SCROLL);
    city_scroll = layer_scroll(sx, GNG_CITY_LOGICAL_W, 15, 100);

    draw_layer_window(SCB_SKY, GNG_SKY_TILE, GNG_SKY_COLS, GNG_SKY_ROWS,
                      GNG_SKY_PALMAP, (s16)-px(sky_scroll), 0);
    draw_layer_window(SCB_CITY, GNG_CITY_TILE, GNG_CITY_COLS, GNG_CITY_ROWS,
                      GNG_CITY_PALMAP, (s16)-px(city_scroll), 0);

    if (sx > INSIDE_BEGIN + 56) {
        stage = STAGE_INSIDE;
        d = (s16)(sx - INSIDE_BEGIN);
        inside_scroll = layer_scroll(d, GNG_INSIDE_LOGICAL_W, 295, 1000);
        load_stage_palettes(stage);
        draw_stage_variant(stage, (s16)-px(inside_scroll));
        draw_repeating_layer(SCB_PITCH, GNG_PITCH_TILE, GNG_PITCH_COLS, GNG_PITCH_ROWS,
                             GNG_PITCH_PALMAP, (s16)(((s32)sx * 70) / 100), 0);
    } else {
        stage = outside_stage_kind(sx);
        load_stage_palettes(stage);
        stadium_x = (s16)(GNG_LOGICAL_W - ((s32)(sx - STADIUM_FROM) * 35) / 100);
        min_stage_x = (s16)(GNG_LOGICAL_W - GNG_RETRO_LOGICAL_W);
        stadium_x = clamp16(stadium_x, min_stage_x, GNG_LOGICAL_W);
        draw_stage_variant(stage, px(stadium_x));
        hide_range(SCB_PITCH, VISIBLE_STRIPS);
    }

    draw_repeated_ground();

    surge_a = transition_surge(sx, STAGE_BLEND_START, STAGE_BLEND_END);
    surge_b = transition_surge(sx, (s16)(INSIDE_BEGIN - 30), (s16)(INSIDE_BEGIN + 120));
    surge = surge_a > surge_b ? surge_a : surge_b;
    /* Low-density fog is always present; it swells during scene transitions. */
    if (surge < 72) surge = 72;
    draw_fog(surge);
}

static void draw_foreground(void) {
    s16 scroll = (s16)(((s32)camera_x * 135) / 100);
    if (camera_x > INSIDE_BEGIN + 40)
        draw_repeating_layer(SCB_FOREGROUND, GNG_FG_PITCH_TILE, GNG_FG_PITCH_COLS, GNG_FG_PITCH_ROWS,
                             GNG_FG_PITCH_PALMAP, scroll, 0);
    else
        draw_repeating_layer(SCB_FOREGROUND, GNG_FG_CEMETERY_TILE, GNG_FG_CEMETERY_COLS, GNG_FG_CEMETERY_ROWS,
                             GNG_FG_CEMETERY_PALMAP, scroll, 0);
}

static void init_screen(void) {
    ng_cls(); set_backdrop(0x8000); load_static_palettes(); stage_kind = 255; load_stage_palettes(STAGE_RETRO);
}

static void demo_mode(void) {
    ng_cls(); set_backdrop(0x0088);
    ng_center_text_tall(8, 0, "GHOSTS N GOALS");
    ng_center_text(12, 0, "NEO GEO MVS");
    ng_center_text(17, 0, "PARALLAX BUILD");
    ng_center_text(22, 0, bios_mvs_flag == SYSTEM_MVS ? "INSERT COIN" : "PRESS START");
    { u16 frames = 60 * 15; while (frames-- && bios_user_mode != USER_MODE_GAME) ng_wait_vblank(); }
}

static void title_mode(void) {
    ng_cls(); set_backdrop(0x0800);
    ng_center_text_tall(8, 0, "GHOSTS N GOALS");
    ng_center_text(16, 0, "LEVEL 1 - MANUZZI");
    ng_center_text(22, 0, "PRESS START");
    while (bios_user_mode != USER_MODE_GAME) {
        sprintf(str, "TIMER %02x", bios_compulsion_timer);
        ng_center_text(24, 0, str); ng_wait_vblank();
    }
}

static void update_camera(void) {
    s16 desired = (s16)(player_x - (GNG_LOGICAL_W / 2) + (player_facing_right ? 34 : -34));
    s32 target;
    desired = clamp16(desired, 0, CAMERA_MAX);
    target = ((s32)desired << FP_SHIFT);
    /* Phaser's 60 Hz follow smoothing is about 0.042/frame. */
    camera_x_fp += (target - camera_x_fp) / 24;
    camera_x = (s16)(camera_x_fp >> FP_SHIFT);
}

static void spawn_ball(u8 up, u8 down_air) {
    u8 i;
    for (i = 0; i < MAX_BALLS; ++i) if (!balls[i].active) {
        Ball *b = &balls[i];
        b->active = 1; b->life = BALL_LIFE; b->bounces = 0;
        b->x = (s16)(player_x + (player_facing_right ? 15 : -15));
        b->y = (s16)(player_y + (up ? 10 : down_air ? 34 : 25));
        if (up) { b->vx = player_facing_right ? 3 : -3; b->vy = -3; }
        else if (down_air) { b->vx = player_facing_right ? 3 : -3; b->vy = 3; }
        else { b->vx = player_facing_right ? 4 : -4; b->vy = 0; }
        kick_timer = 9;
        return;
    }
}

static void update_player(u8 pad) {
    u8 left = !!(pad & CNT_LEFT), right = !!(pad & CNT_RIGHT), up = !!(pad & CNT_UP), down = !!(pad & CNT_DOWN);
    u8 jump_edge = !!((pad & CNT_A) && !(prev_pad & CNT_A));
    u8 shoot_edge = !!((pad & CNT_B) && !(prev_pad & CNT_B));
    s32 target_vx = 0;
    s32 accel;
    s32 old_x_fp = player_x_fp;

    if (left) { target_vx = -RUN_SPEED_FP; player_facing_right = 0; }
    if (right) { target_vx = RUN_SPEED_FP; player_facing_right = 1; }
    accel = player_ground ? GROUND_ACCEL_FP : AIR_ACCEL_FP;
    player_vx_fp = approach32(player_vx_fp, target_vx, accel);
    player_x_fp += player_vx_fp;

    if (player_x_fp < (20L << FP_SHIFT)) player_x_fp = (20L << FP_SHIFT);
    if (player_x_fp > ((s32)(LEVEL1_WORLD_W - 20) << FP_SHIFT)) player_x_fp = ((s32)(LEVEL1_WORLD_W - 20) << FP_SHIFT);
    sync_player_xy();

    if (zombie_state == 0 && iabs16(player_x - zombie_x) < 28 && player_y > 116) {
        player_x_fp = old_x_fp;
        player_vx_fp = 0;
        sync_player_xy();
    }

    if (jump_edge && player_ground) {
        player_vy_fp = JUMP_VEL_FP;
        player_ground = 0;
    }

    if (!player_ground) {
        if (!(pad & CNT_A) && player_vy_fp < RELEASE_CUT_FP) player_vy_fp /= 2;
        player_vy_fp += GRAVITY_FP;
        if (player_vy_fp > MAX_FALL_FP) player_vy_fp = MAX_FALL_FP;
        player_y_fp += player_vy_fp;
        if (player_y_fp >= ((s32)PLAYER_GROUND_Y << FP_SHIFT)) {
            player_y_fp = ((s32)PLAYER_GROUND_Y << FP_SHIFT);
            player_vy_fp = 0;
            player_ground = 1;
        }
        sync_player_xy();
    }

    if (shoot_edge) spawn_ball(up, down && !player_ground);
}

static void update_balls(void) {
    u8 i;
    for (i = 0; i < MAX_BALLS; ++i) {
        Ball *b = &balls[i];
        if (!b->active) continue;
        b->x += b->vx; b->y += b->vy;
        if (b->life) b->life--; else { b->active = 0; continue; }
        if (b->y >= GROUND_Y - 7) {
            if (b->bounces == 0) {
                b->y = GROUND_Y - 8;
                b->vy = (s16)(-b->vy);
                if (b->vy == 0) b->vy = -2;
                b->bounces = 1;
            } else { b->active = 0; continue; }
        }
        if (b->x < camera_x - 24 || b->x > camera_x + GNG_LOGICAL_W + 24) { b->active = 0; continue; }
        if (zombie_state == 0 && iabs16(b->x - zombie_x) < 24 && iabs16(b->y - (zombie_y + 30)) < 30) {
            zombie_state = 1; zombie_die_timer = 28; b->active = 0;
        }
    }
}

static void update_zombie(void) {
    if (zombie_state != 0) {
        if (zombie_die_timer) zombie_die_timer--; else zombie_state = 2;
        return;
    }
    if ((frame_counter & 1) == 0) {
        if (zombie_x > player_x + 40) zombie_x--;
        else if (zombie_x < player_x - 40) zombie_x++;
    }
}

static u8 player_frame_for(u8 moving) {
    if (kick_timer) return GNG_PLAYER_FRAME_KICK;
    if (!player_ground) return player_vy_fp < 0 ? GNG_PLAYER_FRAME_JUMP : GNG_PLAYER_FRAME_FALL;
    if (moving) return (u8)(GNG_PLAYER_FRAME_RUN0 + ((frame_counter >> 2) & 3));
    return (u8)(GNG_PLAYER_FRAME_IDLE0 + ((frame_counter >> 5) & 1));
}

static void draw_characters(u8 pf) {
    u8 i;
    draw_actor(SCB_PLAYER, GNG_PLAYER_TILE, GNG_PLAYER_COLS, GNG_PLAYER_PALMAP, pf,
               (s16)(player_x - camera_x - 32), player_y, player_facing_right ? 0 : 1);

    if (zombie_state == 2) hide_range(SCB_ZOMBIE, GNG_ACTOR_COLS);
    else {
        u8 zf = zombie_state == 1
            ? (zombie_die_timer > 14 ? GNG_ZOMBIE_FRAME_DIE0 : GNG_ZOMBIE_FRAME_DIE1)
            : (u8)(GNG_ZOMBIE_FRAME_WALK0 + ((frame_counter >> 4) & 3));
        draw_actor(SCB_ZOMBIE, GNG_ZOMBIE_TILE, GNG_ZOMBIE_COLS, GNG_ZOMBIE_PALMAP, zf,
                   (s16)(zombie_x - camera_x - 32), zombie_y, zombie_x > player_x ? 1 : 0);
    }
    for (i = 0; i < MAX_BALLS; ++i) draw_ball_slot(i);
}

static void game_mode(void) {
    u8 i;
    ng_cls(); set_backdrop(0x8000); load_static_palettes(); stage_kind = 255; load_stage_palettes(STAGE_RETRO);

    player_x_fp = (48L << FP_SHIFT);
    player_y_fp = ((s32)PLAYER_GROUND_Y << FP_SHIFT);
    player_vx_fp = 0; player_vy_fp = 0;
    player_ground = 1; player_facing_right = 1; kick_timer = 0;
    sync_player_xy();

    zombie_x = 240; zombie_y = 144; zombie_state = 0; zombie_die_timer = 0;
    camera_x_fp = 0; camera_x = 0; prev_pad = 0; frame_counter = 0;
    for (i = 0; i < MAX_BALLS; ++i) balls[i].active = 0;

    for (;;) {
        u8 pad = bios_p1current;
        u8 moving = !!(pad & (CNT_LEFT | CNT_RIGHT));
        update_player(pad);
        update_balls();
        update_zombie();
        update_camera();
        draw_scenery();
        draw_characters(player_frame_for(moving));
        draw_foreground();
        if (kick_timer) kick_timer--;
        prev_pad = pad;
        frame_counter++;
        ng_wait_vblank();
    }
}

void player_start(void) { bios_user_mode = USER_MODE_GAME; bios_player_mod1 = 1; }
void coin_sound(void) {}

int main(void) { init_screen(); demo_mode(); if (bios_user_mode == USER_MODE_GAME) game_mode(); return 0; }
int main_mvs_title(void) { init_screen(); title_mode(); game_mode(); return 0; }
