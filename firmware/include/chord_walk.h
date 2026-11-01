// The chord walk: the joystick harp's stick changing the held chord's quality one note at a time, the
// way a guitarist's line cliché or a pedal steel's pedal does, rather than swapping it for another
// chord. Left and right walk a voice by half steps, a step for each part of the stick's travel: right
// the fifth climbs (C, C+, C6, C7; Cm, Cm#5, Cm6, Cm7), left the top note sinks, the seventh where the
// chord has one and the octave's root where it hasn't (C, Cmaj7, C7, C6; Cm, Cm(maj7), Cm7, Cm6;
// Cmaj7, C7, C6). Up and down lean the third: up to the fourth (sus4), down to the second (sus2). The
// two axes move different voices, so a diagonal does both (up and right on C: C6sus4).
//
// A chord with a seventh walks two steps either way, a triad three; a voice stops short of landing on
// another chord tone (C7 right: C7#5, C13, not a doubled seventh). The rest of the chord stays where it
// is, entry for entry, so the voice that sounded the moving note is the one that moves. The harp's
// extra notes (entries 4-6) keep what the chord lets go: a walked note landing on an extra takes the
// note it left in its place (C6: the harp still has the G), and a leaned third landing on one takes the
// first of the other sus note, the sixth and the seventh the chord hasn't got.
//
// Latched (joystick mode's 32 with the walk), the walk goes a step for each flick of the stick, out
// past flick_at and back, and stays there: right after right climbs C, C+, C6, C7, and lefts come
// back the same way, through the held chord and on down the left side (C, Cmaj7, C7, C6). A flick
// always moves something it can: one beyond what the chord reaches that way counts from its end, so
// coming back from C7 on a seventh chord, the first left is heard.
//
// Twelve steps an octave only: the chord arrays hold the division's steps, and a walk in 19 or 31 would
// need its own idea of a half step. The firmware walks nothing there (chord_walk_select in main.cpp).
//
// Plain C++ with no Arduino in it, so firmware/tools/chord_walk tests it on a computer.
#ifndef CHORD_WALK_H
#define CHORD_WALK_H
#include <stdint.h>
#include <math.h>

#ifndef CHORD_WALK_CODE   // FLASHMEM on the Teensy (main.cpp sets it); empty on a computer
#define CHORD_WALK_CODE
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

CHORD_WALK_CODE static inline bool voice_has(const uint8_t *chord, int8_t skip, uint8_t pc) {
  for (int8_t s = 0; s < 4; s++) {
    if (s != skip && chord[s] % 12 == pc) return true;
  }
  return false;
}

CHORD_WALK_CODE static inline bool any_has(const uint8_t *chord, uint8_t pc) {
  for (uint8_t s = 0; s < 7; s++) {
    if (chord[s] % 12 == pc) return true;
  }
  return false;
}

// One voice moved to v; an extra that was already v takes the note the voice left
CHORD_WALK_CODE static inline void move_voice(uint8_t *chord, int8_t slot, uint8_t v) {
  uint8_t left = chord[slot];
  chord[slot] = v;
  for (uint8_t e = 4; e < 7; e++) {
    if (chord[e] % 12 == v % 12) chord[e] = left;
  }
}

// The held chord (seven entries, twelve steps an octave) walked `walk` steps (-3 to 3) and leaned
// `lean` (-1, 0, 1), into out. False when that moves nothing (no such voice, or no room for it), and
// out is then the held chord.
CHORD_WALK_CODE static inline bool walk(const uint8_t *held, int8_t walk, int8_t lean, uint8_t *out) {
  for (uint8_t s = 0; s < 7; s++) out[s] = held[s];
  int8_t third = -1, fifth = -1, seventh = -1, octave = -1;
  for (int8_t s = 0; s < 4; s++) {
    uint8_t pc = held[s] % 12;
    if ((pc == 3 || pc == 4) && third < 0) third = s;
    else if (pc >= 6 && pc <= 8 && fifth < 0) fifth = s;
    else if ((pc == 10 || pc == 11) && seventh < 0) seventh = s;
    else if (pc == 0 && held[s] > 0 && octave < 0) octave = s;
  }
  uint8_t limit = seventh >= 0 ? 2 : 3;
  uint8_t steps = walk < 0 ? -walk : walk;
  if (steps > limit) steps = limit;
  bool moved = false;
  // right: the fifth climbs; left: the seventh, or the octave's root, sinks
  int8_t slot = walk > 0 ? fifth : walk < 0 ? (seventh >= 0 ? seventh : octave) : -1;
  if (slot >= 0) {
    int8_t dir = walk > 0 ? 1 : -1;
    int16_t v = out[slot];
    for (uint8_t k = 0; k < steps; k++) {
      int16_t next = v + dir;
      if (next < 0 || voice_has(out, slot, (uint8_t)(next % 12))) break;
      v = next;
    }
    if (v != out[slot]) {
      move_voice(out, slot, (uint8_t)v);
      moved = true;
    }
  }
  // up and down: the third leans to the fourth or the second
  if (lean != 0 && third >= 0) {
    uint8_t v = out[third] - out[third] % 12 + (lean > 0 ? 5 : 2);
    if (!voice_has(out, third, v % 12)) {
      out[third] = v;
      // an extra it lands on takes a note the sus chord can use instead of the third it let go
      const uint8_t instead[3] = {(uint8_t)(lean > 0 ? 2 : 5), 9, 10};
      for (uint8_t e = 4; e < 7; e++) {
        if (out[e] % 12 != v % 12) continue;
        for (uint8_t c = 0; c < 3; c++) {
          if (!any_has(out, instead[c])) { out[e] = instead[c]; break; }
        }
      }
      moved = true;
    }
  }
  return moved;
}

// A flick of the stick's left and right: 1 right, -1 left, 0 none; armed is the stick having come back
CHORD_WALK_CODE static inline int8_t flick(float x, bool &armed) {
  if (!armed) {
    if (fabsf(x) < flick_rearm) armed = true;
    return 0;
  }
  if (fabsf(x) < flick_at) return 0;
  armed = false;
  return x > 0 ? 1 : -1;
}

// How many steps the held chord walks one way (dir 1 right, -1 left) before it can't move further
CHORD_WALK_CODE static inline int8_t reach(const uint8_t *held, int8_t dir) {
  uint8_t was[7], now[7];
  for (uint8_t s = 0; s < 7; s++) was[s] = held[s];
  for (int8_t k = 1; k <= 3; k++) {
    walk(held, dir * k, 0, now);
    bool same = true;
    for (uint8_t s = 0; s < 7; s++) same = same && now[s] == was[s];
    if (same) return k - 1;
    for (uint8_t s = 0; s < 7; s++) was[s] = now[s];
  }
  return 3;
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

// The four voices as a set of pitch classes, bit n for n steps above the root
CHORD_WALK_CODE static inline uint16_t voice_set(const uint8_t *chord) {
  uint16_t b = 0;
  for (uint8_t s = 0; s < 4; s++) b |= 1u << (chord[s] % 12);
  return b;
}

}  // namespace chordwalk

#endif
