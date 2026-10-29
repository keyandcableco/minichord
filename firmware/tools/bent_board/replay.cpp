// The bent board's decoder (include/bent_board.h) over a recording of the real board: the text that
// ~/minichord-bench/bentboard/record.py writes beside its json (STEP lines, then F <ms> <12 strengths> in harp
// order as the plate lies). For each step, what the firmware would have read, frame by frame, the chip's
// touch and release thresholds (40, 20) and the border guard applied as in the firmware:
//   harp, slide, endcold   the places read (frames each), and how many touches landed on the right one
//   rail, railpair         the rail points held (frames each) and each point's deepest depth
//   palm                   the palm's depth, its median and its deepest while touched
//   ribbon...              the ribbon's place while a thumb is on it: median, spread, range
//   border, key            which zones counted as touched (after the guard)
//     g++ -std=c++17 -O2 -I../../include -o replay replay.cpp && ./replay ~/minichord-bench/results/bentboard_rec_HHMM.txt
#include "bent_board.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

using namespace bentboard;

struct frame { uint32_t ms; int16_t s[12]; };
struct step { std::string kind, name; int truth; std::vector<frame> f; };

static std::vector<step> load(const char *path) {
  std::vector<step> out;
  FILE *fp = fopen(path, "r");
  if (!fp) { perror(path); exit(2); }
  char line[512];
  while (fgets(line, sizeof line, fp)) {
    if (!strncmp(line, "STEP ", 5)) {
      char kind[32], name[256]; int truth;
      if (sscanf(line + 5, "%31s %d %255s", kind, &truth, name) == 3) out.push_back(step{kind, name, truth, {}});
    } else if (line[0] == 'F' && !out.empty()) {
      frame f; unsigned ms; int v[12];
      if (sscanf(line + 2, "%u %d %d %d %d %d %d %d %d %d %d %d %d", &ms, v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6,
                 v + 7, v + 8, v + 9, v + 10, v + 11) == 13) {
        f.ms = ms;
        for (int k = 0; k < 12; k++) f.s[k] = (int16_t)v[k];
        out.back().f.push_back(f);
      }
    }
  }
  fclose(fp);
  return out;
}

struct chip { bool raw[12] = {false}, d[12] = {false};
  void step(const int16_t *s) {
    for (int k = 0; k < 12; k++) { raw[k] = raw[k] ? s[k] >= 20 : s[k] >= 40; d[k] = raw[k]; }
    border_guard(d, s, false);
  } };

static float median(std::vector<float> v) {
  if (v.empty()) return NAN;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "replay <record.txt>\n"); return 2; }
  for (const step &st : load(argv[1])) {
    printf("%-12s %4d  %s  (%zu frames)\n", st.kind.c_str(), st.truth, st.name.c_str(), st.f.size());
    chip c;
    if (st.kind == "harp" || st.kind == "slide" || st.kind == "endcold") {
      // each touch's place where it read firmest, its end known by the touch before it if it lands at an end
      std::map<int, int> frames;
      int touches = 0, right = 0;
      bool on = false;
      float best_total = 0, best_place = -1;
      uint8_t last_end = 0;
      for (const frame &f : st.f) {
        c.step(f.s);
        line l; read_line(c.d, f.s, nullptr, false, -1, last_end, l);
        run r[7]; int n = line_runs(l, false, r, 7);
        if (n) {
          frames[(int)lroundf(r[0].place)]++;
          if (!on) { on = true; best_total = 0; }
          if (r[0].total > best_total) { best_total = r[0].total; best_place = r[0].place; }
        } else if (on) {
          on = false; touches++;
          int p = (int)lroundf(best_place);
          right += p == st.truth;
          last_end = best_place > 6 ? 1 : 0;
        }
      }
      printf("    places read (frames):");
      for (auto &kv : frames) printf(" %d:%d", kv.first, kv.second);
      if (st.kind != "slide") printf("\n    touches %d, at place %d: %d\n", touches, st.truth, right);
      else printf("\n");
    } else if (st.kind == "rail" || st.kind == "railpair") {
      rail_state rs = {};
      std::map<std::string, int> held;
      float deepest[rail_points] = {0};
      float depth[rail_points];
      for (const frame &f : st.f) {
        c.step(f.s);
        rail_update(rs, c.d, f.s, false, f.ms, depth);
        std::string key;
        for (int i = 0; i < rail_touch_max; i++) if (rs.t[i].active) key += std::to_string(rs.t[i].point) + (rs.t[i].landing ? "L" : "") + " ";
        if (!key.empty()) held[key]++;
        for (int k = 0; k < rail_points; k++) deepest[k] = std::max(deepest[k], depth[k]);
      }
      printf("    points held (frames):");
      for (auto &kv : held) printf(" [%s]:%d", kv.first.c_str(), kv.second);
      printf("\n    deepest:");
      for (int k = 0; k < rail_points; k++) printf(" %.2f", deepest[k]);
      printf("\n");
    } else if (st.kind == "palm") {
      std::vector<float> d;
      int raw_max = 0;
      for (const frame &f : st.f) {
        c.step(f.s);
        float x = palm_depth(c.d, f.s, false);
        if (c.d[palm_zone]) { d.push_back(x); raw_max = std::max(raw_max, (int)f.s[palm_zone]); }
      }
      printf("    touched %zu frames, depth median %.2f, deepest %.2f, strength at most %d\n", d.size(), median(d),
             d.empty() ? 0.f : *std::max_element(d.begin(), d.end()), raw_max);
    } else if (st.kind.rfind("ribbon", 0) == 0) {
      ribbon_state rb = {};
      std::vector<float> places;
      for (const frame &f : st.f) {
        c.step(f.s);
        ribbon_update(rb, c.d, f.s, false, f.ms);
        if (rb.active) places.push_back(rb.place);
      }
      if (places.empty()) { printf("    no thumb read\n"); continue; }
      std::vector<float> sorted = places;
      std::sort(sorted.begin(), sorted.end());
      printf("    place median %.2f, middle half %.2f-%.2f, range %.2f-%.2f (%zu frames)\n", median(places),
             sorted[sorted.size() / 4], sorted[3 * sorted.size() / 4], sorted.front(), sorted.back(), places.size());
    } else {
      int counts[12] = {0}, any = 0;
      for (const frame &f : st.f) {
        c.step(f.s);
        bool a = false;
        for (int k = 0; k < 12; k++) if (c.d[k]) { counts[k]++; a = true; }
        any += a;
      }
      printf("    frames with a zone touched: %d;", any);
      for (int k = 0; k < 12; k++) if (counts[k]) printf(" T%d:%d", 12 - k, counts[k]);
      printf("\n");
    }
  }
  return 0;
}
