#include <ngdevkit/neogeo.h>
#include <ngdevkit/bios-ram.h>
#include <ngdevkit/ng-fix.h>
#include <ngdevkit/ng-video.h>
#include <stdio.h>
#include "src/level1_assets.h"
#include "snd_commands.h"

#define USER_MODE_GAME 0x2
#define SYSTEM_MVS 0x80
#define VISIBLE_STRIPS 21
#define REG_SOUND ((volatile u8*)0x320000)

#define FP_SHIFT 8
#define FP_ONE (1L<<FP_SHIFT)
#define MAX_BALLS 3
#define MAX_ENEMIES 12
#define ENEMY_STRIDE 8
#define WORLD_END (GNG_LEVEL1_WORLD_W-24)
#define CAMERA_MAX (GNG_LEVEL1_WORLD_W-GNG_LOGICAL_W)
#define GROUND_Y 208
#define PLAYER_CELL_Y (GROUND_Y-GNG_PLAYER_CELL_H)

#define RUN_SPEED_FP 469
#define GROUND_ACCEL_FP 156
#define AIR_ACCEL_FP 92
#define JUMP_VEL_FP (-1293)
#define GRAVITY_FP 60
#define MAX_FALL_FP 2219
#define RELEASE_CUT_FP (-469)

#define SCB_SKY 0
#define SCB_MID 21
#define SCB_GROUND 42
#define SCB_FOREGROUND 63
#define SCB_PLAYER 84
#define SCB_ENEMY_BASE 92
#define SCB_BALL0 190

#define ET_NONE 0
#define ET_ZOMBIE 1
#define ET_BAT 2
#define ET_PUMPKIN 3
#define ET_GHOST 4
#define ET_MAZ 5
#define ES_ALIVE 0
#define ES_DYING 1
#define ES_DEAD 2
#define ARENA_X (242*16)
#define CHECKPOINT_X (121*16)

static volatile u8 start_requested;
static u8 prev_pad;
static u16 frame_counter;
static u32 score;
static u8 lives,invuln,stage_clear,boss_spawned,checkpoint_reached;
static u16 zombie_timer,music_timer,wave_mask;
static s32 player_x_fp,player_y_fp,player_vx_fp,player_vy_fp,camera_x_fp;
static s16 player_x,player_y,camera_x;
static u8 player_ground,player_facing_right,kick_timer;

typedef struct { s16 x,y,vx,vy; u8 life,bounces,active; } Ball;
typedef struct { u8 type,state,hp,anim,flags; s16 x,y,vx,vy,home_y; u16 timer; } Enemy;
typedef struct { u16 at; u8 type,count; } Wave;
static Ball balls[MAX_BALLS];
static Enemy enemies[MAX_ENEMIES];

static const Wave waves[]={{30*16,ET_BAT,1},{46*16,ET_PUMPKIN,1},{62*16,ET_BAT,2},{80*16,ET_PUMPKIN,2},{100*16,ET_BAT,2},{132*16,ET_GHOST,2},{150*16,ET_PUMPKIN,2},{162*16,ET_GHOST,2},{176*16,ET_BAT,2},{192*16,ET_GHOST,2},{204*16,ET_PUMPKIN,2},{216*16,ET_GHOST,2},{226*16,ET_BAT,2}};
#define WAVE_COUNT (sizeof(waves)/sizeof(waves[0]))
static const u16 ground_spans[][2]={{0,34},{36,58},{60,96},{98,152},{155,196},{199,262}};
#define GROUND_SPAN_COUNT 6

static s16 iabs16(s16 v){return v<0?-v:v;}
static s16 clamp16(s16 v,s16 lo,s16 hi){if(v<lo)return lo;if(v>hi)return hi;return v;}
static s32 approach32(s32 v,s32 t,s32 a){if(v<t){v+=a;if(v>t)v=t;}else if(v>t){v-=a;if(v<t)v=t;}return v;}
static s16 px_fp(s32 f){return (s16)((f*GNG_X_NUM)/(GNG_X_DEN*FP_ONE));}
static void set_backdrop(u16 c){*((volatile u16*)0x401ffe)=c;}
static void sound(u8 cmd){*REG_SOUND=cmd;}
static void load_palette(u8 slot,const unsigned short*colors){u16 b=(u16)slot*16,i;for(i=0;i<16;++i)MMAP_PALBANK1[b+i]=colors[i];}
static void load_set(u8 first,const unsigned short*p,u16 count){u16 i;for(i=0;i<count;++i)load_palette((u8)(first+i),p+i*16);}
static void init_palettes(void){const u16 fix[]={0x8000,0x0fff,0x0f80,0x0cc0,0x0666,0,0,0,0,0,0,0,0,0,0,0};load_palette(0,fix);
#define LP(N) load_set(GNG_##N##_PAL_FIRST,&GNG_##N##_PALETTES[0][0],GNG_##N##_PAL_COUNT)
LP(SKY);LP(CITY);LP(STADIUM_BACK);LP(STADIUM_FRONT);LP(INSIDE0);LP(INSIDE1);LP(GROUND);LP(FG_CEMETERY);LP(PLAYER);LP(ZOMBIE);LP(BAT);LP(PUMPKIN);LP(GHOST);LP(MAZ);LP(BALL);
#undef LP
}
static void position_strip(u16 scb,u8 rows,s16 x,s16 y){*REG_VRAMMOD=0x200;*REG_VRAMADDR=ADDR_SCB2+scb;*REG_VRAMRW=0x0fff;*REG_VRAMRW=(u16)((-y*128)+rows);*REG_VRAMRW=(u16)(x*128);}
static void hide_strip(u16 scb){position_strip(scb,1,500,240);}
static void hide_range(u16 first,u16 count){u16 i;for(i=0;i<count;++i)hide_strip(first+i);}
static void draw_layer(u16 first,u16 tile,u16 cols,u8 rows,const unsigned char*pm,s32 scroll_fp,s16 y){s32 ps=(scroll_fp*GNG_X_NUM)/(GNG_X_DEN*FP_ONE);u16 c,r,start;s16 bx;if(ps<0)ps=0;start=(u16)(ps>>4);bx=(s16)(-(ps&15));for(c=0;c<VISIBLE_STRIPS;++c){u16 scb=first+c,src=(u16)(start+c);s16 x=(s16)(bx+c*16);if(src>=cols){hide_strip(scb);continue;}*REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;for(r=0;r<rows;++r){u16 idx=(u16)(r*cols+src);*REG_VRAMRW=(u16)(tile+idx);*REG_VRAMRW=(u16)(pm[idx]<<8);}position_strip(scb,rows,x,y);}}
static void draw_positioned(u16 first,u16 tile,u16 cols,u8 rows,const unsigned char*pm,s16 lx,s16 y){s16 px=(s16)((lx*GNG_X_NUM)/GNG_X_DEN);u16 c,r;for(c=0;c<VISIBLE_STRIPS;++c){u16 scb=first+c;s16 x=(s16)(px+c*16);if(c>=cols||x<=-16||x>=320){hide_strip(scb);continue;}*REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;for(r=0;r<rows;++r){u16 idx=(u16)(r*cols+c);*REG_VRAMRW=(u16)(tile+idx);*REG_VRAMRW=(u16)(pm[idx]<<8);}position_strip(scb,rows,x,y);}}
static void draw_actor(u16 first,u16 tile,u16 cols,const unsigned char*pm,u8 frame,u8 fcols,u8 frows,s16 sx,s16 y,u8 flip){u16 col0=(u16)frame*fcols,c,r;for(c=0;c<fcols;++c){u16 scb=first+c,src=col0+c;*REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;for(r=0;r<frows;++r){u16 idx=(u16)(r*cols+src),attr=(u16)(pm[idx]<<8);if(flip)attr|=1;*REG_VRAMRW=(u16)(tile+idx);*REG_VRAMRW=attr;}position_strip(scb,frows,!flip?(s16)(sx+c*16):(s16)(sx+(fcols-1-c)*16),y);}for(c=fcols;c<ENEMY_STRIDE;++c)hide_strip(first+c);}
static u8 is_ground(s16 x){u8 i;for(i=0;i<GROUND_SPAN_COUNT;++i){s16 a=(s16)ground_spans[i][0]*16,b=(s16)ground_spans[i][1]*16;if(x>=a&&x<b)return 1;}return 0;}
static s16 ground_top_for_cell(u8 rows){return (s16)(GROUND_Y-rows*16);}

static void draw_background(void){s16 sx=camera_x;if(sx<720){draw_layer(SCB_SKY,GNG_SKY_TILE,GNG_SKY_COLS,GNG_SKY_ROWS,GNG_SKY_PALMAP,(camera_x_fp*3)/100,0);draw_layer(SCB_MID,GNG_CITY_TILE,GNG_CITY_COLS,GNG_CITY_ROWS,GNG_CITY_PALMAP,(camera_x_fp*15)/100,0);}else if(sx<1570){s16 lx=(s16)(256-((sx-694)*35)/100);draw_layer(SCB_SKY,GNG_SKY_TILE,GNG_SKY_COLS,GNG_SKY_ROWS,GNG_SKY_PALMAP,(camera_x_fp*3)/100,0);draw_positioned(SCB_MID,GNG_STADIUM_BACK_TILE,GNG_STADIUM_BACK_COLS,GNG_STADIUM_BACK_ROWS,GNG_STADIUM_BACK_PALMAP,lx,0);}else if(sx<2050){s16 lx=(s16)(96-((sx-1570)*12)/100);draw_layer(SCB_SKY,GNG_SKY_TILE,GNG_SKY_COLS,GNG_SKY_ROWS,GNG_SKY_PALMAP,(camera_x_fp*3)/100,0);draw_positioned(SCB_MID,GNG_STADIUM_FRONT_TILE,GNG_STADIUM_FRONT_COLS,GNG_STADIUM_FRONT_ROWS,GNG_STADIUM_FRONT_PALMAP,lx,0);}else{s32 local=((s32)(sx-2050)<<FP_SHIFT)*30/100;if(frame_counter&32)draw_layer(SCB_SKY,GNG_INSIDE1_TILE,GNG_INSIDE1_COLS,GNG_INSIDE1_ROWS,GNG_INSIDE1_PALMAP,local,0);else draw_layer(SCB_SKY,GNG_INSIDE0_TILE,GNG_INSIDE0_COLS,GNG_INSIDE0_ROWS,GNG_INSIDE0_PALMAP,local,0);hide_range(SCB_MID,VISIBLE_STRIPS);}draw_layer(SCB_GROUND,GNG_GROUND_TILE,GNG_GROUND_COLS,GNG_GROUND_ROWS,GNG_GROUND_PALMAP,camera_x_fp,GROUND_Y);draw_layer(SCB_FOREGROUND,GNG_FG_CEMETERY_TILE,GNG_FG_CEMETERY_COLS,GNG_FG_CEMETERY_ROWS,GNG_FG_CEMETERY_PALMAP,camera_x_fp,0);}

static u8 enemy_width(u8 t){switch(t){case ET_ZOMBIE:return 50;case ET_BAT:return 34;case ET_PUMPKIN:return 30;case ET_GHOST:return 36;default:return 88;}}
static u8 enemy_rows(u8 t){switch(t){case ET_ZOMBIE:return GNG_ZOMBIE_FRAME_ROWS;case ET_BAT:return GNG_BAT_FRAME_ROWS;case ET_PUMPKIN:return GNG_PUMPKIN_FRAME_ROWS;case ET_GHOST:return GNG_GHOST_FRAME_ROWS;default:return GNG_MAZ_FRAME_ROWS;}}
static s16 enemy_ground_y(u8 t){return ground_top_for_cell(enemy_rows(t));}
static void clear_enemies(void){u8 i;for(i=0;i<MAX_ENEMIES;++i){enemies[i].type=ET_NONE;enemies[i].state=ES_DEAD;}}
static Enemy* alloc_enemy(void){u8 i;for(i=0;i<MAX_ENEMIES;++i)if(enemies[i].type==ET_NONE||enemies[i].state==ES_DEAD)return &enemies[i];return 0;}
static u8 count_type(u8 type){u8 i,n=0;for(i=0;i<MAX_ENEMIES;++i)if(enemies[i].type==type&&enemies[i].state==ES_ALIVE)n++;return n;}
static void spawn_enemy(u8 type,s16 x,s16 yoff){Enemy*e=alloc_enemy();if(!e)return;e->type=type;e->state=ES_ALIVE;e->timer=0;e->anim=0;e->vx=0;e->vy=0;e->x=x;e->flags=0;if(type==ET_BAT){e->y=(s16)(68+yoff);e->home_y=e->y;e->hp=1;}else if(type==ET_GHOST){e->y=(s16)(112+yoff);e->home_y=e->y;e->hp=1;}else if(type==ET_MAZ){e->y=enemy_ground_y(type);e->home_y=e->y;e->hp=8;}else{e->y=enemy_ground_y(type);e->home_y=e->y;e->hp=1;}}
static void spawn_wave(u8 wi){u8 n;const Wave*w=&waves[wi];for(n=0;n<w->count;++n){s16 x=(s16)(w->at+150+n*54);if(x>WORLD_END)x=WORLD_END;spawn_enemy(w->type,x,(s16)(n*12));}}
static void update_spawns(void){u8 i;for(i=0;i<WAVE_COUNT;++i)if(!(wave_mask&(1u<<i))&&player_x+190>=waves[i].at){wave_mask|=(u16)(1u<<i);spawn_wave(i);}if(player_x>=CHECKPOINT_X)checkpoint_reached=1;if(!boss_spawned&&player_x>=ARENA_X-100){boss_spawned=1;clear_enemies();spawn_enemy(ET_MAZ,ARENA_X+150,0);}if(!boss_spawned){u8 in_zone=(player_x>=6*16&&player_x<118*16)||(player_x>=126*16&&player_x<238*16);if(in_zone){if(zombie_timer)zombie_timer--;else{zombie_timer=(player_x<126*16)?250:275;if(count_type(ET_ZOMBIE)<2)spawn_enemy(ET_ZOMBIE,(s16)clamp16(player_x+180,40,WORLD_END),0);}}}}
static void damage_player(void){if(invuln||stage_clear)return;if(lives)lives--;invuln=90;sound(SND_SFX_HIT);if(!lives)return;player_x=(checkpoint_reached?CHECKPOINT_X+32:48);player_x_fp=(s32)player_x<<FP_SHIFT;player_y_fp=(s32)PLAYER_CELL_Y<<FP_SHIFT;player_vx_fp=player_vy_fp=0;player_ground=1;clear_enemies();wave_mask=0;boss_spawned=0;if(checkpoint_reached){u8 i;for(i=0;i<WAVE_COUNT;++i)if(waves[i].at<CHECKPOINT_X+256)wave_mask|=(u16)(1u<<i);}}
static void update_enemy(Enemy*e){if(e->state==ES_DEAD)return;if(e->state==ES_DYING){if(e->timer)e->timer--;else e->state=ES_DEAD;return;}e->timer++;switch(e->type){case ET_ZOMBIE:if((frame_counter&1)==0)e->x+=(player_x<e->x)?-1:1;e->anim=(u8)((frame_counter>>4)&3);break;case ET_BAT:e->x+=(player_x<e->x)?-1:1;e->y=(s16)(e->home_y+((frame_counter+e->x)&31)/3);e->anim=(u8)((frame_counter>>3)%3);if(iabs16(player_x-e->x)<90&&e->y<150)e->y+=2;break;case ET_PUMPKIN:if(!e->flags){e->vy=-5;e->flags=1;}e->x+=(player_x<e->x)?-1:1;e->y+=e->vy;if((frame_counter&3)==0&&e->vy<6)e->vy++;if(e->y>=e->home_y){e->y=e->home_y;e->flags=0;}e->anim=(u8)((frame_counter>>3)&3);break;case ET_GHOST:if((frame_counter&1)==0)e->x+=(player_x<e->x)?-1:1;e->y=(s16)(e->home_y+((frame_counter+e->x)&31)/4);e->anim=(u8)((frame_counter>>4)&1);break;case ET_MAZ:if((frame_counter&3)==0)e->x+=(player_x<e->x)?-1:1;e->anim=(u8)(GNG_MAZ_FRAME_MAZWALK0+((frame_counter>>4)&3));if((frame_counter%180)>145)e->anim=GNG_MAZ_FRAME_MAZTHROW;break;}if(invuln==0&&iabs16(player_x-e->x)<22&&iabs16((player_y+GNG_PLAYER_CELL_H/2)-(e->y+enemy_rows(e->type)*8))<32)damage_player();}
static void update_enemies(void){u8 i;for(i=0;i<MAX_ENEMIES;++i)if(enemies[i].type!=ET_NONE)update_enemy(&enemies[i]);}
static void kill_enemy(Enemy*e){if(e->type==ET_MAZ){if(e->hp)e->hp--;sound(SND_SFX_BOSS_HIT);if(e->hp)return;stage_clear=1;score+=5000;sound(SND_SFX_STAGE_CLEAR);return;}e->state=ES_DYING;e->timer=24;score+=100;sound(SND_SFX_ENEMY_DIE);}
static void spawn_ball(u8 up,u8 down_air){u8 i;for(i=0;i<MAX_BALLS;++i)if(!balls[i].active){Ball*b=&balls[i];b->active=1;b->life=52;b->bounces=0;b->x=(s16)(player_x+(player_facing_right?18:-8));b->y=(s16)(player_y+24);if(up){b->vx=player_facing_right?4:-4;b->vy=-3;}else if(down_air){b->vx=player_facing_right?4:-4;b->vy=3;}else{b->vx=player_facing_right?5:-5;b->vy=0;}kick_timer=9;sound(SND_SFX_KICK);return;}}
static void update_balls(void){u8 i,j;for(i=0;i<MAX_BALLS;++i){Ball*b=&balls[i];if(!b->active)continue;b->x+=b->vx;b->y+=b->vy;if(b->life)b->life--;else{b->active=0;continue;}if(b->y>=GROUND_Y-6&&is_ground(b->x)){if(!b->bounces){b->y=GROUND_Y-7;b->vy=b->vy?-b->vy:-2;b->bounces=1;}else{b->active=0;continue;}}for(j=0;j<MAX_ENEMIES;++j){Enemy*e=&enemies[j];if(e->type==ET_NONE||e->state!=ES_ALIVE)continue;if(iabs16(b->x-e->x)<(enemy_width(e->type)/2+8)&&iabs16(b->y-(e->y+enemy_rows(e->type)*8))<38){kill_enemy(e);b->active=0;break;}}}}
static void sync_player(void){player_x=(s16)(player_x_fp>>FP_SHIFT);player_y=(s16)(player_y_fp>>FP_SHIFT);}
static void update_player(u8 pad){u8 l=!!(pad&CNT_LEFT),r=!!(pad&CNT_RIGHT),up=!!(pad&CNT_UP),down=!!(pad&CNT_DOWN);u8 jump=!!((pad&CNT_A)&&!(prev_pad&CNT_A));u8 shoot=!!((pad&CNT_B)&&!(prev_pad&CNT_B));s32 target=0,accel=player_ground?GROUND_ACCEL_FP:AIR_ACCEL_FP;if(l){target=-RUN_SPEED_FP;player_facing_right=0;}if(r){target=RUN_SPEED_FP;player_facing_right=1;}player_vx_fp=approach32(player_vx_fp,target,accel);player_x_fp+=player_vx_fp;if(player_x_fp<(24L<<FP_SHIFT))player_x_fp=24L<<FP_SHIFT;if(player_x_fp>((s32)WORLD_END<<FP_SHIFT))player_x_fp=(s32)WORLD_END<<FP_SHIFT;sync_player();if(jump&&player_ground){player_vy_fp=JUMP_VEL_FP;player_ground=0;}if(!player_ground){player_vy_fp+=GRAVITY_FP;if(player_vy_fp>MAX_FALL_FP)player_vy_fp=MAX_FALL_FP;if(!(pad&CNT_A)&&player_vy_fp<RELEASE_CUT_FP)player_vy_fp/=2;player_y_fp+=player_vy_fp;sync_player();if(player_vy_fp>=0&&is_ground(player_x)&&player_y>=PLAYER_CELL_Y){player_y_fp=(s32)PLAYER_CELL_Y<<FP_SHIFT;player_vy_fp=0;player_ground=1;sync_player();}}else if(!is_ground(player_x)){player_ground=0;player_vy_fp=0;}if(player_y>224){damage_player();sync_player();}if(shoot)spawn_ball(up,(u8)(down&&!player_ground));if(kick_timer)kick_timer--;if(invuln)invuln--;}
static void update_camera(void){s16 desired=(s16)(player_x-128+(player_facing_right?30:-30));if(boss_spawned&&!stage_clear&&desired>ARENA_X-150)desired=ARENA_X-150;desired=clamp16(desired,0,CAMERA_MAX);camera_x_fp+=(((s32)desired<<FP_SHIFT)-camera_x_fp)/5;camera_x=(s16)(camera_x_fp>>FP_SHIFT);}
static u8 player_frame(u8 moving){if(kick_timer)return GNG_PLAYER_FRAME_KICK;if(!player_ground)return player_vy_fp<0?GNG_PLAYER_FRAME_JUMP:GNG_PLAYER_FRAME_FALL;if(moving)return(u8)(GNG_PLAYER_FRAME_RUN0+((frame_counter>>2)&3));return(u8)(GNG_PLAYER_FRAME_IDLE0+((frame_counter>>5)&1));}
static void draw_player(u8 pf){s32 sxfp=player_x_fp-camera_x_fp-(32L<<FP_SHIFT);s16 sx=px_fp(sxfp);if(invuln&&(frame_counter&2)){hide_range(SCB_PLAYER,GNG_PLAYER_FRAME_COLS);return;}draw_actor(SCB_PLAYER,GNG_PLAYER_TILE,GNG_PLAYER_COLS,GNG_PLAYER_PALMAP,pf,GNG_PLAYER_FRAME_COLS,GNG_PLAYER_FRAME_ROWS,sx,player_y,player_facing_right?0:1);}
static void draw_enemy_slot(u8 slot,Enemy*e){u16 scb=(u16)(SCB_ENEMY_BASE+slot*ENEMY_STRIDE);s16 sx=(s16)(((e->x-camera_x)*GNG_X_NUM)/GNG_X_DEN);u8 frame=0;if(e->type==ET_NONE||e->state==ES_DEAD||sx<-140||sx>340){hide_range(scb,ENEMY_STRIDE);return;}if(e->state==ES_DYING){if(e->type==ET_ZOMBIE)frame=(e->timer>12)?GNG_ZOMBIE_FRAME_DIE0:GNG_ZOMBIE_FRAME_DIE1;else if(e->type==ET_PUMPKIN)frame=(e->timer>12)?GNG_PUMPKIN_FRAME_DIE0:GNG_PUMPKIN_FRAME_DIE1;else if(e->type==ET_GHOST)frame=(e->timer>12)?GNG_GHOST_FRAME_DIE0:GNG_GHOST_FRAME_DIE1;}switch(e->type){case ET_ZOMBIE:if(e->state==ES_ALIVE)frame=(u8)(GNG_ZOMBIE_FRAME_WALK0+(e->anim&3));draw_actor(scb,GNG_ZOMBIE_TILE,GNG_ZOMBIE_COLS,GNG_ZOMBIE_PALMAP,frame,GNG_ZOMBIE_FRAME_COLS,GNG_ZOMBIE_FRAME_ROWS,(s16)(sx-GNG_ZOMBIE_CELL_W/2),e->y,e->x>player_x?1:0);break;case ET_BAT:frame=(e->state==ES_DYING)?GNG_BAT_FRAME_DIE0:(u8)(GNG_BAT_FRAME_BAT0+(e->anim%3));draw_actor(scb,GNG_BAT_TILE,GNG_BAT_COLS,GNG_BAT_PALMAP,frame,GNG_BAT_FRAME_COLS,GNG_BAT_FRAME_ROWS,(s16)(sx-GNG_BAT_CELL_W/2),e->y,e->x>player_x?1:0);break;case ET_PUMPKIN:if(e->state==ES_ALIVE)frame=(u8)(GNG_PUMPKIN_FRAME_PK0+(e->anim&3));draw_actor(scb,GNG_PUMPKIN_TILE,GNG_PUMPKIN_COLS,GNG_PUMPKIN_PALMAP,frame,GNG_PUMPKIN_FRAME_COLS,GNG_PUMPKIN_FRAME_ROWS,(s16)(sx-GNG_PUMPKIN_CELL_W/2),e->y,e->x>player_x?1:0);break;case ET_GHOST:if(e->state==ES_ALIVE)frame=(u8)(GNG_GHOST_FRAME_GH0+(e->anim&1));draw_actor(scb,GNG_GHOST_TILE,GNG_GHOST_COLS,GNG_GHOST_PALMAP,frame,GNG_GHOST_FRAME_COLS,GNG_GHOST_FRAME_ROWS,(s16)(sx-GNG_GHOST_CELL_W/2),e->y,e->x>player_x?1:0);break;case ET_MAZ:frame=e->anim;draw_actor(scb,GNG_MAZ_TILE,GNG_MAZ_COLS,GNG_MAZ_PALMAP,frame,GNG_MAZ_FRAME_COLS,GNG_MAZ_FRAME_ROWS,(s16)(sx-GNG_MAZ_CELL_W/2),e->y,e->x>player_x?1:0);break;}}
static void draw_balls(void){u8 i;for(i=0;i<MAX_BALLS;++i){Ball*b=&balls[i];u16 scb=(u16)(SCB_BALL0+i);if(!b->active){hide_strip(scb);continue;}*REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;*REG_VRAMRW=GNG_BALL_TILE;*REG_VRAMRW=(u16)(GNG_BALL_PALMAP[0]<<8);position_strip(scb,1,(s16)(((b->x-camera_x)*GNG_X_NUM)/GNG_X_DEN),b->y);}}
static void hud(void){char s[40];sprintf(s,"SCORE %06lu   x%u",(unsigned long)score,lives);ng_text(1,1,0,s);if(boss_spawned&&!stage_clear){u8 hp=0,i;for(i=0;i<MAX_ENEMIES;++i)if(enemies[i].type==ET_MAZ&&enemies[i].state==ES_ALIVE)hp=enemies[i].hp;sprintf(s,"MAZAPEGUL %u/8",hp);ng_text(22,1,0,s);}}
static void reset_game(void){u8 i;score=0;lives=3;invuln=0;stage_clear=0;boss_spawned=0;checkpoint_reached=0;zombie_timer=180;music_timer=0;wave_mask=0;clear_enemies();for(i=0;i<MAX_BALLS;++i)balls[i].active=0;player_x_fp=48L<<FP_SHIFT;player_y_fp=(s32)PLAYER_CELL_Y<<FP_SHIFT;player_vx_fp=player_vy_fp=0;sync_player();player_ground=1;player_facing_right=1;kick_timer=0;camera_x_fp=0;camera_x=0;prev_pad=0;frame_counter=0;}
static void title_screen(void){u16 t=0;ng_cls();set_backdrop(0x8000);init_palettes();sound(SND_SFX_INTRO);for(;;){u8 pad=bios_p1current;ng_wait_vblank();draw_layer(SCB_SKY,GNG_SKY_TILE,GNG_SKY_COLS,GNG_SKY_ROWS,GNG_SKY_PALMAP,((s32)(t%240)<<FP_SHIFT)/8,0);draw_positioned(SCB_MID,GNG_STADIUM_BACK_TILE,GNG_STADIUM_BACK_COLS,GNG_STADIUM_BACK_ROWS,GNG_STADIUM_BACK_PALMAP,-40,0);ng_center_text_tall(6,0,"GHOSTS");ng_center_text_tall(9,0,"'N GOALS");ng_center_text(17,2,"PRESS START");ng_center_text(20,0,"FULL LEVEL 1 R1");if((t&31)<16)ng_center_text(23,1,"5 COIN  /  1 START");else ng_center_text(23,1,"                    ");if(start_requested||bios_user_mode==USER_MODE_GAME||(pad&CNT_START1))break;t++;if(t>=2376){sound(SND_SFX_INTRO);t=0;}}sound(3);start_requested=0;}
static void stage_clear_screen(void){u16 t=180;while(t--){ng_wait_vblank();ng_center_text_tall(9,0,"STAGE CLEAR");ng_center_text(14,2,"DAI BURDEL!");}}
static void game_over_screen(void){u16 t=180;while(t--){ng_wait_vblank();ng_center_text_tall(10,0,"GAME OVER");}}
static void game_loop(void){reset_game();ng_cls();set_backdrop(0x8000);init_palettes();sound(SND_SFX_LEVEL_LOOP);while(lives&&!stage_clear){u8 pad=bios_p1current,moving=!!(pad&(CNT_LEFT|CNT_RIGHT));u8 pf;update_player(pad);update_camera();update_spawns();update_enemies();update_balls();pf=player_frame(moving);if(music_timer++>=930){sound(SND_SFX_LEVEL_LOOP);music_timer=0;}ng_wait_vblank();draw_background();draw_player(pf);{u8 i;for(i=0;i<MAX_ENEMIES;++i)draw_enemy_slot(i,&enemies[i]);}draw_balls();hud();prev_pad=pad;frame_counter++;}sound(3);if(stage_clear)stage_clear_screen();else game_over_screen();}
void player_start(void){start_requested=1;}
void coin_sound(void){}
void main_mvs_title(void){title_screen();}
int main(void){for(;;){title_screen();game_loop();}return 0;}
