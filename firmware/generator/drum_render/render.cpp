// Renders each groove of drum_patterns.h through drum_machine.h: four bars and a fill bar, a crash on the
// next downbeat, at the groove's tempo, to <name>.wav; and one file of every kit's voices struck in turn.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
// the assembler isn't the Teensy's: no primask here

#include "drum_machine.h"
#include "drum_patterns.h"
const int16_t AudioWaveformSine[257] = {
#include "sine.inc"
};
static void wav(const std::string &path, const std::vector<int16_t> &lr) {
  FILE *f = fopen(path.c_str(), "wb");
  uint32_t n = lr.size() * 2, rate = 44118, br = rate * 4;
  fwrite("RIFF", 1, 4, f); uint32_t sz = 36 + n; fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  uint32_t fl = 16; uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
  fwrite(&fl, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
  fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
  fwrite(lr.data(), 2, lr.size(), f); fclose(f);
}
struct Hit { double at; uint8_t v, vel; };
static float dayan_hz = 261.63f;   // a minichord in C: the dayan at Sa, C4 (see dayan_tuning in main.cpp)
static std::vector<int16_t> run(AudioDrumMachine &dm, std::vector<Hit> hits, double seconds, int &peak, int &clips) {
  std::vector<int16_t> lr;
  std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.at < b.at; });
  size_t h = 0; peak = 0; clips = 0;
  int blocks = (int)(seconds * AUDIO_SAMPLE_RATE_EXACT / 128);
  for (int b = 0; b < blocks; b++) {
    double t = b * 128 / AUDIO_SAMPLE_RATE_EXACT;
    while (h < hits.size() && hits[h].at < t + 128 / AUDIO_SAMPLE_RATE_EXACT) {
      const drum_voice_t &dv = dm.currentKit()->v[hits[h].v];
      float pitch = dv.follow && dv.sample >= 0 ? dayan_hz : 45.0f;   // the dayan in the key, an 808's bass at 45 Hz
      dm.hit(hits[h].v, hits[h].vel / 127.0f, pitch, (uint32_t)((hits[h].at - t) * AUDIO_SAMPLE_RATE_EXACT));
      h++;
    }
    dm.out[0] = dm.out[1] = nullptr;
    dm.update();
    for (int n = 0; n < 128; n++) {
      int16_t l = dm.out[0] ? dm.out[0]->data[n] : 0, r = dm.out[1] ? dm.out[1]->data[n] : 0;
      lr.push_back(l); lr.push_back(r);
      peak = std::max(peak, std::max(abs(l), abs(r)));
      if (abs(l) >= 32767 || abs(r) >= 32767) clips++;
    }
    delete dm.out[0]; delete dm.out[1];
  }
  return lr;
}
int main() {
  if (getenv("DAYAN_HZ")) dayan_hz = atof(getenv("DAYAN_HZ"));   // another key than C
  for (int g = 0; g < drum_groove_count; g++) {
    const drum_groove_t &gr = drum_grooves[g];
    AudioDrumMachine dm; dm.setKit(&drum_kits[gr.kit]); dm.level(0.7f);
    double bpm = 96, tick = 60.0 / bpm / 24, bar = gr.beats_per_bar * 24 * tick;
    std::vector<Hit> hits;
    int bars_main = 4;
    for (double start = 0; start < bars_main * bar - 1e-9; start += gr.length * tick)
      for (int e = 0; e < gr.count; e++) { double at = start + gr.events[e].at * tick; if (at < bars_main * bar) hits.push_back({at, gr.events[e].voice, gr.events[e].velocity}); }
    for (int e = 0; e < gr.fill_count; e++) hits.push_back({bars_main * bar + gr.fill[e].at * tick, gr.fill[e].voice, gr.fill[e].velocity});
    hits.push_back({(bars_main + 1) * bar, DRUM_CYMBAL, 118}); hits.push_back({(bars_main + 1) * bar, DRUM_KICK, 118});
    int peak, clips;
    auto lr = run(dm, hits, (bars_main + 1) * bar + 2.0, peak, clips);
    std::string name = gr.name; for (auto &c : name) if (c == ' ' || c == '/') c = '-';
    char fn[128]; snprintf(fn, sizeof fn, "grooves/%02d-%s.wav", g + 1, name.c_str());
    wav(fn, lr);
    printf("%-22s kit %-18s peak %5.1f dB  clipped %d\n", gr.name, drum_kits[gr.kit].name, 20 * log10(peak / 32767.0 + 1e-9), clips);
  }
  for (int k = 0; k < drum_kit_count; k++) {
    AudioDrumMachine dm; dm.setKit(&drum_kits[k]); dm.level(0.7f);
    std::vector<Hit> hits;
    for (int v = 0; v < DRUM_VOICES; v++) hits.push_back({v * 0.45, (uint8_t)v, 110});
    int peak, clips;
    auto lr = run(dm, hits, DRUM_VOICES * 0.45 + 1.5, peak, clips);
    std::string name = drum_kits[k].name; for (auto &c : name) if (c == ' ') c = '-';
    char fn[128]; snprintf(fn, sizeof fn, "kits/%02d-%s.wav", k + 1, name.c_str());
    wav(fn, lr);
    printf("kit %-18s every voice: peak %5.1f dB clipped %d\n", drum_kits[k].name, 20 * log10(peak / 32767.0 + 1e-9), clips);
  }
}
