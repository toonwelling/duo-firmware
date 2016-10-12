/*
  DATO DUO firmware
  (c) 2016, David Menting <david@dato.mu>

  Hardware:
  - Runs on a Teensy 3.1/3.2 or on the DUO brains with Kinetis K20DX256
  - Output goes to the K20's 12 bit DAC output
  - A button matrix is connected to the pins specified in pinmap.h
  - LED drivers connected as specified in Leds.h
  
  The main loop() defines the sequencer behaviour. Parameters are updated
  in read_pots() and keys are handled in handle_keys().

*/
#include "Arduino.h"
#include <Keypad.h>
#include "midinotes.h"
#include <MIDI.h>
//#define BRAINS_FEB
#define BRAINS_SEP
// #define NO_POTS
#include "pinmap.h"
#include "Buttons.h"

const int MIDI_CHANNEL = 1;
int gate_length_msec = 40;
const int SYNC_LENGTH_MSEC = 1;
const int MIN_TEMPO_MSEC = 666; // Tempo is actually an interval in ms

// Sequencer settings
const int NUM_STEPS = 8;

unsigned char current_step = 0;
unsigned char target_step = 0;
unsigned int tempo = 0;
unsigned long next_step_time = 0;
unsigned long gate_off_time = 0;
unsigned long sync_off_time = 0;
unsigned long note_on_time;
unsigned long previous_note_on_time;
unsigned long note_off_time;
// Define the array of leds

char set_key = 9;
bool sequencer_is_running = true;

// Musical settings
//const int BLACK_KEYS[] = {22,25,27,30,32,34,37,39,42,44,46,49,51,54,56,58,61,63,66,68,73,75,78,80};
const int SCALE[] = { 49,51,54,56,58,61,63,66,68,70 }; // Low with 2 note split
const float SAMPLERATE_STEPS[] = { 44100,4435,2489,1109 }; 
const char DETUNE_OFFSET_SEMITONES[] = { 3,4,5,7,9 };
#define INITIAL_VELOCITY 100

// Variable declarations
int detune_amount = 0;
int osc1_frequency = 0;
byte osc1_midi_note = 0;
int note_is_playing = 0;
boolean note_is_triggered = false;
boolean note_is_played = false;
boolean double_speed = false;
int transpose = 0;
boolean next_step_is_random = false;
int num_notes_held = 0;
int tempo_interval;
int volume_pot_value;
boolean random_flag = 0;

void read_pots();
void midi_note_on(byte channel, byte note, byte velocity);
void midi_note_off(byte channel, byte note, byte velocity);
void note_on(byte midi_note, byte velocity, boolean enabled);
void note_off();
float midi_note_to_frequency(int x) ;
int detune(int note, int amount);
void handle_input_until(unsigned long until);
void handle_keys();
void handle_midi();
int tempo_interval_msec();
void midi_init();
void sequencer_start();
void sequencer_stop();
float fscale( float originalMin, float originalMax, float newBegin, float
newEnd, float inputValue, float curve);

MIDI_CREATE_DEFAULT_INSTANCE();

#include "Synth.h"
#include "Sequencer.h"
#include "Leds.h"

void setup() {

  Serial.begin(57600);
  audio_init();

  midi_init();

  led_init();
  keys_init();
  pins_init();

  Serial.println("Dato DUO firmware build 5");
  previous_note_on_time = millis();

  digitalWrite(AMP_ENABLE, HIGH);
}

void loop() {

  // Check if speed pot is turned all the way down
  if(sequencer_is_running) {
    if(!double_speed) {
      tempo_interval = tempo_interval_msec();
    } else {
      tempo_interval = (tempo_interval_msec()/2);
    }

    // Make sure the gate length is never longer than one step
    if(gate_length_msec > tempo_interval) {
      gate_length_msec = tempo_interval;
    }

    note_on_time = previous_note_on_time + tempo_interval;

    if(millis() >= note_on_time)  {
      if(note_is_triggered) {

      } else {
        note_is_triggered = true;
        note_is_played = false;
        // time for the next note
        previous_note_on_time = millis();
        note_off_time = previous_note_on_time + gate_length_msec;

        if (!next_step_is_random && !random_flag) {
          current_step++;
          if (current_step >= NUM_STEPS) current_step = 0;
        } else {
          random_flag = false;
          int random_step = random(NUM_STEPS);

          while (random_step == current_step || random_step == current_step+1 || random_step+NUM_STEPS == current_step) { // Prevent random from stepping to the current or next step
            random_step = random(NUM_STEPS);
          }

          current_step = random_step;
        }
        step_velocity[current_step] = INITIAL_VELOCITY;

        note_on(SCALE[step_note[current_step]]+transpose, step_velocity[current_step], step_enable[current_step]);
        digitalWrite(SYNC_PIN, HIGH);
      }

    }

    if(!note_is_played && millis() >= note_off_time) { // We want to update the gate length continuously responding to pot readings
      note_is_played = true;
      digitalWrite(SYNC_PIN, LOW);
      note_off();
      note_is_triggered = false;
      target_step = current_step + 1;
      if (target_step >= NUM_STEPS) target_step = 0;
      note_off_time = millis() + tempo_interval - gate_length_msec; // Set note off time to sometime in the future
    }
  }

  // TODO: leds only need to be updated when necessary
  update_leds();
  handle_keys();
  handle_midi();
  read_pots();

  Serial.print("CPU: ");
  Serial.print(AudioProcessorUsageMax());
  Serial.print(" RAM: ");
  Serial.print(AudioMemoryUsageMax());
  Serial.println();
  AudioMemoryUsageMaxReset(); 
  AudioProcessorUsageMaxReset();
}

void read_pots() {

  // If the DUO brains are running without pots attached, do nothing
  #ifdef NO_POTS
    return;
  #endif

  // Read out the pots/switches
  gate_length_msec = map(analogRead(GATE_POT),1023,0,10,200);
  int volume_pot_value = analogRead(FADE_POT);
  int resonance = analogRead(FILTER_RES_POT);
  int amp_env_release = map(analogRead(AMP_ENV_POT),0,1023,30,300);
  int filter_pot_value = analogRead(FILTER_FREQ_POT);
  detune_amount = analogRead(OSC_DETUNE_POT);

  analogWrite(FILTER_LED, filter_pot_value>>2);
  analogWrite(OSC_LED, detune_amount>>2);

  // Audio interrupts have to be off to apply settings
  AudioNoInterrupts();


  if(detune_amount > 511) {
    // Do PWM stuff
    waveform2.frequency(detune(osc1_midi_note,1000));
    waveform2.pulseWidth(map(detune_amount,511,1023,1000,50)/1000.0);
    // mix osc1 away
    mixer1.gain(0, 0.0); // OSC1
    mixer1.gain(1, 1.0); // OSC2
  } else {
    waveform2.frequency(detune(osc1_midi_note,detune_amount*2));
    // mix osc1 in
    mixer1.gain(0, 0.5); // OSC1
    mixer1.gain(1, 0.5); // OSC2
  }
  filter1.frequency(map(filter_pot_value,0,1023,60,300));
  //filter1.frequency(fscale(0.,1023.,60.,300.,filter_pot_value,0));

  // TODO: do exponential filter pot behaviour
  filter1.resonance(map(resonance,0,1023,500,70)/100.0); // 0.7-5.0 range

  envelope1.release(amp_env_release);

  if(digitalRead(BITC_PIN)) {
    bitcrusher1.sampleRate(SAMPLERATE_STEPS[0]);
  } else {
    bitcrusher1.sampleRate(SAMPLERATE_STEPS[2]);
  }
  
  if(digitalRead(NOISE_PIN)) {
    noise1.amplitude(0.0);
  } else {
    noise1.amplitude(0.3);
  }

  mixer2.gain(0, map(volume_pot_value,0,1023,1000,10)/1000.);
  mixer2.gain(1, map(volume_pot_value,0,1023,700,70)/1000.);

  AudioInterrupts(); 
}

void midi_note_on(byte channel, byte note, byte velocity) {
  note_on(note, velocity, true);
}

void midi_note_off(byte channel, byte note, byte velocity) {
  note_off();
}

void note_on(byte midi_note, byte velocity, boolean enabled) {

  digitalWrite(LED_PIN, HIGH);

  note_is_playing = midi_note;

  if(enabled) {
    AudioNoInterrupts();

    dc1.amplitude(velocity / 127.); // DC amplitude controls filter env amount
    osc1_midi_note = midi_note;
    osc1_frequency = (int)midi_note_to_frequency(midi_note);
    waveform1.frequency(osc1_frequency);
    // Detune OSC2
    waveform2.frequency(detune(osc1_midi_note,detune_amount));

    AudioInterrupts(); 

    MIDI.sendNoteOn(midi_note, velocity, MIDI_CHANNEL);
    envelope1.noteOn();
    envelope2.noteOn();
  } else {
    // Set LED to white but don't play a note
    leds(current_step) = LED_WHITE;
  }
}

void note_off() {

  if (note_is_playing) {
    if(!step_enable[current_step]) {
      leds(current_step) = CRGB::Black;
    } else {
      envelope1.noteOff();
      envelope2.noteOff();
      MIDI.sendNoteOff(note_is_playing, 64, MIDI_CHANNEL);
    }
    note_is_playing = 0;
  } 
  digitalWrite(LED_PIN, LOW);
}

float midi_note_to_frequency(int x) {
  return MIDI_NOTE_FREQUENCY[x];
}

int detune(int note, int amount) { // amount goes from 0-1023
  if (amount > 800) {
    return midi_note_to_frequency(note) * (amount+9000)/10000.;
  } else if (amount < 100) {
    return midi_note_to_frequency(note - 12) * ( 20000 - amount )/20000.;
  } else {
    int offset = map(amount,200,900,4,0);
    return midi_note_to_frequency(note - DETUNE_OFFSET_SEMITONES[offset]);
  }
}

// Scans the keypad and handles step enable and keys
void handle_keys() {
   // Fills keypad.key[ ] array with up-to 10 active keys.
  // Returns true if there are ANY active keys.
  if (keypad.getKeys())
  {
    for (int i=0; i<LIST_MAX; i++)   // Scan the whole key list.
    {
      if ( keypad.key[i].stateChanged )   // Only find keys that have changed state.
      {
        char k = keypad.key[i].kchar;
        switch (keypad.key[i].kstate) {  // Report active key state : IDLE, PRESSED, HOLD, or RELEASED
            case PRESSED:    
                if (k <= KEYB_9 && k >= KEYB_0) {
                  if(sequencer_is_running) {
                    step_note[target_step] = k - KEYB_0;
                    step_enable[target_step] = 1;
                    step_velocity[target_step] = INITIAL_VELOCITY; 
                  } else {
                    current_step++;
                    if (current_step >= NUM_STEPS) current_step = 0;
                    target_step=current_step;
                    step_note[target_step] = k - KEYB_0;
                    step_enable[target_step] = 1;
                    step_velocity[target_step] = INITIAL_VELOCITY; 
                    num_notes_held++;
                    note_on(SCALE[k-KEYB_0]+transpose, INITIAL_VELOCITY, true);
                  }
                } else if (k <= STEP_7 && k >= STEP_0) {
                  step_enable[k-STEP_0] = 1-step_enable[k-STEP_0];
                  if(!step_enable[k-STEP_0]) { leds(k-STEP_0) = CRGB::Black; }
                  step_velocity[k-STEP_0] = INITIAL_VELOCITY;
                } else if (k == DBL_SPEED) {
                  double_speed = true;
                } else if (k == OCT_DOWN) {
                  transpose--;
                  if(transpose<-12){transpose = -24;}
                } else if (k == OCT_UP) {
                  transpose++;
                  if(transpose>12){transpose = 24;}
                } else if (k == SEQ_RANDOM) {
                  next_step_is_random = true;
                  random_flag = true;
                } else if (k == SEQ_START) {
                  if(sequencer_is_running) {
                    sequencer_stop();
                  } else {
                    sequencer_start();
                  }
                }
                break;
            case HOLD:
                break;
            case RELEASED:
                if (k <= KEYB_9 && k >= KEYB_0) {
                  MIDI.sendNoteOff(SCALE[k-KEYB_0]+transpose,64,MIDI_CHANNEL);
                  num_notes_held--;
                  if(num_notes_held <= 0) note_off();
                } else if (k == DBL_SPEED) {
                  double_speed = false;
                } else if (k == OCT_DOWN) {
                  if(transpose<-12){transpose = -12;}
                  if(transpose>12){transpose = 12;}
                } else if (k == OCT_UP) {
                  if(transpose<-12){transpose = -12;}
                  if(transpose>12){transpose = 12;}
                } else if (k == SEQ_RANDOM) {
                  next_step_is_random = false;
                } 
                break;
            case IDLE:
                break;
        }
      }
    }
  }
}

void handle_midi() {
  MIDI.read();
}

int tempo_interval_msec() {
  #ifdef NO_POTS
    return 300;
  #endif
  int potvalue = 1023-analogRead(TEMPO_POT);
  if(potvalue < 10) {
    return 0;
  } else {
    return map(potvalue,10,1023,MIN_TEMPO_MSEC,40);
  }
}

void midi_init() {
  MIDI.begin(MIDI_CHANNEL);
  MIDI.setHandleNoteOn(midi_note_on);
  MIDI.setHandleNoteOff(midi_note_off);
}

void sequencer_start() {
  sequencer_is_running = true;
  physical_leds[0] = CRGB::Black;
}

void sequencer_stop() {
  sequencer_is_running = false;
  physical_leds[0] = CRGB::White;
  note_off();
}
/* fscale
 Floating Point Autoscale Function V0.1
 Paul Badger 2007
 Modified from code by Greg Shakar

 This function will scale one set of floating point numbers (range) to another set of floating point numbers (range)
 It has a "curve" parameter so that it can be made to favor either the end of the output. (Logarithmic mapping)

 It takes 6 parameters

 originalMin - the minimum value of the original range - this MUST be less than origninalMax
 originalMax - the maximum value of the original range - this MUST be greater than orginalMin

 newBegin - the end of the new range which maps to orginalMin - it can be smaller, or larger, than newEnd, to facilitate inverting the ranges
 newEnd - the end of the new range which maps to originalMax  - it can be larger, or smaller, than newBegin, to facilitate inverting the ranges

 inputValue - the variable for input that will mapped to the given ranges, this variable is constrained to originaMin <= inputValue <= originalMax
 curve - curve is the curve which can be made to favor either end of the output scale in the mapping. Parameters are from -10 to 10 with 0 being
          a linear mapping (which basically takes curve out of the equation)

 */
float fscale( float originalMin, float originalMax, float newBegin, float newEnd, float inputValue, float curve){
  float OriginalRange = 0;
  float NewRange = 0;
  float zeroRefCurVal = 0;
  float normalizedCurVal = 0;
  float rangedValue = 0;
  boolean invFlag = 0;

  // condition curve parameter
  // limit range
  if (curve > 10) curve = 10;
  if (curve < -10) curve = -10;

  curve = (curve * -.1) ; // - invert and scale - this seems more intuitive - postive numbers give more weight to high end on output 
  curve = pow(10, curve); // convert linear scale into lograthimic exponent for other pow function

  // Check for out of range inputValues
  if (inputValue < originalMin) {
    inputValue = originalMin;
  }
  if (inputValue > originalMax) {
    inputValue = originalMax;
  }

  // Zero Refference the values
  OriginalRange = originalMax - originalMin;

  if (newEnd > newBegin) { 
    NewRange = newEnd - newBegin;
  }
  else {
    NewRange = newBegin - newEnd; 
    invFlag = 1;
  }

  zeroRefCurVal = inputValue - originalMin;
  normalizedCurVal  =  zeroRefCurVal / OriginalRange;   // normalize to 0 - 1 float

  // Check for originalMin > originalMax  - the math for all other cases i.e. negative numbers seems to work out fine 
  if (originalMin > originalMax ) {
    return 0;
  }

  if (invFlag == 0){
    rangedValue =  (pow(normalizedCurVal, curve) * NewRange) + newBegin;
  } else {   
    rangedValue =  newBegin - (pow(normalizedCurVal, curve) * NewRange); 
  }
  return rangedValue;
}