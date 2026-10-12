// The drone: a tanpura, or a shruti box, playing the key's Sa under everything (see DRONE in main.cpp).
// A tanpura is four open strings plucked in turn, over and over, never stopped: Pa (or Ma, or Ni) below Sa,
// Sa, Sa again, and Sa an octave down, then a breath before the next round. Its sound is the jawari: the
// strings end on a broad, gently curved bone bridge, with a cotton thread under each (the jiva) that
// sets how closely the string lies along it. Every swing toward the bridge the string wraps a little way
// onto the curve and lifts off again, so where it ends moves back and forth each cycle, and how far
// follows how hard the string is swinging. That moving end keeps making high partials for as long as the
// note rings: the note blooms after the pluck and the bright bands sweep down through the partials as
// it fades (Raman, 1921). Players slide the thread to open the jawari (more buzz) or close it.
// Here each string is two travelling-wave loops, one for each plane it swings in: a delay line a round
// trip long, read between its samples, filtered a little and fed back. The plane toward the bridge
// meets it: the loop's last stretch is read as the string's shape along the bridge (the wave going in
// plus the wave coming back) against the bridge's curve lifted by the thread's gap, and where the string
// would pass inside the curve it is pushed back out, the push going out along the string both ways. The
// jawari setting (381) is the thread: open, the gap closes and the curve flattens; closed, the string
// clears the bridge and rings plain. The other plane never touches the bridge and rings a hair apart in
// pitch, so the two beat slowly, as a tanpura's do. The strings share the bridge, which yields a little
// to each of them, so each gets a share of the others. What is heard is the force on the bridge, through
// two resonances for the gourd and the soundboard. Tuned pure to Sa, 3:2 and 4:3 and 15:8, whatever the
// temperament, as a tanpura is. The shruti box is Sa, Pa and Sa below as reeds: three sawtooths,
// softened, breathing slowly.
// Rendered with the drum machine, into its output after the kit's grit, at its own level. The bridge
// plane's lines live in RAM2 (8.8 kB), the other plane's in RAM1 (8.8 kB). Off, nothing runs.
#ifndef TANPURA_H
#define TANPURA_H
#include <Arduino.h>
#ifndef DMAMEM
#define DMAMEM
#endif

#define TANPURA_STRINGS 4
#define TANPURA_LINE 1100   // samples: a period at 40 Hz, Sa's low octave in the low register (Sa from E2)
#define TANPURA_BRIDGE 12   // points along the bridge the string is checked against

DMAMEM static int16_t tanpura_lines[TANPURA_STRINGS][TANPURA_LINE];   // the plane that meets the bridge
static int16_t tanpura_lines2[TANPURA_STRINGS][TANPURA_LINE];         // the plane clear of it
static float tanpura_arrived[TANPURA_STRINGS][AUDIO_BLOCK_SAMPLES];   // each string's wave at the bridge, this block

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
  // The thread: 0 closed, the string clears the bridge and rings plain; 1 open, the string lies along it
  void jawari(float j) {
    float closed = 1.0f - constrain(j, 0.0f, 1.0f);
    gap = gap_open + closed * closed * closed * (gap_closed - gap_open);
    curve = curve_open + closed * (curve_closed - curve_open);
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) profile(string[s]);
  }
  // Sa, in Hz
  void tune(float sa) {
    if (sa <= 0) return;
    sa_hz = sa;
    static const float first[5] = {0, 3.0f / 4, 2.0f / 3, 15.0f / 16, 1.0f};   // below Sa: Pa, Ma, Ni, Sa
    float ratio[TANPURA_STRINGS] = {first[kind >= 1 && kind <= 4 ? kind : 1], 1.0f, 1.0f, 0.5f};
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      float hz = sa_hz * ratio[s];
      float period = AUDIO_SAMPLE_RATE_EXACT / hz - loop_delay;   // less the loop's own delay, the filter's
      string[s].period = constrain(period, 16.0f, (float)(TANPURA_LINE - TANPURA_BRIDGE - 4));
      // the other plane a hair longer: the two beat at about a quarter of a hertz, slower on the low string
      string[s].period2 = string[s].period * (1.0f + detune[s] / hz);
      string[s].hz = hz;
      // the force on the bridge is the wave's change, which the same swing makes more of the higher the
      // string: the low strings are brought up toward the others (a thicker string presses harder)
      string[s].force = out_level * sqrtf(110.0f / hz);
      profile(string[s]);
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
    float mono[AUDIO_BLOCK_SAMPLES];
    for (uint32_t n = 0; n < n_samples; n++) mono[n] = 0;
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      string_t &st = string[s];
      float *arrived = tanpura_arrived[s];
      if (st.energy < 1e-5f) { for (uint32_t n = 0; n < n_samples; n++) arrived[n] = 0; continue; }
      int16_t *line = tanpura_lines[s], *line2 = tanpura_lines2[s];
      // the other strings' pull on the bridge: the earlier strings' this block, the later ones' last block
      float others[AUDIO_BLOCK_SAMPLES];
      for (uint32_t n = 0; n < n_samples; n++) {
        float sum = 0;
        for (uint8_t j = 0; j < TANPURA_STRINGS; j++) if (j != s) sum += tanpura_arrived[j][n];
        others[n] = sum;
      }
      float lp = st.lp, lp2 = st.lp2, dc = st.dc, last_y = st.last_y, last_y2 = st.last_y2, peak = 0;
      const float period = st.period, period2 = st.period2, force = st.force;
      const float left = pan_l[s] * gain, right = pan_r[s] * gain;
      const uint8_t points = st.points;
      const uint16_t *at = st.at;
      const float *height = st.height;
      uint32_t w = st.write;
      for (uint32_t n = 0; n < n_samples; n++) {
        // the wave arriving at the bridge, read between samples (cubic: it keeps the treble a linear read loses)
        float pos = (float)w - period;
        if (pos < 0) pos += TANPURA_LINE;
        uint32_t i1 = (uint32_t)pos;
        float frac = pos - i1;
        uint32_t i0 = i1 ? i1 - 1 : TANPURA_LINE - 1;
        uint32_t i2 = i1 + 1; if (i2 >= TANPURA_LINE) i2 -= TANPURA_LINE;
        uint32_t i3 = i2 + 1; if (i3 >= TANPURA_LINE) i3 -= TANPURA_LINE;
        float y0 = line[i0], y1 = line[i1], y2 = line[i2], y3 = line[i3];
        float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2 * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        float y = (((c3 * frac + c2) * frac + c1) * frac + y1) * (1.0f / 32767.0f);
        // the string's loss: a gentle lowpass and a little less each time round; and the bridge yielding
        // under the strings' pull, which takes from each and gives it a share of the others'
        lp += loss * (y - lp);
        float x = keep * lp - couple * (others[n] + y);
        line[w] = (int16_t)constrain(x * 32767.0f, -32767.0f, 32767.0f);
        arrived[n] = y;
        // the jawari: along the bridge, the string's shape is the wave going in plus the wave coming back
        // (the loop holds the returning wave with its sign turned, so the difference); inside the curve,
        // it is pushed back out, and the push goes both ways along the string
        float push = 0;
        for (uint8_t k = 0; k < points; k++) {
          uint32_t ii = i1 + at[k]; if (ii >= TANPURA_LINE) ii -= TANPURA_LINE;
          uint32_t oo = w + TANPURA_LINE - at[k]; if (oo >= TANPURA_LINE) oo -= TANPURA_LINE;
          float u = (line[ii] - line[oo]) * (1.0f / 32767.0f) - height[k];
          if (u > 0) {
            float e = u * stiffness;
            int16_t ei = (int16_t)(e * 32767.0f);
            line[ii] -= ei;
            line[oo] += ei;
            push += e;
          }
        }
        w = (w + 1) % TANPURA_LINE;
        // the other plane, clear of the bridge, read a sample behind (its period allows for it)
        float pos2 = (float)w - period2;
        if (pos2 < 0) pos2 += TANPURA_LINE;
        uint32_t j1 = (uint32_t)pos2;
        float frac2 = pos2 - j1;
        uint32_t j2 = j1 + 1; if (j2 >= TANPURA_LINE) j2 -= TANPURA_LINE;
        float yb = (line2[j1] + (line2[j2] - line2[j1]) * frac2) * (1.0f / 32767.0f);
        lp2 += loss * (yb - lp2);
        float x2 = keep * lp2;
        uint32_t w2 = w ? w - 1 : TANPURA_LINE - 1;
        line2[w2] = (int16_t)constrain(x2 * 32767.0f, -32767.0f, 32767.0f);
        // out: the force on the bridge (the waves' change, and the pushes), without the DC the one-sided
        // bridge leaves
        float o = ((y - last_y) + plane2 * (yb - last_y2) + push * push_out) * force;
        last_y = y; last_y2 = yb;
        o -= dc;
        dc += 0.0005f * o;
        l[n] += o * left;
        r[n] += o * right;
        mono[n] += o;
        peak = fmaxf(peak, fabsf(x) + fabsf(x2));
      }
      st.write = w;
      st.lp = lp; st.lp2 = lp2; st.dc = dc; st.last_y = last_y; st.last_y2 = last_y2;
      st.energy = peak;
    }
    // the body: the gourd and the soundboard, two resonances over the dry sound
    const float body_gain = gain * 0.45f;
    for (uint32_t n = 0; n < n_samples; n++) {
      float m = mono[n];
      float b = (body1.run(m) * body_mix1 + body2.run(m) * body_mix2) * body_gain;
      l[n] += b;
      r[n] += b;
    }
  }

private:
  struct string_t {
    float period = 400, period2 = 400.5f, hz = 110, force = 1;
    float lp = 0, lp2 = 0, dc = 0, last_y = 0, last_y2 = 0, energy = 0;
    uint8_t points = 0;
    uint16_t at[TANPURA_BRIDGE];     // where along the loop each bridge point sits, in samples from the end
    float height[TANPURA_BRIDGE];    // the bridge's curve there, lifted by the thread, in the loop's units
    uint32_t write = 0;
  };
  struct resonator_t {
    float a1 = 0, a2 = 0, b0 = 0, z1 = 0, z2 = 0;
    void set(float hz, float q) {   // unity gain at its peak
      float w = 2 * 3.14159265f * hz / AUDIO_SAMPLE_RATE_EXACT, r = expf(-w / (2 * q));
      a1 = 2 * r * cosf(w); a2 = -r * r;
      float re = 1 - a1 * cosf(w) - a2 * cosf(2 * w), im = a1 * sinf(w) + a2 * sinf(2 * w);
      b0 = sqrtf(re * re + im * im);
      z1 = z2 = 0;
    }
    float run(float x) { float y = b0 * x + a1 * z1 + a2 * z2; z2 = z1; z1 = y; return y; }
  };
  static constexpr float loss = 0.95f;                          // the loop's lowpass: 1 none; the treble rings seconds
  static constexpr float loop_delay = (1.0f - loss) / loss;     // its delay at low frequencies, in samples
  static constexpr float keep = 0.9996f;                        // of the loop each time round
  static constexpr float couple = 0.0015f;                      // of the strings' pull the bridge yields to
  static constexpr float stiffness = 0.25f;                     // of the way back out of the bridge each sample
  static constexpr float push_out = 0.6f;                       // how much of the pushes is heard, as the bridge takes them
  static constexpr float plane2 = 0.5f;                         // the plane clear of the bridge, in the output
  static constexpr float gap_open = 0.0f, gap_closed = 0.06f;   // the thread's gap, in the loop's units (full swing about 0.5)
  static constexpr float curve_open = 40.0f, curve_closed = 80.0f;   // the bridge's curve: height over (distance / string length)^2
  static constexpr float bridge_reach = 0.03f;                  // of the string's length the bridge is checked over
  static constexpr float pluck_at = 0.23f;                      // of the string's length from the bridge, where the finger plucks
  static constexpr float out_level = 13.2f;                     // the force, scaled to the old drone's loudness at 50
  static constexpr float body_mix1 = 0.35f, body_mix2 = 0.8f;   // the gourd, the soundboard
  const float pan_l[TANPURA_STRINGS] = {0.59f, 0.42f, 0.28f, 0.48f};
  const float pan_r[TANPURA_STRINGS] = {0.28f, 0.42f, 0.59f, 0.48f};
  const float detune[TANPURA_STRINGS] = {0.22f, 0.3f, -0.26f, 0.17f};   // Hz between the planes

  // Where the bridge points sit along this string and how high the curve is there: at even steps over the
  // bridge's reach, a sample apart at the least (the loop is the round trip, but so is a sample's travel)
  void profile(string_t &st) {
    float span = st.period * bridge_reach;
    float spacing = fmaxf(span / TANPURA_BRIDGE, 1.0f);
    uint8_t points = 0;
    for (uint8_t k = 0; k < TANPURA_BRIDGE; k++) {
      float d = spacing * (k + 1);
      if (d > span + 0.5f) break;
      float along = d / st.period;   // of the string's length
      float h = gap + curve * along * along;
      if (h > 1.0f) break;
      st.at[points] = (uint16_t)lroundf(d);
      st.height[points] = h;
      points++;
    }
    st.points = points;
  }

  void clear() {
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      for (uint32_t i = 0; i < TANPURA_LINE; i++) tanpura_lines[s][i] = tanpura_lines2[s][i] = 0;
      for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) tanpura_arrived[s][n] = 0;
      string[s].energy = 0;
      string[s].lp = string[s].lp2 = string[s].dc = string[s].last_y = string[s].last_y2 = 0;
    }
    body1.set(150.0f, 2.5f);    // the gourd
    body2.set(1800.0f, 1.2f);   // the soundboard and bridge, under the jawari's bright band
    clock = pluck_samples - 1;   // the first pluck at once
    step = 0;
  }

  // A finger's pluck: the string drawn aside into a triangle, its corner a little under a quarter of the
  // way up from the bridge and rounded by the fingertip, and let go. In the loop that is the shape over
  // the first half of the round trip and its mirror image turned over on the second (the two waves that
  // make it, one each way), over what still rings, in both planes; a breath of softened noise for the
  // finger's touch. The low string is plucked a little firmer.
  void pluck(uint8_t s) {
    string_t &st = string[s];
    uint32_t len = (uint32_t)(st.period + loop_delay + 0.5f);
    float half = len * 0.5f, amp = s == 3 ? 0.65f : 0.55f, soft = 0;
    for (uint32_t k = 0; k < len; k++) {
      float x = k < half ? k / half : (len - k) / half;   // along the string, 0 at the bridge, 1 at the nut
      float d = x < pluck_at ? x / pluck_at : (1 - x) / (1 - pluck_at);
      d = d * d * (3 - 2 * d);   // the fingertip's rounding
      float v = (k < half ? -0.5f : 0.5f) * d * amp;   // drawn toward the bridge's side
      noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
      soft += 0.2f * ((int32_t)noise * (1.0f / 2147483648.0f) - soft);
      v += soft * amp * 0.05f;
      uint32_t at = (st.write + TANPURA_LINE - len + k) % TANPURA_LINE;
      tanpura_lines[s][at] = (int16_t)constrain(tanpura_lines[s][at] * 0.3f + v * 32767.0f, -32767.0f, 32767.0f);
      tanpura_lines2[s][at] = (int16_t)constrain(tanpura_lines2[s][at] * 0.3f + v * 0.8f * 32767.0f, -32767.0f, 32767.0f);
    }
    st.energy = 1;
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
  float gap = 0.00384f, curve = 56.0f;   // jawari(0.6), the setting's default
  resonator_t body1, body2;
  uint32_t pluck_samples = 52941, clock = 0;   // 1.2 s
  uint8_t step = 0;
  string_t string[TANPURA_STRINGS];
  uint32_t noise = 0x9E3779B9u;
  float breath = 0, reed_phase[3] = {0, 0.33f, 0.66f}, reed_lp[3] = {0, 0, 0};
};

#endif
