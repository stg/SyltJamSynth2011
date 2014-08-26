/*!
 *  @file     Synth.cpp
 *  Project   Synth Library
 *  @brief    Synth Library for the Arduino
 *  Version   1.0
 *  @author   Davey Taylor
 *  @date     2012-03-06 (YYYY-MM-DD)

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
#include "Arduino.h"
#include <avr/eeprom.h>

Synth_Class Synth;
ß
Synth_Class::Synth_Class() { }
Synth_Class::~Synth_Class() { }

uint8_t sample( void );

// MIDI frequency table
const float noteTable[] = {
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

#define SYSEX 0xF0

static const uint8_t len_channel[ 8 ] = { 2, 2, 2, 2, 1, 1, 2, 0 };
static const uint8_t len_system[ 8 ]  = { SYSEX, 1, 2, 1, 0, 0, 0, 0 };

static uint8_t typebits;

static uint8_t midi_length;
static uint8_t midi_count;
static midi_t midi;
static midi_t midi_read;

static midi_t queue[ MIDI_QUEUE_SIZE ];
static midi_t *p_queue_head = queue;
static midi_t *p_queue_tail = queue;
static uint8_t volatile midi_free = MIDI_QUEUE_SIZE;

static uint8_t ( *p_sampler )();

// Pushes a complete MIDI message into queue
// temp is only used when pushing interleaved real-time messages
// but restoring it here allows for some nice code size optimizations
#define PUSH() \
  { if( midi_free ) { \
    *p_queue_head = midi; \
    midi.message = temp; \
    if( ++p_queue_head ==  &queue[ MIDI_QUEUE_SIZE ] ) p_queue_head = queue; \
    midi_free--; \
  } }

// Writes data 0...3 to filter address 0...7 (filter 0) or 80...87 (filter 1)
void filter_write( uint8_t addr, uint8_t data ) {
  fastWrite( FLT_A3, ( addr & 0x80 ? HIGH : LOW ) );
  fastWrite( FLT_A2, ( addr & 0x04 ? HIGH : LOW ) );
  fastWrite( FLT_A1, ( addr & 0x02 ? HIGH : LOW ) );
  fastWrite( FLT_A0, ( addr & 0x01 ? HIGH : LOW ) );
  fastWrite( FLT_D1, ( data & 0x02 ? HIGH : LOW ) );
  fastWrite( FLT_D0, ( data & 0x01 ? HIGH : LOW ) );
  fastWrite( FLT_WR, HIGH );
  fastWrite( FLT_WR, LOW );
}

ISR( TIMER1_COMPA_vect ) {
  uint8_t register data;
  uint8_t register temp; // temp is used to promote optimized code where the
                         // compiler decides to do 16-bit operations on 8-bit
                         // variables. temp increases performance by ~5%.

  // Latch out previous sample & filter selection
  fastWrite( PCM_LCH, HIGH );
  fastWrite( PCM_LCH, LOW );
  
  // Shift out filter selection
  SPDR = typebits;
  
  // Check UART for available data, ignore errors
  temp = UCSR0A & ( 1 << RXC0 );
  
  if( temp ) {
    // Read byte from UART
    data = UDR0;
#ifdef MIDI_ECHO
    UDR0 = data;
#endif
    // SYSEX gets stored in midi_length when a system-exclusive arrives
    if( midi_length == ( uint8_t )SYSEX ) {
      // Ignore until end of system-exclusive is received
      if( data == 0xF7 ) midi_length = 0;
    } else {  
      // Check MSB - all MIDI messages start with a byte where MSB is set
      if( data & 0x80 ) {
        // Check for interleaved real-time messages
        temp = ( data & 0xF8 );
        if( temp == 0xF8 ) {
          // This is a realtime message
          temp = midi.message; // Remember original message, PUSH restores
          midi.message = data; // Replace with real-time message
          PUSH();              // Push message to queue
        } else {
          // Reset count
          midi_count = 0;
          // Check message type
          temp = ( data & 0xF0 );
          if( temp == 0xF0 ) {
            // This is a system message
            midi.message = data; // Entire byte specifies message
            // Get message length from array
            temp = data & 0x07;
            midi_length = *( len_system + temp );
          } else {
            // This is a channel message
            midi.message = data & 0xF0; // High nibble specifies message
            midi.channel = data & 0x0F; // Low nibble specifies channel
            // Get message length from array
            temp = ( data & 0x70 );
            temp >>= 4;
            midi_length = *( len_channel + temp );
            // Check if single byte message
            if( midi_length == 0 ) PUSH(); // Push message to queue
          }
        }
      } else {
        // Data byte received, store at the associated location
        if( midi_count < 2 ) {
          if( midi_count == 0 ) {
            midi.data1 = data;
          } else {
            midi.data2 = data;
          }
          // Count received bytes, check for message complete
          if( ++midi_count == midi_length ) {
            midi_count = 0; // Reset count, allow for running status
            PUSH(); // Push message to queue
          }
        }
      }
    }
  }
  // Shift out sample
  SPDR = p_sampler();
}

void Synth_Class::attachInterrupt( uint8_t ( *p_cb )(), uint16_t sample_rate ) {
  uint16_t isr_rr;

  // Requires a minimum of 3125 for MIDI
  if( sample_rate < 3125 ) sample_rate = 3125;

  p_sampler = p_cb;

  // Set pin modes
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

  cli();

  // Disable all timer interrupts 
  TIMSK0 = 0;
  TIMSK1 = 0;
  TIMSK2 = 0;

  // Set up timers (T0, T2) to generate filter clock
  TCCR0A = 0b01000010;
  TCCR0B = 0b00000001;
  TCCR2A = 0b00010010;
  TCCR2B = 0b00000001;
  OCR0B  = 0;
  
  // Set up timer (T1) to generate interrupt at SAMPLE_FREQ hz
  isr_rr = F_CPU / sample_rate;
  TCCR1A = 0b00000000;     // Set timer mode
  TCCR1B = 0b00001001;
  OCR1AH = isr_rr >> 8;    // Set frequency
  OCR1AL = isr_rr & 0xFF;
  TIMSK1 = 1 << OCIE1A;    // Enable interrupt

  // Enable SPI
  SPCR = 0b01010000; 
  SPSR = 0b00000001;

  // Enable serial(MIDI) communication
  Serial.begin( 31250 );

  // Disable default Arduino Serial.read behaviour
  UCSR0B &= ~( 1 << RXCIE0 );

  // Set filter defaults
  setResonance( 0, 0x14 );
  setResonance( 1, 0x14 );
  setShift( 0, 0x20 );
  setShift( 1, 0x20 );
  setCutoff( 0, 200 );
  setCutoff( 1, 10000 );
  setFilter( 0, FILTER_HP );
  setFilter( 1, FILTER_LP );
  setMode( 0, 2 );
  setMode( 1, 0 );

  // Enable interrupts
  sei();    
}

uint8_t Synth_Class::midiAvailable( void ) {
  return MIDI_QUEUE_SIZE - midi_free;
}

void Synth_Class::midiRead( void ) {
  midi_read = *p_queue_tail;
  if( ++p_queue_tail ==  &queue[ MIDI_QUEUE_SIZE ] ) p_queue_tail = queue;
  cli();
  midi_free++;
  sei();
}

uint8_t Synth_Class::midiMessage( void ) {
  return midi_read.message;
}

uint8_t Synth_Class::midiChannel( void ) {
  return midi_read.channel;
}

uint8_t Synth_Class::midiData1( void ) {
  return midi_read.data1;
}

uint8_t Synth_Class::midiData2( void ) {
  return midi_read.data2;
}

midi_t* Synth_Class::getMidi( void ) {
  midi_t *p_midi;
  if( midi_free != MIDI_QUEUE_SIZE ) {
    p_midi = p_queue_tail;
  } else {
    p_midi = NULL;
  }
  return p_midi;
}

void Synth_Class::freeMidi( void ) {
  if( ++p_queue_tail ==  &queue[ MIDI_QUEUE_SIZE ] ) p_queue_tail = queue;
 	cli();
  midi_free++;
  sei();
}

// Sets the filter FILTER_LP/FILTER_BP/FILTER_HP for filter 0...1
void Synth_Class::setFilter( uint8_t filter, uint8_t type ) {
  if( filter ) {
    typebits &= 0x07;
    typebits |= type << 3;
  } else {
    typebits &= 0x38;
    typebits |= type;
  }
}  

// Sets the center frequency using a direct (much faster) approach
void Synth_Class::setClock( uint8_t filter, uint8_t psb, uint8_t ocr ) {
    if( filter ) {
      TCCR0B = ( TCCR0B & 0xF8 ) | psb;
      OCR0A = ocr;
      TCNT0 = 0;
    } else {
      TCCR2B = ( TCCR2B & 0xF8 ) | psb;
      OCR2A = ocr;
      TCNT2 = 0;
    }
}

// Sets the center frequency 4...40000hz for filter 0...1
void Synth_Class::setCutoff( uint8_t filter, float freq ) {
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
  lb = ( ( ( float )( F_CPU / 2 ) ) / ( freq * ps ) ) - 1;
  if( filter ) {
    if( psbm[ 0 ] != psb ) {
      psbm[ 0 ] = psb;
      TCCR0B = ( TCCR2B & 0xF8 ) | psb;
    }
    OCR0A = lb;
    TCNT0 = 0;
  } else {
    if( psbm[ 1 ] != psb ) {
      psbm[ 1 ] = psb;
      TCCR2B = ( TCCR2B & 0xF8 ) | psb;
    }
    OCR2A = lb;
    TCNT2 = 0;
  }
}

// Sets mode for filter 0...1 to 0...3
void Synth_Class::setMode( uint8_t filter, uint8_t mode ) {
  filter_write( ( filter ? 0x80 : 0x00 ), mode );
}

// Sets f0-value of filter 0...1 to 0...3F
void Synth_Class::setShift( uint8_t filter, uint8_t mode ) {
  filter = ( filter ? 0x80 : 0x00 ) + 1;
  filter_write( filter++, mode       );
  filter_write( filter++, mode >>= 2 );
  filter_write( filter++, mode >>= 2 );
}

// Sets q-value of filter 0...1 to 0...7F
void Synth_Class::setResonance( uint8_t filter, uint8_t q ) {
  filter = ( filter ? 0x80 : 0x00 ) + 4;
  filter_write( filter++, q       );
  filter_write( filter++, q >>= 2 );
  filter_write( filter++, q >>= 2 );
  filter_write( filter++, q >>= 2 );
}

// Sets filter and mode-value for filter 0...1
void Synth_Class::setFilterMode( uint8_t filter, uint8_t fm ) {
  switch( fm ) {
    case FM_LP:
      this->setFilter( filter, FILTER_LP );
      this->setMode( filter, 0 );
      break;
    case FM_FATLP:
      this->setFilter( filter, FILTER_LP );
      this->setMode( filter, 3 );
      break;
    case FM_BP:
      this->setFilter( filter, FILTER_BP );
      this->setMode( filter, 0 );
      break;
    case FM_FATBP:
      this->setFilter( filter, FILTER_BP );
      this->setMode( filter, 3 );
      break;
    case FM_HP:
      this->setFilter( filter, FILTER_HP );
      this->setMode( filter, 2 );
      break;
    case FM_PHASER:
      this->setFilter( filter, FILTER_HP );
      this->setMode( filter, 0 );
      break;
  }
}

// Save patch data to EEPROM
bool Synth_Class::savePatch( uint16_t id, uint8_t slot, void* data, uint16_t size ) {
  uint16_t n;
  uint16_t addr = slot * 42;
  if(slot < 24) {
  	eeprom_write_byte((uint8_t*)(addr++), id >> 8);
  	eeprom_write_byte((uint8_t*)(addr++), id);
    for(n = 0; n < size; n++) {
  		eeprom_write_byte((uint8_t*)(addr++), ((uint8_t*)data)[n]);
    }
    return true;
  }
  return false;
}

// Load patch data from EEPROM
bool Synth_Class::loadPatch( uint16_t id, uint8_t slot, void* data, uint16_t size ) {
  uint16_t n;
  uint16_t addr = slot * 42;
  if(slot < 24) {
    if(eeprom_read_byte((uint8_t*)(addr++)) == (uint8_t)(id >> 8)) {
      if(eeprom_read_byte((uint8_t*)(addr++)) == (uint8_t)id) {
        for(n = 0; n < size; n++) {
          ((uint8_t*)data)[n] = eeprom_read_byte((uint8_t*)(addr++));
        }
        return true;
      }
    }
  }
  return false;
}