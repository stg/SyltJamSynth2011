#include <MIDI.h>
#include <digitalWriteFast.h>

#define PCM_LCH 10
#define PCM_CLK 13
#define PCM_SDA A3

#define FLT_WR 12
#define FLT_D0  8
#define FLT_D1  9
#define FLT_A0  A2
#define FLT_A1  A4
#define FLT_A2  5
#define FLT_A3  7

#define FLT_F0 6
#define FLT_F1 11

#define POT_A  A0
#define POT_B  A1

#define BTN_F1 2
#define BTN_F2 3
#define BTN_F3 4

#define SAMPLE_FREQ 10000

#define VCAL( CH ) pm[ CH ].volume = ( pm[ CH ].factor * pm[ CH ].velocity ) >> 6

typedef enum {
  STATE_OFF = 0,
  STATE_ATTACK = 1,
  STATE_DECAY = 2,
  STATE_SUSTAIN = 3,
  STATE_RELEASE = 4
} state_e;

typedef struct {
    unsigned char note;
    unsigned char velocity;
              int factor;
    unsigned char volume;
    unsigned char state;
} pm_t;

const float ffreq[] = {
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

// default startup pitch
float pitch = 1.0;

// default startup supersaw spread
float spread = 0;

// portamento
float port_freq, port_dest, port_speed = 10.0;

unsigned  int pfreq[ 4 ][ 4 ];

pm_t          pm[ 4 ] = { { 0xFF, 0x40, 0, 0, STATE_OFF },
                          { 0xFF, 0x40, 0, 0, STATE_OFF },
                          { 0xFF, 0x40, 0, 0, STATE_OFF },
                          { 0xFF, 0x40, 0, 0, STATE_OFF } };
                          
unsigned  int ppcm[ 4 ][ 4 ];
unsigned char out;
          int pout[ 4 ];
          int iout;

// Envelope shaping parameters
unsigned char env_initial = 0x40;
unsigned char env_attack = 0x40;
unsigned char env_decay = 0x00;
unsigned char env_sustain = 0x40;
unsigned char env_release = 0x40;
unsigned int  env_count = 0;

unsigned char poly = 0, mono = 0;

void p_add( unsigned char m, unsigned char v ) {
  unsigned char q;
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
    pm[ q ].velocity = v >> 1;
    pm[ q ].factor = env_initial;
    pm[ q ].state = STATE_ATTACK;
    VCAL( q );
    freqset( q );
    // reset waveform for this channel
    ppcm[ q ][ 0 ] = 0;
    ppcm[ q ][ 1 ] = 0;
    ppcm[ q ][ 2 ] = 0;
    ppcm[ q ][ 3 ] = 0;
  } else {
      // insert new note data
      if( pm[ 0 ].state == STATE_OFF ) {
        pm[ 0 ].note = m;
        pm[ 0 ].velocity = v >> 1;
        pm[ 0 ].factor = env_initial;
        pm[ 0 ].state = STATE_ATTACK;
        VCAL( 0 );
        port_dest = port_freq = ffreq[ m ];
        freqman( 0, port_freq );
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
        pm[ q ].state = STATE_ATTACK;
        port_dest = ffreq[ m ];
      }
    
  }
}

// set decay mode, called when note is released
void p_release( unsigned char m ) {
  unsigned char n;
  if( poly ) {
    for( n = 0; n != 4; n++ ) {
      if( pm[ n ].note == m ) {
        pm[ n ].state = STATE_RELEASE;
      }
    }
  } else {
    if( pm[ 0 ].note == m ) {
      pm[ 0 ].state = STATE_RELEASE;
      pm[ 1 ].state = STATE_RELEASE;
    }
  }
}

// remove note from "playlist"
void p_rem( unsigned char m ) {
  unsigned char n, z, q;
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
      pm[ 0 ].state   = STATE_OFF;
      ppcm[ 0 ][ 0 ] = 0;
      ppcm[ 0 ][ 1 ] = 0;
      ppcm[ 0 ][ 2 ] = 0;
      ppcm[ 0 ][ 3 ] = 0;
    }
  }
}

ISR( TIMER1_COMPA_vect ) {
  // clock out 8-bits of pcm data
  // IMPORTANT: the 0x## list for each bit is shifted in this code
  //            due to a hardware error on my board. for a correctly
  //            constructed board the list should work it's way down
  //            from 0x80 to 0x01 - sorry about this!
  digitalWriteFast( PCM_CLK, LOW );
  digitalWriteFast( PCM_SDA, ( out & 0x40 ) );
  digitalWriteFast( PCM_CLK, HIGH );
  digitalWriteFast( PCM_CLK, LOW );
  digitalWriteFast( PCM_SDA, ( out & 0x20 ) );
  digitalWriteFast( PCM_CLK, HIGH );
  digitalWriteFast( PCM_CLK, LOW );
  digitalWriteFast( PCM_SDA, ( out & 0x10 ) );
  digitalWriteFast( PCM_CLK, HIGH );
  digitalWriteFast( PCM_CLK, LOW );
  digitalWriteFast( PCM_SDA, ( out & 0x08 ) );
  digitalWriteFast( PCM_CLK, HIGH );
  digitalWriteFast( PCM_CLK, LOW );
  digitalWriteFast( PCM_SDA, ( out & 0x04 ) );
  digitalWriteFast( PCM_CLK, HIGH );
  digitalWriteFast( PCM_CLK, LOW );
  digitalWriteFast( PCM_SDA, ( out & 0x02 ) );
  digitalWriteFast( PCM_CLK, HIGH );
  digitalWriteFast( PCM_CLK, LOW );
  digitalWriteFast( PCM_SDA, ( out & 0x01 ) );
  digitalWriteFast( PCM_CLK, HIGH );
  digitalWriteFast( PCM_CLK, LOW );
  digitalWriteFast( PCM_SDA, ( out & 0x80 ) );
  digitalWriteFast( PCM_CLK, HIGH );
  digitalWriteFast( PCM_LCH, HIGH );
  digitalWriteFast( PCM_LCH, LOW );

  env_count++;

  // update pcm values for all 16 polyphonys
  ppcm[ 0 ][ 0 ] += pfreq[ 0 ][ 0 ];
  ppcm[ 0 ][ 1 ] += pfreq[ 0 ][ 1 ];
  ppcm[ 0 ][ 2 ] += pfreq[ 0 ][ 2 ];
  ppcm[ 0 ][ 3 ] += pfreq[ 0 ][ 3 ];
  ppcm[ 1 ][ 0 ] += pfreq[ 1 ][ 0 ];
  ppcm[ 1 ][ 1 ] += pfreq[ 1 ][ 1 ];
  ppcm[ 1 ][ 2 ] += pfreq[ 1 ][ 2 ];
  ppcm[ 1 ][ 3 ] += pfreq[ 1 ][ 3 ];
  ppcm[ 2 ][ 0 ] += pfreq[ 2 ][ 0 ];
  ppcm[ 2 ][ 1 ] += pfreq[ 2 ][ 1 ];
  ppcm[ 2 ][ 2 ] += pfreq[ 2 ][ 2 ];
  ppcm[ 2 ][ 3 ] += pfreq[ 2 ][ 3 ];
  ppcm[ 3 ][ 0 ] += pfreq[ 3 ][ 0 ];
  ppcm[ 3 ][ 1 ] += pfreq[ 3 ][ 1 ];
  ppcm[ 3 ][ 2 ] += pfreq[ 3 ][ 2 ];
  ppcm[ 3 ][ 3 ] += pfreq[ 3 ][ 3 ];
  // sum pcm values for all 4 channels
  pout[ 0 ] = ( ( char )( ppcm[ 0 ][ 0 ] >> 8 ) )
            + ( ( char )( ppcm[ 0 ][ 1 ] >> 8 ) )
            + ( ( char )( ppcm[ 0 ][ 2 ] >> 8 ) )
            + ( ( char )( ppcm[ 0 ][ 3 ] >> 8 ) );
  pout[ 1 ] = ( ( char )( ppcm[ 1 ][ 0 ] >> 8 ) )
            + ( ( char )( ppcm[ 1 ][ 1 ] >> 8 ) )
            + ( ( char )( ppcm[ 1 ][ 2 ] >> 8 ) )
            + ( ( char )( ppcm[ 1 ][ 3 ] >> 8 ) );
  pout[ 2 ] = ( ( char )( ppcm[ 2 ][ 0 ] >> 8 ) )
            + ( ( char )( ppcm[ 2 ][ 1 ] >> 8 ) )
            + ( ( char )( ppcm[ 2 ][ 2 ] >> 8 ) )
            + ( ( char )( ppcm[ 2 ][ 3 ] >> 8 ) );
  pout[ 3 ] = ( ( char )( ppcm[ 3 ][ 0 ] >> 8 ) )
            + ( ( char )( ppcm[ 3 ][ 1 ] >> 8 ) )
            + ( ( char )( ppcm[ 3 ][ 2 ] >> 8 ) )
            + ( ( char )( ppcm[ 3 ][ 3 ] >> 8 ) );
  // multiply each channels PCM value with it's volume and sum into the final pcm value
  iout = ( ( pout[ 0 ] * pm[ 0 ].volume ) >> 6 )
       + ( ( pout[ 1 ] * pm[ 1 ].volume ) >> 6 )
       + ( ( pout[ 2 ] * pm[ 2 ].volume ) >> 6 )
       + ( ( pout[ 3 ] * pm[ 3 ].volume ) >> 6 );
  // limit
  if( iout > 2047 ) iout = 2047;
  if( iout < -2048 ) iout = -2048;
  // scale and convert to unsigned
  out = ( ( char )( iout >> 4 ) ) ^ 0x80;  
}

// Sets the center frequency 4...40000hz for filter 0...1
void filter_c( unsigned char filter, float freq ) {
	float ps;
	unsigned char psb, lb;
	static psbm[ 2 ] = { 0x7, 0x7 };
	freq *= 150.8;
	if( f < 31250.0 ) {
		if( f < 3905.25 ) {
			if( f < 488.28125 ) {
				if( f < 122.0703125 ) {
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
	lb = 8000000.0 / ( freq * ps );
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
void filter_write( unsigned char addr, unsigned char data ) {
  digitalWriteFast( FLT_A3, ( addr & 0x80 ? HIGH : LOW ) );
  digitalWriteFast( FLT_A2, ( addr & 0x04 ? HIGH : LOW ) );
  digitalWriteFast( FLT_A1, ( addr & 0x02 ? HIGH : LOW ) );
  digitalWriteFast( FLT_A0, ( addr & 0x01 ? HIGH : LOW ) );
  digitalWriteFast( FLT_D1, ( data & 0x02 ? HIGH : LOW ) );
  digitalWriteFast( FLT_D0, ( data & 0x01 ? HIGH : LOW ) );
  digitalWriteFast( FLT_WR, HIGH );
  digitalWriteFast( FLT_WR, LOW );
}

// Sets mode for filter 0...1 to 0...3
void filter_mode( unsigned char filter, unsigned char mode ) {
  filter_write( ( filter & 0x01 ? 0x80 : 0x00 ), mode );
}

// Sets f0-value of filter 0...1 to 0...3F
void filter_f( unsigned char filter, unsigned char mode ) {
  filter_write( ( filter & 0x01 ? 0x80 : 0x00 ) + 1, mode );
  filter_write( ( filter & 0x01 ? 0x80 : 0x00 ) + 2, mode >> 2 );
  filter_write( ( filter & 0x01 ? 0x80 : 0x00 ) + 3, mode >> 4 );
}

// Sets q-value of filter 0...1 to 0...7F
void filter_q( unsigned char filter, unsigned char q ) {
  filter_write( ( filter & 0x01 ? 0x80 : 0x00 ) + 4, q );
  filter_write( ( filter & 0x01 ? 0x80 : 0x00 ) + 5, q >> 2 );
  filter_write( ( filter & 0x01 ? 0x80 : 0x00 ) + 6, q >> 4 );
  filter_write( ( filter & 0x01 ? 0x80 : 0x00 ) + 7, q >> 6 );
}

// Sets/updates frequency for one polyphony channel
void freqset( unsigned char ch ) {
  unsigned char m = pm[ ch ].note;
  if( m != 0xFF ) {
    pfreq[ ch ][ 0 ] = pitch * ffreq[ m ] * ( 65536.0 / ( float )SAMPLE_FREQ );
    pfreq[ ch ][ 1 ] = pitch * ffreq[ m ] * ( ( 65536.0 - ( spread * 3 ) ) / ( float )SAMPLE_FREQ );
    pfreq[ ch ][ 2 ] = pitch * ffreq[ m ] * ( ( 65536.0 - ( spread * 7 ) ) / ( float )SAMPLE_FREQ );
    pfreq[ ch ][ 3 ] = pitch * ffreq[ m ] * ( ( 65536.0 - ( spread * 13 ) ) / ( float )SAMPLE_FREQ );
  } else {
    pfreq[ ch ][ 0 ] = 0;
    pfreq[ ch ][ 1 ] = 0;
    pfreq[ ch ][ 2 ] = 0;
    pfreq[ ch ][ 3 ] = 0;
  }
}

void freqman( unsigned char ch, float f ) {
  pfreq[ ch ][ 0 ] = pitch * f * ( 65536.0 / ( float )SAMPLE_FREQ );
  pfreq[ ch ][ 1 ] = pitch * f * ( ( 65536.0 - ( spread * 3 ) ) / ( float )SAMPLE_FREQ );
  pfreq[ ch ][ 2 ] = pitch * f * ( ( 65536.0 - ( spread * 7 ) ) / ( float )SAMPLE_FREQ );
  pfreq[ ch ][ 3 ] = pitch * f * ( ( 65536.0 - ( spread * 13 ) ) / ( float )SAMPLE_FREQ );
}

void setup( void ) {
  unsigned int limit;
  pinMode( PCM_LCH, OUTPUT );
  pinMode( PCM_SDA, OUTPUT );
  pinMode( PCM_CLK, OUTPUT );
  pinMode( FLT_F0, OUTPUT );
  pinMode( FLT_F1, OUTPUT );
  pinMode( FLT_WR, OUTPUT );
  pinMode( FLT_D0, OUTPUT );
  pinMode( FLT_D1, OUTPUT );
  pinMode( FLT_A0, OUTPUT );
  pinMode( FLT_A1, OUTPUT );
  pinMode( FLT_A2, OUTPUT );
  pinMode( FLT_A3, OUTPUT );
  pinMode( BTN_F1, INPUT );
  pinMode( BTN_F2, INPUT );
  pinMode( BTN_F3, INPUT );

  TIMSK0 = 0; // Disable timer 0 interrupts
  TCCR0A = 0b01000010;
  TCCR0B = 0b00000001;
  
  TIMSK2 = 0; // Disable timer 2 interrupts
  TCCR2A = 0b01000010;
  TCCR2B = 0b00000001;

  limit = 16000000 / SAMPLE_FREQ;
  
  TCCR1A = 0b00000000;
  TCCR1B = 0b00001001;
  OCR1AH = limit >> 8;
  OCR1AL = limit & 0xFF;
  
  // Default filter setup
  filter_mode( 0, 0 );
  filter_mode( 1, 0 );
  filter_q( 0, 0x40 );
  filter_q( 1, 0x40 );
  filter_f( 0, 0x20 );
  filter_f( 1, 0x20 );
  filter_c( 0, 10000 );
  filter_c( 1, 10000 );

  TIMSK1 |=  ( ( 1 << TOIE1  ) | ( 1 << OCIE1A ) );
  
  MIDI.begin();

  sei();  
  
}

void loop( void ) {
  int temp;
  unsigned char n;
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
        freqset( 0 );
        freqset( 1 );
        freqset( 2 );
        freqset( 3 );
        break;
      case CC:
        switch( MIDI.getData1() ) {
          case 1:
            spread = ( ( float )MIDI.getData2() ) * 2;
            freqset( 0 );
            freqset( 1 );
            freqset( 2 );
            freqset( 3 );
            break;
          case 10:
            OCR2A = ( 255 - MIDI.getData2() ) << 1;
            break;
          case 11:
            filter_q( 0, MIDI.getData2() );
            break;
          case 12:
            filter_f( 0, ( 255 - MIDI.getData2() ) >> 1 );
            break;
          case 13:
            filter_mode( 0, MIDI.getData2() >> 5 );
            break;
          case 14:
            OCR0A = ( 255 - MIDI.getData2() ) << 1;
            break;
          case 15:
            filter_q( 1, MIDI.getData2() );
            break;
          case 16:
            filter_f( 1, ( 255 - MIDI.getData2() ) >> 1 );
            break;
          case 17:
            filter_mode( 1, MIDI.getData2() >> 5 );
            break;
          case 18:
            env_initial = MIDI.getData2() >> 1;
            break;
          case 19:
            env_attack = MIDI.getData2() >> 1;
            break;
          case 20:
            env_decay = MIDI.getData2() >> 1;
            break;
          case 21:
            env_sustain = MIDI.getData2() >> 1;
            break;
          case 22:
            env_release = MIDI.getData2() >> 1;
            break;
          case 23:
            poly = MIDI.getData2() >> 6;
            break;
          case 24:
            port_speed = MIDI.getData2();
            break;
        }
        break;
    }
  }
  while( env_count > 200 ) {
    // run every 1/50 second

    if( poly == 0 && port_dest != port_freq && pm[ 0 ].state != STATE_OFF ) {    
      port_freq += ( ( port_dest - port_freq ) * port_speed / 127.0 );
      freqman( 0, port_freq );
      // copy
      pm[ 1 ] = pm[ 0 ];
      pfreq[ 1 ][ 0 ] = pfreq[ 0 ][ 0 ];
      pfreq[ 1 ][ 1 ] = pfreq[ 0 ][ 1 ];
      pfreq[ 1 ][ 2 ] = pfreq[ 0 ][ 2 ];
      pfreq[ 1 ][ 3 ] = pfreq[ 0 ][ 3 ];
    }
   
    
    env_count -= 200;
    for( n = 0; n!= 4; n++ ) {
      // do envelope handling
      switch( pm[ n ].state ) {
        case STATE_ATTACK:
          pm[ n ].factor += env_attack;
          if( pm[ n ].factor >= 0x40 ) {
            pm[ n ].factor = 0x40;
            pm[ n ].state = STATE_DECAY;
          }
          VCAL( n );
          break;
        case STATE_DECAY:
          pm[ n ].factor -= env_decay;
          if( pm[ n ].factor <= env_sustain ) {
            pm[ n ].factor = env_sustain;
            pm[ n ].state = STATE_SUSTAIN;
          }
          VCAL( n );
          break;
        case STATE_RELEASE:
          pm[ n ].factor -= env_release;
          if( pm[ n ].factor <= 0 ) {
            pm[ n ].factor = 0;
            p_rem( pm[ n ].note );
          }
          VCAL( n );
          break;
      }
    }    

  }
}


