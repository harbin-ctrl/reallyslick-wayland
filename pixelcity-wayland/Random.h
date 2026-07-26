#include <stdint.h>
#define COIN_FLIP     (RandomVal (2) == 0)

uint32_t RandomVal (int range);
uint32_t RandomVal (void);
void          RandomInit (uint32_t seed);
