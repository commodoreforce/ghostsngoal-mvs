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

/* Finite painter's order. No scenery SCB is reused by another layer. */
#define SCB_SKY        0
#define SCB_CITY       21
#define SCB_GROUND     42
#define SCB_PLAYER     63
#define SCB_ZOMBIE     68
#define SCB_BALL0      73
#define SCB_FOREGROUND 76

#define GROUND_Y 208
#define PLAYER_GROUND_Y 144
#define CAMERA_MAX (GNG_BENCHMARK_WORLD_W - GNG_LOGICAL_W)
#define MAX_BALLS 3
#define BALL_LIFE 49

#define FP_SHIFT 8
#define FP_ONE (1L << FP_SHIFT)
#define RUN_SPEED_FP 469
#define GROUND_ACCEL_FP 156
#define AIR_ACCEL_FP 92
#define JUMP_VEL_FP (-1293)
#define GRAVITY_FP 60
#define MAX_FALL_FP 2219
#define RELEASE_CUT_FP (-469)

typedef struct { s16 x,y,vx,vy; u8 life,bounces,active; } Ball;

static char str[40];
static u8 prev_pad;
static u16 frame_counter;
static s32 player_x_fp,player_y_fp,player_vx_fp,player_vy_fp;
static s16 player_x,player_y;
static u8 player_ground,player_facing_right,kick_timer;
static s16 zombie_x,zombie_y;
static u8 zombie_state,zombie_die_timer;
static s32 camera_x_fp;
static s16 camera_x;
static Ball balls[MAX_BALLS];

static s16 iabs16(s16 v){return v<0?-v:v;}
static s16 clamp16(s16 v,s16 lo,s16 hi){if(v<lo)return lo;if(v>hi)return hi;return v;}
static s32 approach32(s32 v,s32 t,s32 a){if(v<t){v+=a;if(v>t)v=t;}else if(v>t){v-=a;if(v<t)v=t;}return v;}
static s16 px_fp(s32 logical_fp){return (s16)((logical_fp * GNG_X_NUM) / (GNG_X_DEN * FP_ONE));}
static void sync_player_xy(void){player_x=(s16)(player_x_fp>>FP_SHIFT);player_y=(s16)(player_y_fp>>FP_SHIFT);}
static void set_backdrop(u16 c){*((volatile u16*)0x401ffe)=c;}

static void load_palette(u8 slot,const unsigned short*colors){u16 base=(u16)slot*16,i;for(i=0;i<16;++i)MMAP_PALBANK1[base+i]=colors[i];}
static void load_palette_set(u8 first,const unsigned short*p,u16 count){u16 i;for(i=0;i<count;++i)load_palette((u8)(first+i),p+i*16);}
static void init_palettes(void){
    const u16 fix[]={0x8000,0x0fff,0x0555,0x0bbb,0,0,0,0,0,0,0,0,0,0,0,0};
    load_palette(0,fix);
    load_palette_set(GNG_SKY_PAL_FIRST,&GNG_SKY_PALETTES[0][0],GNG_SKY_PAL_COUNT);
    load_palette_set(GNG_CITY_PAL_FIRST,&GNG_CITY_PALETTES[0][0],GNG_CITY_PAL_COUNT);
    load_palette_set(GNG_GROUND_PAL_FIRST,&GNG_GROUND_PALETTES[0][0],GNG_GROUND_PAL_COUNT);
    load_palette_set(GNG_FG_CEMETERY_PAL_FIRST,&GNG_FG_CEMETERY_PALETTES[0][0],GNG_FG_CEMETERY_PAL_COUNT);
    load_palette_set(GNG_PLAYER_PAL_FIRST,&GNG_PLAYER_PALETTES[0][0],GNG_PLAYER_PAL_COUNT);
    load_palette_set(GNG_ZOMBIE_PAL_FIRST,&GNG_ZOMBIE_PALETTES[0][0],GNG_ZOMBIE_PAL_COUNT);
    load_palette_set(GNG_BALL_PAL_FIRST,&GNG_BALL_PALETTES[0][0],GNG_BALL_PAL_COUNT);
}

static void position_strip(u16 scb,u8 rows,s16 x,s16 y){
    *REG_VRAMMOD=0x200;
    *REG_VRAMADDR=ADDR_SCB2+scb;
    *REG_VRAMRW=0x0fff;
    *REG_VRAMRW=(u16)((-y*128)+rows);
    *REG_VRAMRW=(u16)(x*128);
}
static void hide_strip(u16 scb){position_strip(scb,1,500,240);}

/* Draw a finite painted layer from fixed-point world scroll. No modulo, no wrap,
   no synthetic strips. Every visible column has one stable source column. */
static void draw_clamped_layer_fp(u16 first,u16 tile,u16 cols,u8 rows,const unsigned char*pm,s32 scroll_fp,s16 y){
    s32 ps=(scroll_fp*GNG_X_NUM)/(GNG_X_DEN*FP_ONE);
    u16 c,r,start;
    s16 bx;
    if(ps<0)ps=0;
    start=(u16)(ps>>4);
    bx=(s16)(-(ps&15));
    for(c=0;c<VISIBLE_STRIPS;++c){
        u16 scb=first+c;
        u16 src=(u16)(start+c);
        s16 x=(s16)(bx+c*16);
        if(src>=cols){hide_strip(scb);continue;}
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+scb*64;
        for(r=0;r<rows;++r){
            u16 idx=(u16)(r*cols+src);
            *REG_VRAMRW=(u16)(tile+idx);
            *REG_VRAMRW=(u16)(pm[idx]<<8);
        }
        position_strip(scb,rows,x,y);
    }
}

static void draw_actor_fp(u16 first,u16 tile,u16 cols,const unsigned char*pm,u8 frame,s32 lx_fp,s16 y,u8 flip){
    u16 col0=(u16)frame*GNG_ACTOR_COLS,c,r;
    s16 x=px_fp(lx_fp);
    for(c=0;c<GNG_ACTOR_COLS;++c){
        u16 scb=first+c,src=col0+c;
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+scb*64;
        for(r=0;r<GNG_ACTOR_ROWS;++r){
            u16 idx=(u16)(r*cols+src),attr=(u16)(pm[idx]<<8);
            if(flip)attr|=1;
            *REG_VRAMRW=(u16)(tile+idx);
            *REG_VRAMRW=attr;
        }
        position_strip(scb,GNG_ACTOR_ROWS,!flip?(s16)(x+c*16):(s16)(x+(GNG_ACTOR_COLS-1-c)*16),y);
    }
}

static void draw_ball_slot(u8 n){
    u16 scb=SCB_BALL0+n,pal=GNG_BALL_PALMAP[0];
    Ball*b=&balls[n];
    s32 sx;
    if(!b->active){hide_strip(scb);return;}
    *REG_VRAMMOD=1;
    *REG_VRAMADDR=ADDR_SCB1+scb*64;
    *REG_VRAMRW=GNG_BALL_TILE;
    *REG_VRAMRW=(u16)(pal<<8);
    sx=((s32)b->x<<FP_SHIFT)-camera_x_fp;
    position_strip(scb,1,px_fp(sx),b->y);
}

static void draw_scenery(void){
    /* True finite parallax. Ground and cemetery are world-locked. */
    draw_clamped_layer_fp(SCB_SKY,GNG_SKY_TILE,GNG_SKY_COLS,GNG_SKY_ROWS,GNG_SKY_PALMAP,(camera_x_fp*3)/100,0);
    draw_clamped_layer_fp(SCB_CITY,GNG_CITY_TILE,GNG_CITY_COLS,GNG_CITY_ROWS,GNG_CITY_PALMAP,(camera_x_fp*15)/100,0);
    draw_clamped_layer_fp(SCB_GROUND,GNG_GROUND_TILE,GNG_GROUND_COLS,GNG_GROUND_ROWS,GNG_GROUND_PALMAP,camera_x_fp,160);
    draw_clamped_layer_fp(SCB_FOREGROUND,GNG_FG_CEMETERY_TILE,GNG_FG_CEMETERY_COLS,GNG_FG_CEMETERY_ROWS,GNG_FG_CEMETERY_PALMAP,camera_x_fp,0);
}

static void demo_mode(void){ng_cls();set_backdrop(0x0088);ng_center_text_tall(8,0,"GHOSTS N GOALS");ng_center_text(12,0,"NEO GEO MVS");ng_center_text(17,0,"PREMIUM OPENING V4");ng_center_text(22,0,bios_mvs_flag==SYSTEM_MVS?"INSERT COIN":"PRESS START");{u16 f=60*15;while(f--&&bios_user_mode!=USER_MODE_GAME)ng_wait_vblank();}}
static void title_mode(void){ng_cls();set_backdrop(0x0800);ng_center_text_tall(8,0,"GHOSTS N GOALS");ng_center_text(16,0,"OPENING ZONE V4");ng_center_text(22,0,"PRESS START");while(bios_user_mode!=USER_MODE_GAME){sprintf(str,"TIMER %02x",bios_compulsion_timer);ng_center_text(24,0,str);ng_wait_vblank();}}

static void update_camera(void){
    s16 desired=(s16)(player_x-(GNG_LOGICAL_W/2)+(player_facing_right?28:-28));
    desired=clamp16(desired,0,CAMERA_MAX);
    camera_x_fp+=(((s32)desired<<FP_SHIFT)-camera_x_fp)/5;
    camera_x=(s16)(camera_x_fp>>FP_SHIFT);
}

static void spawn_ball(u8 up,u8 down_air){
    u8 i;
    for(i=0;i<MAX_BALLS;++i)if(!balls[i].active){
        Ball*b=&balls[i];
        b->active=1;b->life=BALL_LIFE;b->bounces=0;
        b->x=(s16)(player_x+(player_facing_right?15:-15));
        b->y=(s16)(player_y+(up?10:down_air?34:25));
        if(up){b->vx=player_facing_right?3:-3;b->vy=-3;}
        else if(down_air){b->vx=player_facing_right?3:-3;b->vy=3;}
        else{b->vx=player_facing_right?4:-4;b->vy=0;}
        kick_timer=9;return;
    }
}

static void update_player(u8 pad){
    u8 left=!!(pad&CNT_LEFT),right=!!(pad&CNT_RIGHT),up=!!(pad&CNT_UP),down=!!(pad&CNT_DOWN);
    u8 jump_edge=!!((pad&CNT_A)&&!(prev_pad&CNT_A));
    u8 shoot_edge=!!((pad&CNT_B)&&!(prev_pad&CNT_B));
    s32 target=0,accel=player_ground?GROUND_ACCEL_FP:AIR_ACCEL_FP;
    if(left){target=-RUN_SPEED_FP;player_facing_right=0;}
    if(right){target=RUN_SPEED_FP;player_facing_right=1;}
    player_vx_fp=approach32(player_vx_fp,target,accel);
    player_x_fp+=player_vx_fp;
    if(player_x_fp<(20L<<FP_SHIFT))player_x_fp=(20L<<FP_SHIFT);
    if(player_x_fp>((GNG_BENCHMARK_WORLD_W-20L)<<FP_SHIFT))player_x_fp=((GNG_BENCHMARK_WORLD_W-20L)<<FP_SHIFT);
    if(jump_edge&&player_ground){player_vy_fp=JUMP_VEL_FP;player_ground=0;}
    if(!player_ground){
        player_vy_fp+=GRAVITY_FP;
        if(player_vy_fp>MAX_FALL_FP)player_vy_fp=MAX_FALL_FP;
        if(!(pad&CNT_A)&&player_vy_fp<RELEASE_CUT_FP)player_vy_fp/=2;
        player_y_fp+=player_vy_fp;
        if(player_y_fp>=((s32)PLAYER_GROUND_Y<<FP_SHIFT)){
            player_y_fp=((s32)PLAYER_GROUND_Y<<FP_SHIFT);player_vy_fp=0;player_ground=1;
        }
    }
    sync_player_xy();
    if(zombie_state==0&&iabs16(player_x-zombie_x)<28&&player_y>116){player_x_fp-=player_vx_fp;player_vx_fp=0;sync_player_xy();}
    if(shoot_edge)spawn_ball(up,down&&!player_ground);
}

static void update_balls(void){
    u8 i;
    for(i=0;i<MAX_BALLS;++i){
        Ball*b=&balls[i];
        if(!b->active)continue;
        b->x+=b->vx;b->y+=b->vy;
        if(b->life)b->life--;else{b->active=0;continue;}
        if(b->y>=GROUND_Y-7){
            if(!b->bounces){b->y=GROUND_Y-8;b->vy=b->vy?-b->vy:-2;b->bounces=1;}
            else{b->active=0;continue;}
        }
        if(b->x<camera_x-24||b->x>camera_x+GNG_LOGICAL_W+24){b->active=0;continue;}
        if(zombie_state==0&&iabs16(b->x-zombie_x)<24&&iabs16(b->y-(zombie_y+30))<30){zombie_state=1;zombie_die_timer=28;b->active=0;}
    }
}

static void update_zombie(void){
    if(zombie_state){if(zombie_die_timer)zombie_die_timer--;else zombie_state=2;return;}
    if((frame_counter&1)==0){if(zombie_x>player_x+40)zombie_x--;else if(zombie_x<player_x-40)zombie_x++;}
}

static u8 player_frame_for(u8 moving){
    if(kick_timer)return GNG_PLAYER_FRAME_KICK;
    if(!player_ground)return player_vy_fp<0?GNG_PLAYER_FRAME_JUMP:GNG_PLAYER_FRAME_FALL;
    if(moving)return(u8)(GNG_PLAYER_FRAME_RUN0+((frame_counter>>2)&3));
    return(u8)(GNG_PLAYER_FRAME_IDLE0+((frame_counter>>5)&1));
}

static void draw_characters(u8 pf){
    u8 i;
    s32 player_screen_fp=player_x_fp-camera_x_fp-(32L<<FP_SHIFT);
    draw_actor_fp(SCB_PLAYER,GNG_PLAYER_TILE,GNG_PLAYER_COLS,GNG_PLAYER_PALMAP,pf,player_screen_fp,player_y,player_facing_right?0:1);
    if(zombie_state==2){
        u16 c;for(c=0;c<GNG_ACTOR_COLS;++c)hide_strip(SCB_ZOMBIE+c);
    }else{
        u8 zf=zombie_state==1?(zombie_die_timer>14?GNG_ZOMBIE_FRAME_DIE0:GNG_ZOMBIE_FRAME_DIE1):(u8)(GNG_ZOMBIE_FRAME_WALK0+((frame_counter>>4)&3));
        s32 zx_fp=((s32)(zombie_x-32)<<FP_SHIFT)-camera_x_fp;
        draw_actor_fp(SCB_ZOMBIE,GNG_ZOMBIE_TILE,GNG_ZOMBIE_COLS,GNG_ZOMBIE_PALMAP,zf,zx_fp,zombie_y,zombie_x>player_x?1:0);
    }
    for(i=0;i<MAX_BALLS;++i)draw_ball_slot(i);
}

static void game_mode(void){
    u8 i;
    ng_cls();set_backdrop(0x8000);init_palettes();
    player_x_fp=48L<<FP_SHIFT;player_y_fp=(s32)PLAYER_GROUND_Y<<FP_SHIFT;
    player_vx_fp=0;player_vy_fp=0;sync_player_xy();
    player_ground=1;player_facing_right=1;kick_timer=0;
    zombie_x=190;zombie_y=144;zombie_state=0;zombie_die_timer=0;
    camera_x_fp=0;camera_x=0;prev_pad=0;frame_counter=0;
    for(i=0;i<MAX_BALLS;++i)balls[i].active=0;

    for(;;){
        u8 pad,moving;
        ng_wait_vblank();
        pad=bios_p1current;
        moving=!!(pad&(CNT_LEFT|CNT_RIGHT));
        update_player(pad);update_balls();update_zombie();update_camera();
        draw_scenery();draw_characters(player_frame_for(moving));
        if(kick_timer)kick_timer--;
        prev_pad=pad;frame_counter++;
    }
}

void player_start(void){bios_user_mode=USER_MODE_GAME;bios_player_mod1=1;}
void coin_sound(void){}
int main(void){ng_cls();init_palettes();demo_mode();if(bios_user_mode==USER_MODE_GAME)game_mode();return 0;}
int main_mvs_title(void){ng_cls();init_palettes();title_mode();game_mode();return 0;}
