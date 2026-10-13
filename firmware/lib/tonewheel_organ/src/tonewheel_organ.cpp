#include "tonewheel_organ.h"
#include <math.h>

// On the Teensy, what runs once (setup, a registration's tables) or once a note lives in flash, so only the
// per-sample render takes fast RAM (ITCM, near a 32 kB step); on a PC FLASHMEM is nothing
#if defined(__IMXRT1062__)
#include <Arduino.h>
#else
#define FLASHMEM
#endif

int16_t ToneOrgan::table[2][ToneOrgan::TABLE_SIZE + 1];
int16_t ToneOrgan::sine[ToneOrgan::TABLE_SIZE + 1];
float ToneOrgan::fs = 44100;
bool ToneOrgan::is_ready = false;
int ToneOrgan::perc_mode = 0;
float ToneOrgan::perc_level = 0, ToneOrgan::perc_decay = 0.999f;

static const float TWO_PI_F = 6.28318531f;
static const float OUT_RMS = 0.2f;   // as loud as a sample (0.2 RMS)
static float table_gain = 1;         // the table's int16 to 0.2 RMS
static float click_decay = 0.99f, click_k = 0.3f;
static int current_registration = 0;   // kept from before setup: a preset may choose it before a voice plays the organ

// The footages as multiples of the 16' pitch: 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'
static const uint8_t HARMONIC[9] = {1, 3, 2, 4, 6, 8, 10, 12, 16};
// The registrations (setting 388), drawbars left to right as an organist reads them, 0-8
static const char *const DRAWBARS[ToneOrgan::REGISTRATIONS] = {
  "888000000",   // 0: the bottom three, the jazz and blues organ (with the third's percussion, the classic)
  "888800000",   // 1: and the 4', rock
  "838000000",   // 2: a softer jazz ballad
  "008000000",   // 3: the 8' alone, a pure flute
  "688600000",   // 4: warm and full, gospel and soul
  "800000888",   // 5: the 16' and the top three, hollow and shrill
  "008080800",   // 6: odd harmonics only, reedy, like a clarinet
  "888888888",   // 7: every drawbar out, full organ
};

FLASHMEM void ToneOrgan::build_tables(int r) {
  if (r < 0 || r >= REGISTRATIONS) r = 0;
  float level[9];
  for (int d = 0; d < 9; d++) {
    int v = DRAWBARS[r][d] - '0';
    level[d] = v > 0 ? powf(10, -3.0f * (8 - v) / 20) : 0;   // each step 3 dB
  }
  float wave[2][TABLE_SIZE];
  float peak = 0, sum2 = 0;
  for (int k = 0; k < TABLE_SIZE; k++) {
    float w = TWO_PI_F * k / TABLE_SIZE;
    float whole = 0, folded = 0;
    for (int d = 0; d < 9; d++) {
      float s = level[d] * sinf(HARMONIC[d] * w);
      whole += s;
      // folded back: the 1 1/3' and 1' an octave down, as the top wheels run out (to 2 2/3' and 2')
      folded += HARMONIC[d] >= 12 ? level[d] * sinf(HARMONIC[d] / 2 * w) : s;
    }
    wave[0][k] = whole; wave[1][k] = folded;
    peak = fmaxf(peak, fmaxf(fabsf(whole), fabsf(folded)));
    sum2 += whole * whole;
  }
  float rms = sqrtf(sum2 / TABLE_SIZE);
  for (int t = 0; t < 2; t++) {
    for (int k = 0; k < TABLE_SIZE; k++) table[t][k] = (int16_t)lrintf(wave[t][k] / peak * 32000);
    table[t][TABLE_SIZE] = table[t][0];
  }
  table_gain = OUT_RMS / (rms / peak * 32000);   // the whole registration at 0.2 RMS, however many drawbars
}

FLASHMEM void ToneOrgan::setup(float sample_rate) {
  fs = sample_rate;
  for (int k = 0; k <= TABLE_SIZE; k++) sine[k] = (int16_t)lrintf(32000 * sinf(TWO_PI_F * k / TABLE_SIZE));
  click_decay = expf(-1 / (0.0025f * fs));       // the contacts' click: a burst of a few milliseconds
  click_k = 1 - expf(-TWO_PI_F * 3000 / fs);     // and not too bright
  build_tables(current_registration);
  percussion(perc_mode);   // its decay for this rate
  is_ready = true;
}

FLASHMEM void ToneOrgan::registration(int r) {
  current_registration = r < 0 || r >= REGISTRATIONS ? 0 : r;
  if (is_ready) build_tables(current_registration);   // otherwise setup builds it
}

FLASHMEM void ToneOrgan::percussion(int p) {
  perc_mode = p < 0 || p > 3 ? 0 : p;
  float t60 = perc_mode == 3 ? 1.2f : 0.35f;           // slow or fast
  perc_decay = expf(-6.9f / (t60 * fs));
  perc_level = perc_mode ? OUT_RMS * 1.41f : 0;         // as loud as a drawbar at 8
}

FLASHMEM void ToneOrgan::frequency(float f) {
  if (f <= 0) return;
  inc = (uint32_t)(f * 0.5f / fs * 4294967296.0f);     // the 16' wheel turns at half the note's pitch
  which = f > 1046.5f ? 1 : 0;                          // above C6 the top footages fold back
}

FLASHMEM void ToneOrgan::strike(float f) {
  frequency(f);
  perc = perc_level ? 1 : 0;
  click = 1;
  click_lp = 0;
  active = true;
}

void ToneOrgan::render(float *out, int n) {
  if (!active) {
    for (int i = 0; i < n; i++) out[i] = 0;
    return;
  }
  const int16_t *t = table[which];
  const float g = table_gain * (1.0f / 32768);
  const int shift = 32 - TABLE_BITS;
  const uint32_t pm = perc_mode == 1 ? 4 : 6;           // the 4' or the 2 2/3', in 16' turns
  const float pl = perc_level * (1.0f / 32000), pd = perc_decay, cd = click_decay, ck = click_k;
  uint32_t ph = phase, nz = noise;
  const uint32_t in = inc;
  float pe = perc, ce = click, cl = click_lp;
  for (int i = 0; i < n; i++) {
    ph += in;
    uint32_t k = ph >> shift, fr = (ph >> (shift - 15)) & 0x7FFF;
    int32_t a = t[k], b = t[k + 1];
    float y = (float)(a + (((b - a) * (int32_t)fr) >> 15)) * g;
    if (pe > 1e-4f) {
      uint32_t pp = ph * pm, pk = pp >> shift;
      y += sine[pk] * pl * pe;
      pe *= pd;
    }
    if (ce > 1e-4f) {
      nz = nz * 1664525u + 1013904223u;
      cl += ck * ((int32_t)nz * (1.0f / 2147483648.0f) - cl);
      y += cl * ce * 0.15f;
      ce *= cd;
    }
    out[i] = y;
  }
  phase = ph; noise = nz; perc = pe; click = ce; click_lp = cl;
}
