/*

Copyright 2011-2012 Davey Taylor

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "Synth.h"

#define SAMPLE_FREQ     44100

uint8_t pcm = 0, tick = 0;

uint8_t sample(void) {
  if(!pcm) tick++;
  return pcm++;
}

void setup(void) {
  Synth.attachInterrupt(sample, SAMPLE_FREQ);
  Synth.setResonance(0, 0x2F);
  Synth.setResonance(1, 0x01);
  Synth.setCutoff(0, 150);
  Synth.setCutoff(1, 600);  
}

void loop(void) {
  uint8_t fc = FILTER_LP;
  uint8_t lad = 0, pad = 0;
  uint8_t ltick;
  while(1) { 
    if(tick > 86) {
      tick = 0;
      fc = (fc == FILTER_LP ? FILTER_BP : (fc == FILTER_BP ? FILTER_HP : FILTER_LP));
      Synth.setFilter(0, fc);
      Synth.setFilter(1, fc);
      cli();
      Synth.setMode( 0, 2 );
      fastWrite( FLT_A3, ( lad > 0 ? HIGH : LOW ) );
      fastWrite( FLT_A2, ( lad > 1 ? HIGH : LOW ) );
      fastWrite( FLT_A1, ( lad > 2 ? HIGH : LOW ) );
      fastWrite( FLT_A0, ( lad > 3 ? HIGH : LOW ) );
      fastWrite( FLT_D1, ( lad > 4 ? HIGH : LOW ) );
      fastWrite( FLT_D0, ( lad > 5 ? HIGH : LOW ) );
      sei();
    }
    if(++pad == 10) {
      pad = 0;
      if(++lad == 7) lad = 0;
      cli();
      fastWrite( FLT_A3, ( lad > 0 ? HIGH : LOW ) );
      fastWrite( FLT_A2, ( lad > 1 ? HIGH : LOW ) );
      fastWrite( FLT_A1, ( lad > 2 ? HIGH : LOW ) );
      fastWrite( FLT_A0, ( lad > 3 ? HIGH : LOW ) );
      fastWrite( FLT_D1, ( lad > 4 ? HIGH : LOW ) );
      fastWrite( FLT_D0, ( lad > 5 ? HIGH : LOW ) );
      sei();
    }
  }
}
