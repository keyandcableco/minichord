// PROGRESSION UPLOAD: a chord progression sent over SysEx (F0 7D 70) into chord memory, and read back from
// it. generator/progression.md is the contract; include/progression_core.h is the part with nothing of the
// Teensy in it (checking a WRITE, building the replies), tested on a computer in test/progression_pc. This is
// the Teensy's side, included by main.cpp after chord memory and the preset store's crc32_update.
//
// A WRITE takes the place of chord memory's progression, the same as one recorded: RAM only, gone at power
// off, played by rhythm mode and driven the same ways (287, control changes 104-108, the double tap). Each
// entry becomes a chord_moment the way a press would make one: its line (6 - column), its chord (type 0 the
// button's own in the layout as it is now, as handle_chord_type picks it; 1-29 chord_catalogue's), sharp and
// slash. Its start is the entries before it added up, in eighths (24ths of a beat / 2), and the length is
// rounded up to whole bars of the meter as it is now, the last chord stretched to fill. Refused while chord
// memory records. If chord memory was playing, or the WRITE asks to play, it plays from the next bar line;
// otherwise it waits, stopped, for a play.
#pragma once
#define PROGRESSION_COLD FLASHMEM
#define PROGRESSION_CRC32 crc32_update
#include "progression_core.h"

static_assert(progression::MAX_ENTRIES == CHORD_MEMORY_MAX, "a progression is chord memory's size");
static_assert(progression::CHORD_TYPES == chord_catalogue_size, "the progression's chord types are the catalogue's");
static_assert(ACC_BEAT % 2 == 0, "an eighth is half of ACC_BEAT");

// What a single button of a row plays in the layout as it is now: handle_chord_type's choice for one button
FLASHMEM static uint8_t (*progression_own_chord(uint8_t row))[7] {
  if (alt_chord_layout) return alt_chord_for(row);
  switch (row) {
    case 0: return barry_harris_mode ? &maj_sixth : &major;
    case 1: return barry_harris_mode ? &min_sixth : &minor;
    default: return &seventh;
  }
}

// a chord back to its type, 1-29 (every chord a press can make is in the catalogue)
FLASHMEM static uint8_t progression_type_of(uint8_t (*chord)[7]) {
  for (uint8_t i = 0; i < chord_catalogue_size; i++) if (chord_catalogue[i] == chord) return i + 1;
  return 1;
}

FLASHMEM static void progression_send(const uint8_t *m, uint32_t len, uint8_t cable) {
  usbMIDI.sendSysEx(len, m, true, cable);   // never send_now(): see USER KIT
}

FLASHMEM static void progression_status(uint8_t op, uint8_t status, uint8_t cable) {
  uint8_t m[7];
  progression_send(m, progression::build_status(m, op, status, chord_memory_count), cable);
}

FLASHMEM static void progression_write(const uint8_t *data, uint32_t length, uint8_t cable) {
  progression::write_t w;   // under 1 kB, on the stack for the message's time
  uint8_t status = progression::parse_write(data, length, w);
  if (status == progression::STATUS_OK && chord_memory_state == CHORD_MEMORY_RECORDING) status = progression::STATUS_BUSY;
  if (status != progression::STATUS_OK) { progression_status(progression::OP_WRITE, status, cable); return; }
  const double eighth = ACC_BEAT / 2;
  const bool was_playing = chord_memory_state == CHORD_MEMORY_PLAYING;
  for (uint8_t i = 0; i < w.count; i++) {
    const progression::moment_t &m = w.moments[i];
    uint8_t (*chord)[7] = m.type ? chord_catalogue[m.type - 1] : progression_own_chord(m.row);
    chord_memory[i] = {m.at * eighth, m.line, chord, m.sharp, m.slashed, m.slash_line};
  }
  chord_memory_count = w.count;
  chord_memory_length = progression::bar_round(w.total, acc_bar_eighths()) * eighth;
  chord_memory_played = -1;
  if (was_playing || (w.flags & progression::FLAG_PLAY)) {
    if (!rythm_mode) set_rythm_mode(true);
    chord_memory_start = chord_memory_bar_line();
    chord_memory_state = CHORD_MEMORY_PLAYING;
  } else {
    chord_memory_state = CHORD_MEMORY_STOPPED;
    chord_memory_show();
  }
  progression_status(progression::OP_WRITE, progression::STATUS_OK, cable);
}

FLASHMEM static void progression_read(uint8_t cable) {
  progression::moment_t mo[progression::MAX_ENTRIES];
  const double eighth = ACC_BEAT / 2;
  const bool kept = chord_memory_state == CHORD_MEMORY_PLAYING || chord_memory_state == CHORD_MEMORY_STOPPED;
  const uint8_t n = kept ? chord_memory_count : 0;
  for (uint8_t i = 0; i < n; i++) {
    const chord_moment &c = chord_memory[i];
    mo[i] = {(uint32_t)lround(c.at / eighth), c.fundamental, 0, progression_type_of(c.chord), c.sharp, c.slashed, c.slash};
  }
  uint8_t m[progression::WRITE_MAX];
  progression_send(m, progression::build_write_reply(m, mo, n, (uint32_t)lround(chord_memory_length / eighth)), cable);
}

// Every SysEx, from mts_receive after the kit's: whether it was a progression message
FLASHMEM bool progression_message(const byte *data, uint32_t length) {
  if (!progression::ours(data, length)) return false;
  const uint8_t op = data[3];
  if (op >= 0x40) return true;   // a reply's shape: never answered
  const uint8_t cable = usbMIDI.getCable();
  if (!progression::framed(data, length)) { progression_status(op, progression::STATUS_BAD_MESSAGE, cable); return true; }
  switch (op) {
    case progression::OP_WRITE:
      progression_write(data, length, cable);
      break;
    case progression::OP_READ:
      if (length != 7 || data[4] || data[5]) progression_status(op, progression::STATUS_BAD_MESSAGE, cable);
      else progression_read(cable);
      break;
    case progression::OP_INFO:
      if (length != 7 || data[4] || data[5]) progression_status(op, progression::STATUS_BAD_MESSAGE, cable);
      else {
        uint8_t m[10];
        progression_send(m, progression::build_info(m, chord_memory_state, chord_memory_count, acc_bar_eighths()), cable);
      }
      break;
    default:
      progression_status(op, progression::STATUS_BAD_MESSAGE, cable);
  }
  return true;
}
