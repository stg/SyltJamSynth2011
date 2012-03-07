#define PCM_LCH 10
#define PCM_CLK 13
#define PCM_SDA A3

#define FLT_CLK A5

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
#define SAMPLE_FREQ     8250

ISR( TIMER1_COMPA_vect ) {
  static   uint8_t out = 0x00; // final out

  out += 33;
  // clock out 8-bits of pcm data
  // IMPORTANT: the 0x## list for each bit is shifted in this code
  //            due to a hardware error on my board. for a correctly
  //            constructed board the list should work it's way down
  //            from 0x80 to 0x01 - sorry about this!
  digitalWrite( PCM_LCH, HIGH );
  digitalWrite( PCM_LCH, LOW );
  digitalWrite( PCM_CLK, LOW );
  digitalWrite( PCM_SDA, ( out & 0x80 ) );
  digitalWrite( PCM_CLK, HIGH );
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
}

// Sets filter mode for both filters
// Bit pattern:
//   F0 F1 (or other way around)
// --HBLHBL (or --LBHLBH)
void filter_sel( uint8_t sel ) {
  cli();
  digitalWrite( PCM_LCH, HIGH );
  digitalWrite( PCM_LCH, LOW );
  digitalWrite( FLT_CLK, LOW );
  digitalWrite( PCM_SDA, ( sel & 0x80 ) );
  digitalWrite( FLT_CLK, HIGH );
  digitalWrite( FLT_CLK, LOW );
  digitalWrite( PCM_SDA, ( sel & 0x40 ) );
  digitalWrite( FLT_CLK, HIGH );
  digitalWrite( FLT_CLK, LOW );
  digitalWrite( PCM_SDA, ( sel & 0x20 ) );
  digitalWrite( FLT_CLK, HIGH );
  digitalWrite( FLT_CLK, LOW );
  digitalWrite( PCM_SDA, ( sel & 0x10 ) );
  digitalWrite( FLT_CLK, HIGH );
  digitalWrite( FLT_CLK, LOW );
  digitalWrite( PCM_SDA, ( sel & 0x08 ) );
  digitalWrite( FLT_CLK, HIGH );
  digitalWrite( FLT_CLK, LOW );
  digitalWrite( PCM_SDA, ( sel & 0x04 ) );
  digitalWrite( FLT_CLK, HIGH );
  digitalWrite( FLT_CLK, LOW );
  digitalWrite( PCM_SDA, ( sel & 0x02 ) );
  digitalWrite( FLT_CLK, HIGH );
  digitalWrite( FLT_CLK, LOW );
  digitalWrite( PCM_SDA, ( sel & 0x01 ) );
  digitalWrite( FLT_CLK, HIGH );
  sei();
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
	lb = ( ( ( float )( CPU_CLOCK / 2 ) ) / ( freq * ps ) ) - 1;
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

void setup( void ) {
  uint16_t isr_rr;
  pinMode( PCM_LCH, OUTPUT );
  pinMode( PCM_SDA, OUTPUT );
  pinMode( PCM_CLK, OUTPUT );
  pinMode( FLT_CLK, OUTPUT );
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
  filter_mode( 1, 3 );
  filter_q( 0, 0x10 ); // TODO
  filter_q( 1, 0x10 );
  filter_f( 0, 0x20 );
  filter_f( 1, 0x20 );
  filter_c( 0, 1000 ); // LP
  filter_c( 1, 1000 ); // HP

  TIMSK1 = 1 << OCIE1A;

  // Enable interrupts
  sei();  
  
}

void loop( void ) {
  filter_sel( 0b00001100 );  
}
