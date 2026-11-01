// The chord walk: the joystick harp's stick colouring the chord the buttons chose, and walking a bass
// under it. The buttons choose the chord (major, minor, 7, maj7, m7); the stick adds what they can't, so
// it never lands on a chord the stock layout's buttons play (Greg, 2026-11-01: C7 is a press away, and
// pressing it is the fun; the alternate layout's add9 or m9 can be a colour, since they take none). Right is sweeter, a colour for each step: the ninth, then the sixth or thirteenth, then
// both or the eleventh. Left is tenser: the sharp eleventh, the major seventh against minor, the altered
// tones. Each step is relative to the chord held, and a step kept (latched) stays while the root does
// (Cadd9, then C7, is C9); a chord pressed on another root is plain, as pressed (main.cpp,
// update_chord_notes).
//
//            left 3      left 2     left 1      right 1    right 2   right 3
//   major    maj7#11     add b6     add #11     add9       6         6/9
//   minor    m(maj9)     m(b6)      m(maj7)     m(add9)    m6        m6/9
//   7        7#9         7#5        7b9         9          13        9sus4
//   maj7                 maj7#5     maj7#11     maj9       maj13
//   m7                   m7b9       m7b5        m9         m13       m11
//
// Any other chord (dim, aug, the alternate layout's, Barry Harris's sixths) takes no colour. A colour
// moves as few voices as it can, each to the nearest note of its new tone, so a step is a voice or two
// stepping (C to Cadd9: the top C up to D). The harp's extra notes (entries 4-6) keep what the chord lets
// go: an extra the colour lands on takes the note the voice left (C6/9: the harp keeps the G), except a
// third a sus colour let go, which would rub against its fourth: that extra takes the fifth (C9sus4).
//
// Up and down are momentary: the third leans to the fourth (sus4) or the second (sus2) while held, and
// comes back let go. Or, in their place, the walking bass (bass_step): each flick down or up steps the
// bass one note of the key's major scale under the chord, and it stays, until the next chord pressed
// starts it on its root again; main.cpp places it (pin_voice, as a slash bass is).
//
// Latched (joystick mode's 32 with the walk), the colour goes a step for each flick of the stick, out
// past flick_at and back, and stays there; lefts come back the same way, through the plain chord and on
// down the left side. A flick always moves something it can: one beyond what the chord reaches that way
// counts from its end, so coming back from a triad's third step on a maj7, the first left is heard.
//
// Twelve steps an octave only: the chord arrays hold the division's steps. The firmware walks nothing in
// another division (chord_walk_select in main.cpp).
//
// Plain C++ with no Arduino in it, so firmware/tools/chord_walk tests it on a computer.
#ifndef CHORD_WALK_H
#define CHORD_WALK_H
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#ifndef CHORD_WALK_CODE   // FLASHMEM on the Teensy (main.cpp sets it); empty on a computer
#define CHORD_WALK_CODE
#endif
#ifndef CHORD_WALK_DATA   // and its tables: PROGMEM, in flash
#define CHORD_WALK_DATA
#endif

namespace chordwalk {

// Where along the stick's travel (0 at rest to 1 at the end, past its dead middle) each step starts,
// and how far back it has to come to drop a step, so a hand held at a step's edge doesn't flicker. The
// first step is past where the other axis's crosstalk reaches (up to ~0.3 on the real plate).
const float walk_at[3] = {0.3f, 0.6f, 0.9f};
const float walk_back = 0.08f;
const float lean_at = 0.5f, lean_off = 0.35f;
// A flick: out past flick_at counts once, and the next counts once the stick is back inside flick_rearm,
// clear of the crosstalk a lean held far gives this axis (up to ~0.3 on the real plate)
const float flick_at = 0.6f, flick_rearm = 0.4f;
// How far the bass walks either way: -3 to 3, the seven notes of the scale once each
const int8_t bass_reach = 3;

// The chords that take colours, by their four voices' pitch classes (bit n, n half steps above the
// root), and each step's four tones, left 3 to 1 then right 1 to 3; a step of 0 isn't there
struct family { uint16_t tones; uint16_t steps[6]; };
#define CW_T(a, b, c, d) (uint16_t)((1u << (a)) | (1u << (b)) | (1u << (c)) | (1u << (d)))
const family families[5] CHORD_WALK_DATA = {
  {CW_T(0, 4, 7, 0),  {CW_T(0, 4, 11, 6), CW_T(0, 4, 7, 8), CW_T(0, 4, 7, 6), CW_T(0, 4, 7, 2), CW_T(0, 4, 7, 9), CW_T(0, 4, 9, 2)}},     // major
  {CW_T(0, 3, 7, 0),  {CW_T(0, 3, 11, 2), CW_T(0, 3, 7, 8), CW_T(0, 3, 7, 11), CW_T(0, 3, 7, 2), CW_T(0, 3, 7, 9), CW_T(0, 3, 9, 2)}},   // minor
  {CW_T(0, 4, 7, 10), {CW_T(0, 4, 10, 3), CW_T(0, 4, 10, 8), CW_T(0, 4, 10, 1), CW_T(0, 4, 10, 2), CW_T(0, 4, 10, 9), CW_T(0, 5, 10, 2)}}, // 7
  {CW_T(0, 4, 7, 11), {0, CW_T(0, 4, 11, 8), CW_T(0, 4, 11, 6), CW_T(0, 4, 11, 2), CW_T(0, 4, 11, 9), 0}},                                // maj7
  {CW_T(0, 3, 7, 10), {0, CW_T(0, 3, 10, 1), CW_T(0, 3, 6, 10), CW_T(0, 3, 10, 2), CW_T(0, 3, 10, 9), CW_T(0, 3, 10, 5)}},                 // m7
};
#undef CW_T

// The walk's step, -3 (full left) to 3 (full right), from the stick's left and right and the step it
// was on
CHORD_WALK_CODE static inline int8_t walk_step(float x, int8_t was) {
  float a = fabsf(x);
  int8_t m = (was > 0) == (x > 0) ? (int8_t)(was < 0 ? -was : was) : 0;   // the other way: from rest
  if (x == 0) m = 0;
  while (m > 0 && a < walk_at[m - 1] - walk_back) m--;
  while (m < 3 && a >= walk_at[m]) m++;
  return x < 0 ? -m : m;
}

// The lean, -1 (down, sus2), 0 or 1 (up, sus4), from the stick's up and down and the lean it had
CHORD_WALK_CODE static inline int8_t lean_step(float y, int8_t was) {
  if (was > 0 && y >= lean_off) return 1;
  if (was < 0 && y <= -lean_off) return -1;
  if (y >= lean_at) return 1;
  if (y <= -lean_at) return -1;
  return 0;
}

// A flick of one axis: 1 out the positive way, -1 the other, 0 none; armed is the stick having come back
CHORD_WALK_CODE static inline int8_t flick(float x, bool &armed) {
  if (!armed) {
    if (fabsf(x) < flick_rearm) armed = true;
    return 0;
  }
  if (fabsf(x) < flick_at) return 0;
  armed = false;
  return x > 0 ? 1 : -1;
}

// The four voices as a set of pitch classes, bit n for n steps above the root
CHORD_WALK_CODE static inline uint16_t voice_set(const uint8_t *chord) {
  uint16_t b = 0;
  for (uint8_t s = 0; s < 4; s++) b |= 1u << (chord[s] % 12);
  return b;
}

// Which family the held chord is, or -1
CHORD_WALK_CODE static inline int8_t family_of(const uint8_t *held) {
  uint16_t t = voice_set(held);
  for (int8_t f = 0; f < 5; f++) {
    if (families[f].tones == t) return f;
  }
  return -1;
}

// A step's tones, or 0 where the family has none that far
CHORD_WALK_CODE static inline uint16_t step_tones(int8_t f, int8_t step) {
  if (f < 0 || step == 0 || step < -3 || step > 3) return 0;
  return families[f].steps[step < 0 ? step + 3 : step + 2];
}

// How many steps the held chord takes one way (dir 1 right, -1 left)
CHORD_WALK_CODE static inline int8_t reach(const uint8_t *held, int8_t dir) {
  int8_t f = family_of(held);
  int8_t k = 0;
  while (k < 3 && step_tones(f, dir * (k + 1))) k++;
  return k;
}

// The note with pitch class pc nearest v (a tie goes up), not below 0
CHORD_WALK_CODE static inline int16_t nearest(int16_t v, uint8_t pc) {
  int16_t below = v - ((v % 12 - pc + 12) % 12);
  if (below < 0) return below + 12;
  return (v - below) < (below + 12 - v) ? below : below + 12;
}

CHORD_WALK_CODE static inline bool any_has(const uint8_t *chord, uint8_t pc) {
  for (uint8_t s = 0; s < 7; s++) {
    if (chord[s] % 12 == pc) return true;
  }
  return false;
}

// The voices moved to the tones of target (four pitch classes), as few as can and each as little as
// it can; an extra the colour lands on takes the note its voice left
CHORD_WALK_CODE static inline void colour(uint8_t *out, uint16_t target) {
  uint8_t vacated[4], missing[4];
  uint8_t nv = 0, nm = 0;
  uint16_t kept = 0;
  for (uint8_t s = 0; s < 4; s++) {
    uint8_t pc = out[s] % 12;
    if ((target >> pc & 1) && !(kept >> pc & 1)) kept |= 1u << pc;
    else vacated[nv++] = s;
  }
  for (uint8_t pc = 0; pc < 12; pc++) {
    if ((target >> pc & 1) && !(kept >> pc & 1) && nm < 4) missing[nm++] = pc;
  }
  if (nm < nv) nv = nm;   // fewer tones than voices: the rest stay doubled
  if (nv > 3) nv = 3;     // a colour keeps the root at least
  // every way of giving the missing tones to the vacated voices (at most three of each), least movement,
  // and of two that move as little, the more open (Cm(maj9): C Eb B D, not C Eb D B)
  static const uint8_t perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  int16_t best = 32767, best_height = -1;
  uint8_t pick = 0;
  for (uint8_t p = 0; p < 6; p++) {
    bool fits = true;
    int16_t cost = 0, height = 0;
    for (uint8_t i = 0; i < nv; i++) {
      if (perms[p][i] >= nv) { fits = false; break; }
      int16_t from = out[vacated[i]];
      int16_t to = nearest(from, missing[perms[p][i]]);
      cost += abs(to - from);
      height += to;
    }
    if (fits && (cost < best || (cost == best && height > best_height))) { best = cost; best_height = height; pick = p; }
  }
  bool sus = !(target >> 3 & 1) && !(target >> 4 & 1);
  for (uint8_t i = 0; i < nv; i++) {
    uint8_t s = vacated[i];
    uint8_t left = out[s];
    uint8_t v = (uint8_t)nearest(left, missing[perms[pick][i]]);
    out[s] = v;
    if (sus && (left % 12 == 3 || left % 12 == 4)) left = 7;   // not the third back against the fourth
    for (uint8_t e = 4; e < 7; e++) {
      if (out[e] % 12 == v % 12) out[e] = left;
    }
  }
}

// The held chord (seven entries, twelve steps an octave) coloured `walk` steps (-3 to 3) and leaned
// `lean` (-1, 0, 1), into out. False when that moves nothing (no colour that far, no third to lean),
// and out is then the held chord.
CHORD_WALK_CODE static inline bool walk(const uint8_t *held, int8_t walk, int8_t lean, uint8_t *out) {
  for (uint8_t s = 0; s < 7; s++) out[s] = held[s];
  bool moved = false;
  int8_t f = family_of(held);
  int8_t r = walk > 0 ? reach(held, 1) : reach(held, -1);
  if (walk > r) walk = r;
  if (walk < -r) walk = -r;
  uint16_t target = step_tones(f, walk);
  if (target) {
    colour(out, target);
    moved = true;
  }
  // up and down: the third leans to the fourth or the second
  if (lean != 0) {
    int8_t third = -1;
    for (int8_t s = 0; s < 4 && third < 0; s++) {
      if (out[s] % 12 == 3 || out[s] % 12 == 4) third = s;
    }
    uint8_t pc = lean > 0 ? 5 : 2;
    bool taken = false;
    for (int8_t s = 0; s < 4; s++) taken = taken || (s != third && out[s] % 12 == pc);
    if (third >= 0 && !taken) {
      uint8_t v = out[third] - out[third] % 12 + pc;
      out[third] = v;
      // an extra it lands on takes a note the sus chord can use instead of the third it let go
      const uint8_t instead[3] = {(uint8_t)(lean > 0 ? 2 : 5), 9, 10};
      for (uint8_t e = 4; e < 7; e++) {
        if (out[e] % 12 != pc) continue;
        for (uint8_t c = 0; c < 3; c++) {
          if (!any_has(out, instead[c])) { out[e] = instead[c]; break; }
        }
      }
      moved = true;
    }
  }
  return moved;
}

// The latched walk after a flick (1 or -1), from where it was, kept within what the held chord reaches
CHORD_WALK_CODE static inline int8_t step_latched(const uint8_t *held, int8_t latched, int8_t flicked) {
  int8_t hi = reach(held, 1), lo = -reach(held, -1);
  if (latched > hi) latched = hi;
  if (latched < lo) latched = lo;
  latched += flicked;
  if (latched > hi) latched = hi;
  if (latched < lo) latched = lo;
  return latched;
}

// The walking bass: the pitch class `steps` notes of the major scale from the chord's root (-3 to 3),
// as half steps above the root (0 to 11). root is the chord's root in half steps above the key's. A root
// outside the scale steps to the scale's next note that way.
CHORD_WALK_CODE static inline uint8_t bass_step(uint8_t root, int8_t steps) {
  const uint16_t major_scale = 0xAB5;   // 0 2 4 5 7 9 11
  int16_t pc = root % 12;
  int8_t dir = steps < 0 ? -1 : 1;
  for (int8_t k = 0; k != steps; k += dir) {
    do pc = (pc + dir + 12) % 12; while (!(major_scale >> pc & 1));
  }
  return (uint8_t)((pc - root % 12 + 12) % 12);
}

}  // namespace chordwalk

#endif
