// The user drum kit's slot (drum kit 13, "your own"): the part with nothing of the Teensy in it, so a
// computer can compile it against a flash of its own and send it every message the contract has. The
// contract is generator/user_kit.md: the image, where it lives, the SysEx messages and what the minichord
// does with each; this follows it, and where the two disagree, the contract wins and this is fixed.
// include/user_kit.h is the Teensy side: the flash, USB MIDI, the DAC's mute, the LED and the drum machine,
// handed in here as a few functions (io_t).
//
// How it's used, on the minichord or on a computer:
//   init(slot, io, header_addr, data_base, capacity)   at boot: the stored kit checked in full, and applied
//   receive(slot, message, length)                     every SysEx in; true when it was a kit message
//   begin_waiting(slot) / run_begin(slot, busy, work)  a BEGIN that came in is only noted by receive(): the
//                                                      program's loop opens the upload (or refuses it) when
//                                                      it's ready, saying whether the rhythm or the looper
//                                                      is running, and lending the buffers an upload needs
//                                                      (work_t), which must last until it ends: the upload
//                                                      loop's own, on its stack
//   is_open(slot) / poll(slot)                         while open, every message in to receive() and
//                                                      poll() between, which ends a silent upload
//   check_image(image, size)                           an image in a file or on the wire, as the contract's
//                                                      check_image: 0, or the reason it fails
// Everything here is in one slot_t, and the functions are static: a copy for each file that includes this
// (one does, on the Teensy), as include/usb_audio_rate.h has it, since GCC won't put functions shared
// between files into a named section beside ones that aren't. The entry points are marked unused, so a file
// that takes only some of them (a test; the minichord, which never packs) compiles without a warning.
#pragma once
#include <stdint.h>
#include <string.h>

// Where the code goes on the Teensy: USER_KIT_COLD is FLASHMEM there (user_kit.h says so before it
// includes this). None of it runs often enough to earn a place in RAM1, where the program's code and the
// stack share 32 kB blocks. On a computer it's nothing.
#ifndef USER_KIT_COLD
#define USER_KIT_COLD
#endif

namespace user_kit {

constexpr uint32_t HEADER_BYTES = 256;        // the image's header, which the sound follows
constexpr uint32_t VOICES = 13;               // the drum machine's (drum_machine.h's voice order)
constexpr uint32_t PIECE = 224;               // the raw bytes in a DATA message, 256 packed
constexpr uint32_t FILE_CAPACITY = 262144;    // what a file is checked against: it has no minichord's room
constexpr uint32_t MIN_CAPACITY = 65536;      // less room than this and the slot is disabled
constexpr uint32_t SECTOR = 4096;             // the flash's erase size
constexpr uint32_t PAGE = 256;                // and its program size
constexpr uint32_t SILENCE_US = 5000000;      // an upload with no DATA, COMMIT, ABORT or CLEAR for this long is ended
constexpr uint8_t PROTO = 1;
// What kit 13 plays while no kit is stored: drum kit 4 in setting 337's numbering (drum_kits[3]), studio, a
// plain acoustic kit to stand in for the player's own (the contract's section 4). The PC test reads the
// name in drum_patterns.h, so a kit added or moved there can't change it unseen.
constexpr uint8_t STAND_IN_KIT = 4;

enum : uint8_t { OP_BEGIN = 1, OP_DATA, OP_COMMIT, OP_ABORT, OP_INFO, OP_CLEAR };
enum : uint8_t { STATE_EMPTY, STATE_VALID, STATE_RECEIVING, STATE_DAMAGED, STATE_DISABLED };
// INFO's reasons, and check_image's. REASON_NO_HEADER (1) is INFO's only: the header sector doesn't start
// MCK1, whether it's blank or holds other bytes, so it holds no kit (state 0)
enum : uint8_t { REASON_NONE, REASON_NO_HEADER, REASON_FORM, REASON_HEADER_CRC, REASON_DATA_BASE, REASON_RULE,
                 REASON_DATA_CRC, REASON_ROOM };
// the status replies'
enum : uint8_t { STATUS_OK, STATUS_BUSY, STATUS_TOO_BIG, STATUS_BAD_MESSAGE, STATUS_UNSUPPORTED, STATUS_NO_UPLOAD,
                 STATUS_OUT_OF_ORDER, STATUS_PIECE_CRC, STATUS_READ_BACK, STATUS_IMAGE_CRC, STATUS_HEADER,
                 STATUS_DISABLED };

// What the slot asks of the device. Flash addresses are the processor's (0x60000000 up on the Teensy);
// a fake flash on a computer can number them as it likes, as long as flash(a) + n is flash(a + n).
struct io_t {
  const uint8_t *(*flash)(uint32_t addr);                            // the flash at addr, to read
  void (*erase_sector)(uint32_t addr);                               // the 4 kB sector at addr, to FF
  void (*program)(uint32_t addr, const uint8_t *data, uint32_t len); // up to 256 bytes, within one page
  void (*send)(const uint8_t *message, uint32_t len);                // a reply, F0 to F7
  uint32_t (*now_us)();                                              // microseconds, on a clock that keeps
                                                                     // counting while the flash is erased
  void (*mute)(bool on);                                             // the DAC muted, or back on
  void (*kit_invalidate)();                                          // the drum machine off the kit, and
                                                                     // its sample entries emptied
  void (*kit_apply)(const uint8_t *header);                          // the kit built from a header (in
                                                                     // flash) that passed every check
  void (*ended)(bool kit);                                           // upload mode left, with a kit or not
};

// What an upload needs only while it's open, lent by the program for that time (run_begin): on the
// Teensy the upload loop's stack, which has the room, rather than RAM2, which hasn't
struct work_t {
  uint8_t header[HEADER_BYTES];       // image bytes 0 to 255, written to flash last
  uint8_t page[PAGE];                 // the sound's page being filled
};

struct slot_t {
  const io_t *io;
  uint32_t header_addr, data_base, capacity;   // the contract's KIT_HEADER, data_base and capacity
  uint8_t state, reason;                       // INFO's, when no upload is open
  // the stored kit, for INFO (all 0 with none)
  uint32_t size;
  uint32_t crc;            // the image as it was sent, data_base 0: worked out at boot, taken from BEGIN's at
  bool crc_known;          // COMMIT, and (should neither have) the first time INFO asks
  uint8_t base_kit;
  uint16_t filled;         // bit v: voice v has a hit
  uint8_t name[20];
  // a BEGIN come in, for the loop to open (run_begin)
  bool begin_waiting;
  uint8_t begin_message[14];
  // the upload
  bool open, muted;
  uint32_t up_size, up_crc;           // BEGIN's
  uint32_t next;                      // the offset of the next piece expected
  bool have_prev;                     // the previous piece, which may come again (its reply was lost)
  uint32_t prev_offset, prev_crc;
  uint32_t last_heard;                // now_us() at BEGIN or the last DATA heard (see heard)
  work_t *work;                       // the buffers lent to the open upload, null with none open
};

// ---- numbers ----------------------------------------------------------------------------------------

static inline uint32_t rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

// CRC-32 (zlib's): main.cpp's crc32_update on the Teensy, which user_kit.h names here; this one otherwise.
// Either can be carried on: crc32(crc32(0, a, n), b, m) is the CRC of a then b.
#ifdef USER_KIT_CRC32
static inline uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n) { return USER_KIT_CRC32(crc, p, n); }
#else
USER_KIT_COLD static uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
#endif

// A number as groups of 7 bits, least significant first: n2, n3 and n5 are 2, 3 and 5 groups
USER_KIT_COLD static void put_n(uint8_t *out, uint64_t v, uint8_t groups) {
  for (uint8_t g = 0; g < groups; g++) { out[g] = v & 0x7F; v >>= 7; }
}
// and back; false if a byte has its top bit set (no group can)
USER_KIT_COLD static bool get_n(const uint8_t *in, uint8_t groups, uint64_t &v) {
  v = 0;
  for (uint8_t g = 0; g < groups; g++) {
    if (in[g] & 0x80) return false;
    v |= (uint64_t)in[g] << (7 * g);
  }
  return true;
}

// 8-to-7 packing: the raw bytes in groups of 7 (the last may be shorter), a group of k becoming k + 1
// bytes, first one whose bit j is bit 7 of raw byte j, then the k with bit 7 cleared. 224 become 256.
static inline uint32_t packed_size(uint32_t raw) { return raw / 7 * 8 + (raw % 7 ? raw % 7 + 1 : 0); }
USER_KIT_COLD __attribute__((unused)) static uint32_t pack(const uint8_t *raw, uint32_t n, uint8_t *out) {
  uint32_t o = 0;
  for (uint32_t at = 0; at < n; at += 7) {
    uint32_t k = n - at < 7 ? n - at : 7;
    uint8_t high = 0;
    for (uint32_t j = 0; j < k; j++) high |= (raw[at + j] >> 7) << j;
    out[o++] = high;
    for (uint32_t j = 0; j < k; j++) out[o++] = raw[at + j] & 0x7F;
  }
  return o;
}
// packed bytes back to raw, n of them; false if the length can't be a packing (a group of one byte), a
// byte has bit 7 set, or a group's first byte has a bit for a byte the group doesn't have
USER_KIT_COLD static bool unpack(const uint8_t *in, uint32_t packed, uint8_t *raw, uint32_t &n) {
  n = 0;
  if (packed % 8 == 1) return false;
  for (uint32_t at = 0; at < packed; at += 8) {
    uint32_t k = (packed - at < 8 ? packed - at : 8) - 1;
    uint8_t high = in[at];
    if (high & 0x80 || high >> k) return false;
    for (uint32_t j = 0; j < k; j++) {
      uint8_t b = in[at + 1 + j];
      if (b & 0x80) return false;
      raw[n++] = b | ((high >> j) & 1) << 7;
    }
  }
  return true;
}

// ---- the image's checks -----------------------------------------------------------------------------

// check_image's steps 2 to 6, in the contract's order, on a header and the sound that follows it (in a
// file they're one piece, in the minichord's flash two), of an image of size bytes, against capacity and
// the data_base the header should hold (0 in a file or on the wire). The first that fails names the
// reason; 0 if none does. Step 1, the size, is the caller's, done before: check_image's own for a file,
// BEGIN's for an upload, and in flash the size is the header's own (256 + data_bytes), so it has nothing to
// say. Every step runs, so step 6 never reads further than the data_bytes step 5 has bounded.
USER_KIT_COLD static uint8_t check_parts(const uint8_t *h, const uint8_t *sound, uint64_t size, uint32_t capacity,
                                         uint32_t data_base) {
  if (memcmp(h, "MCK1", 4) != 0 || h[4] != 1 || h[5] != 0 || h[6] != VOICES || h[7] < 1 || h[7] > 12) return REASON_FORM;
  if (crc32(0, h, 252) != rd32(h + 252)) return REASON_HEADER_CRC;
  if (rd32(h + 8) != data_base) return REASON_DATA_BASE;
  const uint32_t data_bytes = rd32(h + 12);
  if (data_bytes != size - HEADER_BYTES || data_bytes % 4 || data_bytes > capacity) return REASON_RULE;
  if (rd32(h + 40) != 0) return REASON_RULE;   // reserved
  // the name: printable ASCII, then NULs to the end
  bool ended = false;
  for (uint8_t i = 20; i < 40; i++) {
    if (h[i] == 0) ended = true;
    else if (ended || h[i] < 0x20 || h[i] > 0x7E) return REASON_RULE;
  }
  for (uint8_t v = 0; v < VOICES; v++) {
    const uint8_t *e = h + 44 + 16 * v;
    const uint32_t offset = rd32(e), length = rd32(e + 4);
    if (length == 0) {   // empty: the base kit's own voice, and nothing else in the entry
      for (uint8_t i = 0; i < 16; i++) if (e[i]) return REASON_RULE;
      continue;
    }
    if (length < 2 || offset % 4 || (uint64_t)offset + 2 * (uint64_t)length > data_bytes) return REASON_RULE;
    if (rd16(e + 8) < 4000) return REASON_RULE;                    // the rate (to 65,535, all a uint16 holds)
    if ((int8_t)e[11] < -100 || (int8_t)e[11] > 100) return REASON_RULE;   // pan
    if (e[12] > 7) return REASON_RULE;                             // choke group
    if (e[13] & ~0x02) return REASON_RULE;                         // flags: only press bends, in version 1
  }
  if (crc32(0, sound, data_bytes) != rd32(h + 16)) return REASON_DATA_CRC;
  return REASON_NONE;
}

// An image in a file or on the wire (data_base 0, a file's capacity unless another is given)
USER_KIT_COLD __attribute__((unused)) static uint8_t check_image(const uint8_t *image, uint32_t size, uint32_t capacity = FILE_CAPACITY,
                                         uint32_t data_base = 0) {
  if (size < HEADER_BYTES || size % 4) return REASON_RULE;   // step 1 first: no header to look at
  return check_parts(image, image + HEADER_BYTES, size, capacity, data_base);
}

// ---- the slot ---------------------------------------------------------------------------------------

USER_KIT_COLD static void forget_summary(slot_t &s) {
  s.size = s.crc = 0;
  s.crc_known = false;
  s.base_kit = 0;
  s.filled = 0;
  memset(s.name, 0, sizeof s.name);
}

// The state of the stored kit, read from flash: disabled, empty (no kit's header there), damaged (a check
// fails, the reason kept) or valid, with what INFO tells of it. Nothing is applied here.
USER_KIT_COLD static void refresh(slot_t &s) {
  forget_summary(s);
  if (s.capacity < MIN_CAPACITY) { s.state = STATE_DISABLED; s.reason = REASON_ROOM; return; }
  const uint8_t *h = s.io->flash(s.header_addr);
  // a sector that doesn't start MCK1 holds no kit header, blank or not: an old filesystem's bytes (the
  // sector was LittleFS's before firmware 77) or a header write cut short aren't a damaged kit
  if (memcmp(h, "MCK1", 4) != 0) { s.state = STATE_EMPTY; s.reason = REASON_NO_HEADER; return; }
  // in flash the size is the header's own; step 5 checks data_bytes against the capacity before step 6
  // reads that far
  uint8_t why = check_parts(h, s.io->flash(s.data_base), HEADER_BYTES + (uint64_t)rd32(h + 12), s.capacity, s.data_base);
  if (why) { s.state = STATE_DAMAGED; s.reason = why; return; }
  s.state = STATE_VALID;
  s.reason = REASON_NONE;
  s.size = HEADER_BYTES + rd32(h + 12);
  s.base_kit = h[7];
  for (uint8_t v = 0; v < VOICES; v++) if (rd32(h + 44 + 16 * v + 4)) s.filled |= 1 << v;
  memcpy(s.name, h + 20, sizeof s.name);
}

// INFO's image CRC: the stored image as it was sent, data_base 0 (and so header_crc as it was then). init
// works it out at boot, right after the full check has read the same flash, and COMMIT knows it (BEGIN's,
// which the flash was checked against), so an INFO never runs a CRC over 256 kB inside the program's
// loop; worked out here only should neither have.
USER_KIT_COLD static uint32_t stored_crc(slot_t &s) {
  if (s.state != STATE_VALID) return 0;
  if (!s.crc_known) {
    uint8_t h[HEADER_BYTES];
    memcpy(h, s.io->flash(s.header_addr), sizeof h);
    wr32(h + 8, 0);
    wr32(h + 252, crc32(0, h, 252));
    s.crc = crc32(crc32(0, h, sizeof h), s.io->flash(s.data_base), s.size - HEADER_BYTES);
    s.crc_known = true;
  }
  return s.crc;
}

USER_KIT_COLD static void reply_status(slot_t &s, uint8_t op, uint8_t status) {
  uint8_t m[9] = {0xF0, 0x7D, 0x6B, (uint8_t)(0x40 | op), status};
  put_n(m + 5, s.open ? s.next : 0, 3);
  m[8] = 0xF7;
  s.io->send(m, sizeof m);
}

USER_KIT_COLD static void reply_info(slot_t &s) {
  uint8_t m[49] = {0xF0, 0x7D, 0x6B, 0x40 | OP_INFO, PROTO};
  m[5] = s.open ? (uint8_t)STATE_RECEIVING : s.state;
  m[6] = s.open ? (uint8_t)REASON_NONE : s.reason;
  put_n(m + 7, s.capacity, 3);
  put_n(m + 10, s.data_base, 5);
  bool kit = !s.open && s.state == STATE_VALID;
  put_n(m + 15, kit ? s.size : 0, 3);
  put_n(m + 18, kit ? stored_crc(s) : 0, 5);
  put_n(m + 23, PIECE, 2);
  m[25] = kit ? s.base_kit : 0;
  put_n(m + 26, kit ? s.filled : 0, 2);
  for (uint8_t i = 0; i < 20; i++) m[28 + i] = kit ? s.name[i] & 0x7F : 0;
  m[48] = 0xF7;
  s.io->send(m, sizeof m);
}

// Upload mode left, for whatever reason, through here only: the DAC back on, and the device told (it
// sends the drums' note-offs still waiting, empties the rest of its MIDI queue, drops any drum hit still
// queued and shows the LED). Without a kit the state is what the flash now says.
USER_KIT_COLD static void finish(slot_t &s, bool kit) {
  s.open = false;
  s.have_prev = false;
  s.work = nullptr;   // the buffers given back
  if (!kit) refresh(s);
  if (s.muted) { s.io->mute(false); s.muted = false; }
  s.io->ended(kit);
}

// The 256 bytes of the page buffer, or the first len, programmed at addr and read back
USER_KIT_COLD static bool program_checked(slot_t &s, uint32_t addr, const uint8_t *data, uint32_t len) {
  s.io->program(addr, data, len);
  return memcmp(s.io->flash(addr), data, len) == 0;
}

USER_KIT_COLD __attribute__((unused)) static void init(slot_t &s, const io_t *io, uint32_t header_addr, uint32_t data_base, uint32_t capacity) {
  memset(&s, 0, sizeof s);
  s.io = io;
  s.header_addr = header_addr;
  s.data_base = data_base;
  s.capacity = capacity;
  refresh(s);
  if (s.state == STATE_VALID) {
    stored_crc(s);   // INFO's CRC, now, at boot, straight after the full check read the same flash
    io->kit_apply(io->flash(header_addr));
  }
}

static inline bool is_open(const slot_t &s) { return s.open; }
static inline bool begin_waiting(const slot_t &s) { return s.begin_waiting; }

// BEGIN (a well formed one, 14 bytes): refused, in the contract's order, or (not busy) the upload opened
USER_KIT_COLD static void begin(slot_t &s, const uint8_t *m, bool busy, work_t *work) {
  uint64_t size, crc;
  if (!get_n(m + 5, 3, size) || !get_n(m + 8, 5, crc) || crc > 0xFFFFFFFFu) { reply_status(s, OP_BEGIN, STATUS_BAD_MESSAGE); return; }
  uint8_t status = STATUS_OK;
  if (m[4] != PROTO) status = STATUS_UNSUPPORTED;
  else if (s.begin_waiting) status = STATUS_BUSY;   // another BEGIN noted, still to open
  else if (s.capacity < MIN_CAPACITY) status = STATUS_DISABLED;
  else if (size < HEADER_BYTES || size - HEADER_BYTES > s.capacity || size % 4) status = STATUS_TOO_BIG;
  else if (busy || s.open) status = STATUS_BUSY;
  if (status) { reply_status(s, OP_BEGIN, status); return; }
  // muted as a preset save is, the kit given up before its flash changes, then its header gone
  s.io->mute(true);
  s.muted = true;
  s.io->kit_invalidate();
  forget_summary(s);
  s.state = STATE_EMPTY;
  s.reason = REASON_NO_HEADER;
  s.io->erase_sector(s.header_addr);
  s.open = true;
  s.work = work;
  s.up_size = (uint32_t)size;
  s.up_crc = (uint32_t)crc;
  s.next = 0;
  s.have_prev = false;
  memset(s.work->header, 0xFF, sizeof s.work->header);
  s.last_heard = s.io->now_us();
  reply_status(s, OP_BEGIN, STATUS_OK);
}

// The BEGIN receive() noted, run from the program's loop; busy: the rhythm or the looper is running. work
// is the upload's while it's open, and is_open() says when the program may have it back.
USER_KIT_COLD __attribute__((unused)) static void run_begin(slot_t &s, bool busy, work_t &work) {
  if (!s.begin_waiting) return;
  s.begin_waiting = false;
  begin(s, s.begin_message, busy, &work);
}

// The silence starts again: a DATA heard, whether taken, a repeat, out of order or with a bad CRC (a
// computer that is there, sending), but not one that couldn't be read (status 3), nor INFO or BEGIN, so an
// app that keeps asking INFO can't hold an abandoned upload, and the mute, open. COMMIT, ABORT and CLEAR
// end the upload, so they have nothing to start again.
static inline void heard(slot_t &s) { s.last_heard = s.io->now_us(); }

USER_KIT_COLD static void data(slot_t &s, const uint8_t *m, uint32_t len) {
  if (!s.open) { reply_status(s, OP_DATA, STATUS_NO_UPLOAD); return; }
  uint64_t offset, crc;
  if (len < 13 || !get_n(m + 4, 3, offset) || !get_n(m + 7, 5, crc) || crc > 0xFFFFFFFFu) {
    reply_status(s, OP_DATA, STATUS_BAD_MESSAGE);
    return;
  }
  if (offset != s.next || s.next >= s.up_size) {
    heard(s);
    // the previous piece again (its reply lost on the way): answered, not written again
    if (s.have_prev && offset == s.prev_offset && crc == s.prev_crc) reply_status(s, OP_DATA, STATUS_OK);
    else reply_status(s, OP_DATA, STATUS_OUT_OF_ORDER);
    return;
  }
  const uint32_t want = s.up_size - s.next < PIECE ? s.up_size - s.next : PIECE;
  const uint32_t packed = len - 13;
  uint8_t raw[PIECE];
  uint32_t n;
  if (packed != packed_size(want) || !unpack(m + 12, packed, raw, n) || n != want) {
    reply_status(s, OP_DATA, STATUS_BAD_MESSAGE);
    return;
  }
  heard(s);
  if (crc32(0, raw, n) != crc) { reply_status(s, OP_DATA, STATUS_PIECE_CRC); return; }
  uint8_t *header = s.work->header, *page = s.work->page;
  for (uint32_t j = 0; j < n; j++) {
    const uint32_t i = (uint32_t)offset + j;
    if (i < HEADER_BYTES) { header[i] = raw[j]; continue; }
    const uint32_t rel = i - HEADER_BYTES;
    // the first byte to reach a sector (pieces come in order) erases it, even one that reads all FF: an
    // erase cut short by a power cut can read FF and still not hold a page well
    if (rel % SECTOR == 0) s.io->erase_sector(s.data_base + rel);
    page[rel % PAGE] = raw[j];
    if (rel % PAGE == PAGE - 1 && !program_checked(s, s.data_base + rel - (PAGE - 1), page, PAGE)) {
      finish(s, false);
      reply_status(s, OP_DATA, STATUS_READ_BACK);
      return;
    }
  }
  s.have_prev = true;
  s.prev_offset = (uint32_t)offset;
  s.prev_crc = (uint32_t)crc;
  s.next += n;
  reply_status(s, OP_DATA, STATUS_OK);
}

// The upload ended with no kit by a check at COMMIT: the header, if it had been written, erased again
USER_KIT_COLD static void commit_failed(slot_t &s, uint8_t status, bool header_written) {
  if (header_written) s.io->erase_sector(s.header_addr);
  finish(s, false);
  reply_status(s, OP_COMMIT, status);
}

USER_KIT_COLD static void commit(slot_t &s) {
  if (!s.open) { reply_status(s, OP_COMMIT, STATUS_NO_UPLOAD); return; }
  uint8_t *header = s.work->header;
  // the last page, as far as it was filled
  if (s.next > HEADER_BYTES && (s.next - HEADER_BYTES) % PAGE) {
    const uint32_t fill = (s.next - HEADER_BYTES) % PAGE;
    if (!program_checked(s, s.data_base + (s.next - HEADER_BYTES) - fill, s.work->page, fill)) { commit_failed(s, STATUS_READ_BACK, false); return; }
  }
  if (s.next != s.up_size) { commit_failed(s, STATUS_BAD_MESSAGE, false); return; }
  const uint8_t *sound = s.io->flash(s.data_base);
  if (crc32(crc32(0, header, HEADER_BYTES), sound, s.up_size - HEADER_BYTES) != s.up_crc) {
    commit_failed(s, STATUS_IMAGE_CRC, false);
    return;
  }
  // the image as it was sent, header and sound, through all of check_image: data_base 0 on the wire
  if (check_parts(header, sound, s.up_size, s.capacity, 0)) { commit_failed(s, STATUS_HEADER, false); return; }
  // the minichord's own copy: where its sound starts, and the header's CRC over that
  wr32(header + 8, s.data_base);
  wr32(header + 252, crc32(0, header, 252));
  if (!program_checked(s, s.header_addr, header, HEADER_BYTES)) { commit_failed(s, STATUS_READ_BACK, true); return; }
  refresh(s);
  if (s.state != STATE_VALID) { commit_failed(s, STATUS_HEADER, true); return; }
  s.crc = s.up_crc;   // INFO's: the flash was just checked against it
  s.crc_known = true;
  s.io->kit_apply(s.io->flash(s.header_addr));
  finish(s, true);
  reply_status(s, OP_COMMIT, STATUS_OK);
  reply_info(s);
}

// CLEAR (a well formed one, the slot not disabled): a BEGIN waiting to open called off, any upload ended,
// and the header erased
USER_KIT_COLD static void clear(slot_t &s) {
  s.begin_waiting = false;
  bool was_open = s.open;
  if (!was_open) { s.io->mute(true); s.muted = true; s.io->kit_invalidate(); }
  s.io->erase_sector(s.header_addr);
  if (was_open) finish(s, false);   // unmuted there, the state read from the erased header sector
  else {
    refresh(s);
    s.io->mute(false);
    s.muted = false;
  }
  reply_status(s, OP_CLEAR, STATUS_OK);
}

// Every SysEx that comes in, whole (F0 to F7). True if it was a kit message (F0 7D 6B ...), answered or
// noted; false for anything else, which the caller deals with as before (or, in the upload loop, drops).
USER_KIT_COLD __attribute__((unused)) static bool receive(slot_t &s, const uint8_t *m, uint32_t len) {
  if (len < 4 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 0x6B) return false;
  const uint8_t op = m[3];
  // a reply's shape (or an F7 where the op should be): never answered, so two minichords, or an app that
  // echoes, can't set each other off; any other op not known is answered 3
  if (op >= 0x40) return true;
  // not framed (under 7 bytes, no closing F7, a data byte over 7F): 3 whatever the op below 0x40, before
  // any op's own rule, so a COMMIT, ABORT or DATA that isn't framed is 3 even with no upload open, not 5
  bool formed = len >= 7 && m[len - 1] == 0xF7;
  for (uint32_t i = 1; formed && i < len - 1; i++) formed = !(m[i] & 0x80);
  if (!formed) { reply_status(s, op, STATUS_BAD_MESSAGE); return true; }
  switch (op) {
    case OP_BEGIN:
      if (len != 14) reply_status(s, op, STATUS_BAD_MESSAGE);
      // refused: one is open already, or another is noted and waiting to open (or for a reason before that)
      else if (s.open || s.begin_waiting) begin(s, m, true, nullptr);
      else { memcpy(s.begin_message, m, 14); s.begin_waiting = true; }
      break;
    case OP_DATA:   // 5 with no upload open (a BEGIN still waiting too), before its shape: see data()
      data(s, m, len);
      break;
    // COMMIT and ABORT: 5 with no upload, before their shape. A BEGIN still waiting to open has no upload for
    // COMMIT, and an ABORT calls it off: it never opens, and the minichord's kit is left as it is.
    case OP_COMMIT:
      if (!s.open) reply_status(s, op, STATUS_NO_UPLOAD);
      else if (len != 7 || m[4] || m[5]) reply_status(s, op, STATUS_BAD_MESSAGE);
      else commit(s);
      break;
    case OP_ABORT:
      if (!s.open && !s.begin_waiting) reply_status(s, op, STATUS_NO_UPLOAD);
      else if (len != 7 || m[4] || m[5]) reply_status(s, op, STATUS_BAD_MESSAGE);
      else if (!s.open) { s.begin_waiting = false; reply_status(s, op, STATUS_OK); }
      else { finish(s, false); reply_status(s, op, STATUS_OK); }
      break;
    case OP_INFO:
      if (len != 7) reply_status(s, op, STATUS_BAD_MESSAGE);
      else reply_info(s);
      break;
    case OP_CLEAR:
      if (len != 8 || m[4] != 0 || m[5] != 0x43 || m[6] != 0x4C) reply_status(s, op, STATUS_BAD_MESSAGE);
      // no slot, so no kit to erase; and a firmware that left too little room might reach the header's sector
      else if (s.capacity < MIN_CAPACITY) reply_status(s, op, STATUS_DISABLED);
      else clear(s);
      break;
    default:
      reply_status(s, op, STATUS_BAD_MESSAGE);
  }
  return true;
}

// Between messages in the upload loop: 5 s with no DATA heard (see heard) ends the upload, with no kit and
// no reply
USER_KIT_COLD __attribute__((unused)) static void poll(slot_t &s) {
  if (s.open && s.io->now_us() - s.last_heard >= SILENCE_US) finish(s, false);
}

}  // namespace user_kit
