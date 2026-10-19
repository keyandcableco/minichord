// The bends (lib/bent_bus) run on a computer: renders each one to a wav over a test groove, and checks
// what can be checked without ears. From firmware/:
//
//   g++ -std=c++17 -O2 -Wall -Itools/bent_render -Ilib/bent_bus/src tools/bent_render/bent_render.cpp lib/bent_bus/src/bent_bus.cpp -o /tmp/bent_render && /tmp/bent_render [out dir]
//
// The groove: a chord of four detuned saws struck every half second (120 bpm), a kick on each beat and a
// hat between, about as loud as the minichord plays (peaks near -12 dBFS). Each wav is the groove with
// one bend pressed and let go, as a hand would; the depth's shape is printed with it. It checks:
//   - at depth 0 everything comes out bit for bit as it went in, before any bend and after each
//   - no block is kept once every bend is let go but the pitch shifter's 32 (the pool's count)
//   - the most blocks held at once, against the 900 the minichord has
//   - pitch drop: the chord's fundamental, measured, lands at the depth's semitones
// and exits 1 if anything fails.
#include "bent_bus.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <functional>
#include <chrono>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("    FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const float FS = AUDIO_SAMPLE_RATE_EXACT;

// the test groove, sample by sample, both sides (the chord a little wider on the right)
struct Groove {
  bool drums = true, solo = false;
  double t = 0;
  float chord_f[4] = {130.81f, 164.81f, 196.0f, 261.63f};   // C major, from C3
  double ph[2][4] = {};
  float drum_part = 0;   // the last sample's drums, which go in on their own inputs
  float operator()(int side) {
    double beat = fmod(t, 0.5), eighth = fmod(t, 0.25);
    float env = expf(-(float)beat * 3.0f);
    float y = 0;
    for (int k = 0; k < (solo ? 1 : 4); k++) {
      double f = chord_f[k] * (side ? 1.002 : 0.998);
      ph[side][k] += f / FS; ph[side][k] -= floor(ph[side][k]);
      y += (float)(2 * ph[side][k] - 1) * 0.12f * env;
    }
    float kick = sinf(2 * (float)M_PI * (float)(50 * beat + 60 * (1 - expf(-(float)beat * 30)) / 30)) * expf(-(float)beat * 18) * 0.45f;
    float hat = (eighth > 0.125 && eighth < 0.135) ? ((rand() / (float)RAND_MAX) * 2 - 1) * 0.08f * expf(-(float)(eighth - 0.125) * 400) : 0;
    if (side == 1) t += 1.0 / FS;
    if (!drums) kick = hat = 0;
    drum_part = (kick + hat) * 32767.0f * 0.5f;
    return y * 32767.0f * 0.5f;
  }
};

struct Run {
  std::vector<int16_t> in, out;   // interleaved; in is the mix and the drums summed, as the last mix did
  std::vector<int16_t> drums;     // the drums alone
  int most_held = 0, most_pool = 0;
  double us_per_block = 0;
};

// blocks of the groove through a fresh bend object; set(bend, seconds) moves the depths before each block
static Run render(float seconds, std::function<void(AudioEffectBend &, float)> set, bool drums = true, float quiet_until = 0) {
  AudioEffectBend bend;
  Groove g;
  g.drums = drums;
  g.solo = !drums;
  Run r;
  int blocks = (int)(seconds * FS / AUDIO_BLOCK_SAMPLES);
  int base_used = audio_pool().used;
  audio_pool().most = base_used;
  double spent = 0;
  for (int b = 0; b < blocks; b++) {
    float t = b * AUDIO_BLOCK_SAMPLES / FS;
    set(bend, t);
    audio_block_t *in[2] = {AudioStream::allocate(), AudioStream::allocate()};
    audio_block_t *dr[2] = {AudioStream::allocate(), AudioStream::allocate()};
    for (int n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
      for (int c = 0; c < 2; c++) {
        float x = g(c), dx = g.drum_part;
        if (t < quiet_until) x = dx = 0;   // the instrument silent, as at a bank load
        in[c]->data[n] = (int16_t)std::max(-32768.0f, std::min(32767.0f, x));
        dr[c]->data[n] = (int16_t)std::max(-32768.0f, std::min(32767.0f, dx));
      }
    }
    for (int n = 0; n < AUDIO_BLOCK_SAMPLES; n++) for (int c = 0; c < 2; c++) {
      int32_t sum = in[c]->data[n] + dr[c]->data[n];
      r.in.push_back((int16_t)std::max(-32768, std::min(32767, sum)));
      r.drums.push_back(dr[c]->data[n]);
    }
    bend.feed(0, in[0]); bend.feed(1, in[1]); bend.feed(2, dr[0]); bend.feed(3, dr[1]);
    auto t0 = std::chrono::steady_clock::now();
    bend.update();
    spent += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    for (int n = 0; n < AUDIO_BLOCK_SAMPLES; n++) for (int c = 0; c < 2; c++) r.out.push_back(bend.out[c] ? bend.out[c]->data[n] : 0);
    for (int c = 0; c < 2; c++) { AudioStream::release(bend.out[c]); bend.out[c] = nullptr; }
    r.most_held = std::max(r.most_held, (int)bend.held_blocks());
    r.most_pool = std::max(r.most_pool, audio_pool().most - base_used);
  }
  r.us_per_block = spent / blocks;
  // let go of everything, a second more, and count what's kept
  bend.pitch_drop(0, 12, 150); bend.starve(0); bend.crush(0, 0); bend.stutter(0, 0, 1102, true); bend.interact(0);
  for (int b = 0; b < 400; b++) {
    audio_block_t *in[2] = {AudioStream::allocate(), AudioStream::allocate()};
    memset(in[0]->data, 0, sizeof(in[0]->data)); memset(in[1]->data, 0, sizeof(in[1]->data));
    bend.feed(0, in[0]); bend.feed(1, in[1]);
    bend.update();
    for (int c = 0; c < 2; c++) { AudioStream::release(bend.out[c]); bend.out[c] = nullptr; }
  }
  int kept = audio_pool().used - base_used;
  CHECK(kept == 2 * AudioEffectBend::HISTORY, "blocks kept after letting go: %d, not %d", kept, 2 * AudioEffectBend::HISTORY);
  CHECK(bend.resting(), "not resting after letting go");
  return r;
}

static void write_wav(const std::string &path, const std::vector<int16_t> &s) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { printf("can't write %s\n", path.c_str()); failures++; return; }
  uint32_t data = s.size() * 2, rate = 44100, byte_rate = rate * 4;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  uint32_t riff = 36 + data, fmt_len = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt_len, 4, 1, f);
  fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f);
  fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fwrite(s.data(), 2, s.size(), f);
  fclose(f);
}

static double rms_db(const std::vector<int16_t> &s, float from, float to) {
  size_t a = (size_t)(from * FS) * 2, b = std::min(s.size(), (size_t)(to * FS) * 2);
  double sum = 0;
  for (size_t i = a; i < b; i++) sum += (double)s[i] * s[i];
  return 10 * log10(sum / std::max<size_t>(1, b - a) / (32768.0 * 32768.0) + 1e-12);
}

// the period of the left side in a stretch, by its autocorrelation's peak between FS/hi and FS/lo
// samples (parabola through the peak), as a frequency. A spectrum is no good here: the pitch shifter's
// heads, jumping, put sidebands round each partial as strong as it, and their mean is pulled by the
// fades; within each head's stretch the period is exact, and that is what the ear hears as the pitch.
static double pitch_hz(const std::vector<int16_t> &s, float from, float to, double lo, double hi) {
  size_t a = (size_t)(from * FS), b = (size_t)(to * FS);
  int l0 = (int)(FS / hi), l1 = (int)(FS / lo) + 1;
  std::vector<double> r(l1 + 2, 0);
  for (int lag = l0 - 1; lag <= l1 + 1; lag++) {
    double sum = 0, e0 = 0, e1 = 0;
    for (size_t n = a; n + lag < b; n++) { double x = s[2 * n], y = s[2 * (n + lag)]; sum += x * y; e0 += x * x; e1 += y * y; }
    r[lag] = sum / sqrt(e0 * e1 + 1e-9);
  }
  int best = l0;
  for (int lag = l0; lag <= l1; lag++) if (r[lag] > r[best]) best = lag;
  double y0 = r[best - 1], y1 = r[best], y2 = r[best + 1];
  double off = (y0 - y2) / (2 * (y0 - 2 * y1 + y2));
  return FS / (best + off);
}

// a press: up over 30 ms to the depth, held, let go over 200 ms (touches ease off; see the touch
// strength measurements)
static float press(float t, float on, float off, float depth) {
  if (t < on || t > off + 0.2f) return 0;
  if (t < on + 0.03f) return depth * (t - on) / 0.03f;
  if (t <= off) return depth;
  return depth * (1 - (t - off) / 0.2f);
}

int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  srand(1);

  printf("rest: every depth 0\n");
  Run dry = render(4, [](AudioEffectBend &, float) {});
  CHECK(dry.in == dry.out, "the sound at rest isn't the input bit for bit");
  printf("    bit for bit: %s, %.2f us a block, holds %d blocks\n", dry.in == dry.out ? "yes" : "no", dry.us_per_block, dry.most_held);
  write_wav(dir + "/0 dry.wav", dry.out);

  printf("pitch drop: 0.25 / 0.5 / 1 of 12 semitones, sag 150 ms, a press each\n");
  Run drop = render(8, [](AudioEffectBend &b, float t) {
    float d = press(t, 0.5f, 2.0f, 0.25f) + press(t, 2.75f, 4.5f, 0.5f) + press(t, 5.25f, 7.0f, 1.0f);
    b.pitch_drop(d, 12, 150);
  });
  write_wav(dir + "/1 pitch drop.wav", drop.out);
  {
    // measured on the chord's root alone, without the drums
    Run plain = render(2, [](AudioEffectBend &, float) {}, false);
    Run drop = render(8, [](AudioEffectBend &b, float t) {
      float d = press(t, 0.5f, 2.0f, 0.25f) + press(t, 2.75f, 4.5f, 0.5f) + press(t, 5.25f, 7.0f, 1.0f);
      b.pitch_drop(d, 12, 150);
    }, false);
    Run dry = plain;
    // the chord's root, C3 130.8 Hz, on held stretches: down 3, 6 and 12 semitones
    double f0 = pitch_hz(dry.out, 1.0f, 2.0f, 130.81 * 0.93, 130.81 / 0.93);
    auto at = [](double st) { return 130.81 * pow(2, st / 12); };   // a band of +-1.25 semitones round where the root should be
    double f1 = pitch_hz(drop.out, 1.0f, 2.0f, at(-3) * 0.93, at(-3) / 0.93), f2 = pitch_hz(drop.out, 3.5f, 4.5f, at(-6) * 0.93, at(-6) / 0.93), f3 = pitch_hz(drop.out, 6.0f, 7.0f, at(-12) * 0.93, at(-12) / 0.93);
    double s1 = 12 * log2(f1 / f0), s2 = 12 * log2(f2 / f0), s3 = 12 * log2(f3 / f0);
    printf("    root %.1f Hz -> %.1f (%+.2f st), %.1f (%+.2f st), %.1f (%+.2f st); %.2f us a block, most held %d, pool +%d\n",
           f0, f1, s1, f2, s2, f3, s3, drop.us_per_block, drop.most_held, drop.most_pool);
    CHECK(fabs(s1 + 3) < 0.15 && fabs(s2 + 6) < 0.15 && fabs(s3 + 12) < 0.15, "the drop isn't 3, 6 and 12 semitones");
    size_t from = (size_t)(7.7f * FS) * 2;   // let go at 7.0, eased off by 7.2, sagged back by 7.35, the shifter out 10 ms later
    CHECK(std::equal(drop.in.begin() + from, drop.in.end(), drop.out.begin() + from), "not the input bit for bit half a second after letting go");
  }

  printf("level through the shifter: the chord held half a semitone down (its heads crossing every 1.2 s) against dry\n");
  {
    // the heads sit half a window apart, so on one steady partial their crossfade loses or gains by its
    // frequency; over a chord's partials it averages 2/3 of the power, which the shifter makes up
    auto chord_only = [](AudioEffectBend &b, float t, bool drop) { if (drop) b.pitch_drop(0.5f / 12, 12, 150); (void)t; };
    Run plain = render(8, [&](AudioEffectBend &b, float t) { chord_only(b, t, false); });
    Run held = render(8, [&](AudioEffectBend &b, float t) { chord_only(b, t, true); });
    // the drums are on their own inputs and in the output either way; the chord is what's shifted
    double d = rms_db(held.out, 1.0f, 8.0f) - rms_db(plain.out, 1.0f, 8.0f);
    printf("    %+.2f dB\n", d);
    CHECK(fabs(d) < 0.7, "the shifter changes the level of a held chord by %.2f dB", d);
  }

  printf("voltage starve: 0.3, 0.6 and 1, two seconds each\n");
  Run starve = render(8, [](AudioEffectBend &b, float t) {
    b.starve(press(t, 0.5f, 2.3f, 0.3f) + press(t, 2.75f, 4.6f, 0.6f) + press(t, 5.0f, 7.0f, 1.0f));
  });
  write_wav(dir + "/2 voltage starve.wav", starve.out);
  printf("    level against dry: %+.1f dB at 0.3, %+.1f at 0.6, %+.1f at 1; %.2f us a block\n",
         rms_db(starve.out, 1, 2.2f) - rms_db(dry.out, 1, 2.2f) , rms_db(starve.out, 3.2f, 4.5f) - rms_db(dry.out, 3.2f, 4.5f),
         rms_db(starve.out, 5.5f, 7) - rms_db(dry.out, 1.5f, 3.0f), starve.us_per_block);

  printf("bitcrush: a sweep 0 to 1 and back, then bits only and rate only at 0.8\n");
  Run crush = render(9, [](AudioEffectBend &b, float t) {
    if (t < 5) b.crush(t < 2.5f ? t / 2.5f : (5 - t) / 2.5f, 0);
    else if (t < 7) b.crush(0.8f, 1);
    else b.crush(t < 8.8f ? 0.8f : 0, 2);
  });
  write_wav(dir + "/3 bitcrush.wav", crush.out);
  printf("    %.2f us a block\n", crush.us_per_block);

  printf("stutter: presses at 0.1, 0.5 and 1, free; then 0.3 and 0.8 in sync with 120 bpm; the drums playing on\n");
  auto stutter_presses = [](bool drums_through) {
    return [drums_through](AudioEffectBend &b, float t) {
      float d = press(t, 0.6f, 1.8f, 0.1f) + press(t, 2.6f, 3.6f, 0.5f) + press(t, 4.6f, 5.6f, 1.0f);
      float s = press(t, 6.6f, 7.6f, 0.3f) + press(t, 8.6f, 9.6f, 0.8f);
      b.stutter(d + s, t > 6.2f ? (uint32_t)(FS * 0.5f) : 0, 1102, drums_through);
    };
  };
  Run stut = render(10, stutter_presses(true));
  write_wav(dir + "/4 stutter.wav", stut.out);
  Run stut_all = render(10, stutter_presses(false));
  write_wav(dir + "/4b stutter, the drums too.wav", stut_all.out);
  {
    // the kick on 3.0 and 3.5 s (inside the second press): there with the drums playing through, in the
    // output as the drums alone put it, and not where they stutter too
    auto kick_db = [](const std::vector<int16_t> &x, float at) { return rms_db(x, at, at + 0.06f); };
    double through = kick_db(stut.out, 3.0f) - kick_db(stut.out, 2.9f), caught = kick_db(stut_all.out, 3.0f) - kick_db(stut_all.out, 2.9f);
    printf("    the kick at 3.0 s against the 60 ms before it: %+.1f dB with the drums playing on, %+.1f dB with them stuttering\n", through, caught);
    CHECK(through > 6 && caught < 3, "the drums don't carry on through the stutter (or do when they shouldn't)");
  }
  printf("    %.2f us a block, most held %d blocks, pool +%d of 900\n", stut.us_per_block, stut.most_held, stut.most_pool);
  CHECK(stut.most_held <= 2 * (AudioEffectBend::HISTORY + AudioEffectBend::STUTTER_BLOCKS), "held more than it should");

  printf("all at once: drop 0.4, starve 0.4, crush 0.4, stutter 0.6\n");
  Run all = render(6, [](AudioEffectBend &b, float t) {
    float d = press(t, 0.5f, 5.0f, 1.0f);
    b.pitch_drop(0.4f * d, 12, 150); b.starve(0.4f * d); b.crush(0.4f * d, 0); b.stutter(t > 2 && t < 4 ? 0.6f : 0, 0, 1102, true);
  });
  write_wav(dir + "/5 all at once.wav", all.out);
  printf("    %.2f us a block (dry %.2f), most held %d, pool +%d\n", all.us_per_block, dry.us_per_block, all.most_held, all.most_pool);
  CHECK(all.most_pool <= 2 * (AudioEffectBend::HISTORY + AudioEffectBend::STUTTER_BLOCKS) + 6, "used more of the pool than it should");

  printf("bends interact: a pitch drop and then a starve, each with interact 0 and 1\n");
  {
    auto scene = [](float k) {
      return [k](AudioEffectBend &b, float t) {
        b.interact(k);
        b.pitch_drop(press(t, 0.5f, 2.5f, 1.0f), 12, 150);
        b.starve(press(t, 3.5f, 5.5f, 0.7f));
      };
    };
    Run alone = render(6.5f, scene(0)), together = render(6.5f, scene(1));
    write_wav(dir + "/6 interact.wav", together.out);
    // with the drop held an octave down, the sample rate at 5 kHz: little left over 3 kHz that alone had
    auto above = [](const std::vector<int16_t> &x, float from, float to) {   // energy over 3 kHz by a first difference, dB
      size_t a = (size_t)(from * FS) * 2, b = (size_t)(to * FS) * 2; double e = 0, d = 0;
      for (size_t i = a + 2; i < b; i += 2) { double dx = x[i] - x[i - 2]; d += dx * dx; e += (double)x[i] * x[i]; }
      return 10 * log10(d / (e + 1e-9));
    };
    double hf_alone = above(alone.out, 1.5f, 2.4f), hf_together = above(together.out, 1.5f, 2.4f);
    // with the starve, fewer bits: the steps show as a rougher difference
    double st_alone = above(alone.out, 4.5f, 5.4f), st_together = above(together.out, 4.5f, 5.4f);
    printf("    held drop: brightness %+.1f dB alone, %+.1f with interact; held starve: %+.1f alone, %+.1f with interact\n",
           hf_alone, hf_together, st_alone, st_together);
    CHECK(hf_together != hf_alone && st_together != st_alone, "interact changes nothing");
    size_t from = (size_t)(6.2f * FS) * 2;
    CHECK(std::equal(together.in.begin() + from, together.in.end(), together.out.begin() + from), "not bit for bit after letting go with interact on");
  }

  printf("a stutter caught in silence (pressed at a bank load, before anything plays) and held 5 s, refresh 0 and 1 s\n");
  {
    auto held = [](uint32_t refresh) {
      return [refresh](AudioEffectBend &b, float t) { b.stutter(t < 6.0f ? 1.0f : 0, 0, 1102, false, refresh); };
    };
    Run frozen = render(7, held(0), true, 1.0f), rolling = render(7, held((uint32_t)FS), true, 1.0f);
    write_wav(dir + "/4c stutter refreshing.wav", rolling.out);
    double f = rms_db(frozen.out, 2.5f, 5.5f), r = rms_db(rolling.out, 2.5f, 5.5f), live = rms_db(rolling.in, 2.5f, 5.5f);
    printf("    held from silence, 2.5-5.5 s: %.1f dB without refresh, %.1f dB with (the music %.1f dB)\n", f, r, live);
    CHECK(f < -90 && r > live - 6, "a refreshing stutter doesn't leave the silence it caught (or one without refresh does)");
  }

  printf("rest again after all that: bit for bit\n");
  Run again = render(2, [](AudioEffectBend &b, float t) { if (t < 1) { b.crush(0.5f, 0); b.starve(0.5f); } else { b.crush(0, 0); b.starve(0); } });
  {
    size_t from = (size_t)(1.5f * FS) * 2;
    bool same = std::equal(again.in.begin() + from, again.in.end(), again.out.begin() + from);
    CHECK(same, "half a second after letting go of the crush and the starve, not the input bit for bit");
  }

  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
