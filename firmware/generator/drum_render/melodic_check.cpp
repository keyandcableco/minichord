// Checks the melodic ladder (places::melodic_step, MELODIC PADS in main.cpp) against the plate's
// geometry: the 28 places sorted by height then across give the same order, every seam's step lies
// strictly between its two pads' steps, and the ladder is a permutation of 0-27. Run by press_test.sh.
#include <cstdio>
#include <algorithm>
#include "plate_places.h"

int main() {
  const uint8_t n = 12 + places::seam_count;
  uint8_t order[n];
  for (uint8_t k = 0; k < n; k++) order[k] = k;
  std::stable_sort(order, order + n, [](uint8_t a, uint8_t b) {
    float ax, ay, bx, by;
    places::place_middle(a, ax, ay);
    places::place_middle(b, bx, by);
    return ay != by ? ay < by : ax < bx;
  });
  int failures = 0;
  for (uint8_t step = 0; step < n; step++) {
    if (places::melodic_step[order[step]] != step) {
      printf("FAIL  place %d sits at step %d of the ladder, the table says %d\n", order[step], step, places::melodic_step[order[step]]);
      failures++;
    }
  }
  bool seen[n] = {};
  for (uint8_t k = 0; k < n; k++) seen[places::melodic_step[k]] = true;
  for (uint8_t s = 0; s < n; s++) {
    if (!seen[s]) {
      printf("FAIL  no place plays step %d\n", s);
      failures++;
    }
  }
  for (uint8_t k = 0; k < places::seam_count; k++) {
    uint8_t s = places::melodic_step[12 + k];
    uint8_t a = places::melodic_step[places::seam_zones[k][0]], b = places::melodic_step[places::seam_zones[k][1]];
    if (s <= std::min(a, b) || s >= std::max(a, b)) {
      printf("FAIL  seam %d plays step %d, not between its pads' %d and %d\n", k, s, a, b);
      failures++;
    }
  }
  printf(failures ? "%d melodic check(s) FAILED\n" : "PASS  the melodic ladder matches the plate: 28 steps, every seam between its pads\n", failures);
  return failures ? 1 : 0;
}
