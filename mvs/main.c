#include <ngdevkit/neogeo.h>
#include <ngdevkit/bios-ram.h>
#include <ngdevkit/ng-fix.h>
#include <ngdevkit/ng-video.h>
#include <stdio.h>

#define USER_MODE_BOOT 0x0
#define USER_MODE_DEMO 0x1
#define USER_MODE_GAME 0x2
#define SYSTEM_MVS 0x80

static char str[32];

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
    ng_center_text(11, 0, "MVS DIAGNOSTIC BUILD");
    ng_center_text(16, 0, "DEMO MODE");
    ng_center_text(22, 0, bios_mvs_flag == SYSTEM_MVS ? "INSERT COIN" : "PRESS START");

    u16 frames = 60 * 15;
    while (frames-- && bios_user_mode != USER_MODE_GAME) {
        ng_wait_vblank();
    }
}

static void title_mode(void) {
    set_backdrop(0x0800);
    ng_center_text_tall(8, 0, "GHOSTS N GOALS");
    ng_center_text(16, 0, "TITLE MODE");
    ng_center_text(22, 0, "PRESS START");

    while (bios_user_mode != USER_MODE_GAME) {
        sprintf(str, "TIMER %02x", bios_compulsion_timer);
        ng_center_text(24, 0, str);
        ng_wait_vblank();
    }
}

static void game_mode(void) {
    ng_cls();
    set_backdrop(0x8008);
    ng_center_text_tall(8, 0, "GAME RUNNING");
    ng_center_text(14, 0, "MVS BOOT OK");
    ng_center_text(18, 0, "68000 CODE OK");
    ng_center_text(22, 0, "PRESS A FOR GAME OVER");

    while ((bios_p1current & CNT_A) == 0) {
        ng_wait_vblank();
    }

    bios_player_mod1 = 0;
    ng_cls();
    set_backdrop(0x0a00);
    ng_center_text_tall(13, 0, "GAME OVER");
    for (u16 wait = 180; wait; --wait) {
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
