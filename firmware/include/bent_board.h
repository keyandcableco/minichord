// The bent board, harp plate 21: a full-face plate on the stock plate's twelve lines, round the
// minichord's own keys and knobs. Its harp has thirteen places from six lines (the split plate's strip);
// the rest of the board is touch areas: a bend rail of five points along the strip above the chord
// buttons, a palm pad on the nose left of them and a thumb ribbon in the wedge below the seventh row.
// What each area moves is the firmware's (BENT BOARD in main.cpp); this reads where and how firmly.
//
// Plain C++ with no Arduino in it, so firmware/tools/bent_board tests it on a computer, against readings
// modelled from the board's copper (~/minichord-bench/bentboard/model.py). Everything here is the plate as
// it lies, the right way up: zone p is T(12 - p), the zone the harp reads as string p. Upside down (384)
// the touch chip gives the zones in the other order (i becomes 11 - i), and at() reads them back; places,
// rail points and the ribbon are then turned round, so each is numbered as the player holds it.
//
// The calibration (core_reads, the rail's edges and gains, the ribbon's table, the border guard's harp side)
// is fitted from the real board's readings (results/bentboard_rec_1803, 2026-10-28; bent-board.md), and the
// palm's range chosen from them; the rest (the rail's soft and firm, the end rule's margins) is the model's.
#ifndef BENT_BOARD_H
#define BENT_BOARD_H
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Where the firmware puts this code: FLASHMEM (main.cpp sets it), since RAM1, where code and constant tables
// go by default on the Teensy 4, is short; nothing here is timing critical. Empty on a computer.
#ifndef BENT_BOARD_CODE
#define BENT_BOARD_CODE
#endif
#ifndef BENT_BOARD_DATA   // and its tables: PROGMEM, in flash (read in place on the Teensy 4)
#define BENT_BOARD_DATA
#endif

namespace bentboard {

const uint8_t line_zone[6] BENT_BOARD_DATA = {2, 3, 4, 5, 8, 10};   // the harp's lines bottom to top: T10 T9 T8 T7 T4 T2
const uint8_t line_lines = 6;
const uint8_t places = 13;                           // the cores, the bands between, the wrap band and the island
const uint8_t rail_zone[3] BENT_BOARD_DATA = {11, 9, 7};             // the rail left to right: T1 T3 T5
const uint8_t rail_points = 5;                       // T1, T1 and T3, T3, T3 and T5, T5
const uint8_t palm_zone = 6;                         // T6
const uint8_t ribbon_left = 1, ribbon_right = 0;     // T11, its teeth widest at the left; T12, at the right

// harp order to the zone as the plate lies, and back (the same flip)
BENT_BOARD_CODE static inline uint8_t at(uint8_t zone, bool upside_down) { return upside_down ? 11 - zone : zone; }

// The zones that are touch areas rather than the harp, as bits in harp order
BENT_BOARD_CODE static inline uint16_t area_bits(bool upside_down) {
  uint16_t b = 0;
  for (uint8_t k = 0; k < 3; k++) b |= 1u << at(rail_zone[k], upside_down);
  b |= 1u << at(palm_zone, upside_down);
  b |= 1u << at(ribbon_left, upside_down);
  b |= 1u << at(ribbon_right, upside_down);
  return b;
}

// How deep a touch is, 0 to 1, from its strength: as touch_firmness (main.cpp) maps a harp string's, soft
// to firm, linearly
BENT_BOARD_CODE static inline float depth_of(float strength, float soft, float firm) {
  float d = (strength - soft) / (firm - soft);
  return d < 0 ? 0 : d > 1 ? 1 : d;
}

// ---- the harp --------------------------------------------------------------------------------------
// Thirteen places from six lines, bottom to top: T10's core (place 0), the band where T10 and T9 interleave
// (1), T9's core (2) ... T2's core (10), the wrap band where T2's teeth meet the island (11) and the island
// itself (12), T10 again, joined to the bottom core underneath. The line read for the positions (main.cpp's
// handle_harp_positions): line place m is line m's zone, 0-5 bottom to top, and 6 the island, line 0's zone
// again; a run's centre in line places, times two, is its place.
// T10 touched is the bottom core or the island. A touch already held at one end (end_held: -1 none, 0 the
// bottom, 1 the top, as the plate lies) is still there, whatever else is touched, so a finger on T2 or T9 beside
// it doesn't move it. Otherwise T10 with T9 is the bottom core and the band above it, and with T2 the island
// and the wrap band below it. Alone, a finger on the bottom core leans on T9 and one on the island on T2, as
// on the split plate, so it is the island when T2 reads more than T9 by island_margin. Where neither reads
// more than the chip's noise (end_doubt: modelled, a 6-7 mm fingertip wholly on an end core), it is the end
// a touch last left (end_last, as the plate lies), however long ago.
const int16_t island_margin = 0;
const int16_t end_doubt = 6;

struct line {
  bool down[6];           // each line touched, bottom to top
  int16_t read[6];        // and its strength
  bool palmed[6];
  bool place_down[7];     // line places touched: 0-5 each line's core, 6 the island
};

BENT_BOARD_CODE static inline void read_line(const bool *zone_down, const int16_t *strength, const bool *zone_palmed, bool upside_down,
                      int8_t end_held, uint8_t end_last, line &l) {
  for (uint8_t k = 0; k < line_lines; k++) {
    uint8_t z = at(line_zone[k], upside_down);
    l.down[k] = zone_down[z];
    l.read[k] = strength[z];
    l.palmed[k] = zone_palmed ? zone_palmed[z] : false;
    l.place_down[k] = l.down[k];
  }
  l.place_down[6] = false;
  if (!l.down[0]) return;
  bool island;
  if (end_held >= 0) island = end_held == 1;
  else if (l.down[1] != l.down[5]) island = l.down[5];
  else if (l.down[1]) island = false;   // both: a finger on the bottom band, another on T2
  else if (abs(l.read[5] - l.read[1]) > end_doubt) island = l.read[5] > l.read[1] + island_margin;
  else island = end_last == 1;
  if (island) {
    l.place_down[0] = false;
    l.place_down[6] = true;
  }
}

struct run { float place; float total; bool palmed; };

// A core is solid copper, a band two lines' teeth in turn, so a finger moving off a core onto a band moves the
// centre less than it moves: modelled, a finger at the edge between a core and a band reads 0.40 of a place
// from the core's middle, not 0.5, and a band read by plain rounding is narrower than its 6 mm (a fifth of
// the touches near its edges went to the core). So a centre within core_reads of a core is spread over the
// core's own half place, and the rest of the way to the band's middle over the other half. The real board
// (results/bentboard_rec_1803, 2026-10-28) reads 0.58: its slides' quarter points read 0.29 and 0.71, and
// with 0.58 a slide spends about as long on each place and more core taps land on their core.
const float core_reads = 0.58f;

BENT_BOARD_CODE static inline float place_of(float centre_places) {
  float core = 2 * roundf(centre_places / 2);
  float u = centre_places - core, a = fabsf(u);
  float v = a <= core_reads ? a * 0.5f / core_reads : 0.5f + (a - core_reads) * 0.5f / (1 - core_reads);
  return core + (u < 0 ? -v : v);
}

// Each run of touched line places is a finger: its place is the centre of the readings across it and one
// line more either side (which reads a little before it counts as touched, so a finger nearing a band moves
// before the far line crosses its threshold), as harp_find_runs reads the zipper plates, then spread as above.
// Two fingers two or three lines apart join into one run through the lines between them, which each reaches
// the edge of. One finger reads a single hump across the lines (a core, its neighbours less, the next less
// still; a band, its two lines about evenly), so a line inside a run reading under valley_share of the
// strongest on both sides of it is a valley between two, and splits the run there (the valley line itself,
// touched, counts for neither). A finger on a band reads only half on each of its lines, so the valley next to
// it is shallow: modelled, 0.5-0.6 of the band's stronger line. Two fingers on
// neighbouring lines' cores still read as one, on the band between, as on the zipper plates. In places 0-12,
// turned round upside down so the lowest note is at the bottom as the player holds it.
const float valley_share = 0.65f;

BENT_BOARD_CODE static inline void line_centre(const line &l, int8_t lo, int8_t hi, bool upside_down, run &out) {
  const int8_t length = 7;
  float total = 0, moment = 0;
  bool palmed = false;
  for (int8_t m = lo - 1; m <= hi + 1; m++) {
    if (m < 0 || m >= length) continue;
    uint8_t z = m % line_lines;
    bool inside = m >= lo && m <= hi;
    if (!inside && l.down[z]) continue;   // a neighbour another finger is on (T10 on either end)
    if (inside) palmed |= l.palmed[z];
    total += l.read[z];
    moment += l.read[z] * m;
  }
  float centre = total > 0 ? moment / total : (lo + hi) / 2.0f;
  float place = place_of(2 * centre);
  if (place < 0) place = 0;
  if (place > places - 1) place = places - 1;
  out = run{upside_down ? places - 1 - place : place, total, palmed};
}

BENT_BOARD_CODE static inline uint8_t line_runs(const line &l, bool upside_down, run *runs, uint8_t max_runs) {
  const int8_t length = 7;
  uint8_t n = 0;
  for (int8_t k = 0; k < length && n < max_runs; k++) {
    if (!l.place_down[k]) continue;
    int8_t lo = k;
    while (k + 1 < length && l.place_down[k + 1]) k++;
    int8_t hi = k, start = lo;
    for (int8_t m = lo + 1; m < hi && n < max_runs; m++) {
      int16_t left = 0, right = 0, here = l.read[m % line_lines];
      for (int8_t j = start; j < m; j++) if (l.read[j % line_lines] > left) left = l.read[j % line_lines];
      for (int8_t j = m + 1; j <= hi; j++) if (l.read[j % line_lines] > right) right = l.read[j % line_lines];
      if (here < valley_share * left && here < valley_share * right) {
        line_centre(l, start, m - 1, upside_down, runs[n++]);
        start = m + 1;
      }
    }
    if (n < max_runs) line_centre(l, start, hi, upside_down, runs[n++]);
  }
  return n;
}

// ---- the bend rail -----------------------------------------------------------------------------------
// Five points from three lines along the strip above the chord buttons, 8.7 mm apart: T1's core, a band of
// T1 and T3 stripes, T3's core, a band of T3 and T5, T5's core. A finger's readings over the three, as a
// centre (T1 at 0, T3 at 2, T5 at 4), say which point it is on, though not evenly: the edges between points
// read at rail_bound (T1's core is a thin strip, and the bands' stripes run along the rail).
// Two fingers' readings add. A finger landing while others are held is what they leave: each settled finger
// taken out as it read while nothing else was landing (its steady force, so one that soaked up a newcomer's
// share of a line they share doesn't hide it), its point from the centre of what is left round its
// strongest line, and its profile, its own share of each line there, is what it reads. Every pass, each
// finger's force is fitted to the readings by its profile. A finger's point and profile are
// settled once it has pressed down for rail_landing_ms (a band's two stripes don't reach the chip in the
// same millisecond); until then each reading decides them again. rail_profile, a finger's average share, is
// kept for the record only (the decoder doesn't read it): a real finger's share wanders from it, and fitted
// by the average one left the rest to land as a second finger. Three lines tell at most three fingers apart,
// and two points pressed in the same instant read as one where they share a line: land one a moment first.
// Fitted from the real board (results/bentboard_rec_1803, 2026-10-28; the model had {0.42, 1.71, 2.51, 3.69} and
// gains {1.626, 1.601, 1, 1.265, 0.931}): the middle dot reads a third of itself on T1, so the edges sit lower.
const float rail_profile[rail_points][3] BENT_BOARD_DATA = {   // a real finger's share of T1, T3, T5 at each point, at its firmest
  {1.000f, 0.000f, 0.000f}, {0.726f, 0.274f, 0.000f}, {0.325f, 0.673f, 0.002f}, {0.074f, 0.661f, 0.264f}, {0.016f, 0.095f, 0.890f}};
const float rail_bound[rail_points - 1] BENT_BOARD_DATA = {0.24f, 1.01f, 2.03f, 3.24f};   // the centre where each point gives way to the next
const float rail_gain[rail_points] BENT_BOARD_DATA = {2.259f, 1.164f, 1.000f, 0.966f, 0.779f};   // each point read as T3's core reads (T1's copper is a thin strip)
const float rail_soft = 40, rail_firm = 250;     // depth 0 to 1 over this much (after the gain), as touch_firmness maps a string
const float rail_land = 30;                      // counts a touched line reads past what the fingers held explain: a finger landing
const float rail_mismatch = 0.3f;                // ... and past this share of what they read on it (a finger's share drifts as it rolls)
const float rail_lift = 18;                      // a finger's fitted force under this has lifted
const float rail_fade = 0.3f;                    // ... or under this share of its peak, where every line it reads on another holds too
const float rail_peak_relax_ms = 1000;           // how fast that peak follows a lighter touch
const float rail_quiet = 12;                     // every line explained to within this: the fingers' forces are steady
const uint8_t rail_landing_ms = 12;
const uint8_t rail_touch_max = 3;
const float rail_keep = 0.02f;                   // how strongly a held finger's force is kept near what it was

struct rail_touch { bool active; bool landing; uint8_t point; float force; float steady; float prof[3]; uint32_t landed_at; float peak; };
struct rail_state { rail_touch t[rail_touch_max]; uint32_t at; };

BENT_BOARD_CODE static inline uint8_t rail_point_at(float centre) {
  uint8_t k = 0;
  while (k < rail_points - 1 && centre >= rail_bound[k]) k++;
  return k;
}

// Fit each active touch's force to the readings r by its profile, >= 0, a settled one kept near what it was:
// least squares, a few rounds over each in turn. rest gets what is left on each line.
BENT_BOARD_CODE static inline void rail_fit(rail_state &s, const float r[3], float rest[3]) {
  for (uint8_t round = 0; round < 12; round++) {
    for (uint8_t i = 0; i < rail_touch_max; i++) {
      rail_touch &t = s.t[i];
      if (!t.active) continue;
      float num = 0, den = 0;
      for (uint8_t c = 0; c < 3; c++) {
        float left = r[c];
        for (uint8_t j = 0; j < rail_touch_max; j++) if (j != i && s.t[j].active) left -= s.t[j].force * s.t[j].prof[c];
        num += t.prof[c] * left;
        den += t.prof[c] * t.prof[c];
      }
      if (!t.landing) { num += rail_keep * t.force; den += rail_keep; }
      t.force = den > 0 && num > 0 ? num / den : 0;
    }
  }
  for (uint8_t c = 0; c < 3; c++) {
    rest[c] = r[c];
    for (uint8_t j = 0; j < rail_touch_max; j++) if (s.t[j].active) rest[c] -= s.t[j].force * s.t[j].prof[c];
  }
}

// Where a finger is that the others don't explain (not `self`): the readings less each settled finger at its
// steady force and each other landing one at its force, the centre of what is left round its strongest
// touched line and the lines either side; prof gets its share of each line there (0 elsewhere), its profile.
// -1 if nothing touched is left.
BENT_BOARD_CODE static inline float rail_rest_centre(const rail_state &s, int8_t self, const float r[3], const bool d[3], float &total, float prof[3]) {
  float rest[3];
  for (uint8_t c = 0; c < 3; c++) {
    rest[c] = r[c];
    prof[c] = 0;
    for (uint8_t i = 0; i < rail_touch_max; i++) {
      const rail_touch &t = s.t[i];
      if (!t.active || (int8_t)i == self) continue;
      rest[c] -= (t.landing ? t.force : t.steady) * t.prof[c];
    }
  }
  int8_t top = -1;
  for (uint8_t c = 0; c < 3; c++) if (d[c] && rest[c] > 0 && (top < 0 || rest[c] > rest[top])) top = c;
  total = 0;
  if (top < 0) return -1;
  int8_t lo = top, hi = top;   // the touched lines round it, then one either side
  while (lo > 0 && d[lo - 1] && rest[lo - 1] > 0) lo--;
  while (hi < 2 && d[hi + 1] && rest[hi + 1] > 0) hi++;
  float moment = 0;
  for (int8_t c = lo - 1; c <= hi + 1; c++) {
    if (c < 0 || c > 2 || rest[c] <= 0) continue;
    if ((c < lo || c > hi) && d[c]) continue;   // a touched line beyond: another finger's
    total += rest[c];
    moment += rest[c] * 2 * c;
    prof[c] = rest[c];
  }
  if (total <= 0) return -1;
  for (uint8_t c = 0; c < 3; c++) prof[c] /= total;
  return moment / total;
}

BENT_BOARD_CODE static inline bool rail_point_taken(const rail_state &s, uint8_t self, uint8_t k) {
  for (uint8_t j = 0; j < rail_touch_max; j++) if (j != self && s.t[j].active && s.t[j].point == k) return true;
  return false;
}

// One reading of the rail: zone_down and strength in harp order (as harp_array and read_strength give them).
// depth[k] is how deep point k is pressed, 0 to 1, numbered left to right as the player holds it.
BENT_BOARD_CODE static inline void rail_update(rail_state &s, const bool *zone_down, const int16_t *strength, bool upside_down,
                        uint32_t now, float depth[rail_points]) {
  float r[3], rest[3];
  bool d[3], any_down = false, any_active = false;
  for (uint8_t c = 0; c < 3; c++) {
    uint8_t z = at(rail_zone[c], upside_down);
    r[c] = strength[z];
    d[c] = zone_down[z];
    any_down |= d[c];
  }
  for (uint8_t i = 0; i < rail_touch_max; i++) any_active |= s.t[i].active;
  for (uint8_t k = 0; k < rail_points; k++) depth[k] = 0;
  if (!any_down && !any_active) return;
  // a finger still landing: its point and profile decided again, from what the others leave
  for (uint8_t i = 0; i < rail_touch_max; i++) {
    rail_touch &t = s.t[i];
    if (!t.active || !t.landing) continue;
    float total, prof[3];
    float c = rail_rest_centre(s, i, r, d, total, prof);
    if (c >= 0) {
      uint8_t k = rail_point_at(c);
      if (!rail_point_taken(s, i, k)) {
        t.point = k;
        for (uint8_t q = 0; q < 3; q++) t.prof[q] = prof[q];
      }
    }
    if (now - t.landed_at < rail_landing_ms) continue;
    t.landing = false;
    rail_fit(s, r, rest);
    t.steady = t.force;
  }
  rail_fit(s, r, rest);
  uint32_t dt = now - s.at;
  s.at = now;
  bool lifted[rail_touch_max] = {false};
  for (uint8_t i = 0; i < rail_touch_max; i++) {
    rail_touch &t = s.t[i];
    if (!t.active) continue;
    float relaxed = t.peak - t.peak * dt / rail_peak_relax_ms;
    t.peak = t.force > relaxed ? t.force : relaxed;
    // lifted: weak; or none of the lines it reads on touched (a line another finger holds stays touched); or
    // faded far under its peak where every line it reads on another finger reads on too: there the fit can't
    // tell it from that finger's share wandering from its profile (a core lifted beside a band held kept a
    // fifth of its force, from the band's T1 share alone)
    bool on = false, shared = true;
    for (uint8_t c = 0; c < 3; c++) {
      if (t.prof[c] <= 0.2f) continue;
      on |= d[c];
      bool other = false;
      for (uint8_t j = 0; j < rail_touch_max; j++) other |= j != i && s.t[j].active && s.t[j].prof[c] > 0.2f;
      shared &= other;
    }
    lifted[i] = t.force < rail_lift || !on || (shared && !t.landing && t.force < rail_fade * t.peak);
  }
  for (uint8_t i = 0; i < rail_touch_max; i++) if (lifted[i]) s.t[i].active = false;
  // fingers landing: a touched line reading clearly more than the fingers held explain, each as it read
  // steadily (fitted afresh, a held finger soaks up a newcomer's share of a line they share)
  for (uint8_t tries = 0; tries < rail_touch_max; tries++) {
    bool landing = false;
    for (uint8_t c = 0; c < 3; c++) {
      float held = 0;
      for (uint8_t j = 0; j < rail_touch_max; j++) {
        const rail_touch &t = s.t[j];
        if (t.active) held += (t.landing ? t.force : t.steady) * t.prof[c];
      }
      landing |= d[c] && r[c] - held > rail_land + rail_mismatch * held;
    }
    int8_t free_slot = -1;
    for (uint8_t i = 0; i < rail_touch_max && free_slot < 0; i++) if (!s.t[i].active) free_slot = i;
    if (!landing || free_slot < 0) break;
    float total, prof[3];
    float c = rail_rest_centre(s, free_slot, r, d, total, prof);
    if (c < 0) break;
    uint8_t k = rail_point_at(c);
    if (rail_point_taken(s, free_slot, k) || total < rail_lift) break;   // a finger held pressing harder
    rail_touch &t = s.t[free_slot];
    t = rail_touch{true, true, k, total, total, {prof[0], prof[1], prof[2]}, now, total};
  }
  rail_fit(s, r, rest);
  bool quiet = true, settled = true;
  for (uint8_t c = 0; c < 3; c++) quiet &= fabsf(rest[c]) < rail_quiet;
  for (uint8_t i = 0; i < rail_touch_max; i++) settled &= !s.t[i].active || !s.t[i].landing;
  for (uint8_t i = 0; i < rail_touch_max; i++) {
    rail_touch &t = s.t[i];
    if (!t.active) continue;
    if (quiet && settled) t.steady = t.force;
    uint8_t k = upside_down ? rail_points - 1 - t.point : t.point;
    float dd = depth_of(t.force * rail_gain[t.point], rail_soft, rail_firm);
    if (dd > depth[k]) depth[k] = dd;
  }
}

// ---- the palm ----------------------------------------------------------------------------------------
// One pad, under the left palm's edge while the chord buttons are played: how much of the hand is on it. On
// the real board (results/bentboard_rec_1803) a resting palm reads about 455 and a firm press only about 485
// (the pad's baseline is 700), so it can't tell pressure: it goes by contact, from a light touch to a resting
// palm at full. A resting palm turns its area fully on (Greg, 2026-10-28: the pad at 460-505, a hard press
// only, felt dead). The soft end sits over the # button's graze beside the pad (up to 51).
const float palm_soft = 60, palm_firm = 440;

BENT_BOARD_CODE static inline float palm_depth(const bool *zone_down, const int16_t *strength, bool upside_down) {
  uint8_t z = at(palm_zone, upside_down);
  return zone_down[z] ? depth_of(strength[z], palm_soft, palm_firm) : 0;
}

// ---- the thumb ribbon --------------------------------------------------------------------------------
// Two lines' teeth along the wedge, tapering against each other: T11's widest at the left end, T12's at the
// right, so T12's share of the two says where along the wedge a thumb is, whatever its pressure. The share
// isn't even along it (T12's spine runs the wedge's whole length; the right end is T12 alone), so the table
// turns it into the place. On the real board (results/bentboard_rec_1803) T12's share stays mostly above 0.43
// even at the left end (a tenth of a slide's readings fall under it and read 0), the middle reads 0.63 and
// the right end 0.95-1, so the table runs through those
// three (the model had 0.24 to 0.99); the left half is cramped and noisier than the right. As a harp touch,
// a thumb reading under ribbon_lift_ratio of its peak is lifting and keeps its place; the place follows
// over ribbon_follow_ms, the chip's noise steadied.
const float ribbon_share_lo = 0.430f, ribbon_share_hi = 0.970f;   // T12's share at the left end and at the right
const float ribbon_table[21] BENT_BOARD_DATA = {   // the place, 0 left to 1 right, at the share from lo to hi in twentieths
  0.000f, 0.068f, 0.135f, 0.203f, 0.270f, 0.337f, 0.405f, 0.473f, 0.524f, 0.563f, 0.603f,
  0.643f, 0.682f, 0.722f, 0.762f, 0.801f, 0.841f, 0.881f, 0.921f, 0.960f, 1.000f};
const float ribbon_lift_ratio = 0.7f;
const float ribbon_peak_relax_ms = 80;
const float ribbon_follow_ms = 15;
const float ribbon_least = 20;          // both lines together under this: too little to place
const float ribbon_soft = 40, ribbon_firm = 250;   // its depth, for an area set to read the ribbon's pressure

BENT_BOARD_CODE static inline float ribbon_place(float share) {
  float u = (share - ribbon_share_lo) / (ribbon_share_hi - ribbon_share_lo) * 20;
  if (u <= 0) return ribbon_table[0];
  if (u >= 20) return ribbon_table[20];
  uint8_t k = (uint8_t)u;
  return ribbon_table[k] + (ribbon_table[k + 1] - ribbon_table[k]) * (u - k);
}

struct ribbon_state { bool active; float place; float peak; float total; uint32_t at; };

// One reading of the ribbon. Afterwards s.active says whether a thumb is on it, s.place where (0 the left
// end as the player holds it, 1 the right) and s.total how firmly.
BENT_BOARD_CODE static inline void ribbon_update(ribbon_state &s, const bool *zone_down, const int16_t *strength, bool upside_down, uint32_t now) {
  uint8_t zl = at(ribbon_left, upside_down), zr = at(ribbon_right, upside_down);
  bool down = zone_down[zl] || zone_down[zr];
  float total = strength[zl] + strength[zr];
  uint32_t dt = now - s.at;
  s.at = now;
  if (!down) { s.active = false; s.total = 0; return; }
  s.total = total;
  if (total < ribbon_least) return;   // a thumb only just landing, or lifting: no place yet, or the last one kept
  float place = ribbon_place(strength[zr] / total);
  if (upside_down) place = 1 - place;
  if (!s.active) {
    s.active = true;
    s.place = place;
    s.peak = total;
    return;
  }
  float relaxed = s.peak - s.peak * dt / ribbon_peak_relax_ms;
  s.peak = total > relaxed ? total : relaxed;
  if (total < ribbon_lift_ratio * s.peak) return;
  float a = dt >= ribbon_follow_ms ? 1 : dt / ribbon_follow_ms;
  s.place += (place - s.place) * a;
}

// ---- where the ribbon meets the harp -----------------------------------------------------------------
// The ribbon's right end runs 1.5 mm from the harp's left edge, beside T7, T8, T9 and T10's bottom core, so a
// thumb at that end reads on the harp lines beside it, and a finger at the harp's left edge on T12. Modelled,
// a thumb (10-11 mm) centred within 2 mm of the end reads up to 0.85 of itself on T9 or T10; a finger on the
// harp's left edge up to 0.45 of itself on T12. So while a thumb reads as at the right end (T12's share past
// border_end_share), a border line reading under border_thumb_share of the ribbon is the thumb's edge and
// isn't touched, and the ribbon reading under border_share of the strongest border line is a harp finger's
// edge and isn't either. Elsewhere along the ribbon nothing is guarded (a light touch on T10's island,
// at the far top, stays a touch). Not guarded: a thumb pressed into the ribbon's top right corner, on T11's
// spine, which reaches T7's corner too and reads about as strongly on both (2 thumbs in 100 modelled there).
// The real board (results/bentboard_rec_1803) bleeds far less than modelled: a thumb resting at the right end
// read at most 20 on any harp line (0.11 of itself), under the chip's touch threshold, while a real finger
// beside it read 0.34-0.72 of the thumb and was muted by 0.9; hence 0.15 for the harp side. The ribbon side
// keeps 0.9 (a harp finger's edge on T12 not yet measured on the board).
const uint8_t border_zone[4] BENT_BOARD_DATA = {5, 4, 3, 2};    // T7 T8 T9 T10
const float border_share = 0.9f;
const float border_thumb_share = 0.15f;
const float border_end_share = 0.75f;

// zone_down in harp order, guarded in place
BENT_BOARD_CODE static inline void border_guard(bool *zone_down, const int16_t *strength, bool upside_down) {
  uint8_t zl = at(ribbon_left, upside_down), zr = at(ribbon_right, upside_down);
  if (!zone_down[zl] && !zone_down[zr]) return;
  float ribbon = strength[zl] + strength[zr];
  if (ribbon <= 0 || strength[zr] < border_end_share * ribbon) return;
  float harp = 0;
  for (uint8_t k = 0; k < 4; k++) {
    uint8_t z = at(border_zone[k], upside_down);
    if (zone_down[z] && strength[z] > harp) harp = strength[z];
  }
  if (harp <= 0) return;
  for (uint8_t k = 0; k < 4; k++) {
    uint8_t z = at(border_zone[k], upside_down);
    if (zone_down[z] && strength[z] < border_thumb_share * ribbon) zone_down[z] = false;
  }
  if (ribbon < border_share * harp) zone_down[zl] = zone_down[zr] = false;
}

}  // namespace bentboard
#endif
