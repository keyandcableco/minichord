#ifndef REED_PIANO_H
#define REED_PIANO_H

#include <math.h>
#include <stdint.h>

// A reed electric piano, the Wurlitzer 200A kind, as a model rather than samples: what makes it a
// Wurlitzer is how its tone grows a bark as it is struck harder, which a sample at one strength can't.
//
// A felt hammer strikes a steel reed, which rings almost as a pure sine (its first bending mode) with a
// short inharmonic clank above it (its second, ~7x up, gone in a few hundredths of a second). The reed
// swings in and out of the slot of a charged pickup comb, and the pickup's signal follows the
// capacitance between them, which is greatest with the reed in the slot. At rest the reed sits beside
// that peak: a soft note stays on its flank and comes out round with a little second harmonic; a hard
// one swings through the peak and back on each cycle, folding the wave there, and barks. So the bark is
// one fixed bend (pickup()) of the reed's swing, and it dies away as the swing does.
//
// What the instrument adds after it: the pickup's own low cut (a high-impedance input, 40 Hz), a body
// (+5 dB around 400 Hz, broad, as the cabinet and amplifier give the midrange) and the little amplifier and
// speaker's top end. Its tremolo (the 200A's "vibrato" knob, ~5.6 Hz) is the minichord's
// own, the harp's and the chords'.
//
// Plain C++ and float, so it renders the same on a PC as on the Teensy. A voice is a ReedPiano; the
// pickup table is shared (ReedPiano::setup()).
class ReedPiano
{
public:
  static void setup(float sample_rate);   // once, before any voice sounds
  static bool ready() { return is_ready; }
  // The voicing after the pickup: its low cut, and the body, a broad resonance (Hz, dB, Q) as a cabinet
  // gives; 0 dB leaves it out. Shared by every voice; set before the notes it is for.
  static void voicing(float lowcut_hz, float body_hz, float body_db, float body_q);

  // Strikes the reed at freq, strength 0-1: 1 swings a reed in the low middle well through the pickup's
  // peak, a full bark; 0.4 just reaches it. A reed still ringing is struck again where it is, as a real
  // one is, so a repeated note never clicks. strength sets the timbre only: how loud it plays is the
  // caller's.
  void strike(float freq, float strength);
  void frequency(float freq);   // a bend or glide while it rings, its swing kept
  void stop() { active = false; }
  bool sounding() const { return active; }
  // n samples into out (float, about 0.2 RMS for a full-strength note's start)
  void render(float *out, int n);

private:
  static const int TABLE_SIZE = 256;
  static constexpr float TABLE_RANGE = 4.0f;   // the pickup table covers swings of -4..4
  static float table[TABLE_SIZE + 1];
  static float table_slope;                    // the pickup's slope at rest, d pickup / d swing
  static float fs;
  static bool is_ready;
  static float hp_coef;                                  // the low cut's one-pole
  static float bb0, bb1, bb2, ba1, ba2;                  // the body's peaking biquad
  static bool body_on;
  static float pickup(float x);

  void tune(float freq);
  template <bool CLANK, bool BODY> void run(float *out, int n);   // render's loop, one per case

  bool active = false;
  float freq = 220;
  float strength = 1;
  float drive = 1;     // swing scale into the pickup, for this pitch and strength
  float norm = 1;      // brings the fundamental back to one level whatever the drive
  // the two modes, each a decaying rotation: x the reed's displacement, y its quadrature
  float c1 = 1, s1 = 0, r1 = 1, x1 = 0, y1 = 0;
  float c2 = 1, s2 = 0, r2 = 1, x2 = 0, y2 = 0;
  bool mode2 = true;   // off when the clank would be above hearing (and alias)
  bool clank = false;  // the clank still ringing (it dies in a few hundredths of a second)
  float hp_in = 0, hp_out = 0;
  float bz1 = 0, bz2 = 0;   // the body's state
  float lp = 0;
  float quiet = 0;     // how long the swing has been inaudible, in samples
};

#endif
