// The chord walk (include/chord_walk.h) on a computer: each chord the minichord's buttons make,
// coloured and leaned, against the chord a player would name; no colour a button already plays; the
// stick's steps, flicks and their hysteresis; the latch; the walking bass. Run with run.sh; each check
// that fails prints a line, and the exit status is 1 if any did.
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
    char n[8];
    snprintf(n, sizeof n, "%s%d", N[c[i] % 12], c[i] / 12);   // the octave above the root, 0 or 1
    strcat(s, n);
    strcat(s, i == 3 ? " | " : i < 6 ? " " : "");
  }
}

// The voices (in entry order, each with its octave above the root) and the extras
static void expect(const char *what, uint8_t *held, int8_t w, int8_t l, bool moves, const char *want) {
  uint8_t out[7];
  bool m = chordwalk::walk(held, w, l, out);
  char got[96];
  show(out, got);
  if (m != moves || strcmp(got, want)) {
    printf("FAIL %s: walk %d lean %d gave %s (%s), wanted %s (%s)\n", what, w, l, got, m ? "moves" : "still",
           want, moves ? "moves" : "still");
    failed++;
  } else {
    printf("ok   %-30s %s\n", what, got);
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
  // right: sweeter
  expect("C right 1: Cadd9", major, 1, 0, true, "C0 E0 G0 D1 | C1 F0 A0");
  expect("C right 2: C6", major, 2, 0, true, "C0 E0 G0 A0 | D0 F0 C1");
  expect("C right 3: C6/9", major, 3, 0, true, "C0 E0 A0 D1 | C1 F0 G0");
  expect("Cm right 1: Cm(add9)", minor, 1, 0, true, "C0 Eb0 G0 D1 | C#0 F0 Ab0");
  expect("Cm right 2: Cm6", minor, 2, 0, true, "C0 Eb0 G0 A0 | C#0 F0 Ab0");
  expect("Cm right 3: Cm6/9", minor, 3, 0, true, "C0 Eb0 A0 D1 | C#0 F0 Ab0");
  expect("C7 right 1: C9", seventh, 1, 0, true, "C0 E0 Bb0 D0 | G0 F0 A0");
  expect("C7 right 2: C13", seventh, 2, 0, true, "C0 E0 Bb0 A0 | D0 F0 G0");
  expect("C7 right 3: C9sus4", seventh, 3, 0, true, "C0 D0 Bb0 F0 | E0 G0 A0");
  expect("Cmaj7 right 1: Cmaj9", maj_seventh, 1, 0, true, "C0 E0 B0 D0 | G0 F0 A0");
  expect("Cmaj7 right 2: Cmaj13", maj_seventh, 2, 0, true, "C0 E0 B0 A0 | D0 F0 G0");
  expect("Cmaj7 right 3 stops at maj13", maj_seventh, 3, 0, true, "C0 E0 B0 A0 | D0 F0 G0");
  expect("Cm7 right 1: Cm9", min_seventh, 1, 0, true, "C0 Eb0 Bb0 D0 | C#0 F0 Ab0");
  expect("Cm7 right 2: Cm13", min_seventh, 2, 0, true, "C0 Eb0 Bb0 A0 | C#0 F0 Ab0");
  expect("Cm7 right 3: Cm11", min_seventh, 3, 0, true, "C0 Eb0 Bb0 F0 | C#0 G0 Ab0");
  // left: tenser
  expect("C left 1: C(add#11)", major, -1, 0, true, "C0 E0 G0 F#1 | D0 F0 A0");
  expect("C left 2: C(add b6)", major, -2, 0, true, "C0 E0 G0 Ab0 | D0 F0 A0");
  expect("C left 3: Cmaj7#11", major, -3, 0, true, "C0 E0 F#0 B0 | D0 F0 A0");
  expect("Cm left 1: Cm(maj7)", minor, -1, 0, true, "C0 Eb0 G0 B0 | C#0 F0 Ab0");
  expect("Cm left 2: Cm(b6)", minor, -2, 0, true, "C0 Eb0 G0 Ab0 | C#0 F0 C1");
  expect("Cm left 3: Cm(maj9)", minor, -3, 0, true, "C0 Eb0 B0 D1 | C#0 F0 Ab0");
  expect("C7 left 1: C7b9", seventh, -1, 0, true, "C0 E0 Bb0 C#1 | D0 F0 A0");
  expect("C7 left 2: C7#5", seventh, -2, 0, true, "C0 E0 Bb0 Ab0 | D0 F0 A0");
  expect("C7 left 3: C7#9", seventh, -3, 0, true, "C0 E0 Bb0 Eb0 | D0 F0 A0");
  expect("Cmaj7 left 1: Cmaj7#11", maj_seventh, -1, 0, true, "C0 E0 B0 F#0 | D0 F0 A0");
  expect("Cmaj7 left 2: Cmaj7#5", maj_seventh, -2, 0, true, "C0 E0 B0 Ab0 | D0 F0 A0");
  expect("Cm7 left 1: Cm7b5", min_seventh, -1, 0, true, "C0 Eb0 Bb0 F#0 | C#0 F0 Ab0");
  expect("Cm7 left 2: Cm7b9", min_seventh, -2, 0, true, "C0 Eb0 Bb0 C#1 | G0 F0 Ab0");
  // no colours for the rest
  expect("Cdim: none", dim, 2, 0, false, "C0 Eb0 F#0 C1 | D0 F0 A0");
  expect("C+: none", aug, -1, 0, false, "C0 E0 Ab0 C1 | D0 F0 A0");
  expect("Csus4: none", sus_fourth, 1, 0, false, "C0 F0 G0 C1 | D0 A0 Bb0");
  // up and down: the third leans
  expect("C up: Csus4", major, 0, 1, true, "C0 F0 G0 C1 | D0 Bb0 A0");
  expect("C down: Csus2", major, 0, -1, true, "C0 D0 G0 C1 | Bb0 F0 A0");
  expect("C7 up: C7sus4", seventh, 0, 1, true, "C0 F0 Bb0 G0 | D0 F0 A0");
  expect("Cadd9 down: the D is there", major, 1, -1, true, "C0 E0 G0 D1 | C1 F0 A0");
  expect("C6 up: C6sus4", major, 2, 1, true, "C0 F0 G0 A0 | D0 Bb0 C1");
  expect("C at rest", major, 0, 0, false, "C0 E0 G0 C1 | D0 F0 A0");

  // nothing the stick plays is a chord a button plays
  {
    uint8_t *held[5] = {major, minor, seventh, maj_seventh, min_seventh};
    uint8_t *buttons[7] = {major, minor, seventh, maj_seventh, min_seventh, dim, aug};
    int bad = 0;
    for (auto h : held) {
      for (int8_t w = -3; w <= 3; w++) {
        uint8_t out[7];
        if (!w || !chordwalk::walk(h, w, 0, out)) continue;
        for (auto b : buttons) {
          if (chordwalk::voice_set(out) == chordwalk::voice_set(b)) {
            printf("FAIL a button's chord: walk %d gives what a button plays\n", w);
            bad++;
          }
        }
      }
    }
    failed += bad;
    if (!bad) printf("ok   no step plays a button's chord\n");
  }

  const float right[] = {0.1f, 0.3f, 0.5f, 0.6f, 0.53f, 0.51f, 0.9f, 1.0f, 0.85f, 0.2f, 0.0f};
  const int8_t right_want[] = {0, 1, 1, 2, 2, 1, 3, 3, 3, 0, 0};
  expect_steps("walk steps: each from its start, back past its edge less 0.08", right, 11, right_want, false);
  const float flip[] = {-0.95f, -0.55f, -0.5f, 0.0f, 0.35f};
  const int8_t flip_want[] = {-3, -2, -1, 0, 1};
  expect_steps("walk steps: left, rest, right", flip, 5, flip_want, false);
  const float lean[] = {0.4f, 0.5f, 0.4f, 0.3f, -0.6f, -0.4f, -0.2f};
  const int8_t lean_want[] = {0, 1, 1, 0, -1, -1, 0};
  expect_steps("lean: on at 0.5, off below 0.35", lean, 7, lean_want, true);

  // the latch: flicks, each counted once out and armed again back near the middle
  {
    bool armed = true;
    const float xs[] = {0.3f, 0.65f, 0.9f, 0.45f, 0.35f, 0.7f, 0.1f, -0.8f, -0.25f, -0.62f};
    const int8_t want[] = {0, 1, 0, 0, 0, 1, 0, -1, 0, -1};
    bool ok = true;
    for (int i = 0; i < 10; i++) {
      int8_t f = chordwalk::flick(xs[i], armed);
      if (f != want[i]) { printf("FAIL flick at %.2f gave %d, wanted %d\n", xs[i], f, want[i]); failed++; ok = false; break; }
    }
    if (ok) printf("ok   flicks: out past 0.6, again once back inside 0.4\n");
  }
  struct { const char *what; uint8_t *held; int8_t from, flick, want; } latch[] = {
    {"C right from rest: add9", major, 0, 1, 1},
    {"C right at 6/9 stays", major, 3, 1, 3},
    {"C left from C6: add9", major, 2, -1, 1},
    {"C left from rest: add#11", major, 0, -1, -1},
    {"Cmaj7 left from 3 (a triad's): #11 heard", maj_seventh, -3, 1, -1},
    {"Cmaj7 right from 3 (a triad's): maj9", maj_seventh, 3, -1, 1},
    {"Cdim: no colour either way", dim, 2, 1, 0},
  };
  for (auto &t : latch) {
    int8_t got = chordwalk::step_latched(t.held, t.from, t.flick);
    if (got != t.want) { printf("FAIL %s: %d, wanted %d\n", t.what, got, t.want); failed++; }
    else printf("ok   latch %-40s %d\n", t.what, got);
  }
  if (chordwalk::reach(major, 1) != 3 || chordwalk::reach(maj_seventh, 1) != 2 || chordwalk::reach(maj_seventh, -1) != 2 ||
      chordwalk::reach(min_seventh, -1) != 2 || chordwalk::reach(dim, 1) != 0) {
    printf("FAIL reach\n");
    failed++;
  } else printf("ok   reach: C 3, Cmaj7 2 and 2, Cm7 left 2, dim 0\n");

  // the walking bass: the key's major scale from the chord's root (as half steps above the root)
  struct { const char *what; uint8_t root; int8_t steps; uint8_t want; } bass[] = {
    {"C in C, down 1: B", 0, -1, 11}, {"C in C, down 2: A", 0, -2, 9}, {"C in C, down 3: G", 0, -3, 7},
    {"C in C, up 1: D", 0, 1, 2}, {"C in C, up 3: F", 0, 3, 5},
    {"A in C, down 1: G", 9, -1, 10}, {"A in C, down 2: F", 9, -2, 8},
    {"F in C, down 1: E", 5, -1, 11}, {"G in C, up 1: A", 7, 1, 2},
    {"Bb in C, down 1: A", 10, -1, 11}, {"Bb in C, up 1: B", 10, 1, 1},
    {"no step: the root", 4, 0, 0},
  };
  for (auto &b : bass) {
    uint8_t got = chordwalk::bass_step(b.root, b.steps);
    if (got != b.want) { printf("FAIL bass %s: %d, wanted %d\n", b.what, got, b.want); failed++; }
    else printf("ok   bass %s\n", b.what);
  }

  printf(failed ? "%d failed\n" : "all passed\n", failed);
  return failed ? 1 : 0;
}
