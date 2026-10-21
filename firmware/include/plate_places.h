// Reading a harp plate as places rather than strings: the drum pads plate (harp plate 13). It lays the
// twelve zones out as a grid of two columns and six rows, and is read from how strongly each zone reads,
// not only from which zones are touched.
//
// Every two pads that meet have a seam between them, stripes of the two pads' copper in turn, so a finger
// on a pad reads that zone alone and a finger on a seam reads both about alike: twelve pads and sixteen
// seams, 28 places.
//
// Everything here is the plate as it is made, the right way up: zone p is the zone the harp reads as
// string p when the minichord is the right way up (T(12 - p) on the plate), column 0 is the left, row 0
// the bottom. Upside down the caller reads the zones by where they are on the plate and turns what is
// found round (turned()). Nothing here reads the chip or knows Arduino, so a PC can test it against the
// plate's model.
#ifndef PLATE_PLACES_H
#define PLATE_PLACES_H
#include <stdint.h>
#include <math.h>
#include <stdlib.h>

namespace places {

// Where each zone is on the grid. Bottom to top the rows are kick and snare (T12, T11), hat and clap
// (T8, T9), open hat and rim (T7, T10), crash and low tom (T6, T5), bell and high tom (T1, T4), and the
// congas (T2, T3), as the drum pads plate's harp drums lay them out.
static const uint8_t col[12] = {0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0};
static const uint8_t row[12] = {0, 0, 2, 1, 1, 2, 3, 3, 4, 5, 5, 4};
const uint8_t rows = 6;

inline uint8_t zone_at(uint8_t c, uint8_t r) {
  for (uint8_t p = 0; p < 12; p++) if (col[p] == c && row[p] == r) return p;
  return 255;
}

// Two zones whose copper meets in a seam: beside each other in a row, or one above the other.
inline bool meet(uint8_t a, uint8_t b) {
  if (a == b || a > 11 || b > 11) return false;
  return row[a] == row[b] || (col[a] == col[b] && abs((int)row[a] - (int)row[b]) == 1);
}

// The zone that is where p was, with the plate turned half round.
inline uint8_t turned(uint8_t p) { return zone_at(1 - col[p], rows - 1 - row[p]); }

// ---- the drum pads: 28 places ----
// A finger on a seam reads its two zones about alike, and one on a pad reads its neighbours a little
// where its edge reaches over the seam. Counting a pair as their seam once the weaker reads a quarter of
// the stronger, the plate's model (each zone reading the copper under a round contact of 6-9 mm, firm or
// light, with the chip's noise) plays the place under the finger's centre 90% of the time anywhere on
// the plate, and every time once the centre is 2 mm inside the place; at 0.4, the share drum_targets.py
// drew, 78% and 98.6%. The first real board (2026-10-19, 4 taps on each of the 28 places): a finger on a pad
// leans into the pad above more than the model said (its strongest neighbour at 0.19, 0.27 for one tap in
// ten, and past 0.25 on the hat, open hat, high tom and bell), and one on a seam reads its weaker pad at 0.87
// (0.63 for one in ten). At 0.25 94% of the taps struck the place meant, at 0.35 98%.
const float seam_share = 0.35f;
// A zone touched while a neighbour already taken reads more than twice as strongly is that finger's
// edge, not a finger of its own: a second finger landing beside a held one reads as strongly as it, and
// one reaching over the seam's edge reads under half.
const float spill_share = 0.5f;

struct find {
  uint8_t a, b;      // b == a on a pad, else the seam between a and b (a the stronger)
  float strength;    // the finger's reading: the zone's, or the seam's two together
};

// The places fingers are on, strongest first. taken: zones of fingers already placed, which are not
// found again but whose readings still decide what beside them is their spill.
inline uint8_t find_places(const float *s, const bool *down, uint16_t taken, find *out, uint8_t max_found) {
  uint16_t used = taken;
  uint8_t n = 0;
  while (n < max_found) {
    int8_t a = -1;
    for (uint8_t p = 0; p < 12; p++) {
      if (!down[p] || (used >> p & 1)) continue;
      if (a < 0 || s[p] > s[a]) a = p;
    }
    if (a < 0) break;
    bool spill = false;
    for (uint8_t q = 0; q < 12 && !spill; q++) spill = (used >> q & 1) && meet(a, q) && s[a] < spill_share * s[q];
    used |= 1u << a;
    if (spill) continue;
    int8_t b = -1;
    for (uint8_t q = 0; q < 12; q++) {
      if ((used >> q & 1) || !meet(a, q)) continue;
      if (b < 0 || s[q] > s[b]) b = q;
    }
    if (b >= 0 && s[a] > 0 && s[b] >= seam_share * s[a]) {
      used |= 1u << b;
      out[n++] = find{(uint8_t)a, (uint8_t)b, s[a] + s[b]};
    } else {
      out[n++] = find{(uint8_t)a, (uint8_t)a, s[a]};
    }
  }
  return n;
}

// The 28 places numbered: 0-11 the pads (the zone), 12-27 the seams in seam_zones' order.
const uint8_t seam_count = 16;
static const uint8_t seam_zones[seam_count][2] = {
  {0, 1}, {4, 3}, {5, 2}, {6, 7}, {11, 8}, {10, 9},          // across the rows, bottom to top
  {0, 4}, {4, 5}, {5, 6}, {6, 11}, {11, 10},                 // up the left column
  {1, 3}, {3, 2}, {2, 7}, {7, 8}, {8, 9}};                   // up the right column

inline uint8_t place_of(uint8_t a, uint8_t b) {
  if (a == b) return a;
  for (uint8_t k = 0; k < seam_count; k++) {
    if ((seam_zones[k][0] == a && seam_zones[k][1] == b) || (seam_zones[k][0] == b && seam_zones[k][1] == a)) return 12 + k;
  }
  return a;   // zones that don't meet: the stronger pad
}

inline uint8_t place_turned(uint8_t place) {
  if (place < 12) return turned(place);
  const uint8_t *z = seam_zones[place - 12];
  return place_of(turned(z[0]), turned(z[1]));
}

// A place's middle on the grid, in columns and rows.
inline void place_middle(uint8_t place, float &x, float &y) {
  if (place < 12) { x = col[place]; y = row[place]; return; }
  const uint8_t *z = seam_zones[place - 12];
  x = (col[z[0]] + col[z[1]]) * 0.5f;
  y = (row[z[0]] + row[z[1]]) * 0.5f;
}

// ---- the drum pads played melodic (MELODIC PADS in main.cpp) ----
// The 28 places as a ladder of notes, the lowest at the bottom left, rising along each row (pad, row
// seam, pad) with the two column seams between one row and the next: place_middle sorted by height,
// then across. Each seam's note lies between its two pads' notes, so in a scale the seams are the
// steps between, and in chromatic mode the ladder is 28 steps, two octaves and a fourth.
// melodic_check.cpp (generator/drum_render) checks this table against the geometry above.
static const uint8_t melodic_step[12 + seam_count] = {
  0, 2, 12, 7, 5, 10, 15, 17, 22, 27, 25, 20,   // the pads, in zone order
  1, 6, 11, 16, 21, 26,                          // the row seams, bottom to top
  3, 8, 13, 18, 23,                              // up the left column
  4, 9, 14, 19, 24};                             // up the right

}  // namespace places
#endif
