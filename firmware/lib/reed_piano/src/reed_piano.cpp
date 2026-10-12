#include "reed_piano.h"

// On the Teensy, what runs once a note (strike, tune) or once at all (setup) lives in flash, so only the
// per-sample render takes fast RAM (ITCM, which is near a 32 kB step); on a PC FLASHMEM is nothing
#if defined(__IMXRT1062__)
#include <Arduino.h>
#else
#define FLASHMEM
#endif

float ReedPiano::table[ReedPiano::TABLE_SIZE + 1];
float ReedPiano::table_slope = 1;
float ReedPiano::fs = 44100;
bool ReedPiano::is_ready = false;
float ReedPiano::hp_coef = 0.989f;
float ReedPiano::bb0 = 1, ReedPiano::bb1 = 0, ReedPiano::bb2 = 0, ReedPiano::ba1 = 0, ReedPiano::ba2 = 0;
bool ReedPiano::body_on = false;
static float lp_coef = 0.5f;

static const float TWO_PI_F = 6.28318531f;
static const float OUT_RMS = 0.2f;   // a full-strength note's start, as loud as a sample (0.2 RMS)

// The pickup's response to the reed's swing x (0 at rest): the capacitance's peak, with the reed in the
// slot, one unit of swing away. A gaussian is the simplest bump with the right two flanks: rising
// toward the slot, falling on through it.
static inline float pickup_shape(float x) { return expf(-(x - 1) * (x - 1)); }

// How loud the pickup's signal is (RMS, its DC aside) for a sine swing of amplitude d, from the table
// below: a hard swing folds over the peak and comes out quieter than the slope at rest says, which the
// voice makes up, so the bark changes the tone and the caller keeps the loudness.
static const int GAIN_POINTS = 33;
static const float GAIN_STEP = 0.125f;   // drives 0..4
static float gain_of_drive[GAIN_POINTS];

FLASHMEM void ReedPiano::setup(float sample_rate) {
  fs = sample_rate;
  for (int i = 0; i <= TABLE_SIZE; i++) table[i] = pickup_shape(-TABLE_RANGE + 2 * TABLE_RANGE * i / TABLE_SIZE);
  table_slope = 2 / expf(1);   // d/dx exp(-(x-1)^2) at 0
  const int N = 256;
  for (int g = 0; g < GAIN_POINTS; g++) {
    float d = g * GAIN_STEP, sum = 0, sum2 = 0;
    for (int k = 0; k < N; k++) {
      float v = pickup(d * sinf(TWO_PI_F * k / N));
      sum += v;
      sum2 += v * v;
    }
    float mean = sum / N, var = sum2 / N - mean * mean;
    gain_of_drive[g] = var > 0 ? sqrtf(var) : 0;
  }
  gain_of_drive[0] = 0;
  lp_coef = 1 - expf(-TWO_PI_F * 5500 / fs);   // the amplifier and speaker's top
  voicing(40, 400, 5, 0.9f);   // the body: a broad lift in the low middle, chosen by ear over 150, 250 and 200 Hz
  is_ready = true;
}

FLASHMEM void ReedPiano::voicing(float lowcut_hz, float body_hz, float body_db, float body_q) {
  hp_coef = expf(-TWO_PI_F * lowcut_hz / fs);   // the pickup into the preamp's input, which also takes the bark's DC
  body_on = body_db != 0 && body_hz > 0;
  if (!body_on) return;
  // a peaking filter (the Audio EQ Cookbook's), normalized by a0
  float A = powf(10, body_db / 40), w = TWO_PI_F * body_hz / fs, al = sinf(w) / (2 * body_q), cw = cosf(w);
  float a0 = 1 + al / A;
  bb0 = (1 + al * A) / a0; bb1 = -2 * cw / a0; bb2 = (1 - al * A) / a0;
  ba1 = -2 * cw / a0; ba2 = (1 - al / A) / a0;
}

float ReedPiano::pickup(float x) {
  float p = (x + TABLE_RANGE) * (TABLE_SIZE / (2 * TABLE_RANGE));
  if (p <= 0) return table[0];
  if (p >= TABLE_SIZE) return table[TABLE_SIZE];
  int i = (int)p;
  float f = p - i;
  return table[i] + f * (table[i + 1] - table[i]);
}

// The reed's pitch, how long it rings and how long its clank lasts, for freq. A low reed rings for
// seconds and a high one a fraction of one, as the 200A's do (its sustain is shorter than a Rhodes').
FLASHMEM void ReedPiano::tune(float f) {
  freq = f;
  float w = TWO_PI_F * f / fs;
  c1 = cosf(w); s1 = sinf(w);
  float tau1 = 0.9f * powf(110.0f / f, 0.67f);   // seconds to 1/e
  tau1 = tau1 < 0.12f ? 0.12f : tau1 > 2.5f ? 2.5f : tau1;
  r1 = expf(-1 / (tau1 * fs));
  float f2 = f * 7.1f;   // the second bending mode, not a harmonic
  mode2 = f2 < 0.4f * fs;
  if (!mode2) { clank = false; x2 = y2 = 0; }
  if (mode2) {
    c2 = cosf(TWO_PI_F * f2 / fs); s2 = sinf(TWO_PI_F * f2 / fs);
    float tau2 = 0.03f * powf(220.0f / f, 0.4f);
    tau2 = tau2 < 0.008f ? 0.008f : tau2 > 0.08f ? 0.08f : tau2;
    r2 = expf(-1 / (tau2 * fs));
  }
}

FLASHMEM void ReedPiano::frequency(float f) {
  if (f <= 0 || f == freq) return;
  // the decay times stay those of the note struck; only the rotation turns
  float w = TWO_PI_F * f / fs;
  c1 = cosf(w); s1 = sinf(w);
  float f2 = f * 7.1f;
  if (f2 < 0.4f * fs) { c2 = cosf(TWO_PI_F * f2 / fs); s2 = sinf(TWO_PI_F * f2 / fs); }
  else { mode2 = clank = false; x2 = y2 = 0; }
  freq = f;
}

FLASHMEM void ReedPiano::strike(float f, float st) {
  if (f <= 0) return;
  st = st < 0.02f ? 0.02f : st > 1 ? 1 : st;
  if (!active) { x1 = y1 = x2 = y2 = 0; hp_in = hp_out = lp = bz1 = bz2 = 0; }
  else { x1 *= 0.5f; y1 *= 0.5f; x2 *= 0.5f; y2 *= 0.5f; }   // the hammer's felt takes some of the old swing
  tune(f);
  strength = st;
  // The bark: how far through the pickup's peak a hit swings the reed. Highest in the low middle, where
  // the 200A barks most; the top reeds swing too little to, and their harmonics would alias besides.
  float pitch_fall = 1 / sqrtf(1 + (f / 600) * (f / 600));
  drive = 2.35f * pitch_fall * st;
  float g = drive / GAIN_STEP;
  int gi = (int)g;
  float gain = gi >= GAIN_POINTS - 1 ? gain_of_drive[GAIN_POINTS - 1] : gain_of_drive[gi] + (g - gi) * (gain_of_drive[gi + 1] - gain_of_drive[gi]);
  norm = gain > 0 ? OUT_RMS / gain : 0;
  // the hammer gives the reed a velocity: its swing starts from where it is, never with a step
  y1 -= 1;
  if (mode2) { y2 -= st * (0.12f + 0.3f * st); clank = true; }   // a harder (shorter) hammer contact rings the clank more
  quiet = 0;
  active = true;
}

void ReedPiano::render(float *out, int n) {
  if (!active) {
    for (int i = 0; i < n; i++) out[i] = 0;
    return;
  }
  const float g0 = table[TABLE_SIZE / 2];   // the pickup at rest (x = 0 is the table's middle)
  for (int i = 0; i < n; i++) {
    float nx = r1 * (c1 * x1 - s1 * y1);
    y1 = r1 * (s1 * x1 + c1 * y1);
    x1 = nx;
    float x = x1;
    if (clank) {
      nx = r2 * (c2 * x2 - s2 * y2);
      y2 = r2 * (s2 * x2 + c2 * y2);
      x2 = nx;
      x += x2;
    }
    float v = (pickup(drive * x) - g0) * norm;
    float h = hp_coef * (hp_out + v - hp_in);
    hp_in = v;
    hp_out = h;
    if (body_on) {   // transposed direct form II
      float b = bb0 * h + bz1;
      bz1 = bb1 * h - ba1 * b + bz2;
      bz2 = bb2 * h - ba2 * b;
      h = b;
    }
    lp += lp_coef * (h - lp);
    out[i] = lp;
  }
  if (clank && fabsf(x2) + fabsf(y2) < 1e-5f) { x2 = y2 = 0; clank = false; }
  // gone below hearing (-80 dB of a fresh strike) for a block: done
  if (fabsf(x1) + fabsf(y1) < 1e-4f) {
    quiet += n;
    if (quiet > 2048) active = false;
  } else quiet = 0;
}
