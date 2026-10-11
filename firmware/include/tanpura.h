// The drone: a tanpura, or a shruti box, playing the key's Sa under everything (see DRONE in main.cpp).
// A tanpura is four open strings plucked in turn, over and over, never stopped: Pa (or Ma, or Ni) below Sa,
// Sa, Sa again, and Sa an octave down, then a breath before the next round. Its sound is the jawari, the
// broad flat bridge the strings buzz against, which makes each note bloom with high partials long after
// the pluck. Here each string is a Karplus-Strong loop (a delay line a period long, its output averaged a
// little and fed back) with the bridge as a one-sided limit in the loop: the string's swing past it is
// flattened, as a string grazing the bridge is, and the flattening keeps making partials while the note
// rings. The strings are tuned pure to Sa, 3:2 and 4:3 and 15:8, whatever the temperament, as a tanpura is.
// The shruti box is Sa, Pa and Sa below as reeds: three sawtooths, softened, breathing slowly.
// Rendered with the drum machine, into its output after the kit's grit, at its own level. Delay lines
// live in RAM2 (8.8 kB). Off, nothing runs.
#ifndef TANPURA_H
#define TANPURA_H
#include <Arduino.h>
#ifndef DMAMEM
#define DMAMEM
#endif

#define TANPURA_STRINGS 4
#define TANPURA_LINE 1100   // samples: a period at 40 Hz, Sa's low octave in the low register (Sa from E2)

DMAMEM static int16_t tanpura_lines[TANPURA_STRINGS][TANPURA_LINE];

class Tanpura {
public:
  // 0 off, 1-4 a tanpura with Pa, Ma, Ni or Sa as its first string, 5 a shruti box
  void mode(uint8_t m) {
    if (m == kind) return;
    kind = m;
    if (kind) { clear(); tune(sa_hz); }
  }
  uint8_t mode() const { return kind; }
  void level(float g) { gain = g; }
  void pace(float seconds) { pluck_samples = (uint32_t)(fmaxf(seconds, 0.3f) * AUDIO_SAMPLE_RATE_EXACT); }
  // Sa, in Hz
  void tune(float sa) {
    if (sa <= 0) return;
    sa_hz = sa;
    static const float first[5] = {0, 3.0f / 4, 2.0f / 3, 15.0f / 16, 1.0f};   // below Sa: Pa, Ma, Ni, Sa
    float ratio[TANPURA_STRINGS] = {first[kind >= 1 && kind <= 4 ? kind : 1], 1.0f, 1.0f, 0.5f};
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      float hz = sa_hz * ratio[s];
      // the loop's own delay: the average below is weighted half a sample
      float period = AUDIO_SAMPLE_RATE_EXACT / hz - loop_delay;
      string[s].period = constrain(period, 8.0f, (float)(TANPURA_LINE - 2));
      string[s].hz = hz;
    }
  }
  bool active() const { return kind != 0 && gain > 0; }

  // Adds a block of the drone into l and r, in the drum machine's units (about +-1 before its gain).
  void render(float *l, float *r, uint32_t n_samples) {
    if (!active()) return;
    if (kind == 5) { reeds(l, r, n_samples); return; }
    for (uint32_t n = 0; n < n_samples; n++) {
      if (++clock >= pluck_samples) {
        clock = 0;
        // four plucks, then a beat's rest
        if (step < TANPURA_STRINGS) pluck(step);
        step = (step + 1) % (TANPURA_STRINGS + 1);
      }
    }
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      string_t &st = string[s];
      if (st.energy < 1e-5f) continue;
      int16_t *line = tanpura_lines[s];
      float prev = st.prev, peak = 0;
      const float period = st.period, bridge = st.bridge, keep = st.keep;
      const float left = pan_l[s] * gain, right = pan_r[s] * gain;
      uint32_t w = st.write;
      for (uint32_t n = 0; n < n_samples; n++) {
        float pos = (float)w - period;
        if (pos < 0) pos += TANPURA_LINE;
        uint32_t i0 = (uint32_t)pos;
        float frac = pos - i0;
        uint32_t i1 = (i0 + 1) % TANPURA_LINE;
        float y = (line[i0] + (line[i1] - line[i0]) * frac) * (1.0f / 32767.0f);
        // the string's loss: a little lowpass and a little less each time round
        float x = keep * (0.5f * y + 0.5f * prev);
        prev = y;
        // the jawari: swinging past the bridge, the string is held back
        if (x > bridge) x = bridge + (x - bridge) * 0.3f;
        line[w] = (int16_t)constrain(x * 32767.0f, -32767.0f, 32767.0f);
        w = (w + 1) % TANPURA_LINE;
        // out, without the DC the one-sided bridge leaves in the loop
        float o = x - st.dc;
        st.dc += 0.0005f * o;
        l[n] += o * left;
        r[n] += o * right;
        peak = fmaxf(peak, fabsf(x));
      }
      st.write = w;
      st.prev = prev;
      st.energy = peak;
    }
  }

private:
  struct string_t {
    float period = 400, hz = 110, prev = 0, dc = 0, energy = 0;
    float bridge = 0.12f;   // where the bridge stops the swing, of full scale
    float keep = 0.9993f;   // of the loop each time round: about eight seconds to fade
    uint32_t write = 0;
  };
  static constexpr float loop_delay = 0.5f;
  const float pan_l[TANPURA_STRINGS] = {0.59f, 0.42f, 0.28f, 0.48f};
  const float pan_r[TANPURA_STRINGS] = {0.28f, 0.42f, 0.59f, 0.48f};

  void clear() {
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      for (uint32_t i = 0; i < TANPURA_LINE; i++) tanpura_lines[s][i] = 0;
      string[s].energy = 0;
      string[s].prev = string[s].dc = 0;
    }
    clock = pluck_samples - 1;   // the first pluck at once
    step = 0;
  }

  // A finger's pluck: a period of softened noise over what still rings, the low strings a little firmer.
  void pluck(uint8_t s) {
    string_t &st = string[s];
    int16_t *line = tanpura_lines[s];
    uint32_t len = (uint32_t)st.period + 1;
    float soft = 0, amp = s == 3 ? 0.55f : 0.45f;
    for (uint32_t k = 0; k < len; k++) {
      noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
      soft += 0.35f * ((int32_t)noise * (1.0f / 2147483648.0f) - soft);
      uint32_t at = (st.write + TANPURA_LINE - len + k) % TANPURA_LINE;
      float v = line[at] * (0.3f / 32767.0f) + soft * amp * 2.2f;
      line[at] = (int16_t)constrain(v * 32767.0f, -32767.0f, 32767.0f);
    }
    st.energy = 1;
    st.bridge = 0.10f + 0.04f * s / 3;
  }

  // The shruti box: Sa, Pa and Sa below, as softened sawtooths, the bellows breathing every four seconds.
  void reeds(float *l, float *r, uint32_t n_samples) {
    static const float ratio[3] = {1.0f, 1.5f, 0.5f}, mix[3] = {0.17f, 0.12f, 0.15f};
    const float inv = 1.0f / AUDIO_SAMPLE_RATE_EXACT;
    for (uint32_t n = 0; n < n_samples; n++) {
      breath += 0.25f * inv;
      if (breath >= 1) breath -= 1;
      float swell = 0.85f + 0.15f * (breath < 0.5f ? breath * 2 : 2 - breath * 2);
      float sum = 0;
      for (uint8_t k = 0; k < 3; k++) {
        reed_phase[k] += sa_hz * ratio[k] * inv;
        if (reed_phase[k] >= 1) reed_phase[k] -= 1;
        float saw = 2 * reed_phase[k] - 1;
        reed_lp[k] += 0.12f * (saw - reed_lp[k]);
        sum += reed_lp[k] * mix[k];
      }
      sum *= swell * gain;
      l[n] += sum * 0.5f;
      r[n] += sum * 0.5f;
    }
  }

  uint8_t kind = 0;
  float gain = 0.5f, sa_hz = 130.81f;
  uint32_t pluck_samples = 52941, clock = 0;   // 1.2 s
  uint8_t step = 0;
  string_t string[TANPURA_STRINGS];
  uint32_t noise = 0x9E3779B9u;
  float breath = 0, reed_phase[3] = {0, 0.33f, 0.66f}, reed_lp[3] = {0, 0, 0};
};

#endif
