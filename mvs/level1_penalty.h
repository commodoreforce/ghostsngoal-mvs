#ifndef GNG_LEVEL1_PENALTY_H
#define GNG_LEVEL1_PENALTY_H

/* Inter-stage bonus: five penalties under the Curva Mare. B locks the moving
 * target, then B locks power. The keeper learns the preferred shooting side. */
static u8 penalty_state,penalty_kick,penalty_goals,penalty_streak,penalty_aim,penalty_power;
static u16 penalty_timer;
static u32 penalty_bonus_score;
static void penalty_bonus(void){
    u16 f=0;u8 previous=0,history[3]={0,0,0},keeper_plan=1,target_side=1,goal=0;
    s16 target_x=128,target_y=110,ball_x=128,ball_y=194;char text[48];
    u8 i,kicker_frame=GNG_PEN_KICKER_FRAME_IDLE0,keeper_frame=GNG_PEN_KEEPER_FRAME_READY0;
    penalty_state=0;penalty_kick=penalty_goals=penalty_streak=0;penalty_timer=0;penalty_bonus_score=0;
    hide_range(0,380);ng_cls();for(i=0;i<84;++i)layer_columns[i]=0;scene_palette=255;
    load_set(GNG_PEN_KEEPER_PAL_FIRST,&GNG_PEN_KEEPER_PALETTES[0][0],GNG_PEN_KEEPER_PAL_COUNT);
    load_set(GNG_PEN_KICKER_PAL_FIRST,&GNG_PEN_KICKER_PALETTES[0][0],GNG_PEN_KICKER_PAL_COUNT);
    while(!level1_complete){
        u8 pad=bios_p1current,press=(pad&CNT_B)&&!(previous&CNT_B),bg=(f/8)&3;
        if(penalty_state==0&&penalty_timer>=120){penalty_state=1;penalty_timer=0;}
        if(penalty_state==1){
            u16 phase=penalty_timer%160;penalty_aim=phase<80?phase*100/80:(160-phase)*100/80;
            target_x=61+(s16)penalty_aim*134/100;target_y=96+(penalty_timer/3)%34;
            if(press||penalty_timer>=900){target_side=penalty_aim<38?0:penalty_aim>62?2:1;keeper_plan=1;
                for(i=0;i<3;++i)if(history[i]>history[keeper_plan])keeper_plan=i;
                if(!penalty_kick)keeper_plan=arcade_random()%3;
                penalty_state=2;penalty_timer=0;}
        }else if(penalty_state==2){
            u16 phase=penalty_timer%120;penalty_power=phase<60?phase*100/60:(120-phase)*100/60;
            if(press||penalty_timer>=900){
                goal=penalty_power>=12&&penalty_power<=94;
                if(target_side==keeper_plan&&penalty_power<82)goal=0;
                if(target_side==1&&penalty_power<35&&keeper_plan!=1)goal=1;
                history[target_side]++;penalty_state=3;penalty_timer=0;
                kicker_frame=GNG_PEN_KICKER_FRAME_STRIKE;sound(SND_SFX_KICK);
            }
        }else if(penalty_state==3){
            u16 flight=penalty_timer>36?36:penalty_timer;
            ball_x=128+(target_x-128)*flight/36;ball_y=194+(target_y-194)*flight/36;
            if(penalty_power>94)ball_y-=flight/2;
            keeper_frame=keeper_plan==1?GNG_PEN_KEEPER_FRAME_JUMP:GNG_PEN_KEEPER_FRAME_DIVEHIGH;
            if(penalty_timer==42){
                penalty_state=4;penalty_timer=0;
                if(goal){u32 points=500;penalty_goals++;penalty_streak++;
                    if((penalty_aim<15||penalty_aim>85)&&target_y<104)points+=1000;
                    if(target_side==1&&penalty_power<35&&keeper_plan!=1)points+=2000;
                    if(penalty_power>=82)points+=300;
                    points=points*(penalty_streak+1)/2;penalty_bonus_score+=points;award_points(points,0,-1);
                    sound(SND_SFX_PICKUP);keeper_frame=GNG_PEN_KEEPER_FRAME_BEATEN;kicker_frame=GNG_PEN_KICKER_FRAME_CHEER0;
                }else{penalty_streak=0;sound(SND_SFX_HIT);keeper_frame=GNG_PEN_KEEPER_FRAME_CATCH;kicker_frame=GNG_PEN_KICKER_FRAME_SAD;}
            }
        }else if(penalty_state==4&&penalty_timer>=90){
            penalty_kick++;
            if(penalty_kick==5){
                if(penalty_goals==5){if(lives<99)lives++;penalty_bonus_score+=5000;award_points(5000,0,-1);sound(SND_SFX_ONE_UP);}
                level1_complete=1;penalty_state=5;
            }else{penalty_state=1;penalty_timer=0;ball_x=128;ball_y=194;kicker_frame=GNG_PEN_KICKER_FRAME_IDLE0;keeper_frame=GNG_PEN_KEEPER_FRAME_READY0;}
        }
        ng_wait_vblank();select_scene_palette(4+bg);
#define PEN_BG(N) draw_layer(SCB_SKY,GNG_PEN_BG##N##_TILE,GNG_PEN_BG##N##_COLS,GNG_PEN_BG##N##_ROWS,GNG_PEN_BG##N##_PALMAP,0,0)
        switch(bg){case 0:PEN_BG(0);break;case 1:PEN_BG(1);break;case 2:PEN_BG(2);break;default:PEN_BG(3);break;}
#undef PEN_BG
        draw_actor(SCB_PLAYER,GNG_PEN_KICKER_TILE,GNG_PEN_KICKER_COLS,GNG_PEN_KICKER_PALMAP,kicker_frame,GNG_PEN_KICKER_FRAME_COLS,GNG_PEN_KICKER_FRAME_ROWS,54,112,0);
        draw_actor(SCB_ENEMY_BASE,GNG_PEN_KEEPER_TILE,GNG_PEN_KEEPER_COLS,GNG_PEN_KEEPER_PALMAP,keeper_frame,GNG_PEN_KEEPER_FRAME_COLS,GNG_PEN_KEEPER_FRAME_ROWS,112+(penalty_state>=3?(s16)(keeper_plan-1)*24:0),77,keeper_plan==0);
        draw_small(SCB_BALL0,GNG_BALL_TILE,GNG_BALL_COLS,GNG_BALL_PALMAP,(f/3)&3,1,1,ball_x*5/4-8,ball_y-8);
        if(penalty_state==1||penalty_state==2){
            *REG_VRAMMOD=1;*REG_VRAMADDR=ADDR_SCB1+356*64;*REG_VRAMRW=GNG_SCOREPOP_TILE+11;*REG_VRAMRW=GNG_SCOREPOP_PALMAP[0]<<8;
            position_strip(356,1,target_x*5/4-4,target_y-4);
        }else hide_strip(356);
        ng_center_text(3,2,"BONUS RIGORI - CURVA MARE");
        sprintf(text,"TIRO %u/5  GOL %u  BONUS %05lu",penalty_kick<5?penalty_kick+1:5,penalty_goals,(unsigned long)penalty_bonus_score);ng_center_text(5,0,text);
        if(penalty_state==0)ng_center_text(24,0,"B FERMA MIRA, B FERMA FORZA");
        else if(penalty_state==1)ng_center_text(24,0,"SCEGLI IL BERSAGLIO: PREMI B");
        else if(penalty_state==2){sprintf(text,"FORZA %03u  PREMI B          ",penalty_power);ng_center_text(24,2,text);}
        else if(penalty_state==4)ng_center_text(24,2,goal?"GOOOL!                      ":"PARATO / FUORI!              ");
        previous=pad;penalty_timer++;f++;frame_counter++;
    }
    ng_cls();draw_results_panel();hide_range(SCB_PLAYER,8);hide_range(SCB_ENEMY_BASE,8);hide_strip(SCB_BALL0);hide_strip(356);
    for(f=0;f<240;++f){ng_wait_vblank();ng_center_text_tall(8,0,"LEVEL 1 COMPLETE");
        sprintf(text,"SCORE %06lu",(unsigned long)score);ng_center_text(15,2,text);
        sprintf(text,"BONUS RIGORI %05lu",(unsigned long)penalty_bonus_score);ng_center_text(18,0,text);
        ng_center_text(23,0,"DAI BURDEL! GRAZIE BURDEL!");
    }
    hide_range(0,380);
}
#endif
