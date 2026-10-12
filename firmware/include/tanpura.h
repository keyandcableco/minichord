// The drone: a tanpura, or a shruti box, playing the key's Sa under everything (see DRONE in main.cpp).
// A tanpura is four open strings plucked in turn, over and over, never stopped: Pa (or Ma, or Ni) below Sa,
// Sa, Sa again, and Sa an octave down, then a breath before the next round. Its sound is the jawari: the
// strings end on a broad, gently curved bone bridge, with a cotton thread under each (the jiva) that sets
// how closely the string lies along it. Every swing toward the bridge the string wraps a little way onto
// the curve and lifts off again, so where it ends moves back and forth within each cycle, as far as it is
// swinging. That moving end keeps making high partials for as long as the note rings: the note blooms
// after the pluck and the bright bands sweep down through the partials as it fades (Raman, 1921).
// Players slide the thread to open the jawari (more buzz) or close it.
// Here each string is two travelling-wave loops, one for each plane it swings in: a delay line a round
// trip long, read between its samples, filtered a little and fed back. The plane toward the bridge meets
// it: the string's slope at the bridge (its shape a sample in: the wave going in less the wave coming
// back, smoothed to the few harmonics a real string's stiffness lets through) wraps it onto the curve by
// that much, so the wave returns from that far up the string, and the loop is read that much earlier.
// The jawari setting (381) is the thread: open, the string wraps from the least slope; closed, it clears
// the bridge and rings plain. What the wrapping shortens the string by on average is tuned out, as a
// player tunes a string with its jawari. The other plane never touches the bridge and rings a hair apart
// in pitch (one each side of Sa), so the two beat slowly, as a tanpura's do. The strings share the bridge,
// which yields a little to each of them, so each gets a share of the others. What is heard is the force
// on the bridge, through two resonances for the gourd and the soundboard, eased above a knee for the
// wrap's spikes. Tuned pure to Sa, 3:2 and 4:3 and 15:8, whatever the temperament, as a tanpura is.
// The shruti box is Sa, Pa and Sa below as reeds: three sawtooths, softened, breathing slowly.
// The strings and the body run at half the audio rate (their sound is under 6 kHz; a half-band filter
// brings the drone back up, its images under -47 dB), which halves what they cost; the shruti box runs at
// the audio rate. Rendered with the drum machine, into its output after the kit's grit, at its own level.
// The bridge plane's lines live in RAM2 (9 kB), the other plane's in RAM1 (9 kB). Off, nothing runs.
#ifndef TANPURA_H
#define TANPURA_H
#include <Arduino.h>
#ifndef DMAMEM
#define DMAMEM
#endif

#define TANPURA_STRINGS 4
#define TANPURA_RATE (AUDIO_SAMPLE_RATE_EXACT * 0.5f)
#define TANPURA_HALF (AUDIO_BLOCK_SAMPLES / 2)
#define TANPURA_LINE 560   // samples at the half rate: a period at 40 Hz, Sa's low octave in the low register (Sa from E2), and the wrap

DMAMEM static float tanpura_lines[TANPURA_STRINGS][TANPURA_LINE];   // the plane that meets the bridge
static float tanpura_lines2[TANPURA_STRINGS][TANPURA_LINE];         // the plane clear of it
static float tanpura_arrived[TANPURA_STRINGS][TANPURA_HALF];        // each string's wave at the bridge, this block

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
  void pace(float seconds) { pluck_samples = (uint32_t)(fmaxf(seconds, 0.3f) * TANPURA_RATE); }
  // The thread: 0 closed, the string clears the bridge and rings plain; 1 open, it wraps from the least slope
  void jawari(float j) {
    float closed = 1.0f - constrain(j, 0.0f, 1.0f);
    gap = gap_closed * closed * closed * closed;
  }
  // Sa, in Hz
  void tune(float sa) {
    if (sa <= 0) return;
    sa_hz = sa;
    static const float first[5] = {0, 3.0f / 4, 2.0f / 3, 15.0f / 16, 1.0f};   // below Sa: Pa, Ma, Ni, Sa
    float ratio[TANPURA_STRINGS] = {first[kind >= 1 && kind <= 4 ? kind : 1], 1.0f, 1.0f, 0.5f};
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      string_t &st = string[s];
      float hz = sa_hz * ratio[s];
      float round_trip = constrain(TANPURA_RATE / hz, 20.0f, (float)(TANPURA_LINE - 20));
      st.hz = hz;
      // the loop's loss, per pass: scaled by the string's length, so a short string loses as much a
      // second as a long one
      float scale = round_trip / 168.5f;
      st.loss = constrain(1.0f - (1.0f - loss_c3) * scale, 0.8f, 0.995f);
      st.keep = constrain(1.0f - (1.0f - keep_c3) * scale, 0.998f, 0.99995f);
      // the loop's own delay at the pitch: the lowpass lags, the lean remover leads; the read between
      // samples is exact
      float w = 2 * 3.14159265f / round_trip, cw = cosf(w), sw = sinf(w);
      float b = 1 - st.loss;                                  // lp: (1 - b) / (1 - b z^-1)
      float lag = atan2f(b * sw, 1 - b * cw) / w;
      float c = 1 - settle;                                   // hp: c (1 - z^-1) / (1 - c z^-1)
      float lead = (atan2f(sw, 1 - cw) - atan2f(c * sw, 1 - c * cw)) / w;
      float period = round_trip - lag + lead;
      // the two planes a hair apart, one each side of the pitch: they beat at about a quarter of a hertz
      st.period = period * (1.0f - 0.5f * detune[s] / hz);
      st.period2 = period * (1.0f + 0.5f * detune[s] / hz) + 1.0f;   // read a sample after its write, below
      // the slope at the bridge is smoothed to the string's first harmonics
      st.slope_follow = fminf(2 * 3.14159265f * 2.0f * hz / TANPURA_RATE, 0.5f);
      st.wrap_per_slope = round_trip * bridge_radius;
      st.wrap_most = round_trip * wrap_reach;
      // the force on the bridge is the wave's change, which the same swing makes more of the higher the
      // string; the strings are brought toward one loudness
      st.force = out_level * powf(130.81f / hz, 0.6f);
    }
  }
  bool active() const { return kind != 0 && gain > 0; }

  // Adds a block of the drone into l and r, in the drum machine's units (about +-1 before its gain).
  void render(float *l, float *r, uint32_t n_samples) {
    if (!active()) return;
    if (kind == 5) { reeds(l, r, n_samples); return; }
    const uint32_t half = n_samples / 2;
    for (uint32_t n = 0; n < half; n++) {
      if (++clock >= pluck_samples) {
        clock = 0;
        // four plucks, then a beat's rest
        if (step < TANPURA_STRINGS) pluck(step);
        step = (step + 1) % (TANPURA_STRINGS + 1);
      }
    }
    float mono[TANPURA_HALF], lh[TANPURA_HALF], rh[TANPURA_HALF];
    for (uint32_t n = 0; n < half; n++) mono[n] = lh[n] = rh[n] = 0;
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      string_t &st = string[s];
      float *arrived = tanpura_arrived[s];
      if (!st.ringing) { for (uint32_t n = 0; n < half; n++) arrived[n] = 0; continue; }
      float *line = tanpura_lines[s], *line2 = tanpura_lines2[s];
      // the other strings' pull on the bridge: the earlier strings' this block, the later ones' last block
      float others[TANPURA_HALF];
      for (uint32_t n = 0; n < half; n++) {
        float sum = 0;
        for (uint8_t j = 0; j < TANPURA_STRINGS; j++) if (j != s) sum += tanpura_arrived[j][n];
        others[n] = sum;
      }
      // three passes over the block, each with few enough variables to stay in the FPU's registers: the
      // bridge plane, the other plane, then what is heard
      const float period = st.period, period2 = st.period2, force = st.force, loss = st.loss, keep = st.keep;
      const float left = pan_l[s] * gain, right = pan_r[s] * gain;
      uint32_t w = st.write;
      {
        float lp = st.lp, hp = st.hp, wrap = st.wrap, slope = st.slope, mean_wrap = st.mean_wrap;
        const float slope_follow = st.slope_follow, wrap_per_slope = st.wrap_per_slope, wrap_most = st.wrap_most;
        const float slope_scale = period * 0.5f, thread = gap;
        for (uint32_t n = 0; n < half; n++) {
          // the jawari: the string's slope toward the bridge, its shape a sample in (the loop holds the
          // returning wave with its sign turned, so the difference), smoothed; past the thread's gap it
          // wraps the string onto the bridge by the bridge's curve, and the loop is read that much earlier
          float back = (float)w - period;
          if (back < 0) back += TANPURA_LINE;
          uint32_t a1 = (uint32_t)back + 1; if (a1 >= TANPURA_LINE) a1 -= TANPURA_LINE;
          uint32_t b1 = w ? w - 1 : TANPURA_LINE - 1;
          slope += slope_follow * ((line[a1] - line[b1]) * slope_scale - slope);
          wrap = fminf(fmaxf((slope - thread) * wrap_per_slope, 0.0f), wrap_most);
          // what the wrapping shortens the string by (a peak that lets go slowly, taken in part: the fundamental
          // is made in the swing's lobe, where the string wraps, the partials over the whole swing) is tuned
          // out, as a player tunes a string with its jawari
          mean_wrap = fmaxf(wrap, mean_wrap * 0.9999f);
          // the wave arriving at the bridge, read between samples (cubic: it keeps the treble a linear read loses)
          float pos = (float)w - (period + 0.4f * mean_wrap - wrap);
          if (pos < 0) pos += TANPURA_LINE;
          uint32_t i1 = (uint32_t)pos;
          float frac = pos - i1;
          uint32_t i0 = i1 ? i1 - 1 : TANPURA_LINE - 1;
          uint32_t i2 = i1 + 1; if (i2 >= TANPURA_LINE) i2 -= TANPURA_LINE;
          uint32_t i3 = i2 + 1; if (i3 >= TANPURA_LINE) i3 -= TANPURA_LINE;
          float y0 = line[i0], y1 = line[i1], y2 = line[i2], y3 = line[i3];
          float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2 * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
          float y = ((c3 * frac + c2) * frac + c1) * frac + y1;
          // the string's loss: a gentle lowpass and a little less each time round; the bridge yielding under
          // the strings' pull, which takes from each and gives it a share of the others'; no standing lean,
          // which the one-sided bridge would pile up; and a big swing losing a little more, the string's own
          // nonlinear loss, which also bounds the loop
          lp += loss * (y - lp);
          float x = keep * lp - couple * (others[n] + y);
          hp += settle * (x - hp);
          x -= hp;
          x -= 0.1f * x * x * x;
          line[w] = fminf(fmaxf(x, -1.0f), 1.0f);
          arrived[n] = y;
          if (++w == TANPURA_LINE) w = 0;
        }
        st.lp = lp; st.hp = hp; st.wrap = wrap; st.slope = slope; st.mean_wrap = mean_wrap;
      }
      float clear_plane[TANPURA_HALF];
      {
        // the other plane, clear of the bridge, read a sample behind (its period allows for it)
        float lp2 = st.lp2, hp2 = st.hp2;
        uint32_t w2 = st.write;
        for (uint32_t n = 0; n < half; n++) {
          float pos2 = (float)w2 + 1 - period2;
          if (pos2 < 0) pos2 += TANPURA_LINE;
          uint32_t j1 = (uint32_t)pos2;
          float frac2 = pos2 - j1;
          uint32_t j2 = j1 + 1; if (j2 >= TANPURA_LINE) j2 -= TANPURA_LINE;
          float yb = line2[j1] + (line2[j2] - line2[j1]) * frac2;
          lp2 += loss * (yb - lp2);
          float x2 = keep * lp2;
          hp2 += settle * (x2 - hp2);
          x2 -= hp2;
          line2[w2] = fminf(fmaxf(x2, -1.0f), 1.0f);
          clear_plane[n] = yb;
          if (++w2 == TANPURA_LINE) w2 = 0;
        }
        st.lp2 = lp2; st.hp2 = hp2;
      }
      {
        // out: the force on the bridge, the waves' change, without the DC the bridge leaves, eased past the knee
        float dc = st.dc, last_y = st.last_y, last_y2 = st.last_y2;
        for (uint32_t n = 0; n < half; n++) {
          float y = arrived[n], yb = clear_plane[n];
          float o = ((y - last_y) + plane2 * (yb - last_y2)) * force;
          last_y = y; last_y2 = yb;
          o -= dc;
          dc += 0.001f * o;
          if (o > knee) o = knee + (o - knee) / (1 + 4 * (o - knee));
          else if (o < -knee) o = -knee + (o + knee) / (1 - 4 * (o + knee));
          lh[n] += o * left;
          rh[n] += o * right;
          mono[n] += o;
        }
        st.dc = dc; st.last_y = last_y; st.last_y2 = last_y2;
      }
      st.write = w;
    }
    // the body: the gourd and the soundboard, two resonances over the dry sound
    const float body_gain = gain * 0.45f;
    for (uint32_t n = 0; n < half; n++) {
      float m = mono[n];
      float b = (body1.run(m) * body_mix1 + body2.run(m) * body_mix2) * body_gain;
      lh[n] += b;
      rh[n] += b;
    }
    // back up to the audio rate: a half-band filter, the even samples passed on, the odd ones made from
    // the four each side (which puts the drone 8 samples late, nothing to a drone)
    up(lh, l, up_l, half);
    up(rh, r, up_r, half);
  }

private:
  struct string_t {
    float period = 400, period2 = 400.5f, hz = 110, force = 1, loss = 0.96f, keep = 0.9996f;
    float slope_follow = 0.02f, wrap_per_slope = 1.7f, wrap_most = 6;
    float lp = 0, lp2 = 0, hp = 0, hp2 = 0, dc = 0, last_y = 0, last_y2 = 0;
    float wrap = 0, slope = 0, mean_wrap = 0;
    bool ringing = false;
    uint32_t write = 0;
  };
  // x: half samples at the half rate; adds their interpolation into out (2 * half samples); past holds the
  // last seven of the previous block
  static void up(const float *x, float *out, float *past, uint32_t half) {
    static const float c[4] = {0.607550f,-0.138171f,0.034433f,-0.003336f};   // the odd samples' taps, at 0.5, 1.5, 2.5 and 3.5 either side
    float a[TANPURA_HALF + 7];
    for (uint32_t k = 0; k < 7; k++) a[k] = past[k];
    for (uint32_t n = 0; n < half; n++) a[7 + n] = x[n];
    for (uint32_t m = 0; m < half; m++) {
      const float *p = a + m;   // p[0..7] = x[m-7..m]: the even sample is x[m-4], the odd one between x[m-4] and x[m-3]
      out[2 * m] += p[3];
      out[2 * m + 1] += c[0] * (p[3] + p[4]) + c[1] * (p[2] + p[5]) + c[2] * (p[1] + p[6]) + c[3] * (p[0] + p[7]);
    }
    for (uint32_t k = 0; k < 7; k++) past[k] = a[half + k];
  }
  struct resonator_t {
    float a1 = 0, a2 = 0, b0 = 0, z1 = 0, z2 = 0;
    void set(float hz, float q) {   // unity gain at its peak
      float w = 2 * 3.14159265f * hz / TANPURA_RATE, r = expf(-w / (2 * q));
      a1 = 2 * r * cosf(w); a2 = -r * r;
      float re = 1 - a1 * cosf(w) - a2 * cosf(2 * w), im = a1 * sinf(w) + a2 * sinf(2 * w);
      b0 = sqrtf(re * re + im * im);
      z1 = z2 = 0;
    }
    float run(float x) { float y = b0 * x + a1 * z1 + a2 * z2; z2 = z1; z1 = y; return y; }
  };
  static constexpr float loss_c3 = 0.96f;      // the loop's lowpass, for a string of 337 samples (C3): 1 none; the treble rings seconds
  static constexpr float keep_c3 = 0.9996f;    // of the loop each time round, for the same string
  static constexpr float settle = 0.0002f;     // the loop's lean let go: under a hertz, well under the lowest string
  static constexpr float couple = 0.0015f;     // of the strings' pull the bridge yields to
  static constexpr float plane2 = 0.5f;        // the plane clear of the bridge, in the output
  static constexpr float gap_closed = 2.5f;    // the thread's gap closed, as the slope (per string length) it takes to reach the bridge
  static constexpr float bridge_radius = 0.003f;   // of the string's length the bridge's curve takes up, per unit of slope
  static constexpr float wrap_reach = 0.02f;       // of the string's length the bridge can take up at most
  static constexpr float pluck_at = 0.23f;     // of the string's length from the bridge, where the finger plucks
  static constexpr float out_level = 8.3f;     // the force (a difference of half-rate samples), scaled to the old drone's loudness at 50
  static constexpr float knee = 0.35f;         // of full scale, where a string's spikes are eased
  static constexpr float body_mix1 = 0.35f, body_mix2 = 0.8f;   // the gourd, the soundboard
  const float pan_l[TANPURA_STRINGS] = {0.59f, 0.42f, 0.28f, 0.48f};
  const float pan_r[TANPURA_STRINGS] = {0.28f, 0.42f, 0.59f, 0.48f};
  const float detune[TANPURA_STRINGS] = {0.22f, 0.3f, -0.26f, 0.17f};   // Hz between the planes

  void clear() {
    for (uint8_t s = 0; s < TANPURA_STRINGS; s++) {
      for (uint32_t i = 0; i < TANPURA_LINE; i++) tanpura_lines[s][i] = tanpura_lines2[s][i] = 0;
      for (uint32_t n = 0; n < TANPURA_HALF; n++) tanpura_arrived[s][n] = 0;
      string_t &st = string[s];
      st.ringing = false;
      st.lp = st.lp2 = st.hp = st.hp2 = st.dc = st.last_y = st.last_y2 = 0;
      st.wrap = st.slope = st.mean_wrap = 0;
    }
    for (uint32_t k = 0; k < 7; k++) up_l[k] = up_r[k] = 0;
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
    uint32_t len = (uint32_t)(st.period + 0.5f);
    float half = len * 0.5f, amp = s == 3 ? 0.65f : 0.55f, soft = 0;
    for (uint32_t k = 0; k < len; k++) {
      float x = k < half ? k / half : (len - k) / half;   // along the string, 0 at the bridge, 1 at the nut
      float d = x < pluck_at ? x / pluck_at : (1 - x) / (1 - pluck_at);
      d = d * d * (3 - 2 * d);   // the fingertip's rounding
      float v = (k < half ? -0.5f : 0.5f) * d * amp;   // drawn toward the bridge's side
      noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
      soft += 0.35f * ((int32_t)noise * (1.0f / 2147483648.0f) - soft);
      v += soft * amp * 0.05f;
      uint32_t at = (st.write + TANPURA_LINE - len + k) % TANPURA_LINE;
      tanpura_lines[s][at] = tanpura_lines[s][at] * 0.3f + v;
      tanpura_lines2[s][at] = tanpura_lines2[s][at] * 0.3f + v * 0.8f;
    }
    st.ringing = true;
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
  float gap = 0.16f;   // jawari(0.6), the setting's default
  resonator_t body1, body2;
  uint32_t pluck_samples = 26470, clock = 0;   // 1.2 s at the half rate
  float up_l[7] = {0}, up_r[7] = {0};        // the half-band filter's past, each side
  uint8_t step = 0;
  string_t string[TANPURA_STRINGS];
  uint32_t noise = 0x9E3779B9u;
  float breath = 0, reed_phase[3] = {0, 0.33f, 0.66f}, reed_lp[3] = {0, 0, 0};
};

#endif
