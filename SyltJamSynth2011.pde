#include <MIDI.h>
//#include <digitalWriteFast.h>

#define PCM_LCH 10
#define PCM_CLK 13
#define PCM_SDA A3

#define FLT_WR  12
#define FLT_D0   8
#define FLT_D1   9
#define FLT_A0  A2
#define FLT_A1  A4
#define FLT_A2   5
#define FLT_A3   7

#define FLT_F0   6
#define FLT_F1  11

#define POT_A   A0
#define POT_B   A1

#define BTN_F1   2
#define BTN_F2   3
#define BTN_F3   4

#define CPU_CLOCK   16000000
#define SAMPLE_FREQ    10000

#define VOLUME_CALC( CH ) pm[ CH ].volume = ( pm[ CH ].e.value * pm[ CH ].e.velocity ) >> 8

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
    int8_t velocity;
   int16_t value;
   uint8_t state;
} env_t;

typedef struct {
    uint8_t note;
    uint8_t volume;
      env_t e;
} pm_t;

typedef struct {
  uint8_t initial;
  uint8_t attack;
  uint8_t decay;
  uint8_t sustain;
  uint8_t release;
} envcfg_t;

typedef struct {
  uint8_t rate;
  float freq;
  float dest;  
} track_t;

// MIDI frequency table
static const float ffreq[] = {
  8.17579, 8.66195, 9.17702, 10.3008, 10.3008, 10.9133, 11.5623, 12.2498, 12.9782, 13.7500, 14.5676, 15.4338, 
  16.3515, 17.3239, 18.3540, 19.4454, 20.6017, 21.8267, 23.1246, 24.4997, 25.9565, 27.5000, 29.1352, 30.8677, 
  32.7031, 34.6478, 36.7080, 38.8908, 41.2034, 43.6535, 46.2493, 48.9994, 51.9130, 55.0000, 58.2704, 61.7354, 
  65.4063, 69.2956, 73.4161, 77.7817, 82.4068, 87.3070, 92.4986, 97.9988, 103.826, 110.000, 116.540, 123.470, 
  130.812, 138.591, 146.832, 155.563, 164.813, 174.614, 184.997, 195.997, 207.652, 220.000, 233.081, 246.941, 
  261.625, 277.182, 293.664, 311.126, 329.627, 349.228, 369.994, 391.995, 415.304, 440.000, 466.163, 493.883, 
  523.251, 554.365, 587.329, 622.253, 659.255, 698.456, 739.988, 783.990, 830.609, 880.000, 932.327, 987.766, 
  1046.50, 1108.73, 1174.65, 1244.50, 1318.51, 1396.91, 1479.97, 1567.98, 1661.21, 1760.00, 1864.65, 1975.53, 
  2093.00, 2217.46, 2349.31, 2489.01, 2637.02, 2793.82, 2959.95, 3135.96, 3322.43, 3520.00, 3729.31, 3951.06, 
  4186.00, 4434.92, 4698.63, 4978.03, 5274.04, 5587.65, 5919.91, 5919.91, 6644.87, 7040.00, 7458.62, 7902.13, 
  8372.01, 8869.84, 9397.27, 9956.06, 10548.0, 11175.3, 11839.8, 12543.8
};

// Pitch
static float pitch = 1.0;

// Supersaw spread
static float spread = 0.0;

// Pitch portamento
static float port_freq, port_dest, port_speed = 40.0;

// Envelope shaping parameters for filter 0, 1, volume
static envcfg_t env[ 3 ] = { { 0xFF, 0xFF, 0x00, 0xFF, 0xFF },
                             { 0xFF, 0xFF, 0x00, 0xFF, 0xFF },
                             { 0xFF, 0xFF, 0x00, 0xFF, 0xFF } };

// Voices
static uint16_t pfreq[ 4 ][ 4 ];
static uint16_t  ppcm[ 4 ][ 4 ];
static pm_t             pm[ 4 ] = { { 0xFF, 0xFF, 0, 0, STATE_OFF },
                                    { 0xFF, 0xFF, 0, 0, STATE_OFF },
                                    { 0xFF, 0xFF, 0, 0, STATE_OFF },
                                    { 0xFF, 0xFF, 0, 0, STATE_OFF } };

// Envelope state for filter 0, 1
static env_t  filter[ 2 ] = { { 0, 0, STATE_OFF }, 
                              { 0, 0, STATE_OFF } };
static track_t track[ 2 ] = { 0, 0 };
                              
// Interrupt counter
static uint16_t int_count = 0;

// Mode
static  uint8_t poly = 0;

// Add note to "playlist"
void p_add( uint8_t m, uint8_t v ) {
  uint8_t q;
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
    pm[ q ].e.value = env[ ENV_VOL ].initial;
    pm[ q ].e.state = STATE_ATTACK;
    VOLUME_CALC( q );
    pitch_calc( q );
    // reset waveform for this channel
    ppcm[ q ][ 0 ] = 0;
    ppcm[ q ][ 1 ] = 0;
    ppcm[ q ][ 2 ] = 0;
    ppcm[ q ][ 3 ] = 0;
  } else {
      // insert new note data
      if( pm[ 0 ].e.state == STATE_OFF ) {
        pm[ 0 ].note = m;
        pm[ 0 ].e.velocity = v;
        pm[ 0 ].e.value = env[ ENV_VOL ].initial;
        pm[ 0 ].e.state = STATE_ATTACK;
        VOLUME_CALC( 0 );
        port_dest = port_freq = ffreq[ m ];
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
        pm[ q ].note = m;
        pm[ q ].e.state = STATE_ATTACK;
        port_dest = ffreq[ m ];
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
      ppcm[ 0 ][ 0 ] = 0;
      ppcm[ 0 ][ 1 ] = 0;
      ppcm[ 0 ][ 2 ] = 0;
      ppcm[ 0 ][ 3 ] = 0;
    }
  }
}

ISR( TIMER1_COMPA_vect ) {
  register int16_t vout;       // voice out
  register int16_t aout;       // accumulated out
  static   uint8_t out = 0x00; // final out

  // clock out 8-bits of pcm data
  // IMPORTANT: the 0x## list for each bit is shifted in this code
  //            due to a hardware error on my board. for a correctly
  //            constructed board the list should work it's way down
  //            from 0x80 to 0x01 - sorry about this!
  digitalWrite( PCM_LCH, HIGH );
  digitalWrite( PCM_LCH, LOW );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x40 ) );
  digitalWrite( PCM_CLK, HIGH );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x20 ) );
  digitalWrite( PCM_CLK, HIGH );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x10 ) );
  digitalWrite( PCM_CLK, HIGH );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x08 ) );
  digitalWrite( PCM_CLK, HIGH );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x04 ) );
  digitalWrite( PCM_CLK, HIGH );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x02 ) );
  digitalWrite( PCM_CLK, HIGH );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x01 ) );
  digitalWrite( PCM_CLK, HIGH );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x80 ) );
  digitalWrite( PCM_CLK, HIGH );
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
  if( aout > 1023 ) aout = 1023;
  else if( aout < -1024 ) aout = -1024;
  // scale and convert to unsigned
  out = ( static_cast<uint8_t>( aout >> 3 ) ) ^ 0x80;  
  // count (used by envelope shaping)
  int_count++;
}

// Sets the center frequency 4...40000hz for filter 0...1
void filter_c( uint8_t filter, float freq ) {
	float ps;
	uint8_t psb, lb;
	static uint8_t psbm[ 2 ] = { 0x7, 0x7 };
	freq *= 150.8;
	if( freq < 31250.0 ) {
		if( freq < 3905.25 ) {
			if( freq < 488.28125 ) {
				if( freq < 122.0703125 ) {
					ps = 1024.0; psb = 5;
				} else {
					ps = 256.0; psb = 4;
				}
			} else {
				ps = 64.0; psb = 3;
			}
		} else {
			ps = 8.0; psb = 2;
		}
	} else {
		ps = 1.0; psb = 1;
	}
	lb = ( ( float )( CPU_CLOCK / 2 ) ) / ( freq * ps );
	if( filter ) {
		if( psbm[ 1 ] != psb ) {
		  psbm[ 1 ] = psb;
  		TCCR2B = TCCR2B & 0xF8 | psb;
		}
		OCR2A = lb;
		TCNT2 = 0;
	} else {
		if( psbm[ 0 ] != psb ) {
		  psbm[ 0 ] = psb;
  		TCCR0B = TCCR2B & 0xF8 | psb;
  	}
		OCR0A = lb;
		TCNT0 = 0;
	}
}

// Writes data 0...3 to filter address 0...7 (filter 0) or 80...87 (filter 1)
void filter_write( uint8_t addr, uint8_t data ) {
  digitalWrite( FLT_A3, ( addr & 0x80 ? HIGH : LOW ) );
  digitalWrite( FLT_A2, ( addr & 0x04 ? HIGH : LOW ) );
  digitalWrite( FLT_A1, ( addr & 0x02 ? HIGH : LOW ) );
  digitalWrite( FLT_A0, ( addr & 0x01 ? HIGH : LOW ) );
  digitalWrite( FLT_D1, ( data & 0x02 ? HIGH : LOW ) );
  digitalWrite( FLT_D0, ( data & 0x01 ? HIGH : LOW ) );
  digitalWrite( FLT_WR, HIGH );
  digitalWrite( FLT_WR, LOW );
}

// Sets mode for filter 0...1 to 0...3
void filter_mode( uint8_t filter, uint8_t mode ) {
  filter_write( ( filter ? 0x80 : 0x00 ), mode );
}

// Sets f0-value of filter 0...1 to 0...3F
void filter_f( uint8_t filter, uint8_t mode ) {
  filter = ( filter ? 0x80 : 0x00 ) + 1;
  filter_write( filter++, mode       );
  filter_write( filter++, mode >>= 2 );
  filter_write( filter++, mode >>= 2 );
}

// Sets q-value of filter 0...1 to 0...7F
void filter_q( uint8_t filter, uint8_t q ) {
  filter = ( filter ? 0x80 : 0x00 ) + 4;
  filter_write( filter++, q       );
  filter_write( filter++, q >>= 2 );
  filter_write( filter++, q >>= 2 );
  filter_write( filter++, q >>= 2 );
}

// Sets/updates frequency for one polyphony channel
void pitch_calc( uint8_t ch ) {
  uint8_t m = pm[ ch ].note;
  float factor;
  if( m != 0xFF ) {
    factor = pitch * ffreq[ m ] / ( float )SAMPLE_FREQ;
/*  
    pfreq[ ch ][ 0 ] = pitch * ffreq[ m ] * (   65536.0                     / ( float )SAMPLE_FREQ );
    pfreq[ ch ][ 1 ] = pitch * ffreq[ m ] * ( ( 65536.0 - ( spread *  3 ) ) / ( float )SAMPLE_FREQ );
    pfreq[ ch ][ 2 ] = pitch * ffreq[ m ] * ( ( 65536.0 - ( spread *  7 ) ) / ( float )SAMPLE_FREQ );
    pfreq[ ch ][ 3 ] = pitch * ffreq[ m ] * ( ( 65536.0 - ( spread * 13 ) ) / ( float )SAMPLE_FREQ );
*/
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
/*
  pfreq[ ch ][ 0 ] = pitch * f * (   65536.0                     / ( float )SAMPLE_FREQ );
  pfreq[ ch ][ 1 ] = pitch * f * ( ( 65536.0 - ( spread *  3 ) ) / ( float )SAMPLE_FREQ );
  pfreq[ ch ][ 2 ] = pitch * f * ( ( 65536.0 - ( spread *  7 ) ) / ( float )SAMPLE_FREQ );
  pfreq[ ch ][ 3 ] = pitch * f * ( ( 65536.0 - ( spread * 13 ) ) / ( float )SAMPLE_FREQ );
*/  
  pfreq[ ch ][ 0 ] = factor * ( 65536.0               );
  pfreq[ ch ][ 1 ] = factor * ( 65536.0 - spread *  3 );
  pfreq[ ch ][ 2 ] = factor * ( 65536.0 - spread *  7 );
  pfreq[ ch ][ 3 ] = factor * ( 65536.0 - spread * 13 );
}

void setup( void ) {
  uint16_t isr_rr;
  pinMode( PCM_LCH, OUTPUT );
  pinMode( PCM_SDA, OUTPUT );
  pinMode( PCM_CLK, OUTPUT );
  pinMode( FLT_F0,  OUTPUT );
  pinMode( FLT_F1,  OUTPUT );
  pinMode( FLT_WR,  OUTPUT );
  pinMode( FLT_D0,  OUTPUT );
  pinMode( FLT_D1,  OUTPUT );
  pinMode( FLT_A0,  OUTPUT );
  pinMode( FLT_A1,  OUTPUT );
  pinMode( FLT_A2,  OUTPUT );
  pinMode( FLT_A3,  OUTPUT );
  pinMode( BTN_F1,   INPUT );
  pinMode( BTN_F2,   INPUT );
  pinMode( BTN_F3,   INPUT );

  // Disable timer interrupts 
  TIMSK0 = 0;
  TIMSK1 = 0;
  TIMSK2 = 0;
  // Set up timer0/2 (filter frequency generators)
  TCCR0A = 0b01000010;
  TCCR0B = 0b00000001;
  TCCR2A = 0b01000010;
  TCCR2B = 0b00000001;

  // Set up timer1 (sample rate generator)
  isr_rr = CPU_CLOCK / SAMPLE_FREQ;
  TCCR1A = 0b00000000;
  TCCR1B = 0b00001001;
  OCR1AH = isr_rr >> 8;
  OCR1AL = isr_rr & 0xFF;

  // Set up filter defaults
  filter_mode( 0, 0 );
  filter_mode( 1, 0 );
  filter_q( 0, 0x40 );
  filter_q( 1, 0x40 );
  filter_f( 0, 0x20 );
  filter_f( 1, 0x20 );
  filter_c( 0, 10000 );
  filter_c( 1, 10000 );

  //TIMSK1 |=  ( ( 1 << TOIE1  ) | ( 1 << OCIE1A ) );
  TIMSK1 = 1 << OCIE1A;

  MIDI.begin();

  // Enable interrupts
  sei();  
}

void loop( void ) {
  int16_t temp;
  uint8_t n;
  // check for incoming midi packet
  if( MIDI.read() ) {
    // switch message type
    switch( MIDI.getType() ) {
      case NoteOn:
        if( MIDI.getData2() ) {
          // note on, add to "playlist"
          p_add( MIDI.getData1(), MIDI.getData2() );
          break;
        }
      case NoteOff:
        // note off, set decay
        p_release( MIDI.getData1() );
        break;
      case PitchBend:
        temp = ( MIDI.getData2() << 7 ) | MIDI.getData1();
        pitch = 1.0 + ( ( float )( temp - 0x2000 ) ) / 67000.0;
        pitch_calc( 0 );
        pitch_calc( 1 );
        pitch_calc( 2 );
        pitch_calc( 3 );
        break;
      case CC:
        switch( MIDI.getData1() ) {
          case 1: // Waveform spread
            spread = ( ( float )MIDI.getData2() ) * 2;
            pitch_calc( 0 );
            pitch_calc( 1 );
            pitch_calc( 2 );
            pitch_calc( 3 );
            break;
          
          case 10: // F0 center frequency
            OCR2A = ( 255 - MIDI.getData2() ) << 1;
            break;
          case 11: // F0 resonance
            filter_q( 0, MIDI.getData2() );
            break;
          case 12: // F0 center frequency shift
            filter_f( 0, ( 255 - MIDI.getData2() ) >> 1 );
            break;
          case 13: // F0 mode
            filter_mode( 0, MIDI.getData2() >> 5 );
            break;
          
          case 15: // F1 center frequency
            OCR0A = ( 255 - MIDI.getData2() ) << 1;
            break;
          case 16: // F1 resonance
            filter_q( 1, MIDI.getData2() );
            break;
          case 17: // F1 center frequency shift
            filter_f( 1, ( 255 - MIDI.getData2() ) >> 1 );
            break;
          case 18: // F1 mode
            filter_mode( 1, MIDI.getData2() >> 5 );
            break;
          
          case 20: // Pitch portamento speed
            port_speed = MIDI.getData2();
            break;
          case 21: // Volume envelope initial level
            env[ ENV_VOL ].initial = MIDI.getData2() >> 1;
            break;
          case 22: // Volume envelope attack rate
            env[ ENV_VOL ].attack = MIDI.getData2() >> 1;
            break;
          case 23: // Volume envelope decay rate
            env[ ENV_VOL ].decay = MIDI.getData2() >> 1;
            break;
          case 24: // Volume envelope sustain level
            env[ ENV_VOL ].sustain = MIDI.getData2() >> 1;
            break;
          case 25: // Volume envelope decay rate
            env[ ENV_VOL ].release = MIDI.getData2() >> 1;
            break;
          
          case 29: // Mode (mono/poly)
            poly = MIDI.getData2() >> 6;
            break;

          case 30: // F0 tracking (0 off/envelope, 1-127 slow-immediate)
            track[ 0 ].rate = MIDI.getData2();
            break;
          case 31: // F0 envelope initial level
            env[ ENV_F0 ].initial = MIDI.getData2() >> 1;
            break;
          case 32: // F0 envelope attack rate
            env[ ENV_F0 ].attack = MIDI.getData2() >> 1;
            break;
          case 33: // F0 envelope decay rate
            env[ ENV_F0 ].decay = MIDI.getData2() >> 1;
            break;
          case 34: // F0 envelope sustain level
            env[ ENV_F0 ].sustain = MIDI.getData2() >> 1;
            break;
          case 35: // F0 envelope decay rate
            env[ ENV_F0 ].release = MIDI.getData2() >> 1;
            break;
          case 36: // F0 envelope velocity
            filter[ 0 ].velocity = ( ( int8_t )MIDI.getData2() ) - 64;
            break;

          case 40: // F1 tracking (0 off/envelope, 1-127 slow-immediate)
            track[ 1 ].rate = MIDI.getData2();
            break;
          case 41: // F1 envelope initial level
            env[ ENV_F1 ].initial = MIDI.getData2() >> 1;
            break;
          case 42: // F1 envelope attack rate
            env[ ENV_F1 ].attack = MIDI.getData2() >> 1;
            break;
          case 43: // F1 envelope decay rate
            env[ ENV_F1 ].decay = MIDI.getData2() >> 1;
            break;
          case 44: // F1 envelope sustain level
            env[ ENV_F1 ].sustain = MIDI.getData2() >> 1;
            break;
          case 45: // F1 envelope decay rate
            env[ ENV_F1 ].release = MIDI.getData2() >> 1;
            break;
          case 46: // F1 envelope velocity
            filter[ 1 ].velocity = ( ( int8_t )MIDI.getData2() ) - 64;
            break;

        }
        break;
    }
  }

  // run once for every 1/50 second
  while( int_count > ( SAMPLE_FREQ / 50 ) ) {
    cli();
    int_count -= ( SAMPLE_FREQ / 50 );
    sei();

    if( poly == 0 && port_dest != port_freq && pm[ 0 ].e.state != STATE_OFF ) {    
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
    for( n = 0; n!= 4; n++ ) {
      switch( pm[ n ].e.state ) {
        case STATE_ATTACK:
          pm[ n ].e.value += env[ ENV_VOL ].attack;
          if( pm[ n ].e.value >= 0x00FF ) {
            pm[ n ].e.value = 0x00FF;
            pm[ n ].e.state = STATE_DECAY;
          }
          VOLUME_CALC( n );
          break;
        case STATE_DECAY:
          pm[ n ].e.value -= env[ ENV_VOL ].decay;
          if( pm[ n ].e.value <= env[ ENV_VOL ].sustain ) {
            pm[ n ].e.value = env[ ENV_VOL ].sustain;
            pm[ n ].e.state = STATE_SUSTAIN;
          }
          VOLUME_CALC( n );
          break;
        case STATE_RELEASE:
          pm[ n ].e.value -= env[ ENV_VOL ].release;
          if( pm[ n ].e.value <= 0x0000 ) {
            pm[ n ].e.value = 0x0000;
            p_rem( pm[ n ].note );
          }
          VOLUME_CALC( n );
          break;
      }
    }
  }
}