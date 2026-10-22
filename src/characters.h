#ifndef GNG_CHARACTERS_H
#define GNG_CHARACTERS_H

#include <stdint.h>
#include "game.h"

typedef struct {
    const char *name;
    uint8_t speed;
    uint8_t jump;
    uint8_t shot;
    uint8_t boost;
    uint16_t run_speed;
    int16_t jump_velocity;
    uint16_t ball_speed;
    uint8_t ball_damage;
    uint8_t max_balls;
    uint16_t boost_frames;
} GngCharacterTuning;

extern const GngCharacterTuning gng_characters[GNG_CHAR_COUNT];
const GngCharacterTuning *gng_character(uint8_t id);

#endif
