// The chord walk (include/chord_walk.h) on a computer: each chord the minichord's buttons make, walked
// and leaned, against the chord a player would name, and the stick's steps and their hysteresis.
// Run with run.sh; each check that fails prints a line, and the exit status is 1 if any did.
#include "chord_walk.h"
#include <cstdio>
#include <cstring>

static int failed = 0;

// main.cpp's chord arrays, twelve steps an octave
uint8_t major[7] = {0, 4, 7, 12, 2, 5, 9};
uint8_t minor[7] = {0, 3, 7, 12, 1, 5, 8};
uint8_t seventh[7] = {0, 4, 10, 7, 2, 5, 9};
uint8_t maj_seventh[7] = {0, 4, 11, 7, 2, 5, 9};
uint8_t min_seventh[7] = {0, 3, 10, 7, 1, 5, 8};
uint8_t aug[7] = {0, 4, 8, 12, 2, 5, 9};
uint8_t dim[7] = {0, 3, 6, 12, 2, 5, 9};
uint8_t full_dim[7] = {0, 3, 6, 9, 2, 5, 12};
uint8_t sus_fourth[7] = {0, 5, 7, 12, 2, 9, 10};

static const char *N[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static void show(const uint8_t *c, char *s) {
  s[0] = 0;
  for (int i = 0; i < 7; i++) {
    strcat(s, N[c[i] % 12]);
    strcat(s, i == 3 ? " | " : i < 6 ? " " : "");
  }
}

// The voices (in entry order) and the extras walked to, as note names
static void expect(const char *what, uint8_t *held, int8_t w, int8_t l, bool moves, const char *voices_extras) {
  uint8_t out[7];
  bool m = chordwalk::walk(held, w, l, out);
  char got[64];
  show(out, got);
  if (m != moves || strcmp(got, voices_extras)) {
    printf("FAIL %s: walk %d lean %d gave %s (%s), wanted %s (%s)\n", what, w, l, got, m ? "moves" : "still",
           voices_extras, moves ? "moves" : "still");
    failed++;
  } else {
    printf("ok   %-34s %s\n", what, got);
  }
}

static void expect_steps(const char *what, const float *xs, int n, const int8_t *want, bool lean) {
  int8_t s = 0;
  for (int i = 0; i < n; i++) {
    s = lean ? chordwalk::lean_step(xs[i], s) : chordwalk::walk_step(xs[i], s);
    if (s != want[i]) {
      printf("FAIL %s: at %.2f step %d, wanted %d\n", what, xs[i], s, want[i]);
      failed++;
      return;
    }
  }
  printf("ok   %s\n", what);
}

int main() {
  // right: the fifth climbs
  expect("C right 1: C+", major, 1, 0, true, "C E Ab C | D F A");
  expect("C right 2: C6", major, 2, 0, true, "C E A C | D F G");
  expect("C right 3: C7", major, 3, 0, true, "C E Bb C | D F A");
  expect("Cm right 1: Cm#5", minor, 1, 0, true, "C Eb Ab C | C# F G");
  expect("Cm right 2: Cm6", minor, 2, 0, true, "C Eb A C | C# F Ab");
  expect("Cm right 3: Cm7", minor, 3, 0, true, "C Eb Bb C | C# F Ab");
  expect("C7 right 2: C13", seventh, 2, 0, true, "C E Bb A | D F G");
  expect("C7 right 3 stops at C13", seventh, 3, 0, true, "C E Bb A | D F G");
  expect("Cmaj7 right 2: Cmaj13", maj_seventh, 2, 0, true, "C E B A | D F G");
  expect("Cm7 right 2: Cm13", min_seventh, 2, 0, true, "C Eb Bb A | C# F Ab");
  // left: the top sinks
  expect("C left 1: Cmaj7", major, -1, 0, true, "C E G B | D F A");
  expect("C left 2: C7", major, -2, 0, true, "C E G Bb | D F A");
  expect("C left 3: C6", major, -3, 0, true, "C E G A | D F C");
  expect("Cm left 1: Cm(maj7)", minor, -1, 0, true, "C Eb G B | C# F Ab");
  expect("Cm left 2: Cm7", minor, -2, 0, true, "C Eb G Bb | C# F Ab");
  expect("Cm left 3: Cm6", minor, -3, 0, true, "C Eb G A | C# F Ab");
  expect("Cmaj7 left 1: C7", maj_seventh, -1, 0, true, "C E Bb G | D F A");
  expect("Cmaj7 left 2: C6", maj_seventh, -2, 0, true, "C E A G | D F B");
  expect("Cmaj7 left 3 stops at C6", maj_seventh, -3, 0, true, "C E A G | D F B");
  expect("C7 left 2: C+ over C", seventh, -2, 0, true, "C E Ab G | D F A");
  expect("Cdim left 2: Cm7b5", dim, -2, 0, true, "C Eb F# Bb | D F A");
  expect("Cdim7 left: no top to walk", full_dim, -2, 0, false, "C Eb F# A | D F C");
  // up and down: the third leans
  expect("C up: Csus4", major, 0, 1, true, "C F G C | D Bb A");
  expect("C down: Csus2", major, 0, -1, true, "C D G C | Bb F A");
  expect("Cm up: Csus4", minor, 0, 1, true, "C F G C | C# D Ab");
  expect("C7 up: C7sus4 (no free note for the extra)", seventh, 0, 1, true, "C F Bb G | D F A");
  expect("Csus4 up: no third", sus_fourth, 0, 1, false, "C F G C | D A Bb");
  // both
  expect("C up right 2: C6sus4", major, 2, 1, true, "C F A C | D Bb G");
  expect("C at rest", major, 0, 0, false, "C E G C | D F A");

  const float right[] = {0.1f, 0.3f, 0.5f, 0.6f, 0.53f, 0.51f, 0.9f, 1.0f, 0.85f, 0.2f, 0.0f};
  const int8_t right_want[] = {0, 1, 1, 2, 2, 1, 3, 3, 3, 0, 0};
  expect_steps("walk steps: each from its start, back past its edge less 0.08", right, 11, right_want, false);
  const float flip[] = {-0.95f, -0.55f, -0.5f, 0.0f, 0.35f};
  const int8_t flip_want[] = {-3, -2, -1, 0, 1};
  expect_steps("walk steps: left, rest, right", flip, 5, flip_want, false);
  const float lean[] = {0.4f, 0.5f, 0.4f, 0.3f, -0.6f, -0.4f, -0.2f};
  const int8_t lean_want[] = {0, 1, 1, 0, -1, -1, 0};
  expect_steps("lean: on at 0.5, off below 0.35", lean, 7, lean_want, true);

  printf(failed ? "%d failed\n" : "all passed\n", failed);
  return failed ? 1 : 0;
}
