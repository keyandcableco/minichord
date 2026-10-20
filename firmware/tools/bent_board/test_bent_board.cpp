// The bent board's decoder (include/bent_board.h) against readings modelled from its copper
// (vectors.txt, from ~/minichord-bench/bentboard/model.py: each zone reads the area of a round contact over
// its copper, a 7 mm fingertip flat on copper 400, light to firm, the chip's noise). Run with ./run.sh.
// A reading's zones count as touched as the chip counts them: past the touch threshold (40) to land, under the
// release threshold (20) to let go. Every check prints its rate; the run fails where a rate falls under what
// the decoder is meant to do (the limits at the end of each check).
#include "bent_board.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

using namespace bentboard;

struct vec { std::string kind; int truth; float x, y; int d; int16_t s[12]; };
static std::vector<vec> V;
static int failures = 0;
static const int touch_threshold = 40, release_threshold = 20;
static const float harp_y_bottom = 91.0f, place_mm = 79.0f / 13;   // make_bentboard.py HARP_Y, q
static const float rail_x0 = 4.0f, point_mm = 43.5f / 5;           // RAIL_X

static void check(const char *what, double rate, double least) {
  bool ok = rate >= least;
  printf("  %-58s %6.1f%%  (at least %.1f%%)%s\n", what, 100 * rate, 100 * least, ok ? "" : "  <-- FAIL");
  if (!ok) failures++;
}

static void load(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) { perror(path); exit(2); }
  char kind[16];
  vec v;
  while (fscanf(f, "%15s %d %f %f %d", kind, &v.truth, &v.x, &v.y, &v.d) == 5) {
    v.kind = kind;
    for (int k = 0; k < 12; k++) { int s; if (fscanf(f, "%d", &s) != 1) exit(3); v.s[k] = (int16_t)s; }
    V.push_back(v);
  }
  fclose(f);
}

// the zones touched for a still reading, as the chip would have them once landed, and as the firmware takes
// them: the ribbon and the harp's left edge guarded from each other
static void down_of(const int16_t *s, bool *d, bool upside_down = false) {
  for (int k = 0; k < 12; k++) d[k] = s[k] >= touch_threshold;
  border_guard(d, s, upside_down);
}

// the chip's touch status following a moving reading, with its hysteresis
struct chip { bool raw[12] = {false}, d[12] = {false};
  void step(const int16_t *s) {
    for (int k = 0; k < 12; k++) { raw[k] = raw[k] ? s[k] >= release_threshold : s[k] >= touch_threshold; d[k] = raw[k]; }
    border_guard(d, s, false);
  } };

// end_held: a touch held at an end (-1 none); end_last: the end a touch last left, here the bottom
static int harp_place(const int16_t *s, int8_t end_held, bool upside_down, int &count, float &where, uint8_t end_last = 0) {
  bool d[12]; down_of(s, d, upside_down);
  line l; read_line(d, s, nullptr, upside_down, end_held, end_last, l);
  run r[7]; count = line_runs(l, upside_down, r, 7);
  where = count ? r[0].place : -1;
  return count ? (int)lroundf(r[0].place) : -1;
}

// upside down the chip gives zone i as 11 - i
static void flip(const int16_t *s, int16_t *t) { for (int k = 0; k < 12; k++) t[11 - k] = s[k]; }

static void test_harp() {
  printf("harp: thirteen places from six lines\n");
  int n = 0, landed = 0, exact = 0, inner = 0, inner_ok = 0, within = 0, ends = 0, ends_ok = 0, ends_out = 0, ends_out_ok = 0, ends_all = 0, ends_hint_ok = 0, ends_last_ok = 0, end_d[4] = {0}, end_d_ok[4] = {0}, turned_ok = 0, area_spill = 0;
  int per[13] = {0}, per_ok[13] = {0};
  for (const vec &v : V) {
    if (v.kind != "harp") continue;
    n++;
    int count; float where;
    int p = harp_place(v.s, -1, false, count, where);
    bool d[12]; down_of(v.s, d);
    uint16_t areas = area_bits(false);
    for (int k = 0; k < 12; k++) if ((areas >> k & 1) && d[k]) { area_spill++; break; }
    if (count == 0) continue;   // too light to touch: the threshold's, not the decoder's
    landed++;
    float centre_y = harp_y_bottom - place_mm * (v.truth + 0.5f);
    bool in = fabsf(v.y - centre_y) <= place_mm / 2 - 1.0f;   // at least 1 mm inside its place
    per[v.truth]++;
    if (p == v.truth) { exact++; per_ok[v.truth]++; }
    if (abs(p - v.truth) <= 1 || (p + v.truth == 12 && (p == 0 || p == 12))) within++;
    if (in && v.truth != 0 && v.truth != 12) { inner++; if (p == v.truth) inner_ok++; }
    if (v.truth == 0 || v.truth == 12) {   // the two ends that read T10 alone
      // toward the band beside it (T9's at the bottom, the wrap band's at the top) or past the middle, toward
      // the board's edge, where neither neighbour reads
      bool inward = v.truth == 0 ? v.y < centre_y : v.y > centre_y;
      (inward ? ends : ends_out)++;
      if (p == v.truth) (inward ? ends_ok : ends_out_ok)++;
      int c2; float w2;
      auto end_of = [](int place) { return place < 0 ? -1 : place <= 1 ? 0 : place >= 11 ? 12 : -1; };
      if (end_of(harp_place(v.s, v.truth == 12 ? 1 : 0, false, c2, w2)) == v.truth) ends_hint_ok++;
      if (end_of(harp_place(v.s, -1, false, c2, w2, v.truth == 12 ? 1 : 0)) == v.truth) ends_last_ok++;
      ends_all++;
      end_d[v.d - 6]++;
      if (p == v.truth) end_d_ok[v.d - 6]++;
    }
    int16_t t[12]; flip(v.s, t);
    int c3; float w3;
    harp_place(t, -1, true, c3, w3);
    if (c3 == count && fabsf(w3 - (12 - where)) < 1e-3f) turned_ok++;
  }
  printf("  %d touches, %d past the touch threshold\n", n, landed);
  for (int k = 0; k < 13; k++) printf("    place %2d: %4d touches, %5.1f%% read there\n", k, per[k], per[k] ? 100.0 * per_ok[k] / per[k] : 0);
  check("anywhere: read at its place", exact / (double)landed, 0.80);
  check("anywhere: within one place (the two T10 ends aside)", within / (double)landed, 0.995);
  check("1 mm or more inside its place: read there (places 1-11)", inner_ok / (double)inner, 0.97);
  printf("  the bottom core and the island, a touch landing cold, the last end left the bottom:\n");
  printf("    toward their band %.1f%%, past their middle toward the board's edge %.1f%%; by fingertip:", 100.0 * ends_ok / ends, 100.0 * ends_out_ok / ends_out);
  for (int k = 0; k < 4; k++) printf(" %d mm %.0f%%", 6 + k, 100.0 * end_d_ok[k] / end_d[k]);
  printf("\n    (wholly on an end core neither neighbour reads: it goes to the end last left, here the bottom)\n");
  check("the bottom core and the island, a touch already held at that end: at that end", ends_hint_ok / (double)ends_all, 0.995);
  check("the bottom core and the island, the last touch left at that end: at that end", ends_last_ok / (double)ends_all, 0.97);
  check("upside down: the same place turned round", turned_ok / (double)landed, 1.0);
  check("no touch area's zone touched by a finger on the harp (guarded)", 1 - area_spill / (double)n, 0.999);

  // two fingers: each read as a run of its own where an untouched line lies between them
  std::mt19937 rng(7);
  std::vector<const vec *> H;
  for (const vec &v : V) if (v.kind == "harp" && v.d == 7) H.push_back(&v);
  int pairs = 0, pairs_ok = 0, pairs_near = 0;
  for (int i = 0; i < 6000; i++) {
    const vec *a = H[rng() % H.size()], *b = H[rng() % H.size()];
    if (a->truth > b->truth) std::swap(a, b);
    // the lines each place reads (line place m = its core, the island 6), with one line clear between
    int a_hi = (a->truth + 1) / 2, b_lo = b->truth / 2;
    if (b->truth == 12) b_lo = 6;
    if (a->truth <= 1 || b->truth >= 11) continue;   // an end place landing cold is the end question, above
    if (b_lo - a_hi < 2) continue;
    int16_t s[12];
    for (int k = 0; k < 12; k++) s[k] = a->s[k] + b->s[k];
    bool d[12]; down_of(s, d);
    // the first finger held already (its end known, if it is at one), the second landing
    int8_t held = a->truth <= 1 ? 0 : a->truth >= 11 ? 1 : -1;
    line l; read_line(d, s, nullptr, false, held, 0, l);
    run r[7]; int c = line_runs(l, false, r, 7);
    bool sa = false, sb = false;
    for (int k = 0; k < 12; k++) { sa |= a->s[k] >= touch_threshold; sb |= b->s[k] >= touch_threshold; }
    if (!sa || !sb) continue;
    pairs++;
    if (c == 2 && lroundf(r[0].place) == a->truth && lroundf(r[1].place) == b->truth) pairs_ok++;
    if (c == 2 && abs(lroundf(r[0].place) - a->truth) <= 1 && abs(lroundf(r[1].place) - b->truth) <= 1) pairs_near++;
  }
  printf("  two fingers on places 2-10 with a clear line between them: %.1f%% both at their places (one finger alone: %.1f%%, squared %.1f%%)\n",
         100.0 * pairs_ok / pairs, 100.0 * exact / landed, 100.0 * exact / landed * exact / landed);
  check("two fingers on places 2-10, a clear line between: two, each within one place", pairs_near / (double)pairs, 0.95);
}

// A finger on the rail landing, held and lifting: its reading scaled from nothing to full over `ramp_ms`,
// one reading a millisecond, as the chip gives them
static void scaled(const int16_t *s, float k, int16_t *t) { for (int z = 0; z < 12; z++) t[z] = (int16_t)lroundf(s[z] * k); }

static int rail_held(const rail_state &st, int *points) {
  int n = 0;
  for (int i = 0; i < rail_touch_max; i++) if (st.t[i].active) points[n++] = st.t[i].point;
  std::sort(points, points + n);
  return n;
}

static void test_rail() {
  printf("rail: five points from three lines\n");
  int n = 0, ok = 0, inner = 0, inner_ok = 0, lifted_ok = 0, harp_spill = 0, skew_ok = 0;
  int per[5] = {0}, per_ok[5] = {0};
  float depth[5];
  for (const vec &v : V) {
    if (v.kind != "rail") continue;
    int16_t peak = 0;
    for (int k = 0; k < 12; k++) peak = std::max(peak, v.s[k]);
    if (peak < touch_threshold) continue;   // too light to touch
    n++;
    bool d[12]; down_of(v.s, d);
    for (int k = 0; k < 6; k++) if (d[line_zone[k]]) { harp_spill++; break; }
    for (int skew = 0; skew < 2; skew++) {
      rail_state st = {};
      chip c;
      uint32_t t = 1000;
      int16_t s[12];
      for (int ms = 0; ms <= 40; ms++, t++) {
        float k = std::min(1.0f, ms / 8.0f);
        scaled(v.s, k, s);
        if (skew) {   // T1 or T5 reaching the chip a few milliseconds before T3
          float k3 = std::min(1.0f, std::max(0.0f, (ms - 3) / 8.0f));
          s[rail_zone[1]] = (int16_t)lroundf(v.s[rail_zone[1]] * k3);
        }
        c.step(s);
        rail_update(st, c.d, s, false, t, depth);
      }
      int pts[3];
      int held = rail_held(st, pts);
      bool right = held == 1 && pts[0] == v.truth;
      if (skew) { skew_ok += right; continue; }
      per[v.truth]++;
      if (right) { ok++; per_ok[v.truth]++; }
      float lo = rail_x0 + point_mm * v.truth;
      if (v.x >= lo + 1.0f && v.x <= lo + point_mm - 1.0f) { inner++; inner_ok += right; }
      for (int ms = 0; ms <= 30; ms++, t++) {
        scaled(v.s, std::max(0.0f, 1 - ms / 10.0f), s);
        c.step(s);
        rail_update(st, c.d, s, false, t, depth);
      }
      lifted_ok += rail_held(st, pts) == 0;
    }
  }
  for (int k = 0; k < 5; k++) printf("    point %d: %4d touches, %5.1f%% read there\n", k, per[k], per[k] ? 100.0 * per_ok[k] / per[k] : 0);
  check("anywhere: read at its point, alone", ok / (double)n, 0.85);
  check("1 mm or more inside its point: read there, alone", inner_ok / (double)inner, 0.97);
  check("a line reaching the chip 3 ms after the others: still its point", skew_ok / (double)n, 0.80);
  check("lifted: nothing left held", lifted_ok / (double)n, 0.999);
  check("no harp line touched by a finger on the rail", 1 - harp_spill / (double)n, 0.999);

  // two fingers, the second landing 30 ms after the first, or both in the same instant
  std::mt19937 rng(11);
  std::vector<const vec *> R[5];
  for (const vec &v : V) {
    if (v.kind != "rail" || v.d != 7) continue;
    float lo = rail_x0 + point_mm * v.truth;
    if (v.x >= lo + 1.5f && v.x <= lo + point_mm - 1.5f) R[v.truth].push_back(&v);
  }
  for (int together = 0; together < 2; together++) {
    int tried[5][5] = {{0}}, right[5][5] = {{0}}, total = 0, total_ok = 0, after_lift = 0, after_lift_ok = 0;
    for (int a = 0; a < 5; a++) for (int b = 0; b < 5; b++) {
      if (a == b) continue;
      for (int i = 0; i < 60; i++) {
        const vec *va = R[a][rng() % R[a].size()], *vb = R[b][rng() % R[b].size()];
        float ma = std::uniform_real_distribution<float>(0.7f, 1.3f)(rng), mb = std::uniform_real_distribution<float>(0.7f, 1.3f)(rng);
        rail_state st = {};
        chip c;
        uint32_t t = 5000;
        int16_t s[12];
        int start_b = together ? 0 : 30;
        for (int ms = 0; ms <= 70; ms++, t++) {
          float ka = std::min(1.0f, ms / 8.0f) * ma, kb = std::min(1.0f, std::max(0.0f, (ms - start_b) / 8.0f)) * mb;
          for (int z = 0; z < 12; z++) s[z] = (int16_t)lroundf(va->s[z] * ka + vb->s[z] * kb);
          c.step(s);
          rail_update(st, c.d, s, false, t, depth);
        }
        int pts[3];
        int held = rail_held(st, pts);
        bool good = held == 2 && pts[0] == std::min(a, b) && pts[1] == std::max(a, b);
        tried[a][b]++; right[a][b] += good; total++; total_ok += good;
        if (!good) continue;
        // the first finger lifts: the second is still where it is
        for (int ms = 0; ms <= 40; ms++, t++) {
          float ka = std::max(0.0f, 1 - ms / 10.0f) * ma;
          for (int z = 0; z < 12; z++) s[z] = (int16_t)lroundf(va->s[z] * ka + vb->s[z] * mb);
          c.step(s);
          rail_update(st, c.d, s, false, t, depth);
        }
        after_lift++;
        after_lift_ok += rail_held(st, pts) == 1 && pts[0] == b;
      }
    }
    printf("    %s: rows the first point, columns the second\n", together ? "both in the same instant" : "the second 30 ms after the first");
    for (int a = 0; a < 5; a++) {
      printf("      %d:", a);
      for (int b = 0; b < 5; b++) printf(a == b ? "     -" : " %5.0f", a == b ? 0.0 : 100.0 * right[a][b] / tried[a][b]);
      printf("\n");
    }
    if (!together) {
      check("two points, one landing after the other: both read", total_ok / (double)total, 0.90);
      check("then the first lifts: the second stays", after_lift_ok / (double)after_lift, 0.97);
    } else {
      int apart = 0, apart_ok = 0;   // points sharing no line: 0 and 4, read at once whatever
      for (int a = 0; a < 5; a += 4) { int b = 4 - a; apart += tried[a][b]; apart_ok += right[a][b]; }
      printf("  both in the same instant, any two points: %.1f%% (only 0 and 4, with T3 untouched between, is promised:\n"
             "  any other two join through touched lines next to each other, as two fingers on neighbouring harp lines do)\n", 100.0 * total_ok / total);
      check("both in the same instant, points 0 and 4: both read", apart_ok / (double)apart, 0.95);
    }
  }
  // depth rises with how firmly a point is pressed, and every point reaches full
  int full[5] = {0}, seen[5] = {0};
  for (const vec &v : V) {
    if (v.kind != "rail" || v.d < 8) continue;
    rail_state st = {};
    chip c;
    int16_t s[12];
    scaled(v.s, 1.3f, s);
    for (int ms = 0; ms < 30; ms++) { c.step(s); rail_update(st, c.d, s, false, 100 + ms, depth); }
    seen[v.truth]++;
    full[v.truth] += depth[v.truth] > 0.95f;
  }
  for (int k = 0; k < 5; k++) printf("    point %d pressed firmly (8-9 mm, 1.3 of the model's normal): %5.1f%% at full depth\n", k, 100.0 * full[k] / seen[k]);
}

static void test_palm() {
  printf("palm\n");
  int n = 0, down = 0, spill = 0, rising = 0, comparisons = 0;
  std::vector<std::pair<int, float>> depths;
  for (const vec &v : V) {
    if (v.kind != "palm") continue;
    n++;
    bool d[12]; down_of(v.s, d);
    down += d[palm_zone];
    for (int k = 0; k < 12; k++) if (k != palm_zone && d[k]) { spill++; break; }
    depths.push_back({v.s[palm_zone], palm_depth(d, v.s, false)});
  }
  std::sort(depths.begin(), depths.end());
  for (size_t i = 1; i < depths.size(); i++) { comparisons++; rising += depths[i].second >= depths[i - 1].second; }
  check("a palm on the pad: touched", down / (double)n, 0.99);
  check("and no other zone", 1 - spill / (double)n, 0.999);
  check("depth never falls as the palm presses harder", rising / (double)comparisons, 1.0);
  int full = 0;
  for (auto &p : depths) full += p.second >= 1;
  printf("    modelled palms at full depth: %.0f%% (the model's palm reads far past the chip's range: calibrate)\n", 100.0 * full / n);
}

static void test_ribbon() {
  printf("ribbon: a thumb's place along the wedge\n");
  double se[4] = {0}, worst[4] = {0};
  int count[4] = {0}, n = 0, spill = 0, landed = 0, moved_ok = 0, moves = 0, unplaced_guard = 0, below_n = 0, top_n = 0;
  double below_se = 0, top_se = 0;
  std::vector<std::pair<float, float>> placed;
  for (const vec &v : V) {
    if (v.kind != "ribbon") continue;
    n++;
    bool d[12]; down_of(v.s, d);
    for (int k = 0; k < 12; k++) if (k != ribbon_left && k != ribbon_right && d[k]) { spill++; break; }
    if (!d[ribbon_left] && !d[ribbon_right]) unplaced_guard++;
    ribbon_state st = {};
    chip c;
    for (int ms = 0; ms < 40; ms++) {
      int16_t s[12]; scaled(v.s, std::min(1.0f, ms / 8.0f), s);
      c.step(s);
      ribbon_update(st, c.d, s, false, 200 + ms);
    }
    if (!st.active) continue;
    landed++;
    float truth = v.truth / 1000.0f, e = st.place - truth;
    int i = v.d - 8;
    se[i] += e * e; count[i]++; worst[i] = std::max(worst[i], (double)fabsf(e));
    if (v.y - 57.7f >= 4) { below_se += e * e; below_n++; } else { top_se += e * e; top_n++; }
    placed.push_back({truth, st.place});
  }
  double all = 0; int total = 0;
  for (int i = 0; i < 4; i++) {
    printf("    thumb %2d mm: %4d, place rms %.3f, worst %.3f of the wedge (38 mm)\n", 8 + i, count[i], sqrt(se[i] / count[i]), worst[i]);
    all += se[i]; total += count[i];
  }
  check("a thumb on the ribbon: placed", landed / (double)n, 0.99);
  check("no other zone touched by it (the border guard; its top right corner reaches T7)", 1 - spill / (double)n, 0.98);
  check("and the guard took off the ribbon only thumbs more on the harp", 1 - unplaced_guard / (double)n, 0.99);
  printf("    the wedge is a triangle, 5.6 mm tall at the left end and 29 at the right, with T11's spine along its top\n"
         "    edge and T12's along its bottom: a thumb on the top spine reads to the left of where it is\n");
  printf("    a thumb 4 mm or more under the wedge's top edge: rms %.3f of the wedge (%.1f mm)\n", sqrt(below_se / below_n), 38 * sqrt(below_se / below_n));
  check("a thumb 4 mm or more under the top edge: rms within 0.1 (as 1 - rms/0.1)", 1 - sqrt(below_se / below_n) / 0.1, 0.0);
  printf("    within 4 mm of the top edge: rms %.3f of the wedge\n", sqrt(top_se / top_n));
  // in eighths: a thumb at the left eighth reads left of one at the right eighth, and so on
  std::sort(placed.begin(), placed.end());
  double eighth_mean[8] = {0}; int eighth_n[8] = {0};
  for (auto &p : placed) { int e = std::min(7, (int)(p.first * 8)); eighth_mean[e] += p.second; eighth_n[e]++; }
  printf("    mean place read in each eighth of the wedge:");
  for (int e = 0; e < 8; e++) printf(" %.2f", eighth_mean[e] / std::max(1, eighth_n[e]));
  printf("\n");
  for (int e = 1; e < 8; e++) { moves++; moved_ok += eighth_mean[e] / eighth_n[e] > eighth_mean[e - 1] / eighth_n[e - 1]; }
  check("each eighth along it reads further right than the last", moved_ok / (double)moves, 1.0);
  // a slide: the place follows along, never jumping back
  std::vector<const vec *> T;
  for (const vec &v : V) if (v.kind == "ribbon" && v.d == 10) T.push_back(&v);
  std::sort(T.begin(), T.end(), [](const vec *a, const vec *b) { return a->x < b->x; });
  ribbon_state st = {};
  chip c;
  uint32_t t = 0;
  float last = -1, back = 0;
  for (size_t i = 0; i < T.size(); i++) {
    for (int ms = 0; ms < 2; ms++, t++) { c.step(T[i]->s); ribbon_update(st, c.d, T[i]->s, false, t); }
    if (last >= 0) back = std::max(back, last - st.place);
    last = st.place;
  }
  printf("    a 10 mm thumb slid left to right (%zu readings, 2 ms each): ends at %.2f, steps back at most %.3f\n", T.size(), last, back);
  check("the slide never steps back more than 0.05", back <= 0.05 ? 1 : 0, 1.0);
}

static void test_keys() {
  printf("keys and knobs: a fingertip anywhere on a keycap or knob shaft near the copper, pressing firmly\n");
  printf("    (modelled flat on the board where the keycap stands: a keycap's top is ~2.4 mm above it, so this overstates)\n");
  int n = 0, touched = 0;
  int16_t worst = 0;
  for (const vec &v : V) {
    if (v.kind != "key") continue;
    n++;
    bool d[12]; down_of(v.s, d);
    bool any = false;
    for (int k = 0; k < 12; k++) { any |= d[k]; worst = std::max(worst, v.s[k]); }
    if (any) {
      printf("    key at (%.0f, %.0f), %d mm:", v.x, v.y, v.d);
      for (int k = 0; k < 12; k++) if (d[k]) printf(" T%d %d", 12 - k, v.s[k]);
      printf("\n");
    }
    touched += any;
  }
  printf("    %d presses, the strongest zone reading among them %d\n", n, worst);
  check("no zone touched by a key or knob pressed (flat finger, pessimistic)", 1 - touched / (double)n, 0.95);
}

int main(int argc, char **argv) {
  load(argc > 1 ? argv[1] : "vectors.txt");
  printf("%zu modelled touches\n", V.size());
  test_harp();
  test_rail();
  test_palm();
  test_ribbon();
  test_keys();
  printf(failures ? "%d checks FAILED\n" : "all checks pass\n", failures);
  return failures ? 1 : 0;
}
