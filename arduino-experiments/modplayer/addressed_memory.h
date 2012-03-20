extern "C" {
#include "stdint.h"
//#define HQ // High quality sampler (16-bit, interpolation, dithering)
void mod_load( const void *p_data );
void mod_reset( void );
#ifdef HQ
int16_t mod_sample( void );
#else
int8_t mod_sample( void );
#endif
}
