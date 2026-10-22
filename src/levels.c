#include "levels.h"
#include "game.h"

/* Exact horizontal ground spans and wave trigger positions from the current web game.
 * More complex vertical/platform objects are ported in the next data pass. */

static const GngSpan l1_ground[] = {
    {0,34},{36,58},{60,96},{98,152},{155,196},{199,262}
};
static const GngWave l1_waves[] = {
    {30,GNG_ENEMY_BAT,1},{46,GNG_ENEMY_PUMPKIN,1},{62,GNG_ENEMY_BAT,2},
    {80,GNG_ENEMY_PUMPKIN,2},{100,GNG_ENEMY_BAT,2},{132,GNG_ENEMY_GHOST,2},
    {150,GNG_ENEMY_PUMPKIN,2},{162,GNG_ENEMY_GHOST,2},{176,GNG_ENEMY_BAT,2},
    {192,GNG_ENEMY_GHOST,2},{204,GNG_ENEMY_PUMPKIN,2},{216,GNG_ENEMY_GHOST,2},
    {226,GNG_ENEMY_BAT,2}
};

static const GngSpan l2_ground[] = {
    {0,18},{21,40},{44,62},{67,80},{85,256}
};
static const GngWave l2_waves[] = {
    {30,GNG_ENEMY_BAT,1},{60,GNG_ENEMY_BAT,2},{88,GNG_ENEMY_BAT,2},
    {104,GNG_ENEMY_WASHER,1},{116,GNG_ENEMY_WASHER,1},{136,GNG_ENEMY_WASHER,2},
    {150,GNG_ENEMY_WASHER,1},{158,GNG_ENEMY_BAT,2},{190,GNG_ENEMY_BOOK,2},
    {200,GNG_ENEMY_BOOK,3},{208,GNG_ENEMY_GHOST,2},{216,GNG_ENEMY_BOOK,3},
    {226,GNG_ENEMY_BOOK,2},{232,GNG_ENEMY_GHOST,2},{242,GNG_ENEMY_BOOK,3},
    {262,GNG_ENEMY_WOLF,1},{276,GNG_ENEMY_WOLF,1},{284,GNG_ENEMY_BAT,2},
    {292,GNG_ENEMY_WOLF,1},{304,GNG_ENEMY_BAT,2}
};

static const GngSpan l3_ground[] = {
    {0,104},{113,121},{130,138},{148,156},{164,176},
    {182,202},{205,226},{229,246},{249,262}
};
static const GngWave l3_waves[] = {
    {18,GNG_ENEMY_GULL,1},{34,GNG_ENEMY_GULL,2},{52,GNG_ENEMY_GULL,2},
    {70,GNG_ENEMY_GULL,3},{60,GNG_ENEMY_GHOST,2},{86,GNG_ENEMY_GULL,2},
    {112,GNG_ENEMY_GULL,2},{128,GNG_ENEMY_BAT,2},{146,GNG_ENEMY_GULL,2},
    {160,GNG_ENEMY_GHOST,2},{196,GNG_ENEMY_GULL,2},{212,GNG_ENEMY_GHOST,2},
    {228,GNG_ENEMY_GULL,3},{244,GNG_ENEMY_BAT,3},{256,GNG_ENEMY_GHOST,2},
    {284,GNG_ENEMY_BAT,2},{300,GNG_ENEMY_GHOST,2},{316,GNG_ENEMY_BAT,3},
    {334,GNG_ENEMY_GHOST,2},{346,GNG_ENEMY_BAT,2}
};

const GngLevelDef gng_levels[3] = {
    {262,224,13,13,121,242,150,180,l1_ground,sizeof(l1_ground)/sizeof(l1_ground[0]),l1_waves,sizeof(l1_waves)/sizeof(l1_waves[0])},
    {332,416,25,13,180,316,220,210,l2_ground,sizeof(l2_ground)/sizeof(l2_ground[0]),l2_waves,sizeof(l2_waves)/sizeof(l2_waves[0])},
    {368,320,18,15,188,352,300,230,l3_ground,sizeof(l3_ground)/sizeof(l3_ground[0]),l3_waves,sizeof(l3_waves)/sizeof(l3_waves[0])}
};

const GngLevelDef *gng_level_def(uint8_t level) {
    if (level >= 3) level = 0;
    return &gng_levels[level];
}
