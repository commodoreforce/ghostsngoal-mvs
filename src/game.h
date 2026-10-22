#ifndef GNG_GAME_H
#define GNG_GAME_H

#include <stdint.h>

#define GNG_MAX_ENEMIES 12
#define GNG_MAX_PROJECTILES 8
#define GNG_LEVEL_COUNT 2

typedef enum {
    GNG_STATE_BOOT = 0,
    GNG_STATE_ATTRACT,
    GNG_STATE_TITLE,
    GNG_STATE_PLAY,
    GNG_STATE_BOSS,
    GNG_STATE_CLEAR,
    GNG_STATE_GAMEOVER,
    GNG_STATE_HIGHSCORE
} GngState;

typedef enum {
    GNG_ENEMY_NONE = 0,
    GNG_ENEMY_ZOMBIE,
    GNG_ENEMY_BAT,
    GNG_ENEMY_PUMPKIN,
    GNG_ENEMY_MAZAPEGUL,
    GNG_ENEMY_AZDORA
} GngEnemyType;

typedef struct { int16_t x,y,vx,vy; uint8_t alive; } GngBall;
typedef struct { int16_t x,y,vx,vy; uint8_t on_ground,invuln,lives,facing; } GngPlayer;
typedef struct { GngEnemyType type; int16_t x,y,vx,vy,hp,timer; uint8_t phase,alive; } GngEnemy;
typedef struct { uint8_t left,right,up,down,jump,kick,start,coin; } GngInput;

typedef struct {
    GngState state;
    uint32_t frame,score,high_score;
    uint16_t state_timer;
    uint8_t credits,level,continue_count,difficulty;
    GngPlayer player;
    GngBall ball;
    GngEnemy enemies[GNG_MAX_ENEMIES];
} GngGame;

void gng_init(GngGame *g);
void gng_reset_run(GngGame *g);
void gng_step(GngGame *g, GngInput in);

#endif
