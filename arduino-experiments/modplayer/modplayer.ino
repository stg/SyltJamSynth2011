#include "Synth.h"
#include "addressed_memory.h"
extern uint8_t mod_data[];

uint8_t sampler() {
  return ( uint8_t )( mod_sample() ^ 0x80 );
}

void setup() {
  mod_load( mod_data );
  Synth.attachInterrupt( sampler, 44100 );
}

void loop() {
}
