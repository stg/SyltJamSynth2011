
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

// Patch memory & defaults
uint8_t  patches[24][20] = {
  {0, 2, 127, 0, 0, 0, 10, 10, 0, 0, 0, 32, 0, 25, 33, 0, 25, 33, 60}
};

// Save sound
uint8_t  savesound[20] = {0, 2, 127, 0, 0, 0, 10, 10, 0, 0, 0, 32, 0, 25, 33, 0, 25, 33, 60};

uint8_t *editing = &patches[0][0];
uint8_t savestate = 0;

drum_t   drum;
uint32_t delta_sg = 0, delta_time = 0;
gen_t    osc[2];
uint8_t  randy[256];
uint8_t  pulse[2] = {192, 192};
int8_t   out;
float    range = 500.0 / 6000.0; // MFO

uint8_t sample(void) {
  int8_t a, b;
  int16_t bwide;
  uint8_t s;
  // Are we playing?
  if(delta_sg) {
    // Progress oscillator B
    osc[1].count += osc[1].cycle;
    s = osc[1].count >> 24;
    // Oscillator B waveform generation
    switch(drum.osc[1].wf) {
      case 0:  b = randy[s];                            break;
      case 1:  b = s ^ 0x80;                            break;
      case 2:  b = (((s & 0x80) ? ~s : s) << 1) ^ 0x80; break;
      default: b = s > drum.osc[1].wf ? 127 : -128;     break;
    }
    osc[1].cycle += osc[1].increment;
    // Oscillator B volume
    bwide = (((int16_t)b) * osc[1].volume);
    // Progress oscillator A
    osc[0].count += osc[0].cycle;
    // Frequency modulate by oscillator B
    if(drum.fm) *(((uint16_t*)&osc[0].count) + 1) += bwide >> 2;
    s = osc[0].count >> 24;
    // Oscillator A waveform generation
    switch(drum.osc[0].wf) {
      case 0:  a = randy[s];                            break;
      case 1:  a = s ^ 0x80;                            break;
      case 2:  a = (((s & 0x80) ? ~s : s) << 1) ^ 0x80; break;
      default: a = s > drum.osc[0].wf ? 127 : -128;     break;
    }
    osc[0].cycle += osc[0].increment;
    // Oscillator A volume
    a = ((((int16_t)a) * osc[0].volume) >> 8);
    // Simply mix A and B when not frequency modulating
    if(!drum.fm) a += (bwide >> 8);
    out = a;
    // Decrease play time
    delta_sg--;
  } else{
    // De-clicker (ramps output back to 0 in ~6ms max)
    if(out < 0) out++;
    else if(out > 0) out--;
  }
  return out ^ 0x80;
}

void setup(void) {
  uint8_t n=0;
 
  // Generate some randoms for noise
  do { randy[n] = rand(); } while(++n);
 
  // Load patch memory - 0x6453 is unique for THIS SYNTH ONLY, do not reuse
  Synth.loadPatch(0x6453, 12, patches, 480);
 
  // Initialize synth
  Synth.attachInterrupt(sample, SAMPLE_FREQ);
 
  // Set fixed resonance
  Synth.setResonance(0, 0x1F);
  Synth.setResonance(1, 0x1F);
 
  pinMode(2, INPUT);
  pinMode(12, OUTPUT);
}

// Scales a MIDI CC value (0-127) to the range (a-b)
float midi_range(uint8_t cc, float a, float b) {
  return (((float)cc) / 127.0) * (b - a) + a;
}

// Returns the frequency ranges for our different oscillator range settings
float get_range(uint8_t id) {
  switch(id) {
    case 0:  return 25.0    / 6000.0; // LFO
    case 1:  return 500.0   / 6000.0; // MFO
    case 2:  return 6000.0  / 6000.0; // HFO
    default: return 20000.0 / 6000.0; // AFO
  }
}

void load_patch(uint8_t *patch) {
  uint8_t n; 
  float fmul;
  drum.osc[0].pitch.b = midi_range(*(patch++), 0, 6000);
  drum.osc[0].pitch.a = midi_range(*(patch++), 0, 6000);
  drum.osc[0].vol.b   = midi_range(*(patch++), 0, 1);
  drum.osc[0].vol.a   = midi_range(*(patch++), 0, 1);
  drum.osc[0].wf      = *(patch++);
  drum.fm             = *(patch++) > 0x20;
  drum.osc[1].pitch.b = midi_range(*(patch++), 0, 6000);
  drum.osc[1].pitch.a = midi_range(*(patch++), 0, 6000);
  drum.osc[1].vol.b   = midi_range(*(patch++), 0, 1);
  drum.osc[1].vol.a   = midi_range(*(patch++), 0, 1);
  drum.osc[1].wf      = *(patch++);
  fmul                = get_range(*(patch++) >> 5);
  drum.flt[0].freq.b  = midi_range(*(patch++), 10, 19990);
  drum.flt[0].freq.a  = midi_range(*(patch++), 10, 19990);
  drum.flt[0].mode    = *(patch++) / 22;
  drum.flt[1].freq.b  = midi_range(*(patch++), 10, 19990);
  drum.flt[1].freq.a  = midi_range(*(patch++), 10, 19990);
  drum.flt[1].mode    = *(patch++) / 22;
  drum.time           = midi_range(*(patch++), 0.05, 2);
  drum.osc[1].pitch.b *= fmul;
  drum.osc[1].pitch.a *= fmul;

  cli();
  delta_time = drum.time * SAMPLE_FREQ;
  delta_sg = delta_time;
  for(n = 0; n != 2; n++) {
    drum.flt[n].freq.b = (drum.flt[n].freq.b - drum.flt[n].freq.a) / (drum.time * SAMPLE_FREQ);
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
  Synth.setCutoff(0, drum.flt[0].freq.a);
  Synth.setCutoff(1, drum.flt[1].freq.a);
  sei();

}

// Where the fun stuff happens!
void loop(void) {
    midi_t *midi = Synth.getMidi(); // Retrieve MIDI message queue pointer
  uint32_t delta;
 
 
  // Trigger on 2, patch is cv on A0 (0-5V)
  static bool last_trig = 0;
  bool trig = digitalRead(2);
  if(trig && !last_trig) {
    // Got a MIDI: NOTE ON message
    editing = &patches[analogRead(A0) / 43][0];
    load_patch(editing);
  }
  last_trig = trig;
 
  if(midi) {
    if(savestate != 3) {
      if(midi->message == 0x90) {
        // Got a MIDI: NOTE ON message
        digitalWrite(12, HIGH); // Trig out on 12 - HIGH
        editing = &patches[midi->data1 % 24][0];
        load_patch(editing);
        digitalWrite(12, LOW); // Trig out - reset to LOW
      } else if(midi->message == 0xB0) {
        // Got a MIDI: CONTROL CHANGE message
        switch(midi->data1) {
          case 10: editing[ 0] = midi->data2; break;
          case 11: editing[ 1] = midi->data2; break;
          case 12: editing[ 2] = midi->data2; break;
          case 13: editing[ 3] = midi->data2; break;
          case 14: editing[ 4] = midi->data2 >> 5; break;
          case 15: pulse[0] = midi->data2 | 0x80; break;
          case 16: editing[ 5] = midi->data2; break;
          case 20: editing[ 6] = midi->data2; break;
          case 21: editing[ 7] = midi->data2; break;
          case 22: editing[ 8] = midi->data2; break;
          case 23: editing[ 9] = midi->data2; break;
          case 24: editing[10] = midi->data2 >> 5; break;
          case 25: pulse[1] = midi->data2 | 0x80; break;
          case 26: editing[11] = midi->data2; break;
          case 30: editing[12] = midi->data2; break;
          case 31: editing[13] = midi->data2; break;
          case 32: editing[14] = midi->data2; break;
          case 40: editing[15] = midi->data2; break;
          case 41: editing[16] = midi->data2; break;
          case 42: editing[17] = midi->data2; break;
          case 1:  editing[18] = midi->data2; break;
          case 127: // Enters into "save mode" when CC 127 is increased above 84
            if(savestate == 1 && midi->data2 >  84) {
              savestate = 3;
              savesound[18] = 40;
            } else savestate = midi->data2 > 84 ? 2 : 1;
        }
        if(editing[ 4] > 2) editing[ 4] = pulse[0];
        if(editing[10] > 2) editing[10] = pulse[1];
      }
    } else if(midi->message == 0xB0 && midi->data1 == 127 && midi->data2 <= 84) {
      // Abort "save state"
      savestate = 1;
    }
    Synth.freeMidi(); // Free MIDI message so the queue doesn't fill up
  }
 
  // Patch memory save routine
  if(savestate == 3) {
    if(delta_sg == 0) {
      if(savesound[18] <= 3) {
        // Save patch memory - 0x6453 is unique for THIS SYNTH ONLY, do not reuse
        Synth.savePatch(0x6453, 12, patches, 480);
        savestate = 0;
      } else {
        savesound[18] *= 0.8;
        load_patch(savesound);
      }
    }     
  } 
 
  // Update volume & cutoff on a "when there is time" basis.
  // Putting this here instead of in the sample generator code offloads the sample
  // generation interrupt allowing for a much higher sample-rate.
  cli();
  delta = delta_time - delta_sg;
  sei();
  osc[0].volume = (uint8_t)((drum.osc[0].vol.a + drum.osc[0].vol.b * delta) * 255.0);
  osc[1].volume = (uint8_t)((drum.osc[1].vol.a + drum.osc[1].vol.b * delta) * 255.0);
  Synth.setCutoff(0, drum.flt[0].freq.a + drum.flt[0].freq.b * delta);
  Synth.setCutoff(1, drum.flt[1].freq.a + drum.flt[1].freq.b * delta);
  // Mutate noise
  randy[rand() & 0xFF] = rand();
}

