#include "game.h"
#include <string.h>

#define SCREEN_W 320
#define GROUND_Y 192
#define PLAYER_H 42
#define BALL_W 12
#define GRAVITY 1

static int16_t clamp16(int16_t v,int16_t lo,int16_t hi){return v<lo?lo:(v>hi?hi:v);} 
static int16_t iabs16(int16_t v){return v<0?-v:v;}

static void clear_enemies(GngGame *g){memset(g->enemies,0,sizeof(g->enemies));}
static void spawn_enemy(GngGame *g,int slot,GngEnemyType type,int x,int y,int hp){
    if(slot<0||slot>=GNG_MAX_ENEMIES)return;
    g->enemies[slot].type=type; g->enemies[slot].x=x; g->enemies[slot].y=y;
    g->enemies[slot].hp=hp; g->enemies[slot].alive=1;
}
static void setup_level(GngGame *g){
    clear_enemies(g);
    g->player.x=36; g->player.y=GROUND_Y-PLAYER_H; g->player.vx=g->player.vy=0;
    g->player.on_ground=1; g->player.facing=1;
    g->ball.x=112; g->ball.y=GROUND_Y-BALL_W; g->ball.vx=g->ball.vy=0; g->ball.alive=1;
    if(g->level==0){
        spawn_enemy(g,0,GNG_ENEMY_ZOMBIE,185,GROUND_Y-38,1);
        spawn_enemy(g,1,GNG_ENEMY_ZOMBIE,250,GROUND_Y-38,1);
        spawn_enemy(g,2,GNG_ENEMY_PUMPKIN,292,GROUND_Y-24,1);
    } else {
        spawn_enemy(g,0,GNG_ENEMY_BAT,180,92,1);
        spawn_enemy(g,1,GNG_ENEMY_ZOMBIE,225,GROUND_Y-38,1);
        spawn_enemy(g,2,GNG_ENEMY_BAT,280,120,1);
    }
}
static void setup_boss(GngGame *g){
    clear_enemies(g);
    g->player.x=42; g->player.y=GROUND_Y-PLAYER_H;
    g->ball.x=118; g->ball.y=GROUND_Y-BALL_W; g->ball.vx=g->ball.vy=0;
    if(g->level==0) spawn_enemy(g,0,GNG_ENEMY_MAZAPEGUL,244,GROUND_Y-48,8);
    else spawn_enemy(g,0,GNG_ENEMY_AZDORA,244,GROUND_Y-52,10);
}
static void lose_life(GngGame *g){
    if(g->player.invuln)return;
    if(g->player.lives)g->player.lives--;
    if(!g->player.lives){g->state=GNG_STATE_GAMEOVER;g->state_timer=240;return;}
    g->player.x=36; g->player.y=GROUND_Y-PLAYER_H; g->player.vx=g->player.vy=0;
    g->player.invuln=120; g->ball.x=108; g->ball.y=GROUND_Y-BALL_W; g->ball.vx=g->ball.vy=0;
}
static void kill_enemy(GngGame *g,GngEnemy *e){
    if(!e->alive)return; e->alive=0;
    switch(e->type){
        case GNG_ENEMY_ZOMBIE:g->score+=100;break;
        case GNG_ENEMY_BAT:g->score+=150;break;
        case GNG_ENEMY_PUMPKIN:g->score+=200;break;
        case GNG_ENEMY_MAZAPEGUL:
        case GNG_ENEMY_AZDORA:g->score+=5000;break;
        default:break;
    }
}
static int remaining_regular(const GngGame *g){
    int n=0; for(int i=0;i<GNG_MAX_ENEMIES;i++) if(g->enemies[i].alive&&g->enemies[i].type!=GNG_ENEMY_MAZAPEGUL&&g->enemies[i].type!=GNG_ENEMY_AZDORA)n++; return n;
}
static void step_player(GngGame *g,GngInput in){
    GngPlayer *p=&g->player; p->vx=0;
    if(in.left){p->vx=-2;p->facing=0;} if(in.right){p->vx=2;p->facing=1;}
    p->x=clamp16((int16_t)(p->x+p->vx),8,294);
    if(in.jump&&p->on_ground){p->vy=-8;p->on_ground=0;}
    if(!p->on_ground){p->vy+=GRAVITY;if(p->vy>8)p->vy=8;p->y+=p->vy;if(p->y>=GROUND_Y-PLAYER_H){p->y=GROUND_Y-PLAYER_H;p->vy=0;p->on_ground=1;}}
    if(p->invuln)p->invuln--;
    if(in.kick&&iabs16((int16_t)(g->ball.x-p->x))<34&&iabs16((int16_t)(g->ball.y-p->y))<44){g->ball.vx=p->facing?7:-7;g->ball.vy=-5;}
}
static void step_ball(GngGame *g){
    GngBall *b=&g->ball; b->vy+=GRAVITY;if(b->vy>10)b->vy=10;b->x+=b->vx;b->y+=b->vy;
    if(b->y>=GROUND_Y-BALL_W){b->y=GROUND_Y-BALL_W;b->vy=iabs16(b->vy)>2?(int16_t)(-(b->vy/2)):0;b->vx=(int16_t)((b->vx*7)/8);} 
    if(b->x<4){b->x=4;b->vx=-b->vx;} if(b->x>304){b->x=304;b->vx=-b->vx;}
}
static void step_enemy(GngGame *g,GngEnemy *e){
    e->timer++;
    if(e->type==GNG_ENEMY_ZOMBIE){e->vx=g->player.x<e->x?-1:1;e->x+=e->vx;}
    else if(e->type==GNG_ENEMY_BAT){e->vx=g->player.x<e->x?-1:1;e->x+=e->vx;if((e->timer%48)<24)e->y--;else e->y++;e->y=clamp16(e->y,70,145);} 
    else if(e->type==GNG_ENEMY_PUMPKIN){if((e->timer%90)==0)e->vx=g->player.x<e->x?-3:3;e->x+=e->vx;e->vx=(int16_t)((e->vx*7)/8);} 
    if(iabs16((int16_t)(g->ball.x-e->x))<18&&iabs16((int16_t)(g->ball.y-e->y))<40&&(iabs16(g->ball.vx)>2||iabs16(g->ball.vy)>2)){kill_enemy(g,e);g->ball.vx=-g->ball.vx;g->ball.vy=-4;}
    if(e->alive&&iabs16((int16_t)(g->player.x-e->x))<18&&iabs16((int16_t)(g->player.y-e->y))<38)lose_life(g);
}
static void step_boss(GngGame *g,GngEnemy *e){
    e->timer++;
    if(e->type==GNG_ENEMY_MAZAPEGUL){
        if(e->phase==0){e->x+=g->player.x<e->x?-1:1;if(e->timer>90){e->phase=1;e->timer=0;e->vy=-7;}}
        else if(e->phase==1){e->vy++;e->y+=e->vy;if(e->y>=GROUND_Y-48){e->y=GROUND_Y-48;e->vy=0;e->phase=2;e->timer=0;}}
        else if(e->timer>45){e->phase=0;e->timer=0;}
    } else {
        if(e->phase==0){e->x+=g->player.x<e->x?-1:1;if(e->timer>75){e->phase=1;e->timer=0;}}
        else if(e->phase==1){e->vx=g->player.x<e->x?-4:4;e->x+=e->vx;if(e->timer>30||e->x<18||e->x>286){e->phase=2;e->timer=0;e->vx=0;}}
        else if(e->timer>60){e->phase=0;e->timer=0;}
    }
    e->x=clamp16(e->x,16,286);
    if(iabs16((int16_t)(g->ball.x-e->x))<24&&iabs16((int16_t)(g->ball.y-e->y))<50&&(iabs16(g->ball.vx)>3||iabs16(g->ball.vy)>3)){
        e->hp--;g->score+=250;g->ball.vx=-g->ball.vx;g->ball.vy=-5;
        if(e->hp<=0){kill_enemy(g,e);g->state=GNG_STATE_CLEAR;g->state_timer=180;}
    }
    if(e->alive&&iabs16((int16_t)(g->player.x-e->x))<24&&iabs16((int16_t)(g->player.y-e->y))<44)lose_life(g);
}
void gng_init(GngGame *g){memset(g,0,sizeof(*g));g->state=GNG_STATE_BOOT;g->high_score=5000;g->difficulty=1;g->state_timer=120;}
void gng_reset_run(GngGame *g){g->score=0;g->level=0;g->continue_count=0;g->player.lives=3;setup_level(g);} 
void gng_step(GngGame *g,GngInput in){
    g->frame++; if(in.coin&&g->credits<9)g->credits++;
    switch(g->state){
        case GNG_STATE_BOOT: if(g->state_timer)g->state_timer--;else{g->state=GNG_STATE_ATTRACT;g->state_timer=360;}break;
        case GNG_STATE_ATTRACT:
        case GNG_STATE_TITLE:
            if(in.start&&g->credits){g->credits--;gng_reset_run(g);g->state=GNG_STATE_PLAY;}
            else if(g->state_timer)g->state_timer--;else{g->state=g->state==GNG_STATE_ATTRACT?GNG_STATE_TITLE:GNG_STATE_ATTRACT;g->state_timer=360;}break;
        case GNG_STATE_PLAY:
            step_player(g,in);step_ball(g);for(int i=0;i<GNG_MAX_ENEMIES;i++)if(g->enemies[i].alive)step_enemy(g,&g->enemies[i]);
            if(remaining_regular(g)==0){g->state=GNG_STATE_BOSS;setup_boss(g);}break;
        case GNG_STATE_BOSS:
            step_player(g,in);step_ball(g);for(int i=0;i<GNG_MAX_ENEMIES;i++)if(g->enemies[i].alive)step_boss(g,&g->enemies[i]);break;
        case GNG_STATE_CLEAR:
            if(g->state_timer)g->state_timer--;else if(g->level+1<GNG_LEVEL_COUNT){g->level++;setup_level(g);g->state=GNG_STATE_PLAY;}else{g->state=GNG_STATE_HIGHSCORE;g->state_timer=360;}break;
        case GNG_STATE_GAMEOVER: if(g->state_timer)g->state_timer--;else{g->state=GNG_STATE_HIGHSCORE;g->state_timer=360;}break;
        case GNG_STATE_HIGHSCORE: if(g->state_timer)g->state_timer--;else{g->state=GNG_STATE_ATTRACT;g->state_timer=360;}break;
        default:break;
    }
    if(g->score>g->high_score)g->high_score=g->score;
}
