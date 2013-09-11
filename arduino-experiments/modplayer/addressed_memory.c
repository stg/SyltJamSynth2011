/*
  This work is published under the Creative Commons BY-NC license.
  See http://creativecommons.org/licenses/by-nc/3.0/ for more information.

  Author: senseitg@gmail.com

  Contact the author for commercial licensing information.

  Allows playback of nearly all ProTracker Modules.

  All effects are implemented and tested, except for FunkRepeat/Invert Loop.
*/

#include "avr/pgmspace.h"
#include "addressed_memory.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef HQ
#include <Math.h>
#endif

#define ROM_READB( ptr ) pgm_read_byte( ptr )
#define ROM_READW( ptr ) pgm_read_word( ptr )

// Amiga period range (used for clamping)
#define PERIOD_MIN 54  // 54 is a modern limit, use 113 for full compatibility
#define PERIOD_MAX 856

// Convenience macros
#define LO4( V ) ( ( (V) & 0x0F )      )
#define HI4( V ) ( ( (V) & 0xF0 ) >> 4 )
#define MIN( A, B ) ( (A) < (B) ? (A) : (B) )
#define MAX( A, B ) ( (A) > (B) ? (A) : (B) )

// Frequencies for PAL/NTSC Amiga systems
#define PAL_FREQ  ( 7093789.2 / ( SAMPLE_RATE * 2 ) )
#define NTSC_FREQ ( 7159090.5 / ( SAMPLE_RATE * 2 ) )

// Frequency selection
#define SYS_FREQ PAL_FREQ

// ProTracker sample header
//#pragma pack(push)
//#pragma pack(1)
typedef struct __attribute__((packed)) {
  int8_t   name[ 22 ];      // Name
  uint8_t  length_msb;      // Length in words
  uint8_t  length_lsb;
  uint8_t  tuning;          // Fine-tune value
  uint8_t  volume;          // Default volume
  uint8_t  loop_offset_msb; // Loop point in words
  uint8_t  loop_offset_lsb;
  uint8_t  loop_len_msb;    // Loop length in words
  uint8_t  loop_len_lsb;
} pts_t;

// ProTracker module header
typedef struct __attribute__((packed)) {
  int8_t   name[ 20 ];    // Name
  pts_t    sample[ 31 ];  // Samples
  uint8_t  order_count;   // Song length
  uint8_t  historical;    // For compatibility (always 0x7F)
  uint8_t  order[ 128 ];  // Pattern order list
  int8_t   ident[ 4 ];    // Identifier (always "M.K.")
} ptm_t;
//#pragma pack(pop)

// Sample definition
typedef struct {
  int8_t*  pa;
  int8_t*  pb;
  int8_t*  pc;
  int8_t*  pd;
  bool     loop;
  int8_t   mode;
  uint8_t  volume;
  uint8_t  tuning;
  intptr_t length;
} sample_t;

// Oscillator memory
typedef struct {
  uint8_t  fxp;
  uint8_t  offset;
  uint8_t  mode;
} osc_t;

// Effect memory
typedef struct {
  uint8_t    sample;
  uint8_t    volume;
  uint8_t    port_speed;
  uint16_t   port_target;
  bool       glissando;
  osc_t      vibr;
  osc_t      trem;
  uint16_t   period;
  uint8_t    loop_order;
  uint8_t    loop_row;
  uint8_t    loop_count;
  uint8_t    param;
} fxm_t;

// DMA emulation memory
typedef struct {
  int8_t *pa;     // Point A (start)
  int8_t *pb;     // Point B (end)
  int8_t *pc;     // Point C (pb reload @end)
  uint8_t active; // Channel is playing
  bool    loop;   // Loop when reached point B
  float   rate;   // Rate/frequency
  uint8_t volume; // Volume
  float   addr;   // Current address (fractional)
} dma_t;

static ptm_t *p_mod;
static uint8_t speed = 6;
static uint8_t tick = 0;
static uint8_t ix_nextrow = 0;
static uint8_t ix_nextorder = 0;
static uint8_t ix_row = 0;
static uint8_t ix_order = 0;
static uint8_t delay = 0;
static fxm_t fxm[ 4 ];
static dma_t dma[ 4 ];
static uint16_t ctr = 0;
static uint16_t cia = SAMPLE_RATE / ( ( 24 * 125 ) / 60 );
static sample_t sample[ 31 ];

// ProTracker(+1 octave) period tables for each finetune value
static const uint16_t period_tbl[ 16 ][ 48 ] PROGMEM = {
  { 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113, 107, 101,  95, 90, 85, 80, 76, 71, 67, 64, 60, 57 },
  { 850, 802, 757, 715, 674, 637, 601, 567, 535, 505, 477, 450, 425, 401, 379, 357, 337, 318, 300, 284, 268, 253, 239, 225, 213, 201, 189, 179, 169, 159, 150, 142, 134, 126, 119, 113, 106, 100,  95, 89, 84, 80, 75, 71, 67, 63, 60, 56 },
  { 844, 796, 752, 709, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376, 355, 335, 316, 298, 282, 266, 251, 237, 224, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118, 112, 106, 100,  94, 89, 84, 79, 75, 70, 66, 63, 59, 56 },
  { 838, 791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373, 352, 332, 314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132, 125, 118, 111, 105,  99,  93, 88, 83, 78, 74, 70, 66, 62, 59, 56 },
  { 832, 785, 741, 699, 660, 623, 588, 555, 524, 495, 467, 441, 416, 392, 370, 350, 330, 312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131, 124, 117, 110, 104,  98,  93, 87, 82, 78, 74, 69, 66, 62, 58, 55 },
  { 826, 779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368, 347, 328, 309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130, 123, 116, 109, 103,  97,  92, 87, 82, 77, 73, 69, 65, 61, 58, 55 },
  { 820, 774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365, 345, 325, 307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129, 122, 115, 109, 102,  97,  91, 86, 81, 77, 72, 68, 64, 61, 58, 54 },
  { 814, 768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363, 342, 323, 305, 288, 272, 256, 242, 228, 216, 204, 192, 181, 171, 161, 152, 144, 136, 128, 121, 114, 108, 102,  96,  91, 86, 81, 76, 72, 68, 64, 60, 57, 54 },
  { 907, 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113, 107, 101, 95, 90, 85, 80, 76, 71, 67, 64, 60 },
  { 900, 850, 802, 757, 715, 675, 636, 601, 567, 535, 505, 477, 450, 425, 401, 379, 357, 337, 318, 300, 284, 268, 253, 238, 225, 212, 200, 189, 179, 169, 159, 150, 142, 134, 126, 119, 112, 106, 100, 95, 89, 84, 80, 75, 71, 67, 63, 60 },
  { 894, 844, 796, 752, 709, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376, 355, 335, 316, 298, 282, 266, 251, 237, 223, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118, 112, 106, 100, 94, 89, 84, 79, 75, 70, 66, 63, 59 },
  { 887, 838, 791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373, 352, 332, 314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132, 125, 118, 111, 105,  99, 93, 88, 83, 78, 74, 70, 66, 62, 59 },
  { 881, 832, 785, 741, 699, 660, 623, 588, 555, 524, 494, 467, 441, 416, 392, 370, 350, 330, 312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131, 123, 117, 110, 104,  98, 93, 87, 82, 78, 74, 69, 66, 62, 58 },
  { 875, 826, 779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368, 347, 328, 309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130, 123, 116, 109, 103,  97, 92, 87, 82, 77, 73, 69, 65, 61, 58 },
  { 868, 820, 774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365, 345, 325, 307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129, 122, 115, 108, 102,  97, 91, 86, 81, 77, 72, 68, 64, 61, 58 },
  { 862, 814, 768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363, 342, 323, 305, 288, 272, 256, 242, 228, 216, 203, 192, 181, 171, 161, 152, 144, 136, 128, 121, 114, 108, 102,  96, 91, 86, 81, 76, 72, 68, 64, 60, 57 }
};
// ProTracker sine table, could be replaced by realtime Math.sin( n * Math.PI / 32 )
static const int8_t sine[ 64 ] PROGMEM = {
  0x00, 0x0C, 0x18, 0x25, 0x30, 0x3C, 0x47, 0x51, 0x5A, 0x62, 0x6A, 0x70, 0x76, 0x7A, 0x7D, 0x7F,
  0x7F, 0x7F, 0x7D, 0x7A, 0x76, 0x70, 0x6A, 0x62, 0x5A, 0x51, 0x47, 0x3C, 0x30, 0x25, 0x18, 0x0C,
  0x00, 0xF3, 0xE7, 0xDA, 0xCF, 0xC3, 0xB8, 0xAE, 0xA5, 0x9D, 0x95, 0x8F, 0x89, 0x85, 0x82, 0x80,
  0x80, 0x80, 0x82, 0x85, 0x89, 0x8F, 0x95, 0x9D, 0xA5, 0xAE, 0xB8, 0xC3, 0xCF, 0xDA, 0xE7, 0xF3
};

// Look up or generate waveform for ProTracker vibrato/tremolo oscillator
static int8_t do_osc( osc_t *p_osc ) {
  int8_t sample = 0;
  int16_t mul;

  switch( p_osc->mode & 0x03 ) {
    case 0: // Sine
      sample = ROM_READB( &sine[ ( p_osc->offset ) & 0x3F ] );
      if( sample > 127 ) sample -= 256;
      break;
    case 1: // Square
      sample = ( p_osc->offset & 0x20 ) ? 127 : -128;
      break;
    case 2: // Saw
      sample = ( ( p_osc->offset << 2 ) & 0xFF ) - 128;
      break;
    case 3: // Noise (random)
      sample = rand() & 0xFF;
      break;
  }
  mul = sample * LO4( p_osc->fxp );
  p_osc->offset = ( p_osc->offset + HI4( p_osc->fxp ) - 1 ) & 0xFF;
  return mul / 64;
}

// Calculates and returns arpeggio period
static uint16_t arpeggio( uint8_t ch, uint8_t halftones ) {
  uint8_t n, tuning = sample[ fxm[ ch ].sample ].tuning;
  // Find base note
  for( n = 0; n != 47; n++ ) {
    if( fxm[ ch ].period >= ROM_READW( &period_tbl[ tuning ][ n ] ) ) break;
  }
  // Clamp and return arpeggio period
  return ROM_READW( &period_tbl[ tuning ][ MIN( n + halftones, 47 ) ] );
}

// Calculates and returns glissando period
static uint16_t glissando( uint8_t ch ) {
  uint8_t n, tuning = sample[ fxm[ ch ].sample ].tuning;
  // Round off to nearest note period
  for( n = 0; n != 47; n++ ) {
    if( fxm[ ch ].period < ROM_READW( &period_tbl[ tuning ][ n ] ) &&
        fxm[ ch ].period >= ROM_READW( &period_tbl[ tuning ][ n + 1 ] ) ) {
      if( ROM_READW( &period_tbl[ tuning ][ n ] ) - fxm[ ch ].period > fxm[ ch ].period - ROM_READW( &period_tbl[ tuning ][ n + 1 ] ) ) n++;
      break;
    }
  }
  // Clamp and return arpeggio period
  return ROM_READW( &period_tbl[ tuning ][ n ] );
}

// Sets up and starts a DMA channel
static void note_start( dma_t *p_dma, uint8_t ix_sample, uint16_t period, uint8_t offset ) {
  // Set address points
  p_dma->pa        = sample[ ix_sample ].pa + ( offset << 8 );
  p_dma->pb        = sample[ ix_sample ].pb;
  p_dma->pc        = sample[ ix_sample ].pc;
  // Set reload register (defines rate)
  p_dma->rate      = SYS_FREQ / ROM_READW( &period_tbl[ sample[ ix_sample ].tuning ][ period ] );
  // Set mode (begin playback)
  p_dma->loop      = sample[ ix_sample ].loop;
  p_dma->active    = true;
  p_dma->addr      = ( float )( intptr_t )p_dma->pa;
  // Set loop-point
  p_dma->pa        = sample[ ix_sample ].pd;
}

// Progress module by one tick
static void play_module() {
  uint8_t  ch, fx, fxp;
  uint8_t  temp_b;
  uint16_t temp_w;
  fxm_t   *p_fxm;
  dma_t   *p_dma;
  bool     pattern_jump = false;
  uint8_t  ix_period;
  uint8_t  ix_sample;
  uint8_t *p_ptn;

  // Advance tick
  if( ++tick == speed ) tick = 0;

  // Handle row delay
  if( delay ) {
    if( tick == 0 ) delay--;
    return;
  }

  // Advance playback
  if( tick == 0 ) {
    if( ++ix_row == 64 ) {
      ix_row = 0;
      if( ++ix_order == ROM_READB( &p_mod->order_count ) ) ix_order = 0;
    }
    // Forced order/row
    if( ix_nextorder != 0xFF ) {
      ix_order = ix_nextorder;
      ix_nextorder = 0xFF;
    }
    if( ix_nextrow != 0xFF ) {
      ix_row = ix_nextrow;
      ix_nextrow = 0xFF;
    }

  }

  // Set up pointers
  p_ptn = ( ( uint8_t* )p_mod )
        + sizeof( ptm_t )
        + ( ROM_READB( &p_mod->order[ ix_order ] ) << 10 )
        + ( ix_row << 4 );

  p_fxm = fxm;
  p_dma = dma;

  for( ch = 0; ch != 4; ch++ ) {

    // Deconstruct cell (what the hell were they smoking?)
    temp_b     = ROM_READB( p_ptn++ );                // sample.msb and period.msb
    temp_w     = ( temp_b & 0x0F ) << 8;
    ix_sample  = temp_b & 0xF0;
    temp_w    |= ROM_READB( p_ptn++ );                // period.lsb
    temp_b     = ROM_READB( p_ptn++ );                // sample.lsb and effect
    ix_sample |= HI4( temp_b );
    fx         = LO4( temp_b ) << 4;
    fxp        = ROM_READB( p_ptn++ );                // parameters
    if( fx == 0xE0 ) {
      fx |= HI4( fxp );                   // extended parameters
      fxp  &= 0x0F;
    }
    ix_period = 0x7F;                     // period index
    if( temp_w ) {
      for( temp_b = 0; temp_b != 36; temp_b++ ) {
        if( ROM_READW( &period_tbl[ 0 ][ temp_b ] ) == temp_w ) {
          ix_period = temp_b;
          break;
        }
      }
    }

    // General effect parameter memory
    // NOTE: Unsure if this is true to the original ProTracker, but alot of
    //       modern players and trackers do implement this functionality.
    if( fx == 0x10 || fx == 0x20 || fx == 0xE1 || fx == 0xE2 || fx == 0x50 || fx == 0x60 || fx == 0xA0 ) {
      if( fxp ) {
        p_fxm->param = fxp;
      } else {
        fxp = p_fxm->param;
      }
    }

    if( tick == ( fx == 0xED ? fxp : 0 ) ) {
      if( ix_sample != 0 ) {
        // Cell has sample
        temp_b = ix_sample - 1;
        p_fxm->sample = temp_b;
        p_fxm->volume = sample[ temp_b ].volume;
        // Reset volume
        p_dma->volume = sample[ temp_b ].volume;
      }

      // Set tuning
      if( fx == 0xE5 ) sample[ p_fxm->sample ].tuning = fxp;
      // Reset oscillators
      if( ( p_fxm->vibr.mode & 0x4 ) == 0x0 ) p_fxm->vibr.offset = 0;
      if( ( p_fxm->trem.mode & 0x4 ) == 0x0 ) p_fxm->trem.offset = 0;

      if( ix_period != 0x7F ) {
        // Cell has note
        if( fx == 0x30 || fx == 0x50 ) {
          // Tone-portamento effect setup
          p_fxm->port_target = ROM_READW( &period_tbl[ sample[ ix_sample ].tuning ][ ix_period ] );
        } else {
          // Start note
          temp_b = p_fxm->sample;
          note_start( p_dma, temp_b, ix_period, ( fx == 0x90 ? fxp : 0 ) );
          // Set required effect memory parameters
          p_fxm->period = ROM_READW( &period_tbl[ sample[ temp_b ].tuning ][ ix_period ] );
        }
      }

      // Effects processed when tick = 0
      switch( fx ) {
        case 0x30: // Portamento
          if( fxp ) p_fxm->port_speed = fxp;
          break;
        case 0xB0: // Jump to pattern
          ix_nextorder = ( fxp >= ROM_READB( &p_mod->order_count ) ? 0x00 : fxp );
          ix_nextrow = 0;
          pattern_jump = true;
          break;
        case 0xC0: // Set volume
          p_fxm->volume = MIN( fxp, 0x40 );
          p_dma->volume = p_fxm->volume;
          break;
        case 0xD0: // Jump to row
          fxp = HI4( fxp ) * 10 + LO4( fxp );
          if( !pattern_jump ) ix_nextorder = ( ( ix_order + 1 ) >= ROM_READB( &p_mod->order_count ) ? 0x00 : ix_order + 1 );
          pattern_jump = true;
          ix_nextrow = ( fxp > 63 ? 0 : fxp );
          break;
        case 0xF0: // Set speed
          if( fxp > 0x20 ) {
            cia = SAMPLE_RATE / ( ( 24 * fxp ) / 60 );
          } else {
            speed = fxp;
          }
          break;
        case 0x40: // Vibrato
          if( fxp ) p_fxm->vibr.fxp = fxp;
          break;
        case 0x70: // Tremolo
          if( fxp ) p_fxm->trem.fxp = fxp;
          break;
        case 0xE1: // Fine slide up
          p_fxm->period = MAX( p_fxm->period - fxp, PERIOD_MIN );
          p_dma->rate = SYS_FREQ / p_fxm->period;
          break;
        case 0xE2: // Fine slide down
          p_fxm->period = MIN( p_fxm->period + fxp, PERIOD_MAX );
          p_dma->rate = SYS_FREQ / p_fxm->period;
          break;
        case 0xE3: // Glissando control
          p_fxm->glissando = ( fxp != 0 );
          break;
        case 0xE4: // Set vibrato waveform
          p_fxm->vibr.mode = fxp;
          break;
        case 0xE6: // Loop-back (advanced looping)
          if( fxp == 0x0 ) {
            p_fxm->loop_order = ix_order;
            p_fxm->loop_row   = ix_row;
          } else {
            p_fxm->loop_count = ( p_fxm->loop_count ? p_fxm->loop_count - 1 : fxp );
            if( p_fxm->loop_count ) {
              ix_nextorder = p_fxm->loop_order;
              ix_nextrow   = p_fxm->loop_row;
            }
          }
          break;
        case 0xE7: // Set tremolo waveform
          p_fxm->trem.mode = fxp;
          break;
        case 0xEA: // Fine volume slide up
          p_fxm->volume = MIN( p_fxm->volume + fxp, 0x40 );
          p_dma->volume = p_fxm->volume;
          break;
        case 0xEB: // Fine volume slide down
          p_fxm->volume = MAX( p_fxm->volume - fxp, 0 );
          p_dma->volume = p_fxm->volume;
          break;
        case 0xEE: // Delay
          delay = fxp;
          break;
      }

    } else {
      // Effects processed when tick > 0
      switch( fx ) {
        case 0x10: // Slide up
          p_fxm->period = MAX( p_fxm->period - fxp, PERIOD_MIN );
          p_dma->rate = SYS_FREQ / p_fxm->period;
          break;
        case 0x20: // Slide down
          p_fxm->period = MIN( p_fxm->period + fxp, PERIOD_MAX );
          p_dma->rate = SYS_FREQ / p_fxm->period;
          break;
        case 0xE9: // Retrigger note
          temp_b = tick; while( temp_b >= fxp ) temp_b -= fxp;
          if( temp_b == 0 ) note_start( p_dma, p_fxm->sample, ix_period, ( fx == 0x90 ? fxp : 0 ) );
          break;
        case 0xEC: // Note cut
          if( fxp == tick ) p_dma->volume = 0x00;
          break;
        default:   // Multi-effect processing
          // Portamento
          if( fx == 0x30 || fx == 0x50 ) {
            if( p_fxm->period < p_fxm->port_target ) p_fxm->period = MIN( p_fxm->period + p_fxm->port_speed,  p_fxm->port_target );
            else                                     p_fxm->period = MAX( p_fxm->period - p_fxm->port_speed,  p_fxm->port_target );
            if( p_fxm->glissando ) p_dma->rate = SYS_FREQ / glissando( ch );
            else                   p_dma->rate = SYS_FREQ / p_fxm->period;
          }
          // Volume slide
          if( fx == 0x50 || fx == 0x60 || fx == 0xA0 ) {
            if( ( fxp & 0xF0 ) == 0 ) p_fxm->volume -= ( LO4( fxp ) );
            if( ( fxp & 0x0F ) == 0 ) p_fxm->volume += ( HI4( fxp ) );
            p_fxm->volume = MAX( MIN( p_fxm->volume, 0x40 ), 0 );
            p_dma->volume = p_fxm->volume;
          }
      }
    }

    // Normal play and arpeggio
    if( fx == 0x00 ) {
      temp_b = tick; while( temp_b > 2 ) temp_b -= 2;
      if( temp_b == 0 ) {
        // Reset
        p_dma->rate = SYS_FREQ / p_fxm->period;
      } else if( fxp ) {
        // Arpeggio
        p_dma->rate = SYS_FREQ / arpeggio( ch, ( temp_b == 1 ? HI4( fxp ) : LO4( fxp ) ) );
      }
    } else if( fx == 0x40 || fx == 0x60 ) {
      // Vibrato
      p_dma->rate = SYS_FREQ / ( p_fxm->period + do_osc( &p_fxm->vibr ) );
    } else if( fx == 0x70 ) {
      // Tremolo
      temp_b = p_fxm->volume + do_osc( &p_fxm->trem );
      p_dma->volume = MAX( MIN( temp_b, 0x40 ), 0 );
    }

    p_fxm++;
    p_dma++;
  }
}

// Resets playback
void mod_reset() {
  memset( fxm, 0, sizeof( fxm ) );
  memset( dma, 0, sizeof( dma ) );
  speed = 6;
  tick = 0;
  ix_nextrow = 0;
  ix_nextorder = 0;
  ix_row = 0;
  ix_order = 0;
  delay = 0;
  ctr = 0;
  cia = SAMPLE_RATE / ( ( 24 * 125 ) / 60 );
}

// Loads a module
void mod_load( const void *p_data ) {
  uint8_t   n;
  int8_t   *p_audio;
  sample_t *p_sample = sample;
  intptr_t  loop_offset, loop_len;
  uint8_t   patterns = 0;
  p_mod = ( ptm_t* )p_data;

  // Find pattern count
  for( n = 0; n < 128; n++ ) {
    if( ROM_READB( &p_mod->order[ n ] ) >= patterns ) patterns = ROM_READB( &p_mod->order[ n ] ) + 1;
  }

  // Read samples
  memset( sample, 0, sizeof( sample ) );
  p_audio = ( ( int8_t* )p_mod ) + sizeof( ptm_t ) + ( patterns << 10 );
  for( n = 0; n != 31; n++ ) {
    p_sample->pa     = p_audio;
    p_sample->length = ( ROM_READB( &p_mod->sample[ n ].length_msb ) << 9 )
                     | ( ROM_READB( &p_mod->sample[ n ].length_lsb ) << 1 );
    p_sample->volume = ROM_READB( &p_mod->sample[ n ].volume );
    p_sample->tuning = ROM_READB( &p_mod->sample[ n ].tuning );
    loop_offset      = ( ROM_READB( &p_mod->sample[ n ].loop_offset_msb ) << 9 )
                     | ( ROM_READB( &p_mod->sample[ n ].loop_offset_lsb ) << 1 );
    loop_len         = ( ROM_READB( &p_mod->sample[ n ].loop_len_msb ) << 9 )
                     | ( ROM_READB( &p_mod->sample[ n ].loop_len_lsb ) << 1 );
    p_sample->pb     = p_audio + p_sample->length;
    p_sample->pd     = ( loop_len < 3 ? 0 : loop_offset ) + p_sample->pa;
    p_sample->pc     = ( loop_len < 3 ? p_sample->pb : p_sample->pd + loop_len );
    p_sample->loop   = loop_len >= 3;
    p_audio     += p_sample->length;
    p_sample++;
  }
  mod_reset();
}


int8_t mod_sample() {
  uint8_t ch;
  dma_t *p_dma;
  int16_t mono = 0;

  if( ctr-- == 0 ) {
    ctr = cia;
    play_module();
  }

  p_dma = dma;
  for( ch = 0; ch != 4; ch++ ) {
    if( p_dma->active ) {
      p_dma->addr += p_dma->rate;
      if( p_dma->addr >= ( float )( intptr_t )p_dma->pb ) {
        if( p_dma->loop ) {
          p_dma->addr -= p_dma->pb - p_dma->pa;
          p_dma->pb = p_dma->pc;
        } else {
          p_dma->addr = p_dma->pb - 1 - p_dma->pa;
          p_dma->active = false;
        }
      }
      mono += ( ( int8_t )ROM_READB( ( intptr_t )p_dma->addr ) ) * p_dma->volume;
    }
    p_dma++;
  }

  return mono / 256;
}

