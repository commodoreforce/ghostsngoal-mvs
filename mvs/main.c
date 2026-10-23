#include <ngdevkit/neogeo.h>
#include <ngdevkit/bios-ram.h>
#include <ngdevkit/ng-fix.h>
#include <ngdevkit/ng-video.h>
#include <ngdevkit/backup-ram.h>
#include <stdio.h>
#include "src/level1_assets.h"
#include "src/level1_audio.h"
#include "snd_commands.h"

#define USER_MODE_GAME 0x2
#define SYSTEM_MVS 0x80
#define VISIBLE_STRIPS 21
#define REG_SOUND ((volatile u8*)0x320000)

#define FP_SHIFT 8
#define FP_ONE (1L<<FP_SHIFT)
#define MAX_BALLS 8
#define MAX_ENEMIES 12
#define MAX_HAZARDS 8
#define SCB_HAZARD_BASE 212
#define BOSS_MAX_HP 32
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
static u8 armored;
static u8 lives,invuln,stage_clear,boss_spawned,checkpoint_reached;
static u16 zombie_timer,music_timer,wave_mask;
static s32 player_x_fp,player_y_fp,player_vx_fp,player_vy_fp,camera_x_fp;
static s16 player_x,player_y,camera_x;
static u8 player_ground,player_facing_right,kick_timer,drop_timer,coyote_timer,jump_buffer;

typedef struct { s16 x,y,vx,vy; u8 life,bounces,active;s32 x_fp,y_fp; } Ball;
typedef struct { u8 type,state,hp,anim,flags; s16 x,y,vx,vy,home_y; u16 timer; } Enemy;
typedef struct { u16 at; u8 type,count; } Wave;
static Ball balls[MAX_BALLS];
static Enemy enemies[MAX_ENEMIES];
static u8 enemy_attack[MAX_ENEMIES],enemy_attack_timer[MAX_ENEMIES],enemy_flash[MAX_ENEMIES];
static u16 enemy_cooldown[MAX_ENEMIES];
static s32 enemy_x_fp[MAX_ENEMIES],enemy_y_fp[MAX_ENEMIES];
typedef struct {s32 x_fp,y_fp;s16 vx,vy;u8 kind,life;} Hazard;
static Hazard hazards[MAX_HAZARDS];
static u8 boss_action,boss_dreams,boss_history_index;
static s16 boss_history_x[192],boss_history_y[192],boss_rewind_x,boss_rewind_y;
static s32 boss_jump_x_fp,boss_jump_y_fp;

typedef struct {s16 x,y;u8 life,dust;} VisualParticle;
static VisualParticle particles[8];
static u8 particle_cursor;
static void impact_fx(s16 x,s16 y,u8 dust){
    VisualParticle*p=&particles[particle_cursor];p->x=x;p->y=y;p->dust=dust;p->life=12;
    particle_cursor=(particle_cursor+1)&7;
}
static const Wave waves[]={{30*16,ET_BAT,1},{46*16,ET_PUMPKIN,1},{62*16,ET_BAT,2},{80*16,ET_PUMPKIN,2},{100*16,ET_BAT,2},{132*16,ET_GHOST,2},{150*16,ET_PUMPKIN,2},{162*16,ET_GHOST,2},{176*16,ET_BAT,2},{192*16,ET_GHOST,2},{204*16,ET_PUMPKIN,2},{216*16,ET_GHOST,2},{226*16,ET_BAT,2}};
#define WAVE_COUNT (sizeof(waves)/sizeof(waves[0]))
static const u16 ground_spans[][2]={{0,34},{36,58},{60,96},{98,152},{155,196},{199,262}};
#define GROUND_SPAN_COUNT 6

static s16 iabs16(s16 v){return v<0?-v:v;}
static s16 clamp16(s16 v,s16 lo,s16 hi){if(v<lo)return lo;if(v>hi)return hi;return v;}
static s32 approach32(s32 v,s32 t,s32 a){if(v<t){v+=a;if(v>t)v=t;}else if(v>t){v-=a;if(v<t)v=t;}return v;}
static s16 px_fp(s32 f){return (s16)((f*GNG_X_NUM)/(GNG_X_DEN*FP_ONE));}
static void set_backdrop(u16 c){*((volatile u16*)0x401ffe)=c;}
static void sound(u8 cmd){
    u16 timeout=10000;*REG_SOUND=cmd;
    /* nullsound acknowledges queued commands with bit 7 set. */
    if(cmd>=4)while(timeout--&&*REG_SOUND!=(u8)(cmd|0x80)){}
}
static void reset_sound(void){
    u8 tries;*REG_SOUND=3;
    for(tries=0;tries<8;++tries){
        ng_wait_vblank();*REG_SOUND=0;ng_wait_vblank();
        if(*REG_SOUND==0x7e)return;
    }
}
static void load_palette(u8 slot,const unsigned short*colors){u16 b=(u16)slot*16,i;for(i=0;i<16;++i)MMAP_PALBANK1[b+i]=colors[i];}
static void load_set(u8 first,const unsigned short*p,u16 count){volatile u16*dest=&MMAP_PALBANK1[(u16)first*16];u16 words=count*16;while(words--)*dest++=*p++;}
static u8 scene_palette=255,stadium_palette=255,gold_palette,actor_tint=255,last_lighting=255;
static u16 layer_columns[84],actor_columns[384];
static u8 strip_hidden[384],hud_dirty=1;
static void init_palettes(void){u16 sc;for(sc=0;sc<384;++sc)actor_columns[sc]=0;u8 c;for(c=0;c<84;++c)layer_columns[c]=0;const u16 fix[]={0x8000,0x0fff,0x0f80,0x0cc0,0x0666,0,0,0,0,0,0,0,0,0,0,0};load_palette(0,fix);load_palette(1,fix);load_palette(2,fix);
#define LP(N) load_set(GNG_##N##_PAL_FIRST,&GNG_##N##_PALETTES[0][0],GNG_##N##_PAL_COUNT)
scene_palette=stadium_palette=255;gold_palette=0;last_lighting=255;load_palette(97,GNG_HIT_PALETTE[0]);load_palette(98,GNG_MIDAS_PALETTE[0]);LP(ITEMS);LP(SCOREPOP);LP(GOLD_BALL);LP(GROUND);LP(FG_CEMETERY);LP(PLAYER);LP(PLAYER_NAKED);LP(ZOMBIE);LP(BAT);LP(PUMPKIN);LP(GHOST);LP(MAZ);LP(BALL);LP(MAZFX);LP(PARTICLES);LP(WAVE);LP(RAIN);LP(FOG);LP(TITLE);
#undef LP
}
/* Only the visible artwork owns the scene banks; actor palettes stay resident. */
#define UPLOAD(N) load_set(GNG_##N##_PAL_FIRST,&GNG_##N##_PALETTES[0][0],GNG_##N##_PAL_COUNT)
static void select_scene_palette(u8 scene){
    if(scene_palette==scene)return;
    if((scene_palette==2||scene_palette==3)&&(scene==2||scene==3)){scene_palette=scene;return;}
    if((scene_palette==8||scene_palette==9)&&(scene==8||scene==9)){scene_palette=scene;return;}
    if((scene_palette==10||scene_palette==11)&&(scene==10||scene==11)){scene_palette=scene;return;}
    switch(scene){case 0:UPLOAD(SKY);break;case 1:UPLOAD(CITY);break;case 2:UPLOAD(INSIDE0);break;case 3:UPLOAD(INSIDE1);break;case 4:UPLOAD(PEN_BG0);break;case 5:UPLOAD(PEN_BG1);break;case 6:UPLOAD(PEN_BG2);break;case 7:UPLOAD(PEN_BG3);break;case 8:UPLOAD(INSIDE0_DARK);break;case 9:UPLOAD(INSIDE1_DARK);break;case 10:UPLOAD(INSIDE0_FLASH);break;case 11:UPLOAD(INSIDE1_FLASH);break;default:UPLOAD(CITY_FLASH);break;}scene_palette=scene;
}
static void select_stadium_palette(u8 scene){
    if(stadium_palette==scene)return;
    switch(scene){case 0:UPLOAD(STADIUM_BACK);break;case 1:UPLOAD(STADIUM_BLEND1);break;case 2:UPLOAD(STADIUM_BLEND2);break;case 3:UPLOAD(STADIUM_BLEND3);break;default:UPLOAD(STADIUM_FRONT);break;}stadium_palette=scene;
}
#undef UPLOAD
static void position_strip(u16 scb,u8 rows,s16 x,s16 y){strip_hidden[scb]=0;*REG_VRAMMOD=0x200;*REG_VRAMADDR=ADDR_SCB2+scb;*REG_VRAMRW=0x0fff;*REG_VRAMRW=(u16)((-(y+16)*128)+rows);*REG_VRAMRW=(u16)(x*128);}
static void hide_strip(u16 scb){if(strip_hidden[scb])return;position_strip(scb,1,500,240);strip_hidden[scb]=1;}
static void hide_range(u16 first,u16 count){u16 i;for(i=0;i<count;++i)hide_strip(first+i);}
static void draw_layer(u16 first,u16 tile,u16 cols,u8 rows,const unsigned char*pm,s32 scroll_fp,s16 y){
    s32 ps=(scroll_fp*GNG_X_NUM)/(GNG_X_DEN*FP_ONE);u16 c,r,start;s16 bx;
    if(ps<0)ps=0;start=(u16)(ps>>4);bx=(s16)(-(ps&15));
    for(c=0;c<VISIBLE_STRIPS;++c){
        u16 scb=first+c,src=start+c,top=0,height=rows,key;s16 x=bx+c*16;
        if(src>=cols){hide_strip(scb);continue;}
        if(tile==GNG_FG_CEMETERY_TILE){top=GNG_FG_TOP[src];height=GNG_FG_HEIGHT[src];}
        if(!height){hide_strip(scb);continue;}
        key=tile+top*cols+src;
        if(layer_columns[scb]!=key){
            *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;
            for(r=top;r<top+height;++r){u16 idx=r*cols+src;*REG_VRAMRW=tile+idx;*REG_VRAMRW=pm[idx]<<8;}
            layer_columns[scb]=key;
        }
        position_strip(scb,height,x,y+top*16);
    }
}
static void draw_positioned(u16 first,u16 tile,u16 cols,u8 rows,const unsigned char*pm,s16 lx,s16 y){
    s16 px=(s16)(((s32)lx*GNG_X_NUM)/GNG_X_DEN);
    u16 start=px<0?(u16)(-px/16):0,c,r;
    s16 bx=(s16)(px+start*16);
    for(c=0;c<VISIBLE_STRIPS;++c){
        u16 scb=first+c,src=start+c;
        s16 x=(s16)(bx+c*16);
        if(src>=cols||x<=-16||x>=320){hide_strip(scb);continue;}
        if(layer_columns[scb]!=(u16)(tile+src)){
            *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;
            for(r=0;r<rows;++r){u16 idx=(u16)(r*cols+src);*REG_VRAMRW=(u16)(tile+idx);*REG_VRAMRW=(u16)(pm[idx]<<8);}
            layer_columns[scb]=(u16)(tile+src);
        }
        position_strip(scb,rows,x,y);
    }
}
static void draw_actor(u16 first,u16 tile,u16 cols,const unsigned char*pm,u8 frame,u8 fcols,u8 frows,s16 sx,s16 y,u8 flip){
    u16 col0=(u16)frame*fcols,c,r;
    for(c=0;c<fcols;++c){u16 scb=first+c,src=col0+c,key=(tile+src)*2+flip;
        /* Tint overrides deliberately bypass the cache for hit/statue feedback. */
        if(actor_columns[scb]!=key||actor_tint!=255){
            *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;
            for(r=0;r<frows;++r){u16 idx=r*cols+src,attr=(actor_tint==255?pm[idx]:actor_tint)<<8;if(flip)attr|=1;*REG_VRAMRW=tile+idx;*REG_VRAMRW=attr;}
            actor_columns[scb]=actor_tint==255?key:0;
        }
        position_strip(scb,frows,!flip?sx+c*16:sx+(fcols-1-c)*16,y);
    }
    for(c=fcols;c<ENEMY_STRIDE;++c)hide_strip(first+c);
}
static u8 is_ground(s16 x){u8 i;for(i=0;i<GROUND_SPAN_COUNT;++i){s16 a=(s16)ground_spans[i][0]*16,b=(s16)ground_spans[i][1]*16;if(x>=a&&x<b)return 1;}return 0;}
static s16 ground_top_for_cell(u8 rows){return (s16)(GROUND_Y-rows*16);}

#include "src/level1_content.h"

static void select_environment_light(u8 dark){
    if(last_lighting==dark)return;
#define DIM(N) load_set(GNG_##N##_PAL_FIRST,dark?&GNG_##N##_DARK_PALETTES[0][0]:&GNG_##N##_PALETTES[0][0],GNG_##N##_PAL_COUNT)
    DIM(GROUND);DIM(FG_CEMETERY);DIM(ZOMBIE);DIM(BAT);DIM(PUMPKIN);DIM(GHOST);
#undef DIM
    last_lighting=dark;
}
/* The painted planes use the web camera coordinates and original image widths.
 * City+sky are precomposited to keep the background within 42 sprite strips.
 * The three in-place dissolve steps preserve transparent stadium silhouettes. */
static void draw_background(void){
    s16 sx=camera_x;u8 flash=lightning_timer>9||(lightning_timer>3&&lightning_timer<7);
    u16 cycle=light_clock%438;
    level_dark=player_x>124*16&&player_x<240*16&&(cycle>=306||(cycle>=270&&!(frame_counter&4)));
    if(flash)level_dark=0;
    select_environment_light(level_dark);
    if(sx<1904){
        select_scene_palette(flash?12:1);
        s16 city_scroll=clamp16((s16)((s32)sx*15/100),0,272);
        s16 stadium_x=clamp16((s16)(256-((s32)(sx-696)*35)/100),-272,256);
        if(flash)draw_layer(SCB_SKY,GNG_CITY_FLASH_TILE,GNG_CITY_FLASH_COLS,GNG_CITY_FLASH_ROWS,GNG_CITY_FLASH_PALMAP,(s32)city_scroll*FP_ONE,0);else draw_layer(SCB_SKY,GNG_CITY_TILE,GNG_CITY_COLS,GNG_CITY_ROWS,GNG_CITY_PALMAP,(s32)city_scroll*FP_ONE,0);
#define STADIUM(N) draw_positioned(SCB_MID,GNG_##N##_TILE,GNG_##N##_COLS,GNG_##N##_ROWS,GNG_##N##_PALMAP,stadium_x,0)
        if(sx<1280){select_stadium_palette(0);STADIUM(STADIUM_BACK);}
        else if(sx<1332){select_stadium_palette(1);STADIUM(STADIUM_BLEND1);}
        else if(sx<1384){select_stadium_palette(2);STADIUM(STADIUM_BLEND2);}
        else if(sx<1436){select_stadium_palette(3);STADIUM(STADIUM_BLEND3);}
        else {select_stadium_palette(4);STADIUM(STADIUM_FRONT);}
#undef STADIUM
    }else{
        u8 crowd=(frame_counter/25)&1;
        s16 local=clamp16((s16)((s32)(sx-1808)*295/1000),0,742);
        select_scene_palette((u8)((flash?10:level_dark?8:2)+crowd));
#define INSIDE(N) draw_layer(SCB_SKY,GNG_##N##_TILE,GNG_##N##_COLS,GNG_##N##_ROWS,GNG_##N##_PALMAP,(s32)local*FP_ONE,0)
        if(flash){if(crowd){INSIDE(INSIDE1_FLASH);}else{INSIDE(INSIDE0_FLASH);}}
        else if(level_dark){if(crowd){INSIDE(INSIDE1_DARK);}else{INSIDE(INSIDE0_DARK);}}
        else{if(crowd){INSIDE(INSIDE1);}else{INSIDE(INSIDE0);}}
#undef INSIDE
        hide_range(SCB_MID,VISIBLE_STRIPS);
    }
    draw_layer(SCB_GROUND,GNG_GROUND_TILE,GNG_GROUND_COLS,GNG_GROUND_ROWS,GNG_GROUND_PALMAP,camera_x_fp,GROUND_Y);
    draw_layer(SCB_FOREGROUND,GNG_FG_CEMETERY_TILE,GNG_FG_CEMETERY_COLS,GNG_FG_CEMETERY_ROWS,GNG_FG_CEMETERY_PALMAP,camera_x_fp,0);
}
/* Sparse foreground rain, below FIX HUD. Eight strips have a bounded scanline cost. */
static void draw_weather(void){
    u8 i;
    for(i=0;i<8;++i){
        u16 scb=276+i;
        s16 x=(s16)((i*43+640-(frame_counter*(i&1?2:1)+camera_x/2)%640)%320);
        s16 y=(s16)((i*37+frame_counter*(i&1?5:3))%216);
        *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;
        *REG_VRAMRW=GNG_RAIN_TILE;*REG_VRAMRW=(u16)(GNG_RAIN_PALMAP[0]<<8);
        position_strip(scb,1,x,y);
    }
}

static void draw_atmosphere(void){
    u8 i,n=0;
    for(i=0;i<2;++i){s16 x=(s16)((i*190+800-(camera_x*6/5+frame_counter/2)%400)%400)-64;
        draw_actor(356+i*8,GNG_FOG_TILE,GNG_FOG_COLS,GNG_FOG_PALMAP,0,4,2,x,174+i*7,0);
    }
    for(i=0;i<MAX_ENEMIES&&n<8;++i){Enemy*e=&enemies[i];s16 x=e->x-camera_x;
        if(!level_dark||e->type==ET_NONE||e->state!=ES_ALIVE||x<-8||x>264)continue;
        *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+(372+n)*64;*REG_VRAMRW=GNG_FOG_TILE+4;*REG_VRAMRW=GNG_FOG_PALMAP[4]<<8;
        position_strip(372+n,1,px_fp((s32)x*FP_ONE)-8,e->y+(e->type==ET_ZOMBIE?12:6));n++;
    }
    while(n<8)hide_strip(372+n++);
}
static u8 enemy_width(u8 t){switch(t){case ET_ZOMBIE:return 50;case ET_BAT:return 34;case ET_PUMPKIN:return 30;case ET_GHOST:return 36;default:return 88;}}
static u8 enemy_rows(u8 t){switch(t){case ET_ZOMBIE:return GNG_ZOMBIE_FRAME_ROWS;case ET_BAT:return GNG_BAT_FRAME_ROWS;case ET_PUMPKIN:return GNG_PUMPKIN_FRAME_ROWS;case ET_GHOST:return GNG_GHOST_FRAME_ROWS;default:return GNG_MAZ_FRAME_ROWS;}}
static s16 enemy_ground_y(u8 t){return ground_top_for_cell(enemy_rows(t));}
static void clear_enemies(void){u8 i;for(i=0;i<MAX_ENEMIES;++i){enemies[i].type=ET_NONE;enemies[i].state=ES_DEAD;}}
static Enemy* alloc_enemy(void){u8 i;for(i=0;i<MAX_ENEMIES;++i)if(enemies[i].type==ET_NONE||enemies[i].state==ES_DEAD)return &enemies[i];return 0;}
static u8 count_type(u8 type){u8 i,n=0;for(i=0;i<MAX_ENEMIES;++i)if(enemies[i].type==type&&enemies[i].state==ES_ALIVE)n++;return n;}
static void spawn_enemy(u8 type,s16 x,s16 yoff){Enemy*e=alloc_enemy();if(!e)return;e->type=type;e->state=ES_ALIVE;{u8 slot=e-enemies;enemy_attack[slot]=enemy_attack_timer[slot]=enemy_flash[slot]=0;enemy_cooldown[slot]=180;enemy_x_fp[slot]=(s32)x*FP_ONE;{u8 bi;for(bi=0;bi<MAX_BALLS;++bi)ball_hits[bi]&=~(1u<<slot);}}e->timer=0;e->anim=0;e->vx=0;e->vy=0;e->x=x;e->flags=0;if(type==ET_BAT){e->y=(s16)(68+yoff);e->home_y=e->y;e->hp=1;}else if(type==ET_GHOST){e->y=(s16)(112+yoff);e->home_y=e->y;e->hp=1;}else if(type==ET_MAZ){e->y=enemy_ground_y(type);e->home_y=e->y;e->hp=BOSS_MAX_HP;e->flags=7;{u16 i;for(i=0;i<192;++i){boss_history_x[i]=x;boss_history_y[i]=e->y;}}boss_action=boss_dreams=boss_history_index=0;}else{e->y=enemy_ground_y(type);e->home_y=e->y;e->hp=type==ET_PUMPKIN?2:1;if(type==ET_ZOMBIE)e->flags=arcade_random()%100<10;if(type==ET_PUMPKIN)e->vx=36;}enemy_y_fp[e-enemies]=(s32)e->y*FP_ONE;}
static void spawn_wave(u8 wi){u8 n;const Wave*w=&waves[wi];for(n=0;n<w->count;++n){s16 x=(s16)(camera_x+GNG_LOGICAL_W+12+n*28);if(w->type==ET_GHOST&&(n&1))x=camera_x-12;if(x>WORLD_END)x=WORLD_END;spawn_enemy(w->type,x,(s16)(n*12));}}
static void update_spawns(void){u8 i;if(death_timer)return;for(i=0;i<WAVE_COUNT;++i)if(!(wave_mask&(1u<<i))&&camera_x+GNG_LOGICAL_W>=waves[i].at){wave_mask|=(u16)(1u<<i);spawn_wave(i);}if(player_x>=CHECKPOINT_X&&!checkpoint_reached){checkpoint_reached=1;level_time=180;time_tick=0;sound(SND_SFX_CHECKPOINT);}if(!boss_spawned&&player_x>=ARENA_X-100){boss_spawned=1;if(level_time<90)level_time=90;clear_enemies();spawn_enemy(ET_MAZ,ARENA_X+150,0);music_timer=0;sound(SND_SFX_BOSS_LOOP);}if(!boss_spawned){u8 in_zone=(player_x>=6*16&&player_x<118*16)||(player_x>=126*16&&player_x<238*16);if(in_zone){if(zombie_timer)zombie_timer--;else{zombie_timer=(player_x<126*16)?250:275;if(count_type(ET_ZOMBIE)<2)spawn_enemy(ET_ZOMBIE,(s16)clamp16(player_x+180,40,WORLD_END),0);}}}}
static void respawn_player(void){
    u8 i;armored=1;invuln=90;player_x=checkpoint_reached?CHECKPOINT_X+32:48;
    player_x_fp=(s32)player_x*FP_ONE;player_y=PLAYER_CELL_Y;player_y_fp=(s32)player_y*FP_ONE;
    player_vx_fp=player_vy_fp=0;player_ground=1;
    camera_x=clamp16(player_x-98,0,CAMERA_MAX);camera_x_fp=(s32)camera_x*FP_ONE;
    for(i=0;i<MAX_BALLS;++i)balls[i].active=0;
    for(i=0;i<MAX_HAZARDS;++i)hazards[i].life=0;
    clear_enemies();wave_mask=0;boss_spawned=0;music_timer=0;sound(SND_SFX_LEVEL_LOOP);
    if(checkpoint_reached)for(i=0;i<WAVE_COUNT;++i)if(waves[i].at<CHECKPOINT_X+256)wave_mask|=1u<<i;
}
static void lose_life(void){
    if(invuln||stage_clear||death_timer)return;
    took_damage=1;boost_timer=combo_timer=charge_timer=shot_buffer=gold_auto_tick=0;combo_count=0;
    level_time=checkpoint_reached?180:150;time_tick=0;
    if(lives)lives--;death_timer=45;invuln=46;player_vx_fp=player_vy_fp=0;
    if(player_y>PLAYER_CELL_Y){player_y=PLAYER_CELL_Y;player_y_fp=(s32)player_y*FP_ONE;}
    sound(SND_SFX_HIT);
}
static void damage_player(void){
    if(invuln||stage_clear||death_timer)return;
    took_damage=1;
    if(armored){armored=0;hurt_timer=12;invuln=90;sound(SND_SFX_HIT);return;}
    lose_life();
}
static void spawn_hazard(u8 kind,s16 x,s16 y,s16 vx,s16 vy){
    u8 i;for(i=0;i<MAX_HAZARDS;++i)if(!hazards[i].life){Hazard*h=&hazards[i];h->kind=kind;h->life=240;h->x_fp=(s32)x*FP_ONE;h->y_fp=(s32)y*FP_ONE;h->vx=vx;h->vy=vy;return;}
}
static void update_hazards(u8 pad){
    if(death_timer)return;
    u8 i;for(i=0;i<MAX_HAZARDS;++i){Hazard*h=&hazards[i];s16 x,y,foot,top;
        if(!h->life)continue;h->life--;h->x_fp+=h->vx;h->y_fp+=h->vy;
        x=(s16)(h->x_fp>>FP_SHIFT);y=(s16)(h->y_fp>>FP_SHIFT);
        if(h->kind==1)h->vy+=60;
        if(x<0||x>GNG_LEVEL1_WORLD_W||y>224){h->life=0;continue;}
        foot=player_y+GNG_PLAYER_CELL_H;top=foot-((player_ground&&(pad&CNT_DOWN))?26:39);
        if(iabs16(x-player_x)<14&&y+7>top&&y-7<foot){h->life=0;damage_player();}
    }
}
static void update_boss(Enemy*e){
    u8 phase=e->hp>21?1:(e->hp>10?2:3),i; s16 dir=player_x<e->x?-1:1;
    boss_history_x[boss_history_index]=e->x;boss_history_y[boss_history_index]=e->y;
    if(++boss_history_index==192)boss_history_index=0;
    switch(e->flags){
    case 7:e->anim=GNG_MAZ_FRAME_MAZ0;if(e->timer>=90){e->flags=0;e->timer=0;}break;
    case 0:
        e->anim=GNG_MAZ_FRAME_MAZ0+((frame_counter>>5)&1);
        if(iabs16(player_x-e->x)>50){e->vx+=phase==3?145:94;if(e->vx>=256){e->x+=dir;e->vx-=256;}e->anim=GNG_MAZ_FRAME_MAZWALK0+((e->timer/9)&3);}
        if(e->timer>=90){e->flags=(u8)(1+boss_action++%3);e->timer=0;}break;
    case 1:
        e->anim=e->timer<12?GNG_MAZ_FRAME_MAZ0:GNG_MAZ_FRAME_MAZTHROW;
        if(e->timer==27){for(i=0;i<phase+1;++i){s16 spread=(s16)(i*34-phase*17);spawn_hazard(1,e->x+dir*8,e->y+6,(s16)(dir*(512+iabs16(spread)*2)+(player_x-e->x)),(s16)(-981+spread*4));}}
        if(e->timer>=48){e->flags=0;e->timer=0;}break;
    case 2:
        e->anim=GNG_MAZ_FRAME_MAZLAUGH;
        if(e->timer==36||(phase==3&&e->timer==66))spawn_hazard(2,e->x+dir*18,GROUND_Y-40,(s16)(dir*(phase==3?576:469)),0);
        if(e->timer>=90){e->flags=0;e->timer=0;}break;
    case 3:
        e->anim=GNG_MAZ_FRAME_MAZCROUCH;
        if(e->timer>=36){e->flags=4;e->timer=0;e->vx=clamp16((s16)(((s32)player_x-e->x)*256/57),-683,683);e->vy=phase==3?-1877:-1707;boss_jump_x_fp=(s32)e->x*FP_ONE;boss_jump_y_fp=(s32)e->y*FP_ONE;}break;
    case 4:
        e->anim=GNG_MAZ_FRAME_MAZJUMP;e->vy+=60;boss_jump_x_fp+=e->vx;boss_jump_y_fp+=e->vy;
        e->x=clamp16((s16)(boss_jump_x_fp>>FP_SHIFT),ARENA_X-64,WORLD_END);e->y=(s16)(boss_jump_y_fp>>FP_SHIFT);
        if(e->vy>0&&e->y>=enemy_ground_y(ET_MAZ)){e->y=enemy_ground_y(ET_MAZ);e->flags=5;e->timer=0;spawn_hazard(3,e->x-18,GROUND_Y-6,-768,0);spawn_hazard(3,e->x+18,GROUND_Y-6,768,0);}break;
    case 5:e->anim=e->timer<16?GNG_MAZ_FRAME_MAZCROUCH:GNG_MAZ_FRAME_MAZ0;if(e->timer>=30){e->flags=0;e->timer=0;}break;
    case 6:
        e->anim=GNG_MAZ_FRAME_MAZLAUGH;
        if(e->timer>=132){e->x=clamp16(boss_rewind_x,ARENA_X-64,WORLD_END);e->y=boss_rewind_y;e->flags=0;e->timer=75;}break;
    }
}
static const signed char flight_wave[32]={0,3,5,8,10,12,13,14,14,14,13,12,10,8,5,3,0,-3,-5,-8,-10,-12,-13,-14,-14,-14,-13,-12,-10,-8,-5,-3};
static void enemy_solid_sides(Enemy*e,s16 old_x,u8 body_height){
    u8 i;s16 foot=e->y+enemy_rows(e->type)*16;
    for(i=0;i<GNG_LEVEL1_SOLIDS_COUNT;++i){const short*b=GNG_LEVEL1_SOLIDS[i];
        if(foot>b[1]&&foot-body_height<GROUND_Y&&e->x+7>b[0]&&e->x-7<b[0]+b[2]){
            if(old_x+7<=b[0])e->x=b[0]-7;else if(old_x-7>=b[0]+b[2])e->x=b[0]+b[2]+7;
        }
    }
    for(i=0;i<GNG_CRATE_COUNT;++i)if(crate_hp[i]){s16 left=GNG_CRATES[i][0]-8,top=GNG_CRATES[i][1];
        if(foot>top&&foot-body_height<top+16&&e->x+7>left&&e->x-7<left+16){
            if(old_x+7<=left)e->x=left-7;else if(old_x-7>=left+16)e->x=left+23;
        }
    }
}
static void update_enemy(Enemy*e){
    u8 slot=e-enemies; s16 old_x=e->x,old_foot=e->y+enemy_rows(e->type)*16,dir=player_x<e->x?-1:1;
    s16 top,bottom,radius=18;
    if(e->state==ES_DEAD)return;
    if(enemy_flash[slot])enemy_flash[slot]--;
    if(e->state==ES_DYING){if(e->timer){e->timer--;if(e->type==ET_BAT)e->y+=2;}else e->state=ES_DEAD;return;}
    if(iabs16((s16)(enemy_x_fp[slot]/FP_ONE)-e->x)>3)enemy_x_fp[slot]=(s32)e->x*FP_ONE;
    if(iabs16((s16)(enemy_y_fp[slot]/FP_ONE)-e->y)>3)enemy_y_fp[slot]=(s32)e->y*FP_ONE;
    e->timer++;if(enemy_cooldown[slot])enemy_cooldown[slot]--;
    switch(e->type){
    case ET_ZOMBIE:
        if(e->timer<36)break;
        if(enemy_attack[slot]){
            if(enemy_attack_timer[slot])enemy_attack_timer[slot]--;
            if(enemy_attack[slot]==1&&!enemy_attack_timer[slot]){enemy_attack[slot]=2;enemy_attack_timer[slot]=27;}
            else if(enemy_attack[slot]==2){e->x+=e->vx*2;if(!enemy_attack_timer[slot]){enemy_attack[slot]=0;enemy_cooldown[slot]=252;}}
        }else{
            if((frame_counter%5)<2)e->x+=dir;
            if((e->flags&1)&&!enemy_cooldown[slot]&&iabs16(player_x-e->x)>30&&iabs16(player_x-e->x)<80){enemy_attack[slot]=1;enemy_attack_timer[slot]=27;e->vx=dir;sound(SND_SFX_TIME_TICK);}
        }
        e->anim=(e->timer/12)&3;enemy_solid_sides(e,old_x,enemy_attack[slot]==2?12:36);
        e->vy+=GRAVITY_FP;enemy_y_fp[slot]+=e->vy;e->y=enemy_y_fp[slot]/FP_ONE;
        {s16 floor=pickup_floor(e->x,old_foot,e->y+48);if(e->vy>=0&&floor<300){e->y=floor-48;enemy_y_fp[slot]=(s32)e->y*FP_ONE;e->vy=0;}}
        break;
    case ET_PUMPKIN:
        if(e->flags){e->x+=e->vx;e->vy+=GRAVITY_FP;enemy_y_fp[slot]+=e->vy;e->y=enemy_y_fp[slot]/FP_ONE;}
        else if(e->vx>0)e->vx--;
        else{e->flags=1;e->vx=dir;e->vy=-1024;enemy_y_fp[slot]=(s32)e->y*FP_ONE;}
        enemy_solid_sides(e,old_x,17);
        if(e->flags){s16 floor=pickup_floor(e->x,old_foot,e->y+32);if(e->vy>=0&&floor<300){e->y=floor-32;enemy_y_fp[slot]=(s32)e->y*FP_ONE;e->vy=0;e->flags=0;e->vx=(s16)(72+arcade_random()%31);}}
        e->anim=e->flags?3:e->vx<12?2:(e->timer/20)&1;
        break;
    case ET_BAT:
        if(e->flags==1){if(++enemy_attack_timer[slot]>=23){e->flags=2;enemy_attack_timer[slot]=0;}}
        else if(e->flags==2){enemy_x_fp[slot]+=e->vx;enemy_y_fp[slot]+=e->vy;if(++enemy_attack_timer[slot]>=31){e->flags=0;enemy_cooldown[slot]=252;e->home_y=e->y;}}
        else{
            s16 target=player_y+6;
            if((frame_counter&7)==0)e->home_y+=(target>e->home_y?1:target<e->home_y?-1:0);
            enemy_x_fp[slot]+=dir*171;enemy_y_fp[slot]=(s32)(e->home_y+flight_wave[(e->timer/3+slot*3)&31])*FP_ONE;
            if(!enemy_cooldown[slot]&&iabs16(player_x-e->x)<110){s16 dx=player_x-e->x,dy=player_y+26-(e->y+16),distance=iabs16(dx)+iabs16(dy);if(distance<1)distance=1;
                e->vx=(s16)((s32)dx*448/distance);e->vy=(s16)((s32)dy*448/distance);e->flags=1;enemy_attack_timer[slot]=0;sound(SND_SFX_TIME_TICK);
            }
        }
        e->x=enemy_x_fp[slot]/FP_ONE;e->y=enemy_y_fp[slot]/FP_ONE;e->anim=(e->timer/6)&3;break;
    case ET_GHOST:
        enemy_x_fp[slot]+=dir*102;
        enemy_y_fp[slot]+=clamp16((player_y-2-e->y)*3,-102,102)+flight_wave[(e->timer/3+slot*5)&31]*6;
        e->x=enemy_x_fp[slot]/FP_ONE;e->y=enemy_y_fp[slot]/FP_ONE;e->anim=(e->timer/15)&1;break;
    case ET_MAZ:update_boss(e);break;
    }
    if(e->y>260){e->state=ES_DEAD;return;}
    if((e->type==ET_ZOMBIE&&e->timer<36)||(e->type==ET_ZOMBIE&&enemy_attack[slot]==1)||(e->type==ET_BAT&&e->flags==1)||(e->type==ET_GHOST&&e->timer%192>=138)||(e->type==ET_MAZ&&(e->flags==6||e->flags==7)))return;
    bottom=e->y+enemy_rows(e->type)*16;top=bottom-36;
    if(e->type==ET_BAT){top=e->y+10;bottom=top+12;radius=16;}
    else if(e->type==ET_GHOST){top=e->y+14;bottom=top+20;radius=17;}
    else if(e->type==ET_PUMPKIN){top=bottom-17;radius=18;}
    else if(e->type==ET_MAZ){top=bottom-56;radius=23;}
    else radius=enemy_attack[slot]==2?23:15;
    if(!invuln&&iabs16(player_x-e->x)<radius&&player_y+GNG_PLAYER_CELL_H>top&&player_y+GNG_PLAYER_CELL_H-((player_ground&&(prev_pad&CNT_DOWN))?26:39)<bottom)damage_player();
}
static void update_enemies(void){u8 i;if(death_timer)return;for(i=0;i<MAX_ENEMIES;++i)if(enemies[i].type!=ET_NONE){
    Enemy*e=&enemies[i];
    if(e->type!=ET_MAZ&&(e->x<camera_x-128||e->x>camera_x+GNG_LOGICAL_W+384)){e->state=ES_DEAD;continue;}
    update_enemy(e);
}}
static u8 enemy_sprite_frame(Enemy*e){
    if(e->state==ES_DYING){
        if(e->type==ET_ZOMBIE)return e->timer>12?GNG_ZOMBIE_FRAME_DIE0:GNG_ZOMBIE_FRAME_DIE1;
        if(e->type==ET_BAT)return GNG_BAT_FRAME_DIE0;
        if(e->type==ET_PUMPKIN)return e->timer>12?GNG_PUMPKIN_FRAME_DIE0:GNG_PUMPKIN_FRAME_DIE1;
        if(e->type==ET_GHOST)return e->timer>12?GNG_GHOST_FRAME_DIE0:GNG_GHOST_FRAME_DIE1;
    }
    switch(e->type){
    case ET_ZOMBIE:if(e->timer<36)return GNG_ZOMBIE_FRAME_RISE0+e->timer/12;if(enemy_attack[e-enemies]==2)return GNG_ZOMBIE_FRAME_SLIDE;return GNG_ZOMBIE_FRAME_WALK0+(e->anim&3);
    case ET_BAT:return e->flags?GNG_BAT_FRAME_DIVE:GNG_BAT_FRAME_BAT0+(e->anim==3?1:e->anim);
    case ET_PUMPKIN:return GNG_PUMPKIN_FRAME_PK0+(e->anim&3)+(e->hp==1?4:0);
    case ET_GHOST:if(e->timer%192>=138)return GNG_GHOST_FRAME_FADE0+(e->anim&1);return iabs16(player_x-e->x)<54?GNG_GHOST_FRAME_ATTACK:GNG_GHOST_FRAME_GH0+(e->anim&1);
    default:return e->anim;
    }
}
static u8 ball_touches_sprite(Ball*b,Enemy*e){
    const short*box;u8 frame=enemy_sprite_frame(e);s16 left,right;u8 radius=ball_kind[b-balls]?6:4;
    switch(e->type){case ET_ZOMBIE:box=GNG_ZOMBIE_SHOTBOX[frame];break;case ET_BAT:box=GNG_BAT_SHOTBOX[frame];break;case ET_PUMPKIN:box=GNG_PUMPKIN_SHOTBOX[frame];break;case ET_GHOST:box=GNG_GHOST_SHOTBOX[frame];break;default:box=GNG_MAZ_SHOTBOX[frame];break;}
    left=e->x+(e->x>player_x?-box[2]:box[0]);right=e->x+(e->x>player_x?-box[0]:box[2]);
    return b->x+radius>left&&b->x-radius<right&&b->y+radius>e->y+box[1]&&b->y-radius<e->y+box[3];
}
static u8 enemy_vulnerable(Enemy*e){
    if(e->state!=ES_ALIVE)return 0;
    if(e->type==ET_ZOMBIE&&e->timer<36)return 0;
    if(e->type==ET_GHOST&&(e->timer%192)>=138)return 0;
    if(e->type==ET_MAZ&&(e->flags==6||e->flags==7))return 0;
    return 1;
}
static void hit_enemy(Enemy*e,s16 ball_y,u8 damage){
    u8 midas=boost_timer>0;
    if(!enemy_vulnerable(e))return;
    impact_fx(e->x,ball_y,0);enemy_flash[e-enemies]=6;
    if(e->type==ET_MAZ){
        if(ball_y<e->y+30)damage*=2;
        e->hp=e->hp>damage?e->hp-damage:0;sound(SND_SFX_BOSS_HIT);
        if(e->hp){if((boss_dreams==0&&e->hp<=21)||(boss_dreams==1&&e->hp<=10)){boss_dreams++;e->flags=6;e->timer=0;boss_rewind_x=boss_history_x[boss_history_index];boss_rewind_y=boss_history_y[boss_history_index];}return;}
        e->state=ES_DEAD;award_points(10000,e->x,e->y);stage_clear=1;return;
    }
    e->hp=e->hp>damage?e->hp-damage:0;
    if(e->hp){enemy_flash[e-enemies]=6;sound(SND_SFX_HIT);return;}
    e->state=ES_DYING;e->timer=e->type==ET_BAT?48:30;
    if(midas)e->flags|=128;
    combo_count=combo_timer?(combo_count<4?combo_count+1:4):1;combo_timer=60;kill_count++;
    award_points((e->type==ET_GHOST?300:e->type==ET_PUMPKIN?200:100)*combo_count*(midas?3:1),e->x,e->y);
    {u16 roll=arcade_random()%100;if(roll<3)add_pickup(PICKUP_POWER,e->x,e->y+8);else if(roll<9)add_pickup(PICKUP_DROP,e->x,e->y+8);}
    sound(SND_SFX_ENEMY_DIE);
}
static u8 spawn_ball_kind(u8 up,u8 down_air,u8 crouch,u8 charged){
    u8 i,count=0,cap=boost_timer?7:3;
    for(i=0;i<MAX_BALLS;++i)if(balls[i].active&&ball_kind[i]!=2)count++;
    if(!charged&&count>=cap)return 0;
    for(i=0;i<MAX_BALLS;++i)if(!balls[i].active){Ball*b=&balls[i];
        b->active=1;b->life=(boost_timer||charged)?66:50;b->bounces=0;b->x=player_x+(player_facing_right?15:-15);
        b->y=player_y+GNG_PLAYER_CELL_H/2+(up?-27:(down_air?6:(crouch?20:-2)));
        b->vx=player_facing_right?(up||down_air?682:964):-(up||down_air?682:964);
        b->vy=up?-682:(down_air?682:0);
        if(charged&&!boost_timer){b->vx=b->vx*115/100;b->vy=b->vy*115/100;}
        ball_kind[i]=boost_timer?1:charged?2:0;ball_damage[i]=ball_kind[i]?3:1;ball_pierce[i]=boost_timer?99:charged?3:0;
        ball_hits[i]=0;ball_counted[i]=0;
        b->x_fp=(s32)b->x*FP_ONE;b->y_fp=(s32)b->y*FP_ONE;shots_fired++;
        kick_timer=9;sound(SND_SFX_KICK);return 1;
    }return 0;
}
static void update_balls(void){
    if(death_timer)return;
    u8 i,j;for(i=0;i<MAX_BALLS;++i){Ball*b=&balls[i];s16 old_x=b->x,old_y=b->y;u8 bounced=0,radius=ball_kind[i]?6:4;
        if(!b->active)continue;b->x_fp+=b->vx;b->y_fp+=b->vy;
        b->x=(s16)(b->x_fp>>FP_SHIFT);b->y=(s16)(b->y_fp>>FP_SHIFT);
        if(!b->life--||b->x<0||b->x>GNG_LEVEL1_WORLD_W||b->y<-32||b->y>256){b->active=0;continue;}
        for(j=0;j<GNG_CRATE_COUNT;++j)if(crate_hp[j]&&iabs16(b->x-GNG_CRATES[j][0])<8+radius+1&&b->y+radius>GNG_CRATES[j][1]&&b->y-radius<GNG_CRATES[j][1]+16){
            hit_crate(j,i);if(ball_kind[i]!=1)b->active=0;break;
        }
        if(!b->active)continue;
        if(b->vy>=0&&old_y+radius<=GROUND_Y&&b->y+radius>=GROUND_Y&&is_ground(b->x)){b->y=GROUND_Y-radius;b->vy=-b->vy;bounced=1;}
        if(!bounced)for(j=0;j<GNG_LEVEL1_SOLIDS_COUNT;++j){const short*s=GNG_LEVEL1_SOLIDS[j];
            if(b->x+radius>s[0]&&b->x-radius<s[0]+s[2]&&b->y+radius>s[1]&&b->y-radius<GROUND_Y){
                if(old_y+radius<=s[1]){b->y=s[1]-radius;b->vy=-b->vy;}
                else if(old_x+radius<=s[0]){b->x=s[0]-radius;b->vx=-b->vx;}
                else if(old_x-radius>=s[0]+s[2]){b->x=s[0]+s[2]+radius;b->vx=-b->vx;}
                else{b->active=0;break;}bounced=1;break;
            }
        }
        if(!b->active)continue;
        if(bounced){b->x_fp=(s32)b->x*FP_ONE;b->y_fp=(s32)b->y*FP_ONE;if(++b->bounces>1){b->active=0;continue;}}
        for(j=0;j<MAX_ENEMIES;++j){Enemy*e=&enemies[j];if(e->type==ET_NONE||!enemy_vulnerable(e)||(ball_hits[i]&(1u<<j)))continue;
            if(ball_touches_sprite(b,e)){ball_hits[i]|=1u<<j;if(!ball_counted[i]){ball_counted[i]=1;shots_hit++;}hit_enemy(e,b->y,ball_damage[i]);if(ball_pierce[i])ball_pierce[i]--;else b->active=0;break;}
        }
    }
}
static void sync_player(void){player_x=(s16)(player_x_fp>>FP_SHIFT);player_y=(s16)(player_y_fp>>FP_SHIFT);}
/* Swept feet tests prevent tunnelling through six-pixel one-way planks. */
static s16 landing_floor(s16 x,s16 old_foot,s16 new_foot){
    s16 floor=300;u8 i;
    if(is_ground(x)&&old_foot<=GROUND_Y&&new_foot>=GROUND_Y)floor=GROUND_Y;
    for(i=0;i<GNG_LEVEL1_SOLIDS_COUNT;++i){const short*b=GNG_LEVEL1_SOLIDS[i];
        if(x+6>b[0]&&x-6<b[0]+b[2]&&old_foot<=b[1]&&new_foot>=b[1]&&b[1]<floor)floor=b[1];}
    if(!drop_timer)for(i=0;i<GNG_LEVEL1_PLANKS_COUNT;++i){const short*b=GNG_LEVEL1_PLANKS[i];
        if(x+6>b[0]&&x-6<b[0]+b[2]&&old_foot<=b[1]&&new_foot>=b[1]&&b[1]<floor)floor=b[1];}
    for(i=0;i<GNG_CRATE_COUNT;++i)if(crate_hp[i]){s16 left=GNG_CRATES[i][0]-8,top=GNG_CRATES[i][1];if(x+6>left&&x-6<left+16&&old_foot<=top&&new_foot>=top&&top<floor)floor=top;}
    return floor;
}
static void update_player(u8 pad){
    if(death_timer){if(!--death_timer&&lives)respawn_player();return;}
    u8 l=!!(pad&CNT_LEFT),r=!!(pad&CNT_RIGHT),up=!!(pad&CNT_UP),down=!!(pad&CNT_DOWN),i;
    u8 jump=!!((pad&CNT_A)&&!(prev_pad&CNT_A));
    u8 shoot=!!((pad&CNT_B)&&!(prev_pad&CNT_B));
    s16 old_x=player_x,old_foot=player_y+GNG_PLAYER_CELL_H,floor;
    s32 target=0,accel=player_ground?GROUND_ACCEL_FP:AIR_ACCEL_FP;
    if(drop_timer)drop_timer--;
    if(player_ground)coyote_timer=7;else if(coyote_timer)coyote_timer--;
    if(jump)jump_buffer=6;else if(jump_buffer)jump_buffer--;
    if(l){target=-RUN_SPEED_FP;player_facing_right=0;}
    if(r){target=RUN_SPEED_FP;player_facing_right=1;}
    if(boost_timer)target=target*6/5;
    if(down&&player_ground)target=0;player_vx_fp=approach32(player_vx_fp,target,accel);player_x_fp+=player_vx_fp;
    if(player_x_fp<(24L<<FP_SHIFT))player_x_fp=24L<<FP_SHIFT;
    if(player_x_fp>((s32)WORLD_END<<FP_SHIFT))player_x_fp=(s32)WORLD_END<<FP_SHIFT;
    sync_player();
    for(i=0;i<GNG_LEVEL1_SOLIDS_COUNT;++i){const short*b=GNG_LEVEL1_SOLIDS[i];
        if(old_foot>b[1]&&old_foot-39<GROUND_Y&&player_x+7>b[0]&&player_x-7<b[0]+b[2]){
            if(old_x+7<=b[0])player_x=b[0]-7;
            else if(old_x-7>=b[0]+b[2])player_x=b[0]+b[2]+7;
            else continue;
            player_x_fp=(s32)player_x<<FP_SHIFT;player_vx_fp=0;
        }
    }
    for(i=0;i<GNG_CRATE_COUNT;++i)if(crate_hp[i]){s16 left=GNG_CRATES[i][0]-8,top=GNG_CRATES[i][1];
        if(old_foot>top&&old_foot-39<top+16&&player_x+7>left&&player_x-7<left+16){
            if(old_x+7<=left)player_x=left-7;else if(old_x-7>=left+16)player_x=left+23;else continue;
            player_x_fp=(s32)player_x*FP_ONE;player_vx_fp=0;
        }
    }
    if(jump&&down&&player_ground&&old_foot<GROUND_Y){
        for(i=0;i<GNG_LEVEL1_PLANKS_COUNT;++i){const short*b=GNG_LEVEL1_PLANKS[i];
            if(old_foot==b[1]&&player_x>=b[0]&&player_x<b[0]+b[2]){drop_timer=16;jump_buffer=0;coyote_timer=0;jump=0;break;}}
    }
    if(jump_buffer&&coyote_timer){player_vy_fp=JUMP_VEL_FP;jump_buffer=0;coyote_timer=0;}
    else if(player_ground)player_vy_fp=0;
    player_ground=0;player_vy_fp+=GRAVITY_FP;
    if(player_vy_fp>MAX_FALL_FP)player_vy_fp=MAX_FALL_FP;
    if(!(pad&CNT_A)&&player_vy_fp<RELEASE_CUT_FP)player_vy_fp/=2;
    player_y_fp+=player_vy_fp;sync_player();
    if(player_vy_fp<0)for(i=0;i<GNG_CRATE_COUNT;++i)if(crate_hp[i]){
        s16 left=GNG_CRATES[i][0]-8,bottom=GNG_CRATES[i][1]+16;
        if(player_x+7>left&&player_x-7<left+16&&old_foot-39>=bottom&&player_y+GNG_PLAYER_CELL_H-39<=bottom){player_y_fp=(s32)(bottom-(GNG_PLAYER_CELL_H-39))*FP_ONE;player_vy_fp=0;sync_player();}
    }
    floor=landing_floor(player_x,old_foot,player_y+GNG_PLAYER_CELL_H);
    if(player_vy_fp>=0&&floor<300){player_y_fp=(s32)(floor-GNG_PLAYER_CELL_H)*FP_ONE;player_vy_fp=0;player_ground=1;sync_player();}
    if(player_y>224){invuln=0;lose_life();sync_player();}
    if(shoot)shot_buffer=13;
    if(shot_buffer){if(spawn_ball_kind(up,down&&!player_ground,down&&player_ground,0))shot_buffer=0;else shot_buffer--;}
    if(pad&CNT_B){
        if(boost_timer){charge_timer=0;if(++gold_auto_tick>=9){spawn_ball_kind(up,down&&!player_ground,down&&player_ground,0);gold_auto_tick=0;}}
        else{gold_auto_tick=0;if(charge_timer<255)charge_timer++;if(charge_timer==39)sound(SND_SFX_CHARGE);}
    }else{if(charge_timer>=39&&!boost_timer)spawn_ball_kind(up,down&&!player_ground,down&&player_ground,1);charge_timer=0;gold_auto_tick=0;}

    if(hurt_timer)hurt_timer--;if(kick_timer)kick_timer--;if(invuln)invuln--;
}
static void update_camera(void){s16 desired=(s16)(player_x-128+(player_facing_right?30:-30));desired=clamp16(desired,0,CAMERA_MAX);camera_x_fp+=(((s32)desired<<FP_SHIFT)-camera_x_fp)/5;camera_x=(s16)(camera_x_fp>>FP_SHIFT);}
static u8 player_frame(u8 moving){if(death_timer)return death_timer>22?GNG_PLAYER_FRAME_KO0:GNG_PLAYER_FRAME_KO1;if(hurt_timer)return GNG_PLAYER_FRAME_HURT;if(player_ground&&(prev_pad&CNT_DOWN))return kick_timer?GNG_PLAYER_FRAME_CROUCHKICK:GNG_PLAYER_FRAME_CROUCH;if(kick_timer)return GNG_PLAYER_FRAME_KICK;if(!player_ground)return player_vy_fp<0?GNG_PLAYER_FRAME_JUMP:GNG_PLAYER_FRAME_FALL;if(moving)return(u8)(GNG_PLAYER_FRAME_RUN0+((frame_counter/6)&3));if(player_ground&&(prev_pad&CNT_UP))return GNG_PLAYER_FRAME_UP;return(u8)(GNG_PLAYER_FRAME_IDLE0+((frame_counter>>5)&1));}
static void draw_player(u8 pf){
    s16 sx=px_fp(player_x_fp-camera_x_fp-(32L<<FP_SHIFT));
    if(invuln&&!death_timer&&(frame_counter&2)){hide_range(SCB_PLAYER,GNG_PLAYER_FRAME_COLS);return;}
    select_player_palette(boost_timer>0);
    if(((boost_timer&&boost_timer<120)||charge_timer>=39)&&(frame_counter&8))actor_tint=97;
    if(boost_timer)draw_actor(SCB_PLAYER,GNG_PLAYER_GOLD_TILE,GNG_PLAYER_GOLD_COLS,GNG_PLAYER_GOLD_PALMAP,pf,GNG_PLAYER_GOLD_FRAME_COLS,GNG_PLAYER_GOLD_FRAME_ROWS,sx,player_y,player_facing_right?0:1);
    else if(armored)draw_actor(SCB_PLAYER,GNG_PLAYER_TILE,GNG_PLAYER_COLS,GNG_PLAYER_PALMAP,pf,GNG_PLAYER_FRAME_COLS,GNG_PLAYER_FRAME_ROWS,sx,player_y,player_facing_right?0:1);
    else draw_actor(SCB_PLAYER,GNG_PLAYER_NAKED_TILE,GNG_PLAYER_NAKED_COLS,GNG_PLAYER_NAKED_PALMAP,pf,GNG_PLAYER_NAKED_FRAME_COLS,GNG_PLAYER_NAKED_FRAME_ROWS,sx,player_y,player_facing_right?0:1);
    actor_tint=255;
}
static void draw_enemy_slot(u8 slot,Enemy*e){
    u16 scb=SCB_ENEMY_BASE+slot*ENEMY_STRIDE,tile,cols,width;u8 fc,fr,frame;const unsigned char*pm;
    s16 sx=px_fp((s32)(e->x-camera_x)*FP_ONE);
    if(e->type==ET_NONE||e->state==ES_DEAD||sx<-140||sx>340){hide_range(scb,ENEMY_STRIDE);return;}
    frame=enemy_sprite_frame(e);
#define SPR(N) tile=GNG_##N##_TILE;cols=GNG_##N##_COLS;pm=GNG_##N##_PALMAP;fc=GNG_##N##_FRAME_COLS;fr=GNG_##N##_FRAME_ROWS;width=GNG_##N##_CELL_W
    switch(e->type){case ET_ZOMBIE:SPR(ZOMBIE);break;case ET_BAT:SPR(BAT);break;case ET_PUMPKIN:SPR(PUMPKIN);break;case ET_GHOST:SPR(GHOST);break;default:SPR(MAZ);break;}
#undef SPR
    actor_tint=(e->state==ES_DYING&&(e->flags&128))||(e->type==ET_ZOMBIE&&enemy_attack[slot]==1)||(e->type==ET_BAT&&e->flags==1)?98:(enemy_flash[slot]&&(frame_counter&1))?97:255;
    draw_actor(scb,tile,cols,pm,frame,fc,fr,sx-width/2,e->y,e->x>player_x?1:0);actor_tint=255;
}
static void draw_balls(void){u8 i;for(i=0;i<MAX_BALLS;++i){Ball*b=&balls[i];u16 scb=SCB_BALL0+i*2;
    if(!b->active){hide_strip(scb);hide_strip(scb+1);continue;}
    if(ball_kind[i])draw_small(scb,GNG_GOLD_BALL_TILE,GNG_GOLD_BALL_COLS,GNG_GOLD_BALL_PALMAP,(b->life/3)&3,2,2,px_fp((s32)(b->x-camera_x)*FP_ONE)-16,b->y-16);
    else draw_small(scb,GNG_BALL_TILE,GNG_BALL_COLS,GNG_BALL_PALMAP,(b->life/3)&3,1,1,px_fp((s32)(b->x-camera_x)*FP_ONE)-8,b->y-8);
}}
static void draw_hazards(void){
    u8 i;for(i=0;i<MAX_HAZARDS;++i){Hazard*h=&hazards[i];u16 scb=SCB_HAZARD_BASE+i*ENEMY_STRIDE;
        if(!h->life){hide_range(scb,ENEMY_STRIDE);continue;}
        if(h->kind<=2){
            u8 frame=h->kind==1?GNG_MAZFX_FRAME_SHOE:(u8)(GNG_MAZFX_FRAME_LAUGH0+((h->life/6)&1));
            draw_actor(scb,GNG_MAZFX_TILE,GNG_MAZFX_COLS,GNG_MAZFX_PALMAP,frame,GNG_MAZFX_FRAME_COLS,GNG_MAZFX_FRAME_ROWS,px_fp(h->x_fp-camera_x_fp)-GNG_MAZFX_CELL_W/2,(s16)(h->y_fp>>FP_SHIFT)-GNG_MAZFX_CELL_H/2,h->vx<0);
        }else draw_actor(scb,GNG_WAVE_TILE,GNG_WAVE_COLS,GNG_WAVE_PALMAP,0,GNG_WAVE_COLS,GNG_WAVE_ROWS,px_fp(h->x_fp-camera_x_fp)-GNG_WAVE_W/2,(s16)(h->y_fp>>FP_SHIFT)-GNG_WAVE_H/2,0);
    }
}
static void draw_particles(void){
    u8 i;
    for(i=0;i<8;++i){VisualParticle*p=&particles[i];u16 scb=284+i;u8 frame;
        if(!p->life){hide_strip(scb);continue;}
        frame=(u8)((12-p->life)/3+(p->dust?4:0));p->life--;
        *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;
        *REG_VRAMRW=(u16)(GNG_PARTICLES_TILE+frame);*REG_VRAMRW=(u16)(GNG_PARTICLES_PALMAP[frame]<<8);
        position_strip(scb,1,px_fp((s32)(p->x-camera_x)*FP_ONE)-8,p->y-8);
    }
}
static void hud(void){
    static u32 old_score=~0UL;static u16 old_time=65535,old_status=65535;
    static u8 old_lives=255,old_pumpkins=255,old_armor=255,old_hp=255;
    char s[44];u8 hp=0,i;u16 status=boost_timer?100+(boost_timer+59)/60:combo_timer&&combo_count>1?combo_count:0;
    if(hud_dirty){old_score=~0UL;old_time=old_status=65535;old_lives=old_pumpkins=old_armor=old_hp=255;hud_dirty=0;}
    if(old_score!=score||old_lives!=lives){sprintf(s,"SCORE %06lu  x%02u",(unsigned long)score,lives);ng_text(1,3,0,s);old_score=score;old_lives=lives;}
    if(old_time!=level_time){sprintf(s,"TIME %03u",level_time);ng_text(30,3,0,s);old_time=level_time;}
    if(old_pumpkins!=pumpkins_collected||old_armor!=armored){sprintf(s,"%s   PUMPKINS %02u  ",armored?"SHIRT":"NAKED",pumpkins_collected);ng_text(1,4,0,s);old_pumpkins=pumpkins_collected;old_armor=armored;}
    if(old_status!=status){if(boost_timer)sprintf(s,"RE MIDA %02u ",(boost_timer+59)/60);else if(status)sprintf(s,"COMBO x%u   ",combo_count);else sprintf(s,"            ");ng_text(27,4,0,s);old_status=status;}
    if(boss_spawned&&!stage_clear){for(i=0;i<MAX_ENEMIES;++i)if(enemies[i].type==ET_MAZ&&enemies[i].state==ES_ALIVE)hp=enemies[i].hp;if(old_hp!=hp){sprintf(s,"MAZAPEGUL %02u/32",hp);ng_text(22,5,0,s);old_hp=hp;}}
    else if(old_hp!=255){ng_text(22,5,0,"                ");old_hp=255;}
}
static void reset_game(void){u8 i;reset_content();for(i=0;i<8;++i)particles[i].life=0;particle_cursor=0;score=0;lives=3;armored=1;invuln=0;stage_clear=0;boss_spawned=0;checkpoint_reached=0;zombie_timer=180;music_timer=0;wave_mask=0;clear_enemies();for(i=0;i<MAX_BALLS;++i)balls[i].active=0;for(i=0;i<MAX_HAZARDS;++i)hazards[i].life=0;player_x_fp=48L<<FP_SHIFT;player_y_fp=(s32)PLAYER_CELL_Y<<FP_SHIFT;player_vx_fp=player_vy_fp=0;sync_player();player_ground=1;player_facing_right=1;kick_timer=0;drop_timer=0;coyote_timer=0;jump_buffer=0;camera_x_fp=0;camera_x=0;prev_pad=0;frame_counter=0;}
static void title_screen(void){u16 t=0;char record[32];{u16 i;for(i=0;i<384;++i)strip_hidden[i]=actor_columns[i]=0;}ng_cls();hud_dirty=1;hide_range(0,380);set_backdrop(0x8000);init_palettes();reset_sound();sound(SND_SFX_INTRO);for(;;){ng_wait_vblank();select_scene_palette(0);select_stadium_palette(0);draw_layer(SCB_SKY,GNG_SKY_TILE,GNG_SKY_COLS,GNG_SKY_ROWS,GNG_SKY_PALMAP,((s32)(t%240)<<FP_SHIFT)/8,0);draw_positioned(SCB_MID,GNG_STADIUM_BACK_TILE,GNG_STADIUM_BACK_COLS,GNG_STADIUM_BACK_ROWS,GNG_STADIUM_BACK_PALMAP,-40,0);draw_positioned(SCB_FOREGROUND,GNG_TITLE_TILE,GNG_TITLE_COLS,GNG_TITLE_ROWS,GNG_TITLE_PALMAP,0,24+(t/54%2));{u8 i;for(i=0;i<4;++i)draw_actor(SCB_ENEMY_BASE+i*ENEMY_STRIDE,GNG_GHOST_TILE,GNG_GHOST_COLS,GNG_GHOST_PALMAP,(t/18)&1,GNG_GHOST_FRAME_COLS,GNG_GHOST_FRAME_ROWS,(s16)(12+i*84),(s16)(12+(i%2)*104+((t/12+i*3)%12)),i&1);}ng_center_text(19,2,"PRESS START");ng_center_text(22,0,"FULL LEVEL 1 R1");ng_center_text(23,0,"LEVEL 1 CONTENT R4");if((t&31)<16)ng_center_text(25,1,"5 COIN  /  1 START");else ng_center_text(25,1,"                    ");sprintf(record,"HI SCORE %06lu",(unsigned long)(high_score_guard==0x474e4734UL?high_score:0));ng_center_text(28,0,record);if(start_requested||bios_user_mode==USER_MODE_GAME)break;t++;if(t>=GNG_INTRO_FRAMES){sound(SND_SFX_INTRO);t=0;}}reset_sound();start_requested=0;}
static u32 clear_time_bonus,clear_accuracy_bonus,clear_nohit_bonus;
static void stage_clear_screen(void){
    u16 t=0;u8 i;char text[40];
    boost_timer=0;select_player_palette(0);
    clear_time_bonus=(u32)level_time*10;
    clear_accuracy_bonus=shots_fired?((u32)shots_hit*5000+shots_fired/2)/shots_fired:0;
    if(clear_accuracy_bonus>5000)clear_accuracy_bonus=5000;
    clear_nohit_bonus=took_damage?0:10000;
    for(i=0;i<MAX_BALLS;++i)balls[i].active=0;
    for(i=0;i<MAX_HAZARDS;++i)hazards[i].life=0;
    hide_range(SCB_ENEMY_BASE,MAX_ENEMIES*ENEMY_STRIDE);draw_balls();draw_hazards();ng_cls();hud_dirty=1;
    while(t<420){u8 pf;
        if(player_x<CAMERA_MAX+128){player_x_fp+=RUN_SPEED_FP;sync_player();pf=GNG_PLAYER_FRAME_RUN0+((frame_counter/6)&3);}
        else pf=GNG_PLAYER_FRAME_CHEER0+((frame_counter/12)&1);
        update_camera();ng_wait_vblank();draw_background();draw_player(pf);hud();
        if(t<90){
            Enemy*boss=&enemies[0];
            if(t<66){draw_actor(SCB_ENEMY_BASE,GNG_MAZ_TILE,GNG_MAZ_COLS,GNG_MAZ_PALMAP,GNG_MAZ_FRAME_MAZCAPLESS,GNG_MAZ_FRAME_COLS,GNG_MAZ_FRAME_ROWS,px_fp((s32)(boss->x-camera_x)*FP_ONE)-GNG_MAZ_CELL_W/2+(t>36?(t-36):0),boss->y,1);
                draw_actor(SCB_HAZARD_BASE,GNG_MAZFX_TILE,GNG_MAZFX_COLS,GNG_MAZFX_PALMAP,GNG_MAZFX_FRAME_CAP,GNG_MAZFX_FRAME_COLS,GNG_MAZFX_FRAME_ROWS,px_fp((s32)(boss->x-camera_x)*FP_ONE)+t/2,boss->y-20-t/2,0);
            }else{hide_range(SCB_ENEMY_BASE,ENEMY_STRIDE);hide_range(SCB_HAZARD_BASE,ENEMY_STRIDE);}
            ng_center_text(12,2,"CAPPELLO RUBATO!");
        }else{
            if(t==90){ng_cls();hud_dirty=1;draw_results_panel();}ng_center_text_tall(9,0,"STAGE CLEAR");ng_center_text(13,2,"DAI BURDEL!");
            if(t==120)award_points(clear_time_bonus,0,-1);
            if(t>=120){sprintf(text,"TIME BONUS       %05lu",(unsigned long)(t<168?clear_time_bonus*(t-120)/48:clear_time_bonus));ng_center_text(17,0,text);}
            if(t==180)award_points(clear_accuracy_bonus,0,-1);
            if(t>=180){sprintf(text,"ACCURACY BONUS   %05lu",(unsigned long)(t<228?clear_accuracy_bonus*(t-180)/48:clear_accuracy_bonus));ng_center_text(19,0,text);}
            if(t==240)award_points(clear_nohit_bonus,0,-1);
            if(t>=240){sprintf(text,"NO DAMAGE BONUS  %05lu",(unsigned long)(t<288?clear_nohit_bonus*(t-240)/48:clear_nohit_bonus));ng_center_text(21,0,text);}
            if(t>=120&&t<288&&t%6==0)sound(SND_SFX_TIME_TICK);
            if(t>=300){sprintf(text,"TOTAL %06lu",(unsigned long)score);ng_center_text(24,2,text);ng_center_text(27,0,"NEXT: BONUS RIGORI");}
        }
        t++;frame_counter++;
    }
}
#include "src/level1_penalty.h"
static void game_over_screen(void){u16 t=180;while(t--){ng_wait_vblank();if(death_timer>1)death_timer--;draw_player(death_timer>22?GNG_PLAYER_FRAME_KO0:GNG_PLAYER_FRAME_KO1);ng_center_text_tall(12,0,"GAME OVER");}}
static void game_loop(void){reset_game();ng_cls();hud_dirty=1;set_backdrop(0x8000);init_palettes();sound(SND_SFX_LEVEL_LOOP);while(lives&&!stage_clear){u8 pad=bios_p1current,moving=!!(pad&(CNT_LEFT|CNT_RIGHT));u8 pf,was_ground=player_ground;update_player(pad);if(!was_ground&&player_ground)impact_fx(player_x,player_y+GNG_PLAYER_CELL_H-5,1);update_content();update_camera();update_spawns();update_enemies();update_balls();update_hazards(pad);pf=player_frame(moving);if(music_timer++>=(boost_timer?GNG_BOOST_LOOP_FRAMES:boss_spawned?GNG_BOSS_LOOP_FRAMES:GNG_LEVEL_LOOP_FRAMES)){sound(boost_timer?SND_SFX_BOOST_LOOP:boss_spawned?SND_SFX_BOSS_LOOP:SND_SFX_LEVEL_LOOP);music_timer=0;}ng_wait_vblank();draw_background();draw_player(pf);{u8 i;for(i=0;i<MAX_ENEMIES;++i)draw_enemy_slot(i,&enemies[i]);}draw_balls();draw_hazards();draw_content();draw_weather();draw_particles();draw_score_popups();draw_atmosphere();hud();if(frame_counter==0||frame_counter==45){ng_center_text(11,2,"LEVEL 1 - NOTTE AL MANUZZI");ng_center_text(13,0,frame_counter<45?"PRONTI?":"VIA!");}else if(frame_counter==90){ng_text(1,11,0,"                                      ");ng_text(1,13,0,"                                      ");}prev_pad=pad;frame_counter++;}hide_range(276,104);if(stage_clear){reset_sound();sound(SND_SFX_STAGE_CLEAR);stage_clear_screen();penalty_bonus();}else{reset_sound();game_over_screen();}reset_sound();}
void player_start(void){bios_user_mode=USER_MODE_GAME;bios_player_mod1=1;start_requested=1;}
void coin_sound(void){}
int main_mvs_title(void){title_screen();game_loop();return 0;}
int main(void){title_screen();if(bios_user_mode==USER_MODE_GAME)game_loop();return 0;}
