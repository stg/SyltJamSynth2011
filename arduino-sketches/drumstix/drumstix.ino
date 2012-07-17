#include "Synth.h"

#define SAMPLE_FREQ 22050

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
//  uint8_t q;
} flt_t;

typedef struct {
   osc_t osc[2];
   flt_t flt[2];
   float time;
   bool fm;
} drum_t;

typedef struct {
  uint32_t count;
  uint32_t cycle;
  uint32_t increment;
  uint8_t volume;
} gen_t;

drum_t drummer;

drum_t drum;
uint32_t delta_sg=0, delta_time=0;
gen_t osc[2];
uint8_t randy[256];
uint8_t pulse[2] = {192, 192};
int8_t out;
float range=500.0/6000.0;

uint8_t sample(void) {
  int8_t a,b;
  int16_t eb;
  uint8_t s;
  if(delta_sg) {
    osc[1].count += osc[1].cycle;
    s = osc[1].count >> 24;
    switch(drum.osc[1].wf) {
      case 0:
        b = randy[s];
        break;
      case 1:
        b = s ^ 0x80;
        break;
      case 2:
        b = (((s & 0x80) ? ~s : s) << 1) ^ 0x80;
        break;
      default:
        b = s > drum.osc[1].wf ? 127 : -128;
        break;
    }
    osc[1].cycle += osc[1].increment;
    
    //b = ((((int16_t)b) * osc[1].volume) >> 8);
    eb = (((int16_t)b) * osc[1].volume);

    osc[0].count += osc[0].cycle;

    //if(drum.fm) *(((uint8_t*)&osc[0].count)+3) += b;
    if(drum.fm) *(((uint16_t*)&osc[0].count)+1) += eb >> 2;

    s = osc[0].count >> 24;
    switch(drum.osc[0].wf) {
      case 0:
        a = randy[s];
        break;
      case 1:
        a = s ^ 0x80;
        break;
      case 2:
        a = (((s & 0x80) ? ~s : s) << 1) ^ 0x80;
        break;
      default:
        a = s > drum.osc[0].wf ? 127 : -128;
        break;
    }
    osc[0].cycle += osc[0].increment;
    a = ((((int16_t)a) * osc[0].volume) >> 8);
    if(!drum.fm) a += (eb >> 8);
//    if(!drum.fm) a += b;
    out=a;

    delta_sg--;
  } else{
    // de-click
    if(out < 0) out++;
    else if(out > 0) out--;
  }
  return out ^ 0x80;
}

void setup(void) {
  uint8_t n=0;
  
  // generate some randoms for noise
  do { randy[n] = rand(); } while(++n);

  // TIMING
  drummer.time          = 1.0;      // time, seconds
                                    
  // OSCILLATOR                                  
  drummer.osc[0].wf     = 0;        // waveform
                                    // 0=noise
                                    // 1=saw
                                    // 2=triangle
                                    // 128-255=var. pulse (192=square)
  drummer.osc[0].pitch.a   = 100.0; // start pitch, hz
  drummer.osc[0].pitch.b   = 10.0;  // end pitch, hz
  drummer.osc[0].vol.a     = 0.0;   // start volume, 0-1
  drummer.osc[0].vol.b     = 1.0;   // end volume, 0-1

  drummer.osc[1].vol.a     = 0.0;   // end volume, 0-1
  drummer.osc[1].vol.b     = 0.0;   // end volume, 0-1

  // FILTER 0                   
  drummer.flt[0].mode   = FM_FATLP; // filter mode FM_LP/FATLP/BP/FATBP/HP/PHASER
//  drummer.flt[0].q      = 50;       // resonance, 0-63
  drummer.flt[0].freq.a = 4000.0;   // start cutoff, hz
  drummer.flt[0].freq.b = 50;       // end cutoff, hz

  // FILTER 1
  drummer.flt[1].mode   = FM_FATLP; // see filter 0
//  drummer.flt[1].q      = 50;       // see filter 0
  drummer.flt[1].freq.a = 4000.0;   // see filter 0
  drummer.flt[1].freq.b = 50.0;     // see filter 0
  
  Synth.attachInterrupt(sample, SAMPLE_FREQ);
  Synth.setResonance(0, 0x1F);
  Synth.setResonance(1, 0x1F);
}

float midi_range(uint8_t cc, float a, float b) {
  return (((float)cc) / 127.0) * (b - a) + a;
}

float get_range(uint8_t id) {
  switch(id) {
    case 0: return 25.0/6000.0; // LFO
    case 1: return 500.0/6000.0; // MFO
    case 2: return 6000.0/6000.0; // HFO
    case 3: return 20000.0/6000.0; // AFO
  }
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
//        Synth.setResonance(n, drum.flt[n].q);
        Synth.setFilterMode(n, drum.flt[n].mode);

        osc[n].count = (drum.osc[n].wf == 2) ? 0x40000000 : 0;
        drum.osc[n].vol.b = (drum.osc[n].vol.b - drum.osc[n].vol.a) / (drum.time * SAMPLE_FREQ);
        drum.osc[n].pitch.a /= SAMPLE_FREQ;
        drum.osc[n].pitch.b = ((drum.osc[n].pitch.b / SAMPLE_FREQ) - drum.osc[n].pitch.a) / (drum.time * SAMPLE_FREQ);
        if(n == 1) {
          drum.osc[n].pitch.a *= range;
          drum.osc[n].pitch.b *= range;
        }
        osc[n].cycle = drum.osc[n].pitch.a * 0xFFFFFFFF;
        osc[n].increment = drum.osc[n].pitch.b * 0xFFFFFFFF;
        osc[n].volume = (uint8_t)(drum.osc[n].vol.a * 255.0);
      }      
      Synth.setCutoff(0, drum.flt[0].freq.a + drum.flt[0].freq.b * delta);
      Synth.setCutoff(1, drum.flt[1].freq.a + drum.flt[1].freq.b * delta);
      sei();
    } else if(midi->message == 0xB0) {
      // MIDI: CONTROL CHANGE
      switch(midi->data1) {
        case 10: drummer.osc[0].pitch.b = midi_range(midi->data2, 0, 6000);   break;
        case 11: drummer.osc[0].pitch.a = midi_range(midi->data2, 0, 6000);   break;
        case 12: drummer.osc[0].vol.b   = midi_range(midi->data2, 0, 1);      break;
        case 13: drummer.osc[0].vol.a   = midi_range(midi->data2, 0, 1);      break;
        case 14: drummer.osc[0].wf      = midi->data2 >> 5;                   break;
        case 15: pulse[0]               = midi->data2 | 0x80;                 break;
        case 16: drummer.fm             = midi->data2 > 0x20;                 break;

        case 20: drummer.osc[1].pitch.b = midi_range(midi->data2, 0, 6000);   break;
        case 21: drummer.osc[1].pitch.a = midi_range(midi->data2, 0, 6000);   break;
        case 22: drummer.osc[1].vol.b   = midi_range(midi->data2, 0, 1);      break;
        case 23: drummer.osc[1].vol.a   = midi_range(midi->data2, 0, 1);      break;
        case 24: drummer.osc[1].wf      = midi->data2 >> 5;                   break;
        case 25: pulse[1]               = midi->data2 | 0x80;                 break;
        case 26: range                  = get_range(midi->data2 >> 5);        break;

        case 30: drummer.flt[0].freq.b  = midi_range(midi->data2, 10, 19990); break;
        case 31: drummer.flt[0].freq.a  = midi_range(midi->data2, 10, 19990); break;
        //case 32: drummer.flt[0].q       = midi->data2 >> 1;                   break;
        case 32: drummer.flt[0].mode    = midi->data2 / 22;                   break;
        case 40: drummer.flt[1].freq.b  = midi_range(midi->data2, 10, 19990); break;
        case 41: drummer.flt[1].freq.a  = midi_range(midi->data2, 10, 19990); break;
        //case 42: drummer.flt[1].q       = midi->data2 >> 1;                   break;
        case 42: drummer.flt[1].mode    = midi->data2 / 22;                   break;
        case 1: drummer.time            = midi_range(midi->data2, 0.05, 2);   break;
      }
      if(drummer.osc[0].wf > 2) drummer.osc[0].wf = pulse[0];
      if(drummer.osc[1].wf > 2) drummer.osc[1].wf = pulse[1];
    }
    Synth.freeMidi();
  }
  // slow update of volume, cutoff - offloads sample generation interrupt
  cli();
  delta = delta_time - delta_sg;
  sei();
  osc[0].volume = (uint8_t)((drum.osc[0].vol.a + drum.osc[0].vol.b * delta) * 255.0);
  osc[1].volume = (uint8_t)((drum.osc[1].vol.a + drum.osc[1].vol.b * delta) * 255.0);
  Synth.setCutoff(0, drum.flt[0].freq.a + drum.flt[0].freq.b * delta);
  Synth.setCutoff(1, drum.flt[1].freq.a + drum.flt[1].freq.b * delta);
}
