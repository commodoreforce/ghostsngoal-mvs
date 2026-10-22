#ifndef GNG_LEVELS_H
#define GNG_LEVELS_H

#include <stdint.h>

typedef struct { uint16_t a, b; } GngSpan;
typedef struct { uint16_t at; uint8_t type; uint8_t count; } GngWave;
typedef struct {
    uint16_t tiles_w;
    uint16_t world_h;
    uint8_t ground_row;
    uint8_t top_row;
    uint16_t checkpoint;
    uint16_t arena;
    uint16_t time_a;
    uint16_t time_b;
    const GngSpan *ground;
    uint8_t ground_count;
    const GngWave *waves;
    uint8_t wave_count;
} GngLevelDef;

extern const GngLevelDef gng_levels[3];
const GngLevelDef *gng_level_def(uint8_t level);

#endif
