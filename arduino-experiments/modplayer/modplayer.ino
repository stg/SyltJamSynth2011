/*
  This work is published under the Creative Commons BY-NC license.
  See http://creativecommons.org/licenses/by-nc/3.0/ for more information.
  
  Author: senseitg@gmail.com
  
  Contact the author for commercial licensing information.
  
  Allows playback of nearly all ProTracker Modules.
  
  All effects are implemented and tested, except for FunkRepeat/Invert Loop.
*/

#include "Synth.h"
#include "addressed_memory.h"
#include <avr/pgmspace.h>
extern uint8_t PROGMEM mod_data[];

#define BUFSZ 64

uint8_t buf[ BUFSZ ];
uint8_t *p_rd = buf, *p_wr = buf;
int8_t buf_c = 0;

uint8_t sampler() {
  buf_c--;
  if( ++p_rd >= &buf[ BUFSZ ] ) p_rd = buf;
  return *p_rd;
}

void setup() {
  mod_load( mod_data );
  Synth.attachInterrupt( sampler, SAMPLE_RATE );
  Synth.setFilter( 0, FILTER_LP );
  Synth.setFilter( 1, FILTER_LP );
  Synth.setMode( 0, 0 );
  Synth.setMode( 1, 0 );
  Synth.setResonance( 0, 32 );
  Synth.setResonance( 1, 32 );
  Synth.setCutoff( 0, 20000 );
  Synth.setCutoff( 1, 20000 );
}

void loop() {
  if( buf_c < BUFSZ ) { 
    buf_c++;
    *p_wr = mod_sample() ^ 0x80;
    if( ++p_wr >= &buf[ BUFSZ ] ) p_wr = buf;
  }
}
