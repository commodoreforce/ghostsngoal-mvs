#ifndef GNG_LEVEL1_CONTENT_H
#define GNG_LEVEL1_CONTENT_H

/* Complete Shpendi Level 1 content. World placements come from the pinned web
 * data, while fixed-size pools bound RAM and sprite use on the MVS. */
#define MAX_PICKUPS 36
#define PICKUP_PUMPKIN 1
#define PICKUP_GOLD 2
#define PICKUP_SHIRT 3
#define PICKUP_LIFE 4
#define PICKUP_POWER 5
#define PICKUP_DROP 6
typedef struct {
    s32 x_fp,y_fp;
    s16 vx,vy,base_y;
    u16 life;
    u8 kind,floating,source,active;
} Pickup;
typedef struct {s16 x,y;u8 life;char text[8];} ScorePopup;
static Pickup pickups[MAX_PICKUPS];
static u8 crate_hp[GNG_CRATE_COUNT],crate_flash[GNG_CRATE_COUNT];
static ScorePopup score_popups[4];
static u8 popup_cursor,combo_count,took_damage,level1_complete,gold_auto_tick,death_timer,hurt_timer;
static u16 light_clock,storm_countdown;
static u8 lightning_timer;
static u8 level_dark;
static u16 level_time,time_tick,boost_timer,combo_timer,charge_timer,shot_buffer;
static u16 shots_fired,shots_hit,kill_count,crates_broken,pumpkins_collected,powers_collected;
static u32 next_life;
static u32 _backup_ram high_score,high_score_guard;
static u16 rng_state=0xc35e;
static u8 ball_kind[MAX_BALLS],ball_damage[MAX_BALLS],ball_pierce[MAX_BALLS],ball_counted[MAX_BALLS];
static u16 ball_hits[MAX_BALLS];
static void lose_life(void);

static u16 arcade_random(void){rng_state^=rng_state<<7;rng_state^=rng_state>>9;rng_state^=rng_state<<8;return rng_state;}
static void score_popup(s16 x,s16 y,u32 value){
    ScorePopup*p=&score_popups[popup_cursor];p->x=x;p->y=y;p->life=48;
    sprintf(p->text,"%lu",(unsigned long)value);popup_cursor=(popup_cursor+1)&3;
}
static void award_points(u32 value,s16 x,s16 y){
    if(!value)return;score+=value;
    if(score>high_score)high_score=score;
    while(score>=next_life){if(lives<99)lives++;next_life=next_life==20000?60000:next_life+100000;sound(SND_SFX_ONE_UP);}
    if(y>=0)score_popup(x,y,value);
}
/* Compact atlas renderer: exactly the advertised columns, never eight-strip
 * actor padding. Each pickup/ball slot owns at most two strips. */
static void draw_small(u16 first,u16 tile,u16 cols,const unsigned char*pm,u8 frame,u8 fc,u8 fr,s16 sx,s16 y){
    u8 c,r;
    for(c=0;c<2;++c){
        u16 scb=first+c;
        if(c>=fc){hide_strip(scb);continue;}
        if(actor_columns[scb]!=(u16)((tile+frame*fc+c)*2)){
            *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;
            for(r=0;r<fr;++r){u16 index=r*cols+frame*fc+c;*REG_VRAMRW=tile+index;*REG_VRAMRW=pm[index]<<8;}
            actor_columns[scb]=(tile+frame*fc+c)*2;
        }
        position_strip(scb,fr,sx+c*16,y);
    }
}
static void select_player_palette(u8 gold){
    if(gold_palette==gold)return;
    if(gold)load_set(GNG_PLAYER_GOLD_PAL_FIRST,&GNG_PLAYER_GOLD_PALETTES[0][0],GNG_PLAYER_GOLD_PAL_COUNT);
    else load_set(GNG_PLAYER_PAL_FIRST,&GNG_PLAYER_PALETTES[0][0],GNG_PLAYER_PAL_COUNT);
    gold_palette=gold;
}
static void add_pickup(u8 kind,s16 x,s16 y){
    u8 i;for(i=GNG_PUMPKIN_COUNT;i<MAX_PICKUPS;++i)if(!pickups[i].active){
        Pickup*p=&pickups[i];p->x_fp=(s32)x*FP_ONE;p->y_fp=(s32)y*FP_ONE;
        p->base_y=y-8;p->vx=(s16)(arcade_random()%257)-128;p->vy=-768;
        p->kind=kind;p->floating=kind==PICKUP_POWER;p->life=p->floating?600:540;p->active=1;p->source=255;return;
    }
}
static void collect_pickup(Pickup*p){
    switch(p->kind){
    case PICKUP_PUMPKIN:award_points(200,p->x_fp/FP_ONE,p->y_fp/FP_ONE);pumpkins_collected++;sound(SND_SFX_PICKUP);break;
    case PICKUP_DROP:award_points(500,p->x_fp/FP_ONE,p->y_fp/FP_ONE);pumpkins_collected++;sound(SND_SFX_PICKUP);break;
    case PICKUP_GOLD:award_points(5000,p->x_fp/FP_ONE,p->y_fp/FP_ONE);pumpkins_collected++;sound(SND_SFX_PICKUP);break;
    case PICKUP_SHIRT:if(armored)award_points(1000,p->x_fp/FP_ONE,p->y_fp/FP_ONE);else{armored=1;invuln=60;}sound(SND_SFX_POWER);break;
    case PICKUP_LIFE:if(lives<99)lives++;sound(SND_SFX_ONE_UP);break;
    case PICKUP_POWER:boost_timer=660;music_timer=0;sound(SND_SFX_BOOST_LOOP);powers_collected++;sound(SND_SFX_POWER);impact_fx(player_x,player_y+32,0);break;
    }
    p->active=0;
}
static s16 pickup_floor(s16 x,s16 old_y,s16 new_y){
    s16 floor=300;u8 i;
    if(is_ground(x)&&old_y<=GROUND_Y&&new_y>=GROUND_Y)floor=GROUND_Y;
    for(i=0;i<GNG_LEVEL1_SOLIDS_COUNT;++i){const short*b=GNG_LEVEL1_SOLIDS[i];if(x>=b[0]&&x<b[0]+b[2]&&old_y<=b[1]&&new_y>=b[1]&&b[1]<floor)floor=b[1];}
    for(i=0;i<GNG_LEVEL1_PLANKS_COUNT;++i){const short*b=GNG_LEVEL1_PLANKS[i];if(x>=b[0]&&x<b[0]+b[2]&&old_y<=b[1]&&new_y>=b[1]&&b[1]<floor)floor=b[1];}
    return floor;
}
static void update_content(void){
    u8 i;if(death_timer)return;
    if(player_x>124*16&&player_x<240*16)light_clock++;else light_clock=0;
    if(lightning_timer)lightning_timer--;
    if(storm_countdown)storm_countdown--;else{lightning_timer=12;storm_countdown=300+arcade_random()%420;sound(SND_SFX_THUNDER);}
    if(boost_timer&&!--boost_timer){music_timer=0;sound(boss_spawned?SND_SFX_BOSS_LOOP:SND_SFX_LEVEL_LOOP);}
    if(combo_timer)combo_timer--;else combo_count=0;
    if(++time_tick>=60){time_tick=0;if(level_time)level_time--;if(level_time&&level_time<=10)sound(SND_SFX_TIME_TICK);if(!level_time){invuln=0;lose_life();level_time=checkpoint_reached?180:150;}}
    for(i=0;i<GNG_CRATE_COUNT;++i)if(crate_flash[i])crate_flash[i]--;
    for(i=0;i<MAX_PICKUPS;++i){Pickup*p=&pickups[i];s16 x,y,old_y,floor;
        if(!p->active)continue;
        if(p->life&&!--p->life){p->active=0;continue;}
        old_y=p->y_fp/FP_ONE;
        if(!p->floating){
            p->x_fp+=p->vx;p->y_fp+=p->vy;p->vy+=GRAVITY_FP;
            x=p->x_fp/FP_ONE;y=p->y_fp/FP_ONE;floor=pickup_floor(x,old_y+7,y+7);
            if(p->vy>0&&floor<300){p->y_fp=(s32)(floor-7)*FP_ONE;p->vy=p->vy>200?-p->vy/3:0;p->vx=p->vx*3/4;}
        }
        x=p->x_fp/FP_ONE;y=p->floating?p->base_y+((frame_counter/8+i)%8<4?1:-1):p->y_fp/FP_ONE;
        if(y>250||x<0||x>GNG_LEVEL1_WORLD_W){p->active=0;continue;}
        if(iabs16(x-player_x)<17&&y+8>player_y+GNG_PLAYER_CELL_H-39&&y-8<player_y+GNG_PLAYER_CELL_H)collect_pickup(p);
    }
}
static void hit_crate(u8 index,u8 ball){
    if(!crate_hp[index])return;
    if(ball_kind[ball]==1)crate_hp[index]=0;
    else crate_hp[index]=crate_hp[index]>ball_damage[ball]?crate_hp[index]-ball_damage[ball]:0;
    crate_flash[index]=6;sound(SND_SFX_CRATE);impact_fx(GNG_CRATES[index][0],GNG_CRATES[index][1]+8,1);
    if(!crate_hp[index]){crates_broken++;award_points(100,GNG_CRATES[index][0],GNG_CRATES[index][1]);add_pickup(GNG_CRATES[index][2],GNG_CRATES[index][0],GNG_CRATES[index][1]+4);}
}
static void draw_content(void){
    u8 i,slot=0;
    if(checkpoint_reached&&iabs16(CHECKPOINT_X-camera_x-128)<160)draw_small(90,GNG_FLAG_TILE,GNG_FLAG_COLS,GNG_FLAG_PALMAP,0,1,2,px_fp((s32)(CHECKPOINT_X-6-camera_x)*FP_ONE),176);
    else{hide_strip(90);hide_strip(91);}
    for(i=0;i<GNG_CRATE_COUNT;++i){s16 x=GNG_CRATES[i][0]-camera_x;
        if(!crate_hp[i]||x<-24||x>280)continue;
        if(crate_flash[i]&1)x++;
        draw_small(292+slot*2,GNG_ITEMS_TILE,GNG_ITEMS_COLS,GNG_ITEMS_PALMAP,(u8)(8-crate_hp[i]),2,2,px_fp((s32)x*FP_ONE)-16,GNG_CRATES[i][1]-8);slot++;
    }
    for(i=0;i<MAX_PICKUPS&&slot<16;++i){Pickup*p=&pickups[i];s16 x=p->x_fp/FP_ONE-camera_x,y;u8 frame;
        if(!p->active||x<-24||x>280||(p->life&&p->life<120&&(frame_counter&4)))continue;
        y=p->floating?p->base_y+((frame_counter/8+i)%8<4?1:-1):p->y_fp/FP_ONE;
        frame=p->kind==PICKUP_DROP?0:p->kind-1;
        draw_small(292+slot*2,GNG_ITEMS_TILE,GNG_ITEMS_COLS,GNG_ITEMS_PALMAP,frame,2,2,px_fp((s32)x*FP_ONE)-16,y-16);slot++;
    }
    while(slot<16){hide_strip(292+slot*2);hide_strip(293+slot*2);slot++;}
}
static void draw_results_panel(void){
    u8 c,r;
    for(c=0;c<18;++c){u16 scb=276+c;
        *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;
        for(r=0;r<11;++r){*REG_VRAMRW=GNG_PANEL_TILE;*REG_VRAMRW=GNG_PANEL_PALMAP[0]<<8;}
        position_strip(scb,11,16+c*16,40);
    }
}
static void draw_score_popups(void){
    u8 i,c;
    for(i=0;i<4;++i){ScorePopup*p=&score_popups[i];u8 length=0;
        if(p->life){p->life--;while(p->text[length]&&length<7)length++;}
        for(c=0;c<8;++c){u16 scb=324+i*8+c;
            if(!p->life||c>=length){hide_strip(scb);continue;}
            if(actor_columns[scb]!=(u16)((GNG_SCOREPOP_TILE+p->text[c]-'0')*2)){
                *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+scb*64;
                *REG_VRAMRW=GNG_SCOREPOP_TILE+(p->text[c]-'0');*REG_VRAMRW=GNG_SCOREPOP_PALMAP[0]<<8;
                actor_columns[scb]=(GNG_SCOREPOP_TILE+p->text[c]-'0')*2;
            }
            position_strip(scb,1,px_fp((s32)(p->x-camera_x)*FP_ONE)+c*8-length*4,p->y-(48-p->life)/4);
        }
    }
}
static void reset_content(void){
    u8 i;light_clock=level_dark=lightning_timer=0;storm_countdown=210;if(high_score_guard!=0x474e4734UL||high_score>9999999){high_score=0;high_score_guard=0x474e4734UL;}level_time=150;time_tick=boost_timer=combo_timer=charge_timer=shot_buffer=0;
    combo_count=took_damage=level1_complete=gold_auto_tick=death_timer=hurt_timer=0;next_life=20000;popup_cursor=0;rng_state=0xc35e;
    shots_fired=shots_hit=kill_count=crates_broken=pumpkins_collected=powers_collected=0;
    for(i=0;i<GNG_CRATE_COUNT;++i){crate_hp[i]=3;crate_flash[i]=0;}
    for(i=0;i<MAX_PICKUPS;++i)pickups[i].active=0;
    for(i=0;i<GNG_PUMPKIN_COUNT;++i){Pickup*p=&pickups[i];p->x_fp=(s32)GNG_PUMPKINS[i][0]*FP_ONE;p->y_fp=(s32)GNG_PUMPKINS[i][1]*FP_ONE;p->base_y=GNG_PUMPKINS[i][1];p->life=0;p->floating=1;p->kind=PICKUP_PUMPKIN;p->source=i;p->active=1;}
    for(i=0;i<4;++i)score_popups[i].life=0;
}
#endif
