#include "characters.h"

#define BOOST_FRAMES(stat) ((uint16_t)(((8000 + (stat) * 1000) * 60) / 1000))

/* Values mirror the current web game's tuning() function, converted to integer arcade data. */
const GngCharacterTuning gng_characters[GNG_CHAR_COUNT] = {
    { "SHPENDI",   4,3,4,3, 110, -303, 226, 1, 3, BOOST_FRAMES(3) },
    { "CIOFI",     3,3,3,5,  98, -303, 212, 1, 2, BOOST_FRAMES(5) },
    { "KLINSMANN", 3,5,2,4,  98, -337, 198, 1, 2, BOOST_FRAMES(4) },
    { "DIAMANTI",  3,3,5,3,  98, -303, 240, 2, 2, BOOST_FRAMES(3) },
    { "HUBNER",    2,2,6,3,  86, -286, 254, 2, 2, BOOST_FRAMES(3) }
};

const GngCharacterTuning *gng_character(uint8_t id) {
    if (id >= GNG_CHAR_COUNT) id = GNG_CHAR_SHPENDI;
    return &gng_characters[id];
}
