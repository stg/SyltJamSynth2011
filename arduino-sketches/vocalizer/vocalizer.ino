// Formant synthesizer for SJS-ONE based on a concept from
// Auduino, the Lo-Fi granular synthesiser
// by Peter Knight, Tinker.it http://tinker.it

#include "Synth.h"

#define SAMPLE_FREQ 31250

word syncPhaseAcc, syncPhaseInc;
word grain1PhaseAcc, grain1PhaseInc, grain1Amp;
byte grain1Decay;
word grain2PhaseAcc, grain2PhaseInc, grain2Amp;
byte grain2Decay;
byte note = 128;

word cci[4];

void setup() {
  Synth.attachInterrupt(sample, SAMPLE_FREQ);
  for(byte n = 0; n != 2; n++) {
    Synth.setFilterMode(n, FM_FATBP);
    Synth.setCutoff(n, 4000.0);
    Synth.setResonance(n, 5);
  }
}

void loop() {
  midi_t *midi = Synth.getMidi();
  if(midi) {
    if(midi->message == NoteOn) {
      note = midi->data1;
      syncPhaseInc = (uint16_t)(noteTable[note] * (65536.0 / SAMPLE_FREQ));
      grain1PhaseInc = cci[0]; grain1Decay = cci[1];
      grain2PhaseInc = cci[2]; grain2Decay = cci[3];
    } else if(midi->message == NoteOff) {
      note = 128;
      syncPhaseInc = 0;
      grain1PhaseInc = grain1Decay = 0;
      grain2PhaseInc = grain2Decay = 0;
    } else if(midi->message == PitchBend) {
      // TBD
    } else if(midi->message == ControlChange) {
      byte cc = midi->data1, val = midi->data2;
      if(note != 128) {
             if(cc == 10) cci[0] = grain1PhaseInc = val << 2;
        else if(cc == 11) cci[1] = grain1Decay    = val << 1;
        else if(cc == 20) cci[2] = grain2PhaseInc = val << 2;
        else if(cc == 21) cci[3] = grain2Decay    = val << 0;
      }
      if(midi->data1 == 30) {
        Synth.setCutoff(0, val * 40 + 1000);
        Synth.setCutoff(1, val * 40 + 1000);
      } else if(midi->data1 == 31) {
        Synth.setResonance(0, val >> 1);
        Synth.setResonance(1, val >> 1);
      }
    }
    Synth.freeMidi();
  }
}

byte sample() {
  byte value;
  word output;

  syncPhaseAcc += syncPhaseInc;
  if(syncPhaseAcc < syncPhaseInc) {
    grain1PhaseAcc = grain2PhaseAcc = 0;
    grain1Amp = grain2Amp = 0x7FFF;
  }

  grain1PhaseAcc += grain1PhaseInc;
  grain2PhaseAcc += grain2PhaseInc;

  value = (grain1PhaseAcc >> 7) & 0xFF;
  if(grain1PhaseAcc & 0x8000) value = ~value;
  output = value * (grain1Amp >> 8);

  value = (grain2PhaseAcc >> 7) & 0xFF;
  if(grain2PhaseAcc & 0x8000) value = ~value;
  output += value * (grain2Amp >> 8);

  grain1Amp -= (grain1Amp >> 8) * grain1Decay;
  grain2Amp -= (grain2Amp >> 8) * grain2Decay;

  return output >> 8;
}
