#include "Synth.h"

#define SAMPLE_FREQ 10000

typedef struct {
  float a;
  float b;
} env_t;

typedef struct {
   uint8_t wf;
   env_t vol;
   env_t pitch;
} osc_t;

typedef struct {
  env_t freq;
  uint8_t type;
  uint8_t mode;
  uint8_t q;
} flt_t;

typedef struct {
   osc_t osc;
   flt_t flt[2];
   float time;
} drum_t;

drum_t drummer;

drum_t drum;
uint32_t delta_sg=0, delta_time=0;
float osc, vol;
uint8_t randy[256];
uint8_t pulse = 192;

uint8_t sample(void) {
  int8_t out;
  if(delta_sg) {
    osc += drum.osc.pitch.a;
    if(osc >= 1.0) osc -= 1.0;
    switch(drum.osc.wf) {
      case 0:
        out = randy[(uint8_t)(osc * 0xFF)];
        break;
      case 1:
        out = ((uint8_t)(osc * 0xFF)) ^ 0x80;
        break;
      case 2:
        out = ((uint8_t)(osc > 0.5 ? ((1.0 - osc) * 0x200) : (osc * 0x200))) ^ 0x80;
        break;
      default:
        out = (uint8_t)(osc * 0x7F) > (drum.osc.wf & 0x7F) ? 127 : -128;
        break;
    }

    drum.osc.pitch.a += drum.osc.pitch.b;
    delta_sg--;
    
    return ((uint8_t)(out*vol))^0x80;
  } else{
    return 0x80;
  }
}

void setup(void) {
  uint8_t n=0;
  
  // generate some randoms for noise
  do { randy[n] = rand(); } while(++n);

  // TIMING
  drummer.time          = 1.0;      // time, seconds
                                    
  // OSCILLATOR                                  
  drummer.osc.wf        = 0;        // waveform
                                    // 0=noise
                                    // 1=saw
                                    // 2=triangle
                                    // 128-255=var. pulse (192=square)
  drummer.osc.pitch.a   = 100.0;    // start pitch, hz
  drummer.osc.pitch.b   = 10.0;     // end pitch, hz
  drummer.osc.vol.a     = 0.0;      // start volume, 0-1
  drummer.osc.vol.b     = 1.0;      // end volume, 0-1
               
  // FILTER 0                   
  drummer.flt[0].mode   = FM_FATLP; // filter mode FM_LP/FATLP/BP/FATBP/HP/PHASER
  drummer.flt[0].q      = 50;       // resonance, 0-63
  drummer.flt[0].freq.a = 4000.0;   // start cutoff, hz
  drummer.flt[0].freq.b = 50;       // end cutoff, hz

  // FILTER 1
  drummer.flt[1].mode   = FM_FATLP; // see filter 0
  drummer.flt[1].q      = 50;       // see filter 0
  drummer.flt[1].freq.a = 4000.0;   // see filter 0
  drummer.flt[1].freq.b = 50.0;     // see filter 0
  
  Synth.attachInterrupt(sample, SAMPLE_FREQ);
}

float midi_range(uint8_t cc, float a, float b) {
  return (((float)cc) / 127.0) * (b - a) + a;
}

void loop(void) {
  midi_t *midi = Synth.getMidi();
  uint8_t n;  
  uint32_t delta;
  if(midi) {
    if(midi->message == 0x90) {
      // MIDI: NOTE ON
      cli();
      drum = drummer;
      delta_time = drum.time * SAMPLE_FREQ;
      delta_sg = delta_time;
      for(n=0;n!=2;n++) {
        drum.flt[n].freq.b = (drum.flt[n].freq.b - drum.flt[n].freq.a) / (drum.time * SAMPLE_FREQ);
        Synth.setResonance(n, drum.flt[n].q);
        Synth.setFilterMode(n, drum.flt[n].mode);
      }
      osc = (drum.osc.wf == 2) ? 0.25 : 0.0;
      drum.osc.vol.b = (drum.osc.vol.b - drum.osc.vol.a) / (drum.time * SAMPLE_FREQ);
      drum.osc.pitch.a /= SAMPLE_FREQ;
      drum.osc.pitch.b = ((drum.osc.pitch.b / SAMPLE_FREQ) - drum.osc.pitch.a) / (drum.time * SAMPLE_FREQ);
      vol = drum.osc.vol.a;
      sei();
    } else if(midi->message == 0xB0) {
      // MIDI: CONTROL CHANGE
      switch(midi->data1) {
        case 10: drummer.osc.pitch.b   = midi_range(midi->data2, 0, 6000);   break;
        case 15: drummer.osc.pitch.a   = midi_range(midi->data2, 0, 6000);   break;
        case 11: drummer.osc.vol.b     = midi_range(midi->data2, 0, 1);      break;
        case 16: drummer.osc.vol.a     = midi_range(midi->data2, 0, 1);      break;
        case 12: drummer.flt[0].freq.b = midi_range(midi->data2, 10, 19990); break;
        case 17: drummer.flt[0].freq.a = midi_range(midi->data2, 10, 19990); break;
        case 13: drummer.flt[1].freq.b = midi_range(midi->data2, 10, 19990); break;
        case 18: drummer.flt[1].freq.a = midi_range(midi->data2, 10, 19990); break;
        case 20: drummer.flt[0].q      = midi->data2 >> 2;                   break;                       
        case 21: drummer.flt[0].mode   = midi->data2 / 22;                   break;                       
        case 22: drummer.flt[1].q      = midi->data2 >> 2;                   break;                       
        case 23: drummer.flt[0].mode   = midi->data2 / 22;                   break;                       
        case 25: drummer.osc.wf        = midi->data2 >> 5;                   break;                       
        case 26: pulse                 = midi->data2 | 0x80;                 break;                       
        case 27: drummer.time          = midi_range(midi->data2, 0.05, 2);   break;
      }
      if(drummer.osc.wf > 2) drummer.osc.wf = pulse;
    }
    Synth.freeMidi();
  }
  // slow update of volume, cutoff - offloads sample generation interrupt
  cli();
  delta = delta_time - delta_sg;
  sei();
  vol = drum.osc.vol.a + drum.osc.vol.b * delta;
  Synth.setCutoff(0, drum.flt[0].freq.a + drum.flt[0].freq.b * delta);
  Synth.setCutoff(1, drum.flt[1].freq.a + drum.flt[0].freq.b * delta);
}
