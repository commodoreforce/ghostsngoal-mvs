#include <stdio.h>
#include <stdlib.h>
#include "../src/game.h"
#include "../src/characters.h"
#include "../src/levels.h"

static void require(int ok,const char *msg){if(!ok){fprintf(stderr,"FAIL: %s\n",msg);exit(1);}}
static unsigned rng_state=0x12345678u;
static unsigned xrnd(void){rng_state^=rng_state<<13;rng_state^=rng_state>>17;rng_state^=rng_state<<5;return rng_state;}

int main(void){
    require(GNG_LEVEL_COUNT==3,"three current stages");
    require(gng_level_def(0)->tiles_w==262,"stage 1 width");
    require(gng_level_def(1)->tiles_w==332,"stage 2 width");
    require(gng_level_def(2)->tiles_w==368,"stage 3 width");
    require(gng_level_def(2)->arena==352,"count arena position");
    require(gng_character(GNG_CHAR_SHPENDI)->max_balls==3,"Shpendi passive");
    require(gng_character(GNG_CHAR_DIAMANTI)->ball_damage==2,"Diamanti shot damage");
    require(gng_character(GNG_CHAR_HUBNER)->shot==6,"Hubner shot stat");

    GngGame g; gng_init(&g);
    require(g.state==GNG_STATE_BOOT,"boot state");
    for(int i=0;i<121;i++)gng_step(&g,(GngInput){0});
    require(g.state==GNG_STATE_ATTRACT||g.state==GNG_STATE_TITLE,"boot transition");
    gng_step(&g,(GngInput){.coin=1}); require(g.credits==1,"coin adds credit");
    gng_step(&g,(GngInput){.start=1}); require(g.state==GNG_STATE_PLAY,"start begins game");
    require(g.player.lives==3,"three lives");

    for(int f=0;f<50000;f++){
        unsigned r=xrnd(); GngInput in={0};
        in.left=(r&1)!=0; in.right=(r&2)!=0&&!in.left; in.jump=(r%47)==0; in.kick=(r%19)==0;
        if(g.state==GNG_STATE_ATTRACT||g.state==GNG_STATE_TITLE){if(!g.credits)in.coin=1;in.start=1;}
        gng_step(&g,in);
        require(g.player.x>=0&&g.player.x<320,"player x bounds");
        require(g.ball.x>=0&&g.ball.x<320,"ball x bounds");
        require(g.player.lives<=3,"lives sane"); require(g.credits<=9,"credits sane");
        require(g.level<GNG_LEVEL_COUNT,"level bounds");
    }
    printf("PASS frames=%u score=%u high=%u state=%d level=%u lives=%u\n",g.frame,g.score,g.high_score,g.state,g.level,g.player.lives);
    return 0;
}
