#ifndef TONEWHEEL_ORGAN_H
#define TONEWHEEL_ORGAN_H

#include <stdint.h>

// A tonewheel organ, the Hammond kind, as a model: nine drawbars, each a sine at one footage (16', 5 1/3',
// 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'), drawn out 0-8 for its level (each step 3 dB); percussion, a second
// or third harmonic struck at the key and dying away; and the key click of the contacts closing.
//
// Every footage is a whole multiple of the 16' pitch (1, 3, 2, 4, 6, 8, 10, 12, 16 times it), so a
// registration is one single-cycle wave at the 16' pitch, built once into a shared table, and a voice is one
// table read a sample. The top keys of a real organ fold their highest footages back an octave (the wheels
// run out); a second table without the 1' and 1 1/3' stands for that above C6, where they would alias anyway.
//
// Plain C++, so it renders the same on a PC as on the Teensy. A voice is a ToneOrgan; the registration and the
// percussion are shared (ToneOrgan::registration(), ToneOrgan::percussion()). Its level is the caller's: the
// organ has no touch, and its swell pedal is the voice's level (on hover, a knob...). Its Leslie is the
// minichord's tremolo, vibrato and speed ramp.
class ToneOrgan
{
public:
  static void setup(float sample_rate);   // once, before any voice sounds
  static bool ready() { return is_ready; }
  static const int REGISTRATIONS = 8;
  // the drawbars, one of REGISTRATIONS settings (registration_drawbars below)
  static void registration(int r);
  // 0 none; 1 the second harmonic (4'), fast; 2 the third (2 2/3'), fast; 3 the third, slow
  static void percussion(int p);

  void strike(float freq);      // a key down: the wheels are always turning, so its phase carries on
  void frequency(float freq);   // a bend or glide while it sounds
  void stop() { active = false; }
  bool sounding() const { return active; }
  void render(float *out, int n);   // about 0.2 RMS

private:
  static const int TABLE_BITS = 8;   // 256 points: against 512, full organ differs 76 dB down (RAM1 is short)
  static const int TABLE_SIZE = 1 << TABLE_BITS;
  static int16_t table[2][TABLE_SIZE + 1];   // the registration, whole and folded back
  static int16_t sine[TABLE_SIZE + 1];
  static float fs;
  static bool is_ready;
  static int perc_mode;
  static float perc_level, perc_decay;
  static void build_tables(int r);

  bool active = false;
  uint32_t phase = 0, inc = 0;   // the 16' wheel, a whole turn 2^32
  uint8_t which = 0;             // its table: 0 whole, 1 folded back
  float perc = 0;                // the percussion's envelope
  float click = 0;               // the key click's envelope
  uint32_t noise = 22222;
  float click_lp = 0;
};

#endif
