#include "Synth.h"

#define SAMPLE_FREQ 10000

// Volume and frequency calculations
#define VOLUME_CALC( CH ) pm[ CH ].volume = ( (pm[ CH ].e.value>>8) * pm[ CH ].e.velocity ) >> 8
#define F_CALC( CH ) Synth.setCutoff( CH, ( track[ CH ].rate ? track[ CH ].freq : track[ CH ].base + ( float )( ( filter[ CH ].value >> 8 ) * filter[ CH ].velocity ) ) );

typedef enum {
  STATE_OFF     = 0,
  STATE_ATTACK  = 1,
  STATE_DECAY   = 2,
  STATE_SUSTAIN = 3,
  STATE_RELEASE = 4
} state_e;

typedef enum {
  ENV_F0  = 0,
  ENV_F1  = 1,
  ENV_VOL = 2
} env_e;

typedef struct {
   int16_t velocity;
   int16_t value;
   uint8_t state;
} env_t;

typedef struct {
    uint8_t note;
    uint8_t volume;
    env_t e;
} pm_t;

typedef struct {
  uint8_t retrig;
  uint8_t attack;
  uint8_t decay;
  uint8_t sustain;
  uint8_t release;
} envcfg_t;

typedef struct {
  uint8_t rate;
  float base;
  float freq;
  float dest;  
} track_t;

// Pitch
static float pitch = 1.0;

// Supersaw spread
static float spread = 100.0;

// Pitch portamento
static float port_freq, port_dest, port_speed = 40.0;

// Envelope shaping parameters for filter 0, 1, volume
static envcfg_t env[ 3 ] = { { 0, 0x80, 0x00, 0x80, 0x80 },
                             { 0, 0x80, 0x47, 0x05, 0x00 },
                             { 0, 0x80, 0x00, 0x3F, 0x40 } };

// Voices
static uint16_t pfreq[ 4 ][ 4 ];
static uint16_t  ppcm[ 4 ][ 4 ] = { { 0x0000, 0x0000, 0x0000, 0x0000 }, 
                                    { 0x0000, 0x0000, 0x0000, 0x0000 },
                                    { 0x0000, 0x0000, 0x0000, 0x0000 },
                                    { 0x0000, 0x0000, 0x0000, 0x0000 } };
static pm_t             pm[ 4 ] = { { 0xFF, 0x00, { 0, 0, STATE_OFF } },
                                    { 0xFF, 0x00, { 0, 0, STATE_OFF } },
                                    { 0xFF, 0x00, { 0, 0, STATE_OFF } },
                                    { 0xFF, 0x00, { 0, 0, STATE_OFF } } };

// Envelope state for filter 0, 1
static env_t  filter[ 2 ] = { { 0, 0, STATE_OFF }, 
                              { 19, 0, STATE_OFF } };
static track_t track[ 2 ] = { { 0, 156, 156, 156 },
                              { 0, 10000, 10000, 10000 } };

// Interrupt counter
static uint16_t int_count = 0;

// Mode
static  uint8_t poly = 1;

// Pulse
static  uint8_t pulse = 0;

// Add note to "playlist"
void p_add( uint8_t m, uint8_t v ) {
  uint8_t q;
  filter[ 0 ].state = STATE_ATTACK;
  filter[ 1 ].state = STATE_ATTACK;
  track[ 0 ].dest = noteTable[ m ];
  track[ 1 ].dest = noteTable[ m ];
  filter[ 0 ].value = 0;
  filter[ 1 ].value = 0;
  F_CALC( 0 );
  F_CALC( 1 );
  // search for already existing instance
  if( poly ) {
    for( q = 0; q != 4; q++ ) {
      if( pm[ q ].note == m ) break;
    }
    if( q == 4 ) {
      // no instance found, shift to remove the oldest note
      pm[ 0 ] = pm[ 1 ];
      pm[ 1 ] = pm[ 2 ];
      pm[ 2 ] = pm[ 3 ];
      for( q = 0; q != 4; q++ ) {
        pfreq[ 0 ][ q ] = pfreq[ 1 ][ q ];
        pfreq[ 1 ][ q ] = pfreq[ 2 ][ q ];
        pfreq[ 2 ][ q ] = pfreq[ 3 ][ q ];
      }
      q = 3;
    }
    // insert new note data
    pm[ q ].note = m;
    pm[ q ].e.velocity = v;
    pm[ q ].e.value = 0;
    pm[ q ].e.state = STATE_ATTACK;
    VOLUME_CALC( q );
    pitch_calc( q );
    // reset waveform for this channel
    ppcm[ q ][ 0 ] = 0x0000;
    ppcm[ q ][ 1 ] = 0x0000;
    ppcm[ q ][ 2 ] = 0x0000;
    ppcm[ q ][ 3 ] = 0x0000;
  } else {
      // insert new note data
      if( pm[ 0 ].e.state == STATE_OFF ) {
        pm[ 0 ].note = m;
        pm[ 0 ].e.velocity = v;
        pm[ 0 ].e.value = 0;
        pm[ 0 ].e.state = STATE_ATTACK;
        VOLUME_CALC( 0 );
        port_dest = port_freq = noteTable[ m ];
        pitch_set( 0, port_freq );
        // copy
        pm[ 1 ] = pm[ 0 ];
        pfreq[ 1 ][ 0 ] = pfreq[ 0 ][ 0 ];
        pfreq[ 1 ][ 1 ] = pfreq[ 0 ][ 1 ];
        pfreq[ 1 ][ 2 ] = pfreq[ 0 ][ 2 ];
        pfreq[ 1 ][ 3 ] = pfreq[ 0 ][ 3 ];
        // reset waveform for this channel
        for( q = 0; q != 2; q++ ) {
          ppcm[ q ][ 0 ] = 0;
          ppcm[ q ][ 1 ] = 0;
          ppcm[ q ][ 2 ] = 0;
          ppcm[ q ][ 3 ] = 0;
        }
      } else {
        pm[ 0 ].note = m;
        pm[ 0 ].e.state = STATE_ATTACK;
        pm[ 1 ] = pm[ 0 ];
        port_dest = noteTable[ m ];
      }
    
  }
}

// Set decay mode, called when note is released
void p_release( uint8_t m ) {
  uint8_t n;
  if( poly ) {
    for( n = 0; n != 4; n++ ) {
      if( pm[ n ].note == m ) {
        pm[ n ].e.state = STATE_RELEASE;
      }
    }
  } else {
    if( pm[ 0 ].note == m ) {
      pm[ 0 ].e.state = STATE_RELEASE;
      pm[ 1 ].e.state = STATE_RELEASE;
    }
  }
}

// Remove note from "playlist"
void p_rem( uint8_t m ) {
  uint8_t n, z, q;
  for( n = 0; n != 4; n++ ) {
    if( pm[ n ].note == m ) {
      for( z = n; z != 0; z-- ) {
        for( q = 0; q != 4; q++ ) {
          pfreq[ z ][ q ] = pfreq[ z - 1 ][ q ];
          pm[ z ] = pm[ z - 1 ];
        }
      }
      pfreq[ 0 ][ 0 ] = 0;
      pfreq[ 0 ][ 1 ] = 0;
      pfreq[ 0 ][ 2 ] = 0;
      pfreq[ 0 ][ 3 ] = 0;
      pm[ 0 ].note    = 255;
      pm[ 0 ].e.state   = STATE_OFF;
      ppcm[ 0 ][ 0 ] = 0x0000;
      ppcm[ 0 ][ 1 ] = 0x0000;
      ppcm[ 0 ][ 2 ] = 0x0000;
      ppcm[ 0 ][ 3 ] = 0x0000;
    }
  }
}

/*
uint8_t sample( void ) {
  register int16_t vout;       // voice out
  register int16_t aout;       // accumulated out
  // update voice 0
  vout = -512;
  ppcm[ 0 ][ 0 ] += pfreq[ 0 ][ 0 ];
  vout += static_cast<uint8_t>( ppcm[ 0 ][ 0 ] >> 8 );
  ppcm[ 0 ][ 1 ] += pfreq[ 0 ][ 1 ];
  vout += static_cast<uint8_t>( ppcm[ 0 ][ 1 ] >> 8 );
  ppcm[ 0 ][ 2 ] += pfreq[ 0 ][ 2 ];
  vout += static_cast<uint8_t>( ppcm[ 0 ][ 2 ] >> 8 );
  ppcm[ 0 ][ 3 ] += pfreq[ 0 ][ 3 ];
  vout += static_cast<uint8_t>( ppcm[ 0 ][ 3 ] >> 8 );
  aout  = ( vout * pm[ 0 ].volume ) >> 8;
  // update voice 1
  vout = -512;
  ppcm[ 1 ][ 0 ] += pfreq[ 1 ][ 0 ];
  vout += static_cast<uint8_t>( ppcm[ 1 ][ 0 ] >> 8 );
  ppcm[ 1 ][ 1 ] += pfreq[ 1 ][ 1 ];
  vout += static_cast<uint8_t>( ppcm[ 1 ][ 1 ] >> 8 );
  ppcm[ 1 ][ 2 ] += pfreq[ 1 ][ 2 ];
  vout += static_cast<uint8_t>( ppcm[ 1 ][ 2 ] >> 8 );
  ppcm[ 1 ][ 3 ] += pfreq[ 1 ][ 3 ];
  vout += static_cast<uint8_t>( ppcm[ 1 ][ 3 ] >> 8 );
  aout += ( vout * pm[ 1 ].volume ) >> 8;
  // update voice 2
  vout = -512;
  ppcm[ 2 ][ 0 ] += pfreq[ 2 ][ 0 ];
  vout += static_cast<uint8_t>( ppcm[ 2 ][ 0 ] >> 8 );
  ppcm[ 2 ][ 1 ] += pfreq[ 2 ][ 1 ];
  vout += static_cast<uint8_t>( ppcm[ 2 ][ 1 ] >> 8 );
  ppcm[ 2 ][ 2 ] += pfreq[ 2 ][ 2 ];
  vout += static_cast<uint8_t>( ppcm[ 2 ][ 2 ] >> 8 );
  ppcm[ 2 ][ 3 ] += pfreq[ 2 ][ 3 ];
  vout += static_cast<uint8_t>( ppcm[ 2 ][ 3 ] >> 8 );
  aout += ( vout * pm[ 2 ].volume ) >> 8;
  // update voice 3
  vout = -512;
  ppcm[ 3 ][ 0 ] += pfreq[ 3 ][ 0 ];
  vout += static_cast<uint8_t>( ppcm[ 3 ][ 0 ] >> 8 );
  ppcm[ 3 ][ 1 ] += pfreq[ 3 ][ 1 ];
  vout += static_cast<uint8_t>( ppcm[ 3 ][ 1 ] >> 8 );
  ppcm[ 3 ][ 2 ] += pfreq[ 3 ][ 2 ];
  vout += static_cast<uint8_t>( ppcm[ 3 ][ 2 ] >> 8 );
  ppcm[ 3 ][ 3 ] += pfreq[ 3 ][ 3 ];
  vout += static_cast<uint8_t>( ppcm[ 3 ][ 3 ] >> 8 );
  aout += ( vout * pm[ 3 ].volume ) >> 8;
  // limit
  if( aout > 255 ) aout = 255;
  else if( aout < -256 ) aout = -256;
  // count (used by envelope shaping)
  int_count++;
  // scale and convert to unsigned
  return ( ( uint8_t )( aout >> 1 ) ) ^ 0x80;  
}
*/

uint8_t sample( void ) {
  register int16_t vout;       // voice out
  register int16_t aout;       // accumulated out
  if( pulse==0 ) {
    // update voice 0
    vout = -512;
    ppcm[ 0 ][ 0 ] += pfreq[ 0 ][ 0 ];
    vout += static_cast<uint8_t>( ppcm[ 0 ][ 0 ] >> 8 );
    ppcm[ 0 ][ 1 ] += pfreq[ 0 ][ 1 ];
    vout += static_cast<uint8_t>( ppcm[ 0 ][ 1 ] >> 8 );
    ppcm[ 0 ][ 2 ] += pfreq[ 0 ][ 2 ];
    vout += static_cast<uint8_t>( ppcm[ 0 ][ 2 ] >> 8 );
    ppcm[ 0 ][ 3 ] += pfreq[ 0 ][ 3 ];
    vout += static_cast<uint8_t>( ppcm[ 0 ][ 3 ] >> 8 );
    aout  = ( vout * pm[ 0 ].volume ) >> 8;
    // update voice 1
    vout = -512;
    ppcm[ 1 ][ 0 ] += pfreq[ 1 ][ 0 ];
    vout += static_cast<uint8_t>( ppcm[ 1 ][ 0 ] >> 8 );
    ppcm[ 1 ][ 1 ] += pfreq[ 1 ][ 1 ];
    vout += static_cast<uint8_t>( ppcm[ 1 ][ 1 ] >> 8 );
    ppcm[ 1 ][ 2 ] += pfreq[ 1 ][ 2 ];
    vout += static_cast<uint8_t>( ppcm[ 1 ][ 2 ] >> 8 );
    ppcm[ 1 ][ 3 ] += pfreq[ 1 ][ 3 ];
    vout += static_cast<uint8_t>( ppcm[ 1 ][ 3 ] >> 8 );
    aout += ( vout * pm[ 1 ].volume ) >> 8;
    // update voice 2
    vout = -512;
    ppcm[ 2 ][ 0 ] += pfreq[ 2 ][ 0 ];
    vout += static_cast<uint8_t>( ppcm[ 2 ][ 0 ] >> 8 );
    ppcm[ 2 ][ 1 ] += pfreq[ 2 ][ 1 ];
    vout += static_cast<uint8_t>( ppcm[ 2 ][ 1 ] >> 8 );
    ppcm[ 2 ][ 2 ] += pfreq[ 2 ][ 2 ];
    vout += static_cast<uint8_t>( ppcm[ 2 ][ 2 ] >> 8 );
    ppcm[ 2 ][ 3 ] += pfreq[ 2 ][ 3 ];
    vout += static_cast<uint8_t>( ppcm[ 2 ][ 3 ] >> 8 );
    aout += ( vout * pm[ 2 ].volume ) >> 8;
    // update voice 3
    vout = -512;
    ppcm[ 3 ][ 0 ] += pfreq[ 3 ][ 0 ];
    vout += static_cast<uint8_t>( ppcm[ 3 ][ 0 ] >> 8 );
    ppcm[ 3 ][ 1 ] += pfreq[ 3 ][ 1 ];
    vout += static_cast<uint8_t>( ppcm[ 3 ][ 1 ] >> 8 );
    ppcm[ 3 ][ 2 ] += pfreq[ 3 ][ 2 ];
    vout += static_cast<uint8_t>( ppcm[ 3 ][ 2 ] >> 8 );
    ppcm[ 3 ][ 3 ] += pfreq[ 3 ][ 3 ];
    vout += static_cast<uint8_t>( ppcm[ 3 ][ 3 ] >> 8 );
    aout += ( vout * pm[ 3 ].volume ) >> 8;
  } else {
    // update voice 0
    vout = -384;
    ppcm[ 0 ][ 0 ] += pfreq[ 0 ][ 0 ];
    vout += ( static_cast<uint8_t>( ppcm[ 0 ][ 0 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    ppcm[ 0 ][ 1 ] += pfreq[ 0 ][ 1 ];
    vout += ( static_cast<uint8_t>( ppcm[ 0 ][ 1 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    ppcm[ 0 ][ 2 ] += pfreq[ 0 ][ 2 ];
    vout += ( static_cast<uint8_t>( ppcm[ 0 ][ 2 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    aout  = ( vout * pm[ 0 ].volume ) >> 8;
    // update voice 1
    vout = -384;
    ppcm[ 1 ][ 0 ] += pfreq[ 1 ][ 0 ];
    vout += ( static_cast<uint8_t>( ppcm[ 1 ][ 0 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    ppcm[ 1 ][ 1 ] += pfreq[ 1 ][ 1 ];
    vout += ( static_cast<uint8_t>( ppcm[ 1 ][ 1 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    ppcm[ 1 ][ 2 ] += pfreq[ 1 ][ 2 ];
    vout += ( static_cast<uint8_t>( ppcm[ 1 ][ 2 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    aout += ( vout * pm[ 1 ].volume ) >> 8;
    // update voice 2
    vout = -384;
    ppcm[ 2 ][ 0 ] += pfreq[ 2 ][ 0 ];
    vout += ( static_cast<uint8_t>( ppcm[ 2 ][ 0 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    ppcm[ 2 ][ 1 ] += pfreq[ 2 ][ 1 ];
    vout += ( static_cast<uint8_t>( ppcm[ 2 ][ 1 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    ppcm[ 2 ][ 2 ] += pfreq[ 2 ][ 2 ];
    vout += ( static_cast<uint8_t>( ppcm[ 2 ][ 2 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    aout += ( vout * pm[ 2 ].volume ) >> 8;
    // update voice 3
    vout = -384;
    ppcm[ 3 ][ 0 ] += pfreq[ 3 ][ 0 ];
    vout += ( static_cast<uint8_t>( ppcm[ 3 ][ 0 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    ppcm[ 3 ][ 1 ] += pfreq[ 3 ][ 1 ];
    vout += ( static_cast<uint8_t>( ppcm[ 3 ][ 1 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    ppcm[ 3 ][ 2 ] += pfreq[ 3 ][ 2 ];
    vout += ( static_cast<uint8_t>( ppcm[ 3 ][ 2 ] >> 8 ) > pulse ? 0x40 : 0xC0 );
    aout += ( vout * pm[ 3 ].volume ) >> 8;
  }
  // limit
  if( aout > 255 ) aout = 255;
  else if( aout < -256 ) aout = -256;
  // count (used by envelope shaping)
  int_count++;
  // scale and convert to unsigned
  return ( ( uint8_t )( aout >> 1 ) ) ^ 0x80;  
}

// Sets/updates frequency for one polyphony channel
void pitch_calc( uint8_t ch ) {
  uint8_t m = pm[ ch ].note;
  float factor;
  if( m != 0xFF ) {
    factor = pitch * noteTable[ m ] / ( float )SAMPLE_FREQ;
    pfreq[ ch ][ 0 ] = factor * ( 65536.0               );
    pfreq[ ch ][ 1 ] = factor * ( 65536.0 - spread *  3 );
    pfreq[ ch ][ 2 ] = factor * ( 65536.0 - spread *  7 );
    pfreq[ ch ][ 3 ] = factor * ( 65536.0 - spread * 13 );
  } else {
    pfreq[ ch ][ 0 ] = 0;
    pfreq[ ch ][ 1 ] = 0;
    pfreq[ ch ][ 2 ] = 0;
    pfreq[ ch ][ 3 ] = 0;
  }
}

void pitch_set( uint8_t ch, float f ) {
  float factor = pitch * f / ( float )SAMPLE_FREQ;
  pfreq[ ch ][ 0 ] = factor * ( 65536.0               );
  pfreq[ ch ][ 1 ] = factor * ( 65536.0 - spread *  3 );
  pfreq[ ch ][ 2 ] = factor * ( 65536.0 - spread *  7 );
  pfreq[ ch ][ 3 ] = factor * ( 65536.0 - spread * 13 );
}

void setup( void ) {
  Synth.attachInterrupt( sample, SAMPLE_FREQ );
}

void loop( void ) {
  int16_t temp;
  uint8_t n;
  midi_t *midi = Synth.getMidi();
  
  if( midi ) {
    // switch message type
    switch( midi->message ) {
      case 0x90: // Note on
        if( midi->data2 ) {
          // note on, add to "playlist"
          p_add( midi->data1, midi->data2 );
          break;
        }
      case 0x80: // Note off
        // note off, set decay
        p_release( midi->data1 );
        break;
      case 0xE0:
        temp = ( midi->data2 << 7 ) | midi->data1;
        pitch = 1.0 + ( ( float )( temp - 0x2000 ) ) / 67000.0;
        pitch_calc( 0 );
        pitch_calc( 1 );
        pitch_calc( 2 );
        pitch_calc( 3 );
        break;
      case 0xB0: // Control change
        switch( midi->data1 ) {
          case 1: // Waveform spread
            spread = ( ( float )midi->data2 ) * 2;
            pitch_calc( 0 );
            pitch_calc( 1 );
            pitch_calc( 2 );
            pitch_calc( 3 );
            break;
          
          case 10: // F0 center frequency
            track[ 0 ].base = 20000.0 / ( float )( ( 128 - midi->data2 ) );
            F_CALC( 0 );
            break;
          case 11: // F0 resonance
            Synth.setResonance( 0, midi->data2 );
            break;
          case 12: // F0 center frequency shift
            Synth.setShift( 0, ( 255 - midi->data2 ) >> 1 );
            break;
          case 13: // F0 style
            switch( midi->data2 / 22 ) {
              case 0: // low-pass
                Synth.setFilter( 0, FILTER_LP );
                Synth.setMode( 0, 0 );
                break;
              case 1: // fat low-pass
                Synth.setFilter( 0, FILTER_LP );
                Synth.setMode( 0, 3 );
                break;
              case 2: // band-pass
                Synth.setFilter( 0, FILTER_BP );
                Synth.setMode( 0, 0 );
                break;
              case 3: // fat band-pass
                Synth.setFilter( 0, FILTER_BP );
                Synth.setMode( 0, 3 );
                break;
              case 4: // high-pass
                Synth.setFilter( 0, FILTER_HP );
                Synth.setMode( 0, 2 );
                break;
              case 5: // phaser
                Synth.setFilter( 0, FILTER_HP );
                Synth.setMode( 0, 0 );
                break;
            }
            break;

          case 15: // F1 center frequency
            track[ 1 ].base = 20000.0 / ( float )( ( 128 - midi->data2 ) );
            F_CALC( 1 );
            break;
          case 16: // F1 resonance
            Synth.setResonance( 1, midi->data2 );
            break;
          case 17: // F1 center frequency shift
            Synth.setShift( 1, ( 255 - midi->data2 ) >> 1 );
            break;
          case 18: // F1 style
            switch( midi->data2 / 22 ) {
              case 0: // low-pass
                Synth.setFilter( 1, FILTER_LP );
                Synth.setMode( 1, 0 );
                break;
              case 1: // fat low-pass
                Synth.setFilter( 1, FILTER_LP );
                Synth.setMode( 1, 3 );
                break;
              case 2: // band-pass
                Synth.setFilter( 1, FILTER_BP );
                Synth.setMode( 1, 0 );
                break;
              case 3: // fat band-pass
                Synth.setFilter( 1, FILTER_BP );
                Synth.setMode( 1, 3 );
                break;
              case 4: // high-pass
                Synth.setFilter( 1, FILTER_HP );
                Synth.setMode( 1, 2 );
                break;
              case 5: // phaser
                Synth.setFilter( 1, FILTER_HP );
                Synth.setMode( 1, 0 );
                break;
            }
            break;

          case 20: // Pitch portamento speed
            port_speed = 127-midi->data2;
            break;
          case 21: // Volume envelope retriggering
            env[ ENV_VOL].retrig = midi->data2 >> 6;
            break;
          case 22: // Volume envelope attack rate
            env[ ENV_VOL ].attack = 128 - midi->data2;
            break;
          case 23: // Volume envelope decay rate
            env[ ENV_VOL ].decay = 128 - midi->data2;
            break;
          case 24: // Volume envelope sustain level
            env[ ENV_VOL ].sustain = midi->data2 >> 1;
            break;
          case 25: // Volume envelope release rate
            env[ ENV_VOL ].release = 128 - midi->data2;
            break;
          
          case 28: // Pulse mode
            pulse = midi->data2;
            break;
          case 29: // Mode (mono/poly)
            poly = midi->data2 >> 6;
            break;

          case 30: // F0 tracking (0 off/envelope, 1-127 slow-immediate)
            track[ 0 ].rate = midi->data2;
            break;
          case 31: // F0 envelope retriggering
            env[ ENV_F0 ].retrig = midi->data2 >> 6;
            break;
          case 32: // F0 envelope attack rate
            env[ ENV_F0 ].attack = 128 - midi->data2;
            break;
          case 33: // F0 envelope decay rate
            env[ ENV_F0 ].decay = 128 - midi->data2;
            break;
          case 34: // F0 envelope sustain level
            env[ ENV_F0 ].sustain = midi->data2 >> 1;
            break;
          case 35: // F0 envelope release rate
            env[ ENV_F0 ].release = 128 - midi->data2;
            break;
          case 36: // F0 envelope velocity
            filter[ 0 ].velocity = midi->data2 << 1;
            break;

          case 40: // F1 tracking (0 off/envelope, 1-127 slow-immediate)
            track[ 1 ].rate = midi->data2;
            break;
          case 41: // F1 envelope retriggering
            env[ ENV_F1 ].retrig = midi->data2 >> 6;
            break;
          case 42: // F1 envelope attack rate
            env[ ENV_F1 ].attack = 128 - midi->data2;
            break;
          case 43: // F1 envelope decay rate
            env[ ENV_F1 ].decay = 128 - midi->data2;
            break;
          case 44: // F1 envelope sustain level
            env[ ENV_F1 ].sustain = midi->data2 >> 1;
            break;
          case 45: // F1 envelope release rate
            env[ ENV_F1 ].release = 128 - midi->data2;
            break;
          case 46: // F1 envelope velocity
            filter[ 1 ].velocity = midi->data2 << 1;
            break;

        }
        break;
    }
    Synth.freeMidi();
  }

  // run once for every 1/64 second
  while( int_count > ( SAMPLE_FREQ / 32 ) ) {
    cli();
    int_count -= ( SAMPLE_FREQ / 32 );
    sei();

    if( poly == 0 && pm[ 0 ].e.state != STATE_OFF ) {    
      port_freq += ( ( port_dest - port_freq ) * port_speed / 127.0 );
      pitch_set( 0, port_freq );
      // copy
      pm[ 1 ] = pm[ 0 ];
      pfreq[ 1 ][ 0 ] = pfreq[ 0 ][ 0 ];
      pfreq[ 1 ][ 1 ] = pfreq[ 0 ][ 1 ];
      pfreq[ 1 ][ 2 ] = pfreq[ 0 ][ 2 ];
      pfreq[ 1 ][ 3 ] = pfreq[ 0 ][ 3 ];
    }

    // do envelope handling
    for( n = 0; n != 4; n++ ) {
      switch( pm[ n ].e.state ) {
        case STATE_ATTACK:
          pm[ n ].e.value += env[ ENV_VOL ].attack << 6;
          if( pm[ n ].e.value >= 0x3F00 || env[ ENV_VOL ].attack == 0x80 ) {
            pm[ n ].e.value = 0x3F00;
            pm[ n ].e.state = STATE_DECAY;
          }
          VOLUME_CALC( n );
          break;
        case STATE_DECAY:
          pm[ n ].e.value -= env[ ENV_VOL ].decay << 6;
          if( pm[ n ].e.value <= ( env[ ENV_VOL ].sustain << 8 ) || env[ ENV_VOL ].decay == 0x80 ) {
            pm[ n ].e.value = (env[ ENV_VOL ].sustain<<8);
            pm[ n ].e.state = env[ ENV_VOL ].retrig ? STATE_ATTACK : STATE_SUSTAIN;
          }
          VOLUME_CALC( n );
          break;
        case STATE_RELEASE:
          pm[ n ].e.value -= env[ ENV_VOL ].release << 6;
          if( pm[ n ].e.value <= 0x0000 || env[ ENV_VOL ].release == 0x80 ) {
            pm[ n ].e.value = 0x0000;
            p_rem( pm[ n ].note );
          }
          VOLUME_CALC( n );
          break;
      }
    }
    for( n = 0; n != 2; n++ ) {
      if( track[ n ].rate ) {
        filter[ n ].state = STATE_OFF;
        track[ n ].freq += ( ( track[ n ].dest - track[ n ].freq ) * ( 128 - track[ n ].rate ) / 127.0 );
        F_CALC( n );
      } else {
        switch( filter[ n ].state ) {
          case STATE_ATTACK:
            filter[ n ].value += env[ n ].attack << 6;
            if( filter[ n ].value >= 0x3F00 || env[ n ].attack == 0x80 ) {
              filter[ n ].value = 0x3F00;
              filter[ n ].state = STATE_DECAY;
            }
            F_CALC( n );
            break;
          case STATE_DECAY:
            filter[ n ].value -= env[ n ].decay << 6;
            if( filter[ n ].value <= ( env[ n ].sustain << 8 ) || env[ n ].decay == 0x80 ) {
              filter[ n ].value = env[ n ].sustain << 8;
              filter[ n ].state = env[ n ].retrig ? STATE_ATTACK : STATE_SUSTAIN;
            }
            F_CALC( n );
            break;
          case STATE_RELEASE:
            filter[ n ].value -= env[ n ].release << 6;
            if( filter[ n ].value <= 0x0000 || env[ ENV_VOL ].release == 0x80 ) {
              filter[ n ].value = 0x0000;
              filter[ n ].state = STATE_OFF;
            }
            F_CALC( n );
            break;
        }
      }
    }
  }

}
