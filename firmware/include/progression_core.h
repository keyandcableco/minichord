// PROGRESSION UPLOAD: a chord progression sent over SysEx (F0 7D 70) into chord memory, and read back
// from it. generator/progression.md is the contract (the messages, the entries and what the minichord does
// with each); this is the part with nothing of the Teensy in it, so a computer can compile it and send it
// every message the contract has (test/progression_pc). include/progression.h is the Teensy's side: chord
// memory, the chord tables, the rhythm's bar and USB MIDI.
//
// How it's used:
//   parse_write(m, len, w)          a WRITE in: its entries checked and turned into moments, or the status
//                                   that refuses it
//   build_write_reply(...)          a READ's answer: chord memory as entries, in WRITE's layout
//   build_status / build_info       the short replies
//   bar_round(total, bar)           a progression's length, rounded up to whole bars
// Everything is static, as in user_kit_core.h: a copy for each file that includes this (one does, on the
// Teensy), and the entry points are marked unused so a file that takes only some compiles without a warning.
#pragma once
#include <stdint.h>
#include <string.h>

// Where the code goes on the Teensy: FLASHMEM there (progression.h says so before it includes this).
#ifndef PROGRESSION_COLD
#define PROGRESSION_COLD
#endif

namespace progression {

constexpr uint8_t NAMESPACE = 0x70;   // F0 7D 70: the kit's is 6B, the lock report's 6C, the page's 6D
constexpr uint8_t PROTO = 1;
constexpr uint8_t MAX_ENTRIES = 64;   // chord memory's CHORD_MEMORY_MAX
constexpr uint8_t BUTTONS = 21;
constexpr uint8_t COLUMNS = 7;
constexpr uint8_t CHORD_TYPES = 29;   // chord_catalogue's, numbered as the alternate layout's slots (202-208)
constexpr uint32_t ENTRY_BYTES = 4;
constexpr uint32_t WRITE_HEADER = 12; // F0 7D 70 op proto flags count crc(5)
constexpr uint32_t WRITE_MAX = WRITE_HEADER + MAX_ENTRIES * ENTRY_BYTES + 1;   // 269, under Teensy's 290

enum : uint8_t { OP_WRITE = 1, OP_READ = 2, OP_INFO = 5 };
// the status replies': the same numbers as the kit's where they mean the same
enum : uint8_t { STATUS_OK = 0, STATUS_BUSY = 1, STATUS_TOO_BIG = 2, STATUS_BAD_MESSAGE = 3, STATUS_UNSUPPORTED = 4,
                 STATUS_CRC = 7 };
constexpr uint8_t FLAG_PLAY = 0x01;   // play from the next bar line once it's in

// An entry as sent: the button in the panel's order (column F to B, then major, minor, seventh), a chord
// type (0 the button's own, 1-29 the catalogue), modifiers (bit 0 sharp, bits 1-3 the slash's column + 1,
// 0 none) and its length in eighths
struct entry_t {
  uint8_t button, type, modifiers, eighths;
};

// An entry as chord memory holds it: line in the firmware's order (B to F, line = 6 - column), the row
// (0 major, 1 minor, 2 seventh: what type 0 resolves), the type, and where it starts in eighths
struct moment_t {
  uint32_t at;         // eighths from the start
  int8_t line;
  uint8_t row;
  uint8_t type;        // 0 the button's own chord, else 1-29
  bool sharp, slashed;
  uint8_t slash_line;  // the slash's line, firmware order (only if slashed)
};

struct write_t {
  uint8_t flags;
  uint8_t count;
  uint32_t total;      // the entries' eighths added up, before bar_round
  moment_t moments[MAX_ENTRIES];
};

// CRC-32 (zlib's): main.cpp's crc32_update on the Teensy, which progression.h names here; this one otherwise
#ifdef PROGRESSION_CRC32
static inline uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n) { return PROGRESSION_CRC32(crc, p, n); }
#else
PROGRESSION_COLD static uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
#endif

// A number as groups of 7 bits, least significant first, as the kit's messages carry them
PROGRESSION_COLD static void put_n(uint8_t *out, uint64_t v, uint8_t groups) {
  for (uint8_t g = 0; g < groups; g++) { out[g] = v & 0x7F; v >>= 7; }
}
PROGRESSION_COLD static bool get_n(const uint8_t *in, uint8_t groups, uint64_t &v) {
  v = 0;
  for (uint8_t g = 0; g < groups; g++) {
    if (in[g] & 0x80) return false;
    v |= (uint64_t)in[g] << (7 * g);
  }
  return true;
}

static inline int8_t line_of_column(uint8_t column) { return (int8_t)(COLUMNS - 1 - column); }
static inline uint8_t column_of_line(int8_t line) { return (uint8_t)(COLUMNS - 1 - line); }

// Whether m is one of ours at all: F0 7D 70 and an op. A reply's op (0x40 and up) is ours but never
// answered, so two minichords, or an app that echoes, can't set each other off.
static inline bool ours(const uint8_t *m, uint32_t len) {
  return len >= 4 && m[0] == 0xF0 && m[1] == 0x7D && m[2] == NAMESPACE;
}

// Framed: at least 7 bytes, closing F7, no data byte over 7F (as the kit's rule)
PROGRESSION_COLD static bool framed(const uint8_t *m, uint32_t len) {
  if (len < 7 || m[len - 1] != 0xF7) return false;
  for (uint32_t i = 1; i < len - 1; i++) if (m[i] & 0x80) return false;
  return true;
}

// A WRITE checked, in the contract's order, and turned into moments. STATUS_OK, or why it's refused.
PROGRESSION_COLD __attribute__((unused)) static uint8_t parse_write(const uint8_t *m, uint32_t len, write_t &w) {
  if (len < WRITE_HEADER + 1) return STATUS_BAD_MESSAGE;
  if (m[4] != PROTO) return STATUS_UNSUPPORTED;
  const uint8_t flags = m[5], count = m[6];
  if (flags & ~FLAG_PLAY) return STATUS_BAD_MESSAGE;
  if (count > MAX_ENTRIES) return STATUS_TOO_BIG;
  if (count == 0 || len != WRITE_HEADER + count * ENTRY_BYTES + 1) return STATUS_BAD_MESSAGE;
  uint64_t crc;
  if (!get_n(m + 7, 5, crc) || crc > 0xFFFFFFFFu) return STATUS_BAD_MESSAGE;
  const uint8_t *e = m + WRITE_HEADER;
  if (crc32(0, e, count * ENTRY_BYTES) != crc) return STATUS_CRC;
  uint32_t at = 0;
  for (uint8_t i = 0; i < count; i++, e += ENTRY_BYTES) {
    const uint8_t button = e[0], type = e[1], modifiers = e[2], eighths = e[3];
    const uint8_t slash = (modifiers >> 1) & 0x07;   // the slash's column + 1
    if (button >= BUTTONS || type > CHORD_TYPES || (modifiers & ~0x0F) || slash > COLUMNS || eighths == 0) return STATUS_BAD_MESSAGE;
    moment_t &mo = w.moments[i];
    mo.at = at;
    mo.line = line_of_column(button / 3);
    mo.row = button % 3;
    mo.type = type;
    mo.sharp = modifiers & 0x01;
    mo.slashed = slash != 0;
    mo.slash_line = slash ? line_of_column(slash - 1) : 0;
    at += eighths;
  }
  w.flags = flags;
  w.count = count;
  w.total = at;
  return STATUS_OK;
}

// A progression's length: rounded up to whole bars, the last chord stretched to fill (chord memory starts and
// loops on bar lines). bar is the meter's eighths, 5 to 24.
static inline uint32_t bar_round(uint32_t total, uint32_t bar) {
  if (bar == 0) return total;
  return (total + bar - 1) / bar * bar;
}

// F0 7D 70 (40 | op) status count F7: count is the entries chord memory holds after it
PROGRESSION_COLD __attribute__((unused)) static uint32_t build_status(uint8_t *out, uint8_t op, uint8_t status, uint8_t count) {
  const uint8_t m[7] = {0xF0, 0x7D, NAMESPACE, (uint8_t)(0x40 | (op & 0x3F)), status, (uint8_t)(count & 0x7F), 0xF7};
  memcpy(out, m, sizeof m);
  return sizeof m;
}

// F0 7D 70 45 proto max state count bar F7: state as chord memory's (0 empty, 1 recording, 2 playing,
// 3 stopped), bar the meter's eighths now
PROGRESSION_COLD __attribute__((unused)) static uint32_t build_info(uint8_t *out, uint8_t state, uint8_t count, uint8_t bar) {
  const uint8_t m[10] = {0xF0, 0x7D, NAMESPACE, 0x40 | OP_INFO, PROTO, MAX_ENTRIES, (uint8_t)(state & 0x7F),
                         (uint8_t)(count & 0x7F), (uint8_t)(bar & 0x7F), 0xF7};
  memcpy(out, m, sizeof m);
  return sizeof m;
}

// A READ's answer, F0 7D 70 42 and then WRITE's layout from proto on: chord memory as entries. Each moment
// goes back as its column's major button with its type given (the row it came from isn't kept), its length
// the gap to the next, the last's to length. A length past 127 eighths is split over more entries of the
// same chord, as many as fit in 64. out must hold WRITE_MAX bytes.
PROGRESSION_COLD __attribute__((unused)) static uint32_t build_write_reply(uint8_t *out, const moment_t *mo, uint8_t n,
                                                                           uint32_t length) {
  uint8_t *e = out + WRITE_HEADER;
  uint8_t count = 0;
  for (uint8_t i = 0; i < n && count < MAX_ENTRIES; i++) {
    const uint32_t end = i + 1 < n ? mo[i + 1].at : length;
    uint32_t left = end > mo[i].at ? end - mo[i].at : 1;
    while (left > 0 && count < MAX_ENTRIES) {
      const uint8_t take = left > 127 ? 127 : (uint8_t)left;
      e[0] = (uint8_t)(column_of_line(mo[i].line) * 3 + (mo[i].type ? 0 : mo[i].row));
      e[1] = mo[i].type;
      e[2] = (uint8_t)((mo[i].sharp ? 0x01 : 0) | (mo[i].slashed ? (column_of_line(mo[i].slash_line) + 1) << 1 : 0));
      e[3] = take;
      e += ENTRY_BYTES;
      count++;
      left -= take;
    }
  }
  out[0] = 0xF0; out[1] = 0x7D; out[2] = NAMESPACE; out[3] = 0x40 | OP_READ;
  out[4] = PROTO;
  out[5] = 0;
  out[6] = count;
  put_n(out + 7, crc32(0, out + WRITE_HEADER, count * ENTRY_BYTES), 5);
  const uint32_t len = WRITE_HEADER + count * ENTRY_BYTES;
  out[len] = 0xF7;
  return len + 1;
}

}  // namespace progression
