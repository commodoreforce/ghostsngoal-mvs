#include "game.h"
#include <string.h>

#define SCREEN_W 320
#define GROUND_Y 192
#define PLAYER_H 42
#define BALL_W 12
#define GRAVITY 1

static int16_t clamp16(int16_t v, int16_t lo, int16_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int16_t iabs16(int16_t v) {
    return v < 0 ? -v : v;
}

static int is_boss_type(GngEnemyType type) {
    return type == GNG_ENEMY_MAZAPEGUL ||
           type == GNG_ENEMY_AZDORA ||
           type == GNG_ENEMY_OCTOPUS ||
           type == GNG_ENEMY_COUNT;
}

static void clear_enemies(GngGame *g) {
    memset(g->enemies, 0, sizeof(g->enemies));
}

static void spawn_enemy(GngGame *g, int slot, GngEnemyType type, int x, int y, int hp) {
    if (slot < 0 || slot >= GNG_MAX_ENEMIES) return;
    g->enemies[slot].type = type;
    g->enemies[slot].x = (int16_t)x;
    g->enemies[slot].y = (int16_t)y;
    g->enemies[slot].hp = (int16_t)hp;
    g->enemies[slot].alive = 1;
}

static void setup_level(GngGame *g) {
    clear_enemies(g);
    g->player.x = 36;
    g->player.y = GROUND_Y - PLAYER_H;
    g->player.vx = 0;
    g->player.vy = 0;
    g->player.on_ground = 1;
    g->player.facing = 1;
    g->player.character_id = g->selected_character;
    g->ball.x = 112;
    g->ball.y = GROUND_Y - BALL_W;
    g->ball.vx = 0;
    g->ball.vy = 0;
    g->ball.alive = 1;

    /* These are only hardware-slice starter groups. Full stage spawning comes
       from the wave tables in levels.c as camera scrolling is connected. */
    if (g->level == 0) {
        spawn_enemy(g, 0, GNG_ENEMY_ZOMBIE, 185, GROUND_Y - 38, 1);
        spawn_enemy(g, 1, GNG_ENEMY_BAT, 250, 100, 1);
        spawn_enemy(g, 2, GNG_ENEMY_PUMPKIN, 292, GROUND_Y - 24, 1);
    } else if (g->level == 1) {
        spawn_enemy(g, 0, GNG_ENEMY_WASHER, 180, GROUND_Y - 38, 1);
        spawn_enemy(g, 1, GNG_ENEMY_BAT, 230, 95, 1);
        spawn_enemy(g, 2, GNG_ENEMY_WOLF, 285, GROUND_Y - 38, 2);
    } else {
        spawn_enemy(g, 0, GNG_ENEMY_CRAB, 175, GROUND_Y - 22, 1);
        spawn_enemy(g, 1, GNG_ENEMY_GULL, 225, 90, 1);
        spawn_enemy(g, 2, GNG_ENEMY_SAILOR, 282, GROUND_Y - 40, 2);
    }
}

static void setup_boss(GngGame *g) {
    clear_enemies(g);
    g->player.x = 42;
    g->player.y = GROUND_Y - PLAYER_H;
    g->ball.x = 118;
    g->ball.y = GROUND_Y - BALL_W;
    g->ball.vx = 0;
    g->ball.vy = 0;

    if (g->level == 0) {
        spawn_enemy(g, 0, GNG_ENEMY_MAZAPEGUL, 244, GROUND_Y - 48, 8);
    } else if (g->level == 1) {
        spawn_enemy(g, 0, GNG_ENEMY_AZDORA, 244, GROUND_Y - 52, 10);
    } else {
        spawn_enemy(g, 0, GNG_ENEMY_COUNT, 244, GROUND_Y - 56, 12);
    }
}

static void lose_life(GngGame *g) {
    if (g->player.invuln) return;
    if (g->player.lives) g->player.lives--;

    if (!g->player.lives) {
        g->state = GNG_STATE_GAMEOVER;
        g->state_timer = 240;
        return;
    }

    g->player.x = 36;
    g->player.y = GROUND_Y - PLAYER_H;
    g->player.vx = 0;
    g->player.vy = 0;
    g->player.invuln = 120;
    g->ball.x = 108;
    g->ball.y = GROUND_Y - BALL_W;
    g->ball.vx = 0;
    g->ball.vy = 0;
}

static void kill_enemy(GngGame *g, GngEnemy *e) {
    if (!e->alive) return;
    e->alive = 0;

    switch (e->type) {
        case GNG_ENEMY_ZOMBIE:  g->score += 100; break;
        case GNG_ENEMY_BAT:     g->score += 150; break;
        case GNG_ENEMY_PUMPKIN: g->score += 200; break;
        case GNG_ENEMY_GHOST:   g->score += 200; break;
        case GNG_ENEMY_WASHER:  g->score += 250; break;
        case GNG_ENEMY_BOOK:    g->score += 200; break;
        case GNG_ENEMY_WOLF:    g->score += 300; break;
        case GNG_ENEMY_GULL:    g->score += 180; break;
        case GNG_ENEMY_CRAB:    g->score += 150; break;
        case GNG_ENEMY_SAILOR:  g->score += 300; break;
        case GNG_ENEMY_RAT:     g->score += 100; break;
        case GNG_ENEMY_OCTOPUS: g->score += 2500; break;
        case GNG_ENEMY_MAZAPEGUL:
        case GNG_ENEMY_AZDORA:
        case GNG_ENEMY_COUNT:   g->score += 5000; break;
        default: break;
    }
}

static int remaining_regular(const GngGame *g) {
    int n = 0;
    for (int i = 0; i < GNG_MAX_ENEMIES; i++) {
        if (g->enemies[i].alive && !is_boss_type(g->enemies[i].type)) n++;
    }
    return n;
}

static void step_player(GngGame *g, GngInput in) {
    GngPlayer *p = &g->player;
    p->vx = 0;

    if (in.left) {
        p->vx = -2;
        p->facing = 0;
    }
    if (in.right) {
        p->vx = 2;
        p->facing = 1;
    }
    p->x = clamp16((int16_t)(p->x + p->vx), 8, 294);

    if (in.jump && p->on_ground) {
        p->vy = -8;
        p->on_ground = 0;
    }
    if (!p->on_ground) {
        p->vy += GRAVITY;
        if (p->vy > 8) p->vy = 8;
        p->y += p->vy;
        if (p->y >= GROUND_Y - PLAYER_H) {
            p->y = GROUND_Y - PLAYER_H;
            p->vy = 0;
            p->on_ground = 1;
        }
    }
    if (p->invuln) p->invuln--;

    if (in.kick &&
        iabs16((int16_t)(g->ball.x - p->x)) < 34 &&
        iabs16((int16_t)(g->ball.y - p->y)) < 44) {
        g->ball.vx = p->facing ? 7 : -7;
        g->ball.vy = -5;
    }
}

static void step_ball(GngGame *g) {
    GngBall *b = &g->ball;
    b->vy += GRAVITY;
    if (b->vy > 10) b->vy = 10;
    b->x += b->vx;
    b->y += b->vy;

    if (b->y >= GROUND_Y - BALL_W) {
        b->y = GROUND_Y - BALL_W;
        b->vy = iabs16(b->vy) > 2 ? (int16_t)(-(b->vy / 2)) : 0;
        b->vx = (int16_t)((b->vx * 7) / 8);
    }
    if (b->x < 4) {
        b->x = 4;
        b->vx = (int16_t)-b->vx;
    }
    if (b->x > 304) {
        b->x = 304;
        b->vx = (int16_t)-b->vx;
    }
}

static void step_enemy(GngGame *g, GngEnemy *e) {
    e->timer++;

    switch (e->type) {
        case GNG_ENEMY_ZOMBIE:
        case GNG_ENEMY_WASHER:
        case GNG_ENEMY_SAILOR:
        case GNG_ENEMY_RAT:
            e->vx = g->player.x < e->x ? -1 : 1;
            e->x += e->vx;
            break;
        case GNG_ENEMY_BAT:
        case GNG_ENEMY_GULL:
        case GNG_ENEMY_GHOST:
            e->vx = g->player.x < e->x ? -1 : 1;
            e->x += e->vx;
            e->y += (e->timer % 48) < 24 ? -1 : 1;
            e->y = clamp16(e->y, 70, 145);
            break;
        case GNG_ENEMY_PUMPKIN:
        case GNG_ENEMY_CRAB:
            if ((e->timer % 90) == 0) e->vx = g->player.x < e->x ? -3 : 3;
            e->x += e->vx;
            e->vx = (int16_t)((e->vx * 7) / 8);
            break;
        case GNG_ENEMY_WOLF:
            e->vx = g->player.x < e->x ? -2 : 2;
            e->x += e->vx;
            break;
        case GNG_ENEMY_BOOK:
            e->vx = g->player.x < e->x ? -1 : 1;
            e->x += e->vx;
            e->y += (e->timer % 36) < 18 ? -1 : 1;
            break;
        default:
            break;
    }

    if (iabs16((int16_t)(g->ball.x - e->x)) < 18 &&
        iabs16((int16_t)(g->ball.y - e->y)) < 40 &&
        (iabs16(g->ball.vx) > 2 || iabs16(g->ball.vy) > 2)) {
        e->hp--;
        g->ball.vx = (int16_t)-g->ball.vx;
        g->ball.vy = -4;
        if (e->hp <= 0) kill_enemy(g, e);
    }

    if (e->alive &&
        iabs16((int16_t)(g->player.x - e->x)) < 18 &&
        iabs16((int16_t)(g->player.y - e->y)) < 38) {
        lose_life(g);
    }
}

static void step_boss(GngGame *g, GngEnemy *e) {
    e->timer++;

    if (e->type == GNG_ENEMY_MAZAPEGUL) {
        /* stalk -> leap -> recovery */
        if (e->phase == 0) {
            e->x += g->player.x < e->x ? -1 : 1;
            if (e->timer > 90) {
                e->phase = 1;
                e->timer = 0;
                e->vy = -7;
            }
        } else if (e->phase == 1) {
            e->vy++;
            e->y += e->vy;
            if (e->y >= GROUND_Y - 48) {
                e->y = GROUND_Y - 48;
                e->vy = 0;
                e->phase = 2;
                e->timer = 0;
            }
        } else if (e->timer > 45) {
            e->phase = 0;
            e->timer = 0;
        }
    } else if (e->type == GNG_ENEMY_AZDORA) {
        /* walk -> charge -> long recovery */
        if (e->phase == 0) {
            e->x += g->player.x < e->x ? -1 : 1;
            if (e->timer > 75) {
                e->phase = 1;
                e->timer = 0;
            }
        } else if (e->phase == 1) {
            e->vx = g->player.x < e->x ? -4 : 4;
            e->x += e->vx;
            if (e->timer > 30 || e->x < 18 || e->x > 286) {
                e->phase = 2;
                e->timer = 0;
                e->vx = 0;
            }
        } else if (e->timer > 60) {
            e->phase = 0;
            e->timer = 0;
        }
    } else if (e->type == GNG_ENEMY_COUNT) {
        /* hover -> dash -> vanish/reposition -> punish window */
        if (e->phase == 0) {
            e->y += (e->timer % 40) < 20 ? -1 : 1;
            if (e->timer > 80) {
                e->phase = 1;
                e->timer = 0;
                e->vx = g->player.x < e->x ? -5 : 5;
            }
        } else if (e->phase == 1) {
            e->x += e->vx;
            if (e->timer > 24 || e->x < 18 || e->x > 286) {
                e->phase = 2;
                e->timer = 0;
                e->vx = 0;
            }
        } else if (e->phase == 2) {
            if (e->timer == 30) {
                e->x = g->player.x < 160 ? 250 : 55;
                e->y = 82;
            }
            if (e->timer > 55) {
                e->phase = 3;
                e->timer = 0;
            }
        } else if (e->timer > 70) {
            e->phase = 0;
            e->timer = 0;
        }
    }

    e->x = clamp16(e->x, 16, 286);
    e->y = clamp16(e->y, 60, GROUND_Y - 40);

    if (iabs16((int16_t)(g->ball.x - e->x)) < 24 &&
        iabs16((int16_t)(g->ball.y - e->y)) < 50 &&
        (iabs16(g->ball.vx) > 3 || iabs16(g->ball.vy) > 3)) {
        e->hp--;
        g->score += 250;
        g->ball.vx = (int16_t)-g->ball.vx;
        g->ball.vy = -5;
        if (e->hp <= 0) {
            kill_enemy(g, e);
            g->state = GNG_STATE_CLEAR;
            g->state_timer = 180;
        }
    }

    if (e->alive &&
        iabs16((int16_t)(g->player.x - e->x)) < 24 &&
        iabs16((int16_t)(g->player.y - e->y)) < 44) {
        lose_life(g);
    }
}

void gng_init(GngGame *g) {
    memset(g, 0, sizeof(*g));
    g->state = GNG_STATE_BOOT;
    g->high_score = 5000;
    g->difficulty = 1;
    g->selected_character = GNG_CHAR_SHPENDI;
    g->state_timer = 120;
}

void gng_reset_run(GngGame *g) {
    g->score = 0;
    g->level = 0;
    g->continue_count = 0;
    g->player.lives = 3;
    setup_level(g);
}

void gng_step(GngGame *g, GngInput in) {
    g->frame++;
    if (in.coin && g->credits < 9) g->credits++;

    switch (g->state) {
        case GNG_STATE_BOOT:
            if (g->state_timer) {
                g->state_timer--;
            } else {
                g->state = GNG_STATE_ATTRACT;
                g->state_timer = 360;
            }
            break;

        case GNG_STATE_ATTRACT:
        case GNG_STATE_TITLE:
            if (in.start && g->credits) {
                g->credits--;
                gng_reset_run(g);
                g->state = GNG_STATE_PLAY;
            } else if (g->state_timer) {
                g->state_timer--;
            } else {
                g->state = g->state == GNG_STATE_ATTRACT ? GNG_STATE_TITLE : GNG_STATE_ATTRACT;
                g->state_timer = 360;
            }
            break;

        case GNG_STATE_PLAY:
            step_player(g, in);
            step_ball(g);
            for (int i = 0; i < GNG_MAX_ENEMIES; i++) {
                if (g->enemies[i].alive) step_enemy(g, &g->enemies[i]);
            }
            if (remaining_regular(g) == 0) {
                g->state = GNG_STATE_BOSS;
                setup_boss(g);
            }
            break;

        case GNG_STATE_BOSS:
            step_player(g, in);
            step_ball(g);
            for (int i = 0; i < GNG_MAX_ENEMIES; i++) {
                if (g->enemies[i].alive) step_boss(g, &g->enemies[i]);
            }
            break;

        case GNG_STATE_CLEAR:
            if (g->state_timer) {
                g->state_timer--;
            } else if (g->level + 1 < GNG_LEVEL_COUNT) {
                g->level++;
                setup_level(g);
                g->state = GNG_STATE_PLAY;
            } else {
                g->state = GNG_STATE_HIGHSCORE;
                g->state_timer = 360;
            }
            break;

        case GNG_STATE_GAMEOVER:
            if (g->state_timer) {
                g->state_timer--;
            } else {
                g->state = GNG_STATE_HIGHSCORE;
                g->state_timer = 360;
            }
            break;

        case GNG_STATE_HIGHSCORE:
            if (g->state_timer) {
                g->state_timer--;
            } else {
                g->state = GNG_STATE_ATTRACT;
                g->state_timer = 360;
            }
            break;

        case GNG_STATE_SELECT:
        case GNG_STATE_MIDBOSS:
        default:
            break;
    }

    if (g->score > g->high_score) g->high_score = g->score;
}
