#ifdef __cplusplus
extern "C" {
#endif
#include "stdint.h"
// Intended playback rate
#define SAMPLE_RATE 8000

void mod_load( const void *p_data );
void mod_reset( void );
int8_t mod_sample( void );
#ifdef __cplusplus
}
#endif
