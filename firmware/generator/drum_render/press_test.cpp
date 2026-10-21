// Checks the drum machine's press calls (DRUM PRESS in main.cpp) on this computer, with the same
// stand-ins as render.cpp: bend() moves a ringing voice's pitch by the ratio asked, on a synthesized
// body and on a recorded stroke, bend(v, 1) brings it back, a later hit lands unbent, and choke()
// lays a hand on a ringing cymbal. Run with ./press_test.sh; prints PASS or FAIL a check.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

#include "drum_machine.h"
#include "drum_patterns.h"
const int16_t AudioWaveformSine[257] = {
#include "sine.inc"
};

static int failures = 0;
static void check(bool ok, const char *what, double got, double wanted) {
  printf("%s  %-52s got %8.3f, wanted about %8.3f\n", ok ? "PASS" : "FAIL", what, got, wanted);
  if (!ok) failures++;
}

// Renders blocks into one mono stream (the left side), returning the seconds actually rendered
static double render(AudioDrumMachine &dm, std::vector<float> &out, double seconds) {
  int blocks = (int)(seconds * AUDIO_SAMPLE_RATE_EXACT / 128);
  for (int b = 0; b < blocks; b++) {
    dm.out[0] = dm.out[1] = nullptr;
    dm.update();
    for (int n = 0; n < 128; n++) out.push_back(dm.out[0] ? dm.out[0]->data[n] : 0);
    delete dm.out[0];
    delete dm.out[1];
  }
  return blocks * 128 / AUDIO_SAMPLE_RATE_EXACT;
}

// The dominant pitch of a stretch, by counting rising zero crossings
static double zero_cross_hz(const std::vector<float> &x, double from_s, double to_s) {
  int from = (int)(from_s * AUDIO_SAMPLE_RATE_EXACT), to = (int)(to_s * AUDIO_SAMPLE_RATE_EXACT);
  if (to > (int)x.size()) to = x.size();
  int crossings = 0;
  for (int n = from + 1; n < to; n++) {
    if (x[n - 1] < 0 && x[n] >= 0) crossings++;
  }
  return crossings * AUDIO_SAMPLE_RATE_EXACT / (to - from);
}

static double rms(const std::vector<float> &x, double from_s, double to_s) {
  int from = (int)(from_s * AUDIO_SAMPLE_RATE_EXACT), to = (int)(to_s * AUDIO_SAMPLE_RATE_EXACT);
  if (to > (int)x.size()) to = x.size();
  double sum = 0;
  for (int n = from; n < to; n++) sum += (double)x[n] * x[n];
  return sqrt(sum / (to - from));
}

// The pitch of a voice struck and, after settle_s, bent by ratio (1 for not), read over the stretch after
static double struck_hz(const drum_kit_t *kit, uint8_t v, float ratio, double settle_s, double read_s, float pitch = 0) {
  AudioDrumMachine dm;
  dm.setKit(kit);
  dm.level(0.7f);
  std::vector<float> x;
  dm.hit(v, 1.0f, pitch);
  double settled = render(dm, x, settle_s);
  if (ratio != 1.0f) dm.bend(v, ratio);
  double read = render(dm, x, read_s);
  return zero_cross_hz(x, settled, settled + read);
}

int main() {
  const float semis3 = exp2f(3 / 12.0f);   // drum press at its default, three semitones

  // A synthesized skin: the low tom of the first kit, past its strike bend (tone_bend has mostly died
  // after 60 ms), read over a short stretch so its own decay moves both readings alike
  const drum_kit_t *kit = &drum_kits[0];
  double plain = struck_hz(kit, DRUM_TOM_LO, 1.0f, 0.06, 0.05);
  double bent = struck_hz(kit, DRUM_TOM_LO, semis3, 0.06, 0.05);
  check(fabs(bent / plain - semis3) < 0.03, "a synth tom bent 3 semitones up", bent / plain, semis3);

  // bend(v, 1) puts it back
  {
    AudioDrumMachine dm;
    dm.setKit(kit);
    dm.level(0.7f);
    std::vector<float> x;
    dm.hit(DRUM_TOM_LO, 1.0f);
    double at = render(dm, x, 0.06);
    dm.bend(DRUM_TOM_LO, semis3);
    at += render(dm, x, 0.03);
    dm.bend(DRUM_TOM_LO, 1.0f);
    double read = render(dm, x, 0.05);
    double back = zero_cross_hz(x, at, at + read);
    check(fabs(back / plain - 1) < 0.03, "eased off, the tom is in tune again", back / plain, 1.0);
  }

  // A recorded stroke: a voice that is nothing but its sample (the tabla's tun, say). Bent up, the
  // stroke plays that much faster, so it ends that much sooner: the lengths say whether it moved.
  const drum_kit_t *sampled = nullptr;
  uint8_t sampled_voice = 0;
  for (int k = 0; k < drum_kit_count && !sampled; k++) {
    for (uint8_t v = 0; v < DRUM_VOICES && !sampled; v++) {
      const drum_voice_t &dv = drum_kits[k].v[v];
      if (dv.sample >= 0 && dv.tone_hz == 0 && dv.noise_level == 0 && dv.metal_level == 0 && !dv.stutter) {
        sampled = &drum_kits[k];
        sampled_voice = v;
      }
    }
  }
  if (sampled) {
    auto rings_for = [&](float ratio) -> double {
      AudioDrumMachine dm;
      dm.setKit(sampled);
      dm.level(0.7f);
      std::vector<float> x;
      dm.hit(sampled_voice, 1.0f, 130.8f);
      double settled = render(dm, x, 0.01);
      if (ratio != 1.0f) dm.bend(sampled_voice, ratio);
      render(dm, x, 4.0);
      int last = 0;
      for (int n = 0; n < (int)x.size(); n++) {
        if (fabsf(x[n]) > 100) last = n;
      }
      return last / AUDIO_SAMPLE_RATE_EXACT - settled;
    };
    double s_plain = rings_for(1.0f), s_bent = rings_for(semis3);
    check(fabs(s_plain / s_bent - semis3) < 0.08, "a recorded stroke bent 3 semitones plays that much sooner", s_plain / s_bent, semis3);
  } else {
    printf("SKIP  no kit with a recorded stroke to bend\n");
  }

  // A later hit lands unbent
  {
    AudioDrumMachine dm;
    dm.setKit(kit);
    dm.level(0.7f);
    std::vector<float> x;
    dm.hit(DRUM_TOM_LO, 1.0f);
    double at = render(dm, x, 0.06);
    dm.bend(DRUM_TOM_LO, semis3);
    at += render(dm, x, 0.05);
    dm.hit(DRUM_TOM_LO, 1.0f);
    render(dm, x, 0.20);
    double again = zero_cross_hz(x, at + 0.06, at + 0.11);
    check(fabs(again / plain - 1) < 0.03, "the next hit on a bent tom lands in tune", again / plain, 1.0);
  }

  // A hand laid on a ringing cymbal: what rings after the choke is a shadow of what rang without it
  {
    AudioDrumMachine with, without;
    with.setKit(kit);
    without.setKit(kit);
    with.level(0.7f);
    without.level(0.7f);
    std::vector<float> xw, xo;
    with.hit(DRUM_CYMBAL, 1.0f);
    without.hit(DRUM_CYMBAL, 1.0f);
    render(with, xw, 0.1);
    render(without, xo, 0.1);
    with.choke(DRUM_CYMBAL);
    render(with, xw, 0.2);
    render(without, xo, 0.2);
    double ratio = rms(xw, 0.15, 0.3) / (rms(xo, 0.15, 0.3) + 1e-9);
    check(ratio < 0.1, "a choked cymbal dies away", ratio, 0.0);
  }

  printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n", failures);
  return failures ? 1 : 0;
}
