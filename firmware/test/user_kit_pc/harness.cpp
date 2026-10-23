// The user drum kit's slot (include/user_kit_core.h), run on a computer against a fake Teensy: a flash
// that behaves as the real one does (an erase sets a 4 kB sector to FF, a program can only clear bits and
// writes at most 256 bytes inside one page, reads go through a pointer and stop at the slot's edges), a
// clock the test sets, and the DAC's mute, the drum machine's kit and the end of upload mode (with the
// flash reads made by then) recorded so the test can see them. The contract is generator/user_kit.md:
// every reply is checked byte for byte against what it says, built here from the contract with this
// file's own CRC, numbers and packing, not the core's.
//
//   harness <test/kits folder> <voices.txt from expect.py> <include/drum_patterns.h>
//
// Five groups, as the README lists them: the synthetic kit's upload, a power cut at every flash
// operation of it, the broken images sent as uploads and placed in flash, the protocol's edge cases (a
// BEGIN waiting to open among them), and a few sizes of kit (empty, a page, a sector, the whole slot) at
// three data_bases; and the kit standing in for kit 13 found by name in drum_patterns.h. Each check that fails prints a line; the exit status is 1 if any did.
#include "user_kit_core.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using bytes = std::vector<uint8_t>;

// ---- checks ---------------------------------------------------------------------------------------

static long checks = 0, failures = 0;
static std::string context;   // the test running, for the failure lines
#define CHECK(cond, ...)                                                                   \
  do {                                                                                     \
    checks++;                                                                              \
    if (!(cond)) {                                                                         \
      failures++;                                                                          \
      if (failures <= 200) {                                                               \
        fprintf(stderr, "FAIL line %d [%s] %s: ", __LINE__, context.c_str(), #cond);       \
        fprintf(stderr, __VA_ARGS__);                                                      \
        fputc('\n', stderr);                                                               \
      }                                                                                    \
    }                                                                                      \
  } while (0)

static std::string hex(const bytes &b) {
  std::string s;
  char t[3];
  for (uint8_t x : b) { snprintf(t, sizeof t, "%02x", x); s += t; }
  return s;
}
static bytes unhex(const std::string &h) {
  bytes b;
  for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back((uint8_t)strtoul(h.substr(i, 2).c_str(), nullptr, 16));
  return b;
}
static bytes read_file(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { fprintf(stderr, "can't read %s\n", path.c_str()); exit(2); }
  return bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ---- the contract, written out again here (not the core's code) ------------------------------------

static uint32_t ref_crc(const uint8_t *p, size_t n, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool made = false;
  if (!made) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    made = true;
  }
  crc = ~crc;
  while (n--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
static uint32_t ref_crc(const bytes &b) { return ref_crc(b.data(), b.size()); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void set32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }
static void put_n(bytes &m, uint64_t v, int groups) { for (int g = 0; g < groups; g++) { m.push_back(v & 0x7F); v >>= 7; } }
static uint64_t get_n(const uint8_t *p, int groups) {
  uint64_t v = 0;
  for (int g = 0; g < groups; g++) v |= (uint64_t)p[g] << (7 * g);
  return v;
}
static bytes pack87(const uint8_t *raw, size_t n) {
  bytes out;
  for (size_t at = 0; at < n; at += 7) {
    size_t k = n - at < 7 ? n - at : 7;
    uint8_t high = 0;
    for (size_t j = 0; j < k; j++) if (raw[at + j] & 0x80) high |= 1 << j;
    out.push_back(high);
    for (size_t j = 0; j < k; j++) out.push_back(raw[at + j] & 0x7F);
  }
  return out;
}

static const bytes HEAD = {0xF0, 0x7D, 0x6B};
static bytes msg(uint8_t op, const bytes &body) {
  bytes m = HEAD;
  m.push_back(op);
  m.insert(m.end(), body.begin(), body.end());
  m.push_back(0xF7);
  return m;
}
static bytes begin_msg(uint64_t size, uint64_t crc, uint8_t proto = 1) {
  bytes b = {proto};
  put_n(b, size, 3);
  put_n(b, crc, 5);
  return msg(0x01, b);
}
static const uint32_t PIECE = 224;
static uint32_t piece_len(const bytes &image, uint32_t offset) { return image.size() - offset < PIECE ? image.size() - offset : PIECE; }
static bytes data_msg(const bytes &image, uint32_t offset, uint32_t crc_xor = 0) {
  const uint32_t len = piece_len(image, offset);
  bytes b;
  put_n(b, offset, 3);
  put_n(b, ref_crc(image.data() + offset, len) ^ crc_xor, 5);
  bytes p = pack87(image.data() + offset, len);
  b.insert(b.end(), p.begin(), p.end());
  return msg(0x02, b);
}
static const bytes COMMIT = msg(0x03, {0, 0}), ABORT = msg(0x04, {0, 0}), INFO = msg(0x05, {1, 0}),
                   CLEAR = msg(0x06, {0, 0x43, 0x4C});
static std::vector<bytes> make_frames(const bytes &image) {
  std::vector<bytes> f = {begin_msg(image.size(), ref_crc(image))};
  for (uint32_t at = 0; at < image.size(); at += PIECE) f.push_back(data_msg(image, at));
  f.push_back(COMMIT);
  return f;
}

// the replies, as section 3 has them
static bytes status_reply(uint8_t op, uint8_t status, uint32_t next) {
  bytes b = {status};
  put_n(b, next, 3);
  return msg(0x40 | op, b);
}
struct info_t {
  uint8_t state, reason;
  uint32_t capacity, data_base, size, crc;
  uint8_t base_kit;
  uint16_t filled;
  std::string name;
};
static bytes info_reply(const info_t &i) {
  bytes b = {1, i.state, i.reason};
  put_n(b, i.capacity, 3);
  put_n(b, i.data_base, 5);
  put_n(b, i.size, 3);
  put_n(b, i.crc, 5);
  put_n(b, PIECE, 2);
  b.push_back(i.base_kit);
  put_n(b, i.filled, 2);
  for (size_t k = 0; k < 20; k++) b.push_back(k < i.name.size() ? i.name[k] & 0x7F : 0);
  bytes m = msg(0x45, b);
  if (m.size() != 49) { fprintf(stderr, "INFO reply built %zu bytes\n", m.size()); exit(2); }
  return m;
}

// ---- the fake Teensy ------------------------------------------------------------------------------

static const uint32_t KIT_HEADER = 0x601E0000, KIT_TOP = 0x601E0000, KIT_FLOOR = 0x601A0000;
static const uint32_t FULL = KIT_TOP - KIT_FLOOR;   // 262,144

// The fake flash: the slot's sound area, KIT_FLOOR to KIT_TOP, and the header's sector, each an allocation
// of its own and exactly its size, so a read that runs off the sound's end (on the Teensy, into the
// header) or off the header's sector is a heap overflow, which ASan stops at
struct flash_t {
  bytes sound = bytes(FULL, 0xFF), header = bytes(4096, 0xFF);
  bool operator==(const flash_t &o) const { return sound == o.sound && header == o.header; }
  void fill(uint8_t b) {
    std::fill(sound.begin(), sound.end(), b);
    std::fill(header.begin(), header.end(), b);
  }
  // the byte at a processor address, for the test's own looks and writes: anywhere in either part
  uint8_t *at(uint32_t addr) { return addr >= KIT_HEADER ? &header[addr - KIT_HEADER] : &sound[addr - KIT_FLOOR]; }
};

enum cut_kind_t { CUT_BEFORE, CUT_TORN, CUT_AFTER };   // the power goes before the operation, halfway, or just after
static const char *cut_names[] = {"before", "torn", "after"};
struct power_cut {};

struct voice_seen_t {   // what the kit build was handed, voice by voice
  bool filled;
  uint32_t addr, length, rate;
  uint8_t level;
  int8_t pan;
  uint8_t choke, flags;
  uint16_t pitch;
};

struct device_t {
  flash_t mem;
  uint32_t now = 0;
  long ops = 0, erases = 0, programs = 0;   // flash operations, since reset_counts
  long header_programs = 0;                 // of them, programs into the header's sector
  long reads = 0;                           // flash(): looks at the flash, never reset
  long cut_at = -1;                         // the operation the power goes at
  cut_kind_t cut_kind = CUT_BEFORE;
  bool corrupt_next_program = false;        // a page that doesn't read back
  std::vector<bytes> sent;
  bool muted = false;
  long mutes = 0, unmutes = 0;
  bool kit_live = false;   // a kit built and not given up since
  long invalidates = 0, applies = 0, ends = 0;
  bool ended_kit = false;
  long reads_at_end = 0;   // reads as upload mode was last left
  uint32_t data_base = KIT_FLOOR;   // the slot's, for the kit build
  uint8_t kit_base = 0;
  voice_seen_t kit[13];
} D;

static void reset_counts() {
  D.ops = D.erases = D.programs = D.header_programs = 0;
  D.mutes = D.unmutes = D.invalidates = D.applies = D.ends = 0;
  D.cut_at = -1;
  D.sent.clear();
}
// the power back on: nothing of the device survives but the flash
static void power_cycle() {
  D.muted = false;
  D.kit_live = false;
  D.corrupt_next_program = false;
  D.sent.clear();
  D.cut_at = -1;
}

// What the slot may touch: its own sound area, data_base up to KIT_TOP (below data_base is the firmware's
// on the Teensy), and the header's sector; n bytes from addr, all inside one of them
static bool in_slot(uint32_t addr, uint32_t n) {
  if (addr >= KIT_HEADER) return addr + n <= KIT_HEADER + 4096;
  return addr >= D.data_base && addr + n <= KIT_TOP;
}
// a read: the pointer handed back, how far the slot reads from it is the allocations' to stop (flash_t)
static const uint8_t *f_flash(uint32_t addr) {
  D.reads++;
  if (!in_slot(addr, 1)) { fprintf(stderr, "flash read at %08X, outside the slot (data_base %08X)\n", addr, D.data_base); abort(); }
  return D.mem.at(addr);
}
static void f_erase(uint32_t addr) {
  CHECK(addr % 4096 == 0 && in_slot(addr, 4096), "erase at %08X", addr);
  if (addr % 4096 || !in_slot(addr, 4096)) return;
  CHECK(D.muted, "erase at %08X with the DAC on", addr);
  CHECK(!D.kit_live, "erase at %08X while a kit plays from flash", addr);
  const long n = D.ops++;
  D.erases++;
  const bool cut = n == D.cut_at;
  if (cut && D.cut_kind == CUT_BEFORE) throw power_cut();
  memset(D.mem.at(addr), 0xFF, cut && D.cut_kind == CUT_TORN ? 2048 : 4096);
  if (cut) throw power_cut();
}
static void f_program(uint32_t addr, const uint8_t *data, uint32_t len) {
  CHECK(len >= 1 && len <= 256 && addr % 256 + len <= 256, "program of %u bytes at %08X crosses a page", len, addr);
  CHECK(in_slot(addr, len), "program at %08X outside the slot", addr);
  if (len < 1 || len > 256 || addr % 256 + len > 256 || !in_slot(addr, len)) return;
  CHECK(D.muted, "program at %08X with the DAC on", addr);
  CHECK(!D.kit_live, "program at %08X while a kit plays from flash", addr);
  const long n = D.ops++;
  D.programs++;
  if (addr >= KIT_HEADER) D.header_programs++;
  const bool cut = n == D.cut_at;
  if (cut && D.cut_kind == CUT_BEFORE) throw power_cut();
  uint8_t *p = D.mem.at(addr);
  const uint32_t upto = cut && D.cut_kind == CUT_TORN ? len / 2 : len;
  bool sets = false;
  for (uint32_t i = 0; i < upto; i++) {
    if (data[i] & ~p[i]) sets = true;
    p[i] &= data[i];   // a program can only clear bits
  }
  CHECK(!sets, "program at %08X over bytes that weren't erased", addr);
  if (D.corrupt_next_program) { p[len / 2] ^= 0x10; D.corrupt_next_program = false; }
  if (cut) throw power_cut();
}
static void f_send(const uint8_t *m, uint32_t len) { D.sent.emplace_back(m, m + len); }
static uint32_t f_now() { return D.now; }
static void f_mute(bool on) {
  if (on) { CHECK(!D.muted, "muted twice"); D.muted = true; D.mutes++; }
  else { CHECK(D.muted, "unmuted without a mute"); D.muted = false; D.unmutes++; }
}
static void f_invalidate() { D.kit_live = false; D.invalidates++; }
static void f_apply(const uint8_t *h) {
  CHECK(h == f_flash(KIT_HEADER), "the kit built from a header that isn't the one in flash");
  D.kit_base = h[7];
  for (int v = 0; v < 13; v++) {
    const uint8_t *e = h + 44 + 16 * v;
    voice_seen_t &k = D.kit[v];
    k.length = le32(e + 4);
    k.filled = k.length != 0;
    k.addr = D.data_base + le32(e);
    k.rate = e[8] | e[9] << 8;
    k.level = e[10];
    k.pan = (int8_t)e[11];
    k.choke = e[12];
    k.flags = e[13];
    k.pitch = e[14] | e[15] << 8;
  }
  D.kit_live = true;
  D.applies++;
}
static void f_ended(bool kit) {
  CHECK(!D.muted, "upload mode left with the DAC still muted");
  CHECK(kit == D.kit_live, "ended(%d) with a kit %s", kit, D.kit_live ? "built" : "not built");
  D.ends++;
  D.ended_kit = kit;
  D.reads_at_end = D.reads;
}
static const user_kit::io_t io = {f_flash, f_erase, f_program, f_send, f_now, f_mute, f_invalidate, f_apply, f_ended};

// The minichord: the slot, booted with a data_base (the capacity follows from it as section 2 has it),
// fed whole SysEx messages as processMIDI and drums_update feed them. The upload's buffers are lent from
// here, as the Teensy's upload loop lends them from its stack; they're filled with junk at each BEGIN, so
// the core can't count on anything left in them from before. Deferred, a BEGIN is left noted after the
// message, as the Teensy leaves it until drums_update's next pass, and more messages can come in first:
// pass() is that next pass.
struct board_t {
  user_kit::slot_t s;
  user_kit::work_t w;
  uint32_t data_base = KIT_FLOOR, capacity = FULL;
  bool deferred = false;
  void boot(uint32_t base = KIT_FLOOR) {
    data_base = base;
    capacity = base < KIT_TOP ? KIT_TOP - base : 0;
    D.data_base = base;
    user_kit::init(s, &io, KIT_HEADER, data_base, capacity);
  }
  void send(const bytes &m, bool busy = false) {
    const long ends = D.ends;
    CHECK(user_kit::receive(s, m.data(), m.size()), "a kit message not taken as one: %s", hex(m).substr(0, 40).c_str());
    // a COMMIT that stored a kit reads no flash from ended(kit) to the end of its handling: the INFO it sends
    // last takes its CRC from BEGIN's, and doesn't work it out over the whole sound in the program's loop
    if (D.ends != ends && D.ended_kit)
      CHECK(D.reads == D.reads_at_end, "COMMIT read the flash %ld times after ended(kit): INFO's CRC wasn't taken from BEGIN's",
            D.reads - D.reads_at_end);
    pass(busy, !deferred);
  }
  // a pass of the program's loop: a BEGIN noted run (unless it's left waiting), and the silence polled
  void pass(bool busy = false, bool run_begin = true) {
    if (run_begin && user_kit::begin_waiting(s)) {
      memset(&w, 0xA5, sizeof w);
      user_kit::run_begin(s, busy, w);
    }
    CHECK(!user_kit::is_open(s) || s.work == &w, "an open upload without the buffers it was lent");
    CHECK(user_kit::is_open(s) || s.work == nullptr, "the buffers still held with no upload open");
    user_kit::poll(s);
  }
  bool open() const { return user_kit::is_open(s); }
};
static board_t B;

static info_t no_kit(uint8_t state, uint8_t reason) { return {state, reason, B.capacity, B.data_base, 0, 0, 0, 0, ""}; }

// the replies since the last look, checked against what should have come and forgotten
static void expect(const std::vector<bytes> &want, const char *what) {
  bool same = D.sent == want;
  std::string got, wanted;
  if (!same) {
    for (auto &m : D.sent) got += hex(m) + " ";
    for (auto &m : want) wanted += hex(m) + " ";
  }
  CHECK(same, "%s: replies %s, the contract says %s", what, got.c_str(), wanted.c_str());
  D.sent.clear();
}
static void expect_status(uint8_t op, uint8_t status, uint32_t next, const char *what) { expect({status_reply(op, status, next)}, what); }
static info_t ask_info() {   // INFO asked, its reply parsed (and checked for shape)
  D.sent.clear();
  B.send(INFO);
  info_t i{};
  CHECK(D.sent.size() == 1 && D.sent[0].size() == 49, "INFO: %zu replies", D.sent.size());
  if (D.sent.size() != 1 || D.sent[0].size() != 49) { D.sent.clear(); return i; }
  const uint8_t *m = D.sent[0].data();
  i.state = m[5];
  i.reason = m[6];
  i.capacity = get_n(m + 7, 3);
  i.data_base = get_n(m + 10, 5);
  i.size = get_n(m + 15, 3);
  i.crc = get_n(m + 18, 5);
  i.base_kit = m[25];
  i.filled = get_n(m + 26, 2);
  i.name = std::string((const char *)m + 28, strnlen((const char *)m + 28, 20));
  D.sent.clear();
  return i;
}
static bool no_kit_state(uint8_t state) { return state == user_kit::STATE_EMPTY || state == user_kit::STATE_DAMAGED; }

// An upload sent whole; true if every reply was status 0 (the replies left in D.sent)
static bool upload(const bytes &image) {
  bool ok = true;
  for (auto &m : make_frames(image)) {
    D.sent.clear();
    B.send(m);
    ok = ok && !D.sent.empty() && D.sent[0][4] == 0;
  }
  return ok;
}

// The header the minichord keeps: the image's with its data_base, and header_crc over that
static bytes stored_header(const bytes &image, uint32_t data_base) {
  bytes h(image.begin(), image.begin() + 256);
  set32(&h[8], data_base);
  set32(&h[252], ref_crc(h.data(), 252));
  return h;
}
// The flash holds this image, as the minichord stores it
static bool flash_holds(const bytes &image, uint32_t data_base) {
  bytes h = stored_header(image, data_base);
  return memcmp(f_flash(KIT_HEADER), h.data(), 256) == 0 &&
         memcmp(f_flash(data_base), image.data() + 256, image.size() - 256) == 0;
}

// ---- what the synthetic kit should build ----------------------------------------------------------

struct voice_want_t {
  bool filled;
  uint32_t offset, length, rate, level, choke, flags, pitch, samples_crc;
  int pan;
};
static voice_want_t want_voices[13];
static int want_base = 0;
static std::string want_name;

static void read_voices(const std::string &path) {
  std::ifstream f(path);
  std::string line;
  int seen = 0;
  while (std::getline(f, line)) {
    std::istringstream in(line);
    std::string first;
    in >> first;
    if (first == "kit") {
      in >> want_base >> want_name;
      for (char &c : want_name) if (c == '_') c = ' ';
      continue;
    }
    int v = atoi(first.c_str());
    voice_want_t &w = want_voices[v];
    std::string second;
    in >> second;
    if (second == "empty") { w = {}; seen++; continue; }
    w.filled = true;
    w.offset = strtoul(second.c_str(), nullptr, 10);
    in >> w.length >> w.rate >> w.level >> w.pan >> w.choke >> w.flags >> w.pitch >> w.samples_crc;
    seen++;
  }
  if (seen != 13 || !want_base) { fprintf(stderr, "%s: %d voices, base kit %d\n", path.c_str(), seen, want_base); exit(2); }
}

// the kit build was handed the synthetic kit, and each hit in flash is that voice's samples
static void check_kit_built() {
  CHECK(D.kit_live, "no kit built");
  CHECK(D.kit_base == want_base, "base kit %d, want %d", D.kit_base, want_base);
  for (int v = 0; v < 13; v++) {
    const voice_seen_t &k = D.kit[v];
    const voice_want_t &w = want_voices[v];
    CHECK(k.filled == w.filled, "voice %d filled %d, want %d", v, k.filled, w.filled);
    if (!w.filled || !k.filled) continue;
    CHECK(k.addr == D.data_base + w.offset, "voice %d at %08X, want %08X", v, k.addr, D.data_base + w.offset);
    CHECK(k.length == w.length && k.rate == w.rate && k.level == w.level && k.pan == w.pan && k.choke == w.choke &&
              k.flags == w.flags && k.pitch == w.pitch,
          "voice %d: length %u rate %u level %u pan %d choke %u flags %u pitch %u, want %u %u %u %d %u %u %u", v, k.length, k.rate,
          k.level, k.pan, k.choke, k.flags, k.pitch, w.length, w.rate, w.level, w.pan, w.choke, w.flags, w.pitch);
    CHECK(ref_crc(f_flash(k.addr), 2 * k.length) == w.samples_crc, "voice %d: the samples in flash aren't synthetic.json's", v);
  }
}

// ---- 1. the synthetic kit's upload ----------------------------------------------------------------

static bytes synthetic, small;
static std::vector<bytes> synthetic_frames;
static info_t synthetic_info;

static void test_synthetic() {
  context = "synthetic";
  D.mem.fill(0xFF);
  power_cycle();
  reset_counts();
  B.boot();
  CHECK(B.s.state == 0 && B.s.reason == 1 && D.applies == 0, "blank flash boots state %d reason %d", B.s.state, B.s.reason);
  B.send(INFO);
  expect({info_reply(no_kit(0, 1))}, "INFO on blank flash");

  // the vector's frames are the contract's framing (this file's own) of the vector's image
  CHECK(user_kit::check_image(synthetic.data(), synthetic.size()) == 0, "core check_image on synthetic.image");
  CHECK(user_kit::check_image(small.data(), small.size()) == 0, "core check_image on small.image");
  CHECK(ref_crc(synthetic) == 0x19A640C5, "synthetic.image's CRC %08X", ref_crc(synthetic));
  std::vector<bytes> mine = make_frames(synthetic);
  CHECK(mine == synthetic_frames, "synthetic.frames isn't this harness's framing of synthetic.image (%zu and %zu lines)",
        synthetic_frames.size(), mine.size());

  B.send(synthetic_frames[0]);
  expect_status(1, 0, 0, "BEGIN");
  CHECK(B.open() && D.muted && D.invalidates == 1 && D.erases == 1, "after BEGIN: open %d muted %d invalidates %ld erases %ld", B.open(),
        D.muted, D.invalidates, D.erases);
  uint32_t offset = 0;
  for (size_t i = 1; i + 1 < synthetic_frames.size(); i++) {
    B.send(synthetic_frames[i]);
    offset += piece_len(synthetic, offset);
    expect_status(2, 0, offset, ("DATA " + std::to_string(i)).c_str());
    if (i == 100) {   // INFO during an upload: receiving, nothing of a kit
      B.send(INFO);
      expect({info_reply(no_kit(2, 0))}, "INFO during the upload");
    }
  }
  CHECK(offset == synthetic.size(), "the frames covered %u of %zu bytes", offset, synthetic.size());
  synthetic_info = {1, 0, FULL, KIT_FLOOR, (uint32_t)synthetic.size(), 0x19A640C5, (uint8_t)want_base, 4091, want_name};
  B.send(synthetic_frames.back());
  expect({status_reply(3, 0, 0), info_reply(synthetic_info)}, "COMMIT");
  CHECK(!B.open() && !D.muted && D.mutes == 1 && D.unmutes == 1 && D.ends == 1 && D.ended_kit, "after COMMIT: open %d muted %d ends %ld",
        B.open(), D.muted, D.ends);
  CHECK(flash_holds(synthetic, KIT_FLOOR), "the flash doesn't hold the image as sent, data_base filled in");
  // the header sector erased, then every sound sector as the sound reaches it, though the flash is blank (an
  // erase cut short can read FF); a page programmed for each 256 bytes of sound, and the header
  CHECK(D.erases == 1 + (long)((synthetic.size() - 256 + 4095) / 4096) && D.programs == (long)((synthetic.size() - 256 + 255) / 256) + 1,
        "erases %ld programs %ld", D.erases, D.programs);
  CHECK(D.applies == 1, "kit built %ld times", D.applies);
  check_kit_built();

  // the boot check on the flash as it is
  context = "synthetic reboot";
  power_cycle();
  reset_counts();
  B.boot();
  CHECK(B.s.state == 1 && D.applies == 1, "reboot: state %d reason %d, built %ld", B.s.state, B.s.reason, D.applies);
  check_kit_built();
  B.send(INFO);
  expect({info_reply(synthetic_info)}, "INFO after reboot");

  // the same kit again over the stored one: every sound sector already written is erased first
  context = "synthetic over a stored kit";
  reset_counts();
  CHECK(upload(synthetic), "upload over the stored kit");
  expect({status_reply(3, 0, 0), info_reply(synthetic_info)}, "COMMIT over the stored kit");
  CHECK(D.erases == 1 + (long)((synthetic.size() - 256 + 4095) / 4096), "erases %ld", D.erases);
  CHECK(flash_holds(synthetic, KIT_FLOOR), "flash after the second upload");
}

// ---- 2. a power cut at every flash operation -----------------------------------------------------

// From a flash as start holds it, the synthetic upload with the power cut at each erase and program in
// turn (before it, halfway through it, just after it), then the boot check: no kit, or the whole new one,
// never part of it (and the old kit only if the power went before anything was erased). Then an upload
// on that flash must go through.
static long cut_runs = 0;
static void cut_suite(const char *name, const flash_t &start, const bytes *old_image) {
  context = std::string("power cut, ") + name;
  D.mem = start;
  power_cycle();
  reset_counts();
  B.boot();
  CHECK(upload(synthetic), "the uncut upload");
  const long total = D.ops;
  CHECK(total > 2, "only %ld flash operations", total);
  long none = 0, whole = 0, old = 0;
  for (long k = 0; k < total; k++) {
    for (int kind = CUT_BEFORE; kind <= CUT_AFTER; kind++) {
      context = std::string("power cut, ") + name + ", op " + std::to_string(k) + " " + cut_names[kind];
      D.mem = start;
      power_cycle();
      reset_counts();
      B.boot();
      reset_counts();
      D.cut_at = k;
      D.cut_kind = (cut_kind_t)kind;
      bool cut = false;
      try {
        for (auto &m : synthetic_frames) B.send(m);
      } catch (power_cut &) {
        cut = true;
      }
      CHECK(cut, "the power cut never came");
      power_cycle();
      reset_counts();
      B.boot();
      cut_runs++;
      if (no_kit_state(B.s.state)) {
        none++;
        CHECK(D.applies == 0 && !D.kit_live, "state %d but a kit was built", B.s.state);
        info_t i = ask_info();
        CHECK(i.size == 0 && i.crc == 0 && i.filled == 0 && i.base_kit == 0, "no kit, but INFO says size %u", i.size);
      } else if (B.s.state == 1 && flash_holds(synthetic, KIT_FLOOR)) {
        whole++;
        check_kit_built();
        B.send(INFO);
        expect({info_reply(synthetic_info)}, "INFO, the new kit after the cut");
      } else if (B.s.state == 1 && old_image && flash_holds(*old_image, KIT_FLOOR)) {
        old++;
        CHECK(k == 0 && kind == CUT_BEFORE, "the old kit is still there after op %ld", k);
      } else {
        CHECK(false, "boot after the cut: state %d reason %d, and not the whole new kit", B.s.state, B.s.reason);
      }
      // and the next upload goes through
      reset_counts();
      bool ok = upload(synthetic);
      CHECK(ok, "the upload after the cut");
      expect({status_reply(3, 0, 0), info_reply(synthetic_info)}, "COMMIT after the cut");
      CHECK(flash_holds(synthetic, KIT_FLOOR) && !D.muted, "flash after the upload after the cut");
    }
  }
  printf("  power cut, %s: %ld flash operations x 3 ways: %ld no kit, %ld the whole new kit, %ld the old kit\n", name, total, none,
         whole, old);
}

static void test_power_cuts() {
  const flash_t blank;
  cut_suite("blank flash", blank, nullptr);
  // the flash with small.image stored, and the rest of the slot's sound area written (as an old, bigger kit
  // would leave it), so the sectors need erasing
  D.mem = blank;
  power_cycle();
  B.boot();
  for (uint32_t i = 0; i < FULL; i++) D.mem.sound[i] = (uint8_t)(i * 131 + 7);
  upload(small);
  CHECK(B.s.state == 1 && flash_holds(small, KIT_FLOOR), "small.image stored");
  const flash_t with_small = D.mem;
  cut_suite("small.image stored, the sound area written", with_small, &small);
}

// ---- 3. the broken images -------------------------------------------------------------------------

static std::map<std::string, int> read_expected(const std::string &path) {
  std::ifstream f(path);
  std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::map<std::string, int> out;
  size_t at = 0;
  while ((at = all.find('"', at)) != std::string::npos) {
    size_t end = all.find('"', at + 1);
    std::string key = all.substr(at + 1, end - at - 1);
    size_t colon = all.find(':', end);
    out[key] = atoi(all.c_str() + colon + 1);
    at = all.find_first_of(",}", colon);
  }
  return out;
}

static void test_broken(const std::string &kits) {
  auto expected = read_expected(kits + "/broken/expected.json");
  CHECK(expected.size() == 44, "%zu broken images", expected.size());
  // start each from the synthetic kit stored: a refused BEGIN leaves it, an upload that fails leaves no kit
  D.mem.fill(0xFF);
  power_cycle();
  B.boot();
  upload(synthetic);
  const flash_t with_synthetic = D.mem;
  int over_wire = 0, at_begin = 0, in_flash = 0, foreign_magic = 0;
  for (auto &[file, reason] : expected) {
    context = "broken " + file;
    bytes img = read_file(kits + "/broken/" + file);
    CHECK(user_kit::check_image(img.data(), img.size()) == reason, "core check_image %d, expected.json %d",
          user_kit::check_image(img.data(), img.size()), reason);

    // sent as an upload
    D.mem = with_synthetic;
    power_cycle();
    reset_counts();
    B.boot();
    if (img.size() < 256 || img.size() % 4) {   // BEGIN refuses it (too big: size < 256 or not a multiple of 4)
      B.send(begin_msg(img.size(), ref_crc(img)));
      expect_status(1, 2, 0, "BEGIN");
      CHECK(B.s.state == 1 && D.kit_live && D.mutes == 0 && D.ops == 0, "a refused BEGIN touched the kit");
      at_begin++;
    } else {
      auto frames = make_frames(img);
      for (size_t i = 0; i + 1 < frames.size(); i++) {
        D.sent.clear();
        B.send(frames[i]);
        CHECK(D.sent.size() == 1 && D.sent[0][4] == 0, "frame %zu refused: %s", i, D.sent.empty() ? "" : hex(D.sent[0]).c_str());
      }
      D.sent.clear();
      B.send(COMMIT);
      // COMMIT runs all of check_image on the image as it was sent (BEGIN's CRC is this image's, so that
      // passes): any step that fails, a nonzero data_base and a bad sound CRC with the rest, is 10, and the
      // header is never written
      expect_status(3, 10, 0, "COMMIT");
      CHECK(D.header_programs == 0 && std::all_of(D.mem.header.begin(), D.mem.header.end(), [](uint8_t b) { return b == 0xFF; }),
            "a refused COMMIT wrote the header (%ld programs)", D.header_programs);
      info_t i = ask_info();
      CHECK(no_kit_state(i.state) && i.size == 0 && i.crc == 0, "after the refused COMMIT: state %d reason %d size %u", i.state,
            i.reason, i.size);
      CHECK(!D.kit_live && !D.muted && D.ends == 1 && !D.ended_kit, "after the refused COMMIT: kit %d muted %d", D.kit_live, D.muted);
      over_wire++;
    }

    // placed in flash as a minichord would hold it, the boot check must give the same reason, except a
    // header that doesn't start MCK1: no kit (state 0, reason 1), not a damaged one. The size cases can't be
    // (in flash the size is the header's data_bytes), nor data_bytes_less/more (the flash then holds a
    // different sound than the header says: a data CRC in flash).
    if (img.size() < 256 || img.size() % 4 || file == "data_bytes_less.image" || file == "data_bytes_more.image") continue;
    context = "broken " + file + " in flash";
    D.mem.fill(0xFF);
    bytes h(img.begin(), img.begin() + 256);
    // the header as the minichord writes it: its data_base, header_crc made again (unless the fault is the
    // header_crc, or comes before it in the check order); a data_base fault, the wrong data_base
    if (reason >= 4) {
      set32(&h[8], KIT_FLOOR + (reason == 4 ? 65536 : 0));
      set32(&h[252], ref_crc(h.data(), 252));
    }
    memcpy(D.mem.at(KIT_HEADER), h.data(), 256);
    memcpy(D.mem.at(KIT_FLOOR), img.data() + 256, img.size() - 256);
    power_cycle();
    reset_counts();
    B.boot();
    const bool foreign = memcmp(h.data(), "MCK1", 4) != 0;
    const uint8_t state = foreign ? 0 : 3, why = foreign ? 1 : reason;
    CHECK(B.s.state == state && B.s.reason == why && D.applies == 0, "boot: state %d reason %d, want %d %d", B.s.state, B.s.reason,
          state, why);
    B.send(INFO);
    expect({info_reply(no_kit(state, why))}, "INFO");
    in_flash++;
    foreign_magic += foreign;
  }
  printf("  broken: %zu images; %d refused at BEGIN, %d refused at COMMIT, %d checked in flash (%d without MCK1: no kit)\n",
         expected.size(), at_begin, over_wire, in_flash, foreign_magic);
}

// ---- 4. the protocol's edge cases -----------------------------------------------------------------

static void fresh(bool with_kit) {
  D.mem.fill(0xFF);
  power_cycle();
  B.boot();
  const bool deferred = B.deferred;
  B.deferred = false;
  if (with_kit) upload(synthetic);
  B.deferred = deferred;
  reset_counts();
}
// BEGIN and the pieces up to (not including) piece `upto`
static void start_upload(size_t upto) {
  for (size_t i = 0; i < upto + 1; i++) B.send(synthetic_frames[i]);
  D.sent.clear();
}
static uint32_t after_piece(size_t piece) { return (uint32_t)(piece * PIECE); }   // pieces counted from 0

static void test_edges() {
  // a repeated piece: answered 0 and not written again
  context = "repeated piece";
  fresh(false);
  start_upload(5);   // pieces 0 to 4 in: next is piece 5
  B.send(synthetic_frames[1 + 5]);
  expect_status(2, 0, after_piece(6), "piece 5");
  long ops = D.ops;
  B.send(synthetic_frames[1 + 5]);
  expect_status(2, 0, after_piece(6), "piece 5 again");
  CHECK(D.ops == ops, "the repeat wrote the flash %ld times", D.ops - ops);
  B.send(synthetic_frames[1 + 6]);
  expect_status(2, 0, after_piece(7), "piece 6 after the repeat");
  B.send(synthetic_frames[1 + 5]);   // no longer the previous piece
  expect_status(2, 6, after_piece(7), "piece 5 after piece 6");
  // out of order
  context = "out of order";
  B.send(synthetic_frames[1 + 8]);
  expect_status(2, 6, after_piece(7), "piece 8 for piece 7");
  B.send(synthetic_frames[1 + 7]);
  expect_status(2, 0, after_piece(8), "piece 7 after it");
  // a bad piece CRC, then the piece right
  context = "bad piece CRC";
  ops = D.ops;
  B.send(data_msg(synthetic, after_piece(8), 1));
  expect_status(2, 7, after_piece(8), "piece 8 with its CRC wrong");
  CHECK(D.ops == ops, "a bad piece wrote the flash");
  bytes corrupt = data_msg(synthetic, after_piece(8));
  corrupt[20] ^= 0x01;   // a payload byte changed: the CRC no longer matches
  B.send(corrupt);
  expect_status(2, 7, after_piece(8), "piece 8 with a payload byte changed");
  B.send(synthetic_frames[1 + 8]);
  expect_status(2, 0, after_piece(9), "piece 8 right");
  // a packed length that isn't the piece's
  context = "short piece";
  bytes shorter = synthetic_frames[1 + 9];
  shorter.erase(shorter.end() - 9, shorter.end() - 1);   // one packed group less
  B.send(shorter);
  expect_status(2, 3, after_piece(9), "piece 9 a group short");
  // BEGIN while one is open
  context = "BEGIN while open";
  B.send(synthetic_frames[0]);
  expect_status(1, 1, after_piece(9), "BEGIN in an upload");
  CHECK(B.open(), "the upload closed");
  // COMMIT with bytes missing: 3, and the upload ends with no kit
  context = "COMMIT early";
  B.send(COMMIT);
  expect_status(3, 3, 0, "COMMIT with bytes missing");
  CHECK(!B.open() && !D.muted && !D.kit_live && D.ends == 1, "after an early COMMIT: open %d", B.open());
  CHECK(ask_info().state == 0, "state after an early COMMIT");
  // DATA, COMMIT and ABORT with no upload
  context = "no upload";
  B.send(synthetic_frames[1]);
  expect_status(2, 5, 0, "DATA with no upload");
  B.send(COMMIT);
  expect_status(3, 5, 0, "COMMIT with no upload");
  B.send(ABORT);
  expect_status(4, 5, 0, "ABORT with no upload");

  // every piece in: the last again is a repeat (0), a piece at offset = size is out of order (6), and COMMIT
  // still works
  context = "after the last piece";
  fresh(false);
  for (size_t i = 0; i + 1 < synthetic_frames.size(); i++) B.send(synthetic_frames[i]);
  D.sent.clear();
  ops = D.ops;
  B.send(synthetic_frames[synthetic_frames.size() - 2]);
  expect_status(2, 0, synthetic.size(), "the last piece again");
  CHECK(D.ops == ops, "the last piece's repeat wrote the flash");
  {
    bytes past;
    put_n(past, synthetic.size(), 3);
    put_n(past, 0, 5);   // the CRC of no bytes
    B.send(msg(0x02, past));
    expect_status(2, 6, synthetic.size(), "a piece at offset = size, every byte in");
  }
  B.send(COMMIT);
  expect({status_reply(3, 0, 0), info_reply(synthetic_info)}, "COMMIT after them");

  // ABORT in an upload, over a stored kit: no kit
  context = "ABORT";
  fresh(true);
  start_upload(3);
  B.send(ABORT);
  expect_status(4, 0, 0, "ABORT");
  CHECK(!B.open() && !D.muted && !D.kit_live && D.mutes == 1 && D.unmutes == 1 && D.ends == 1 && !D.ended_kit, "after ABORT");
  B.send(INFO);
  expect({info_reply(no_kit(0, 1))}, "INFO after ABORT");

  // BEGIN's refusals
  context = "BEGIN refusals";
  fresh(true);
  B.send(begin_msg(256 + FULL + 4, 0));
  expect_status(1, 2, 0, "BEGIN a word over capacity");
  B.send(begin_msg(252, 0));
  expect_status(1, 2, 0, "BEGIN under 256");
  B.send(begin_msg(258, 0));
  expect_status(1, 2, 0, "BEGIN not a multiple of 4");
  B.send(begin_msg(synthetic.size(), ref_crc(synthetic), 2));
  expect_status(1, 4, 0, "BEGIN proto 2");
  B.send(synthetic_frames[0], true);
  expect_status(1, 1, 0, "BEGIN with the rhythm running");
  CHECK(D.ops == 0 && D.mutes == 0 && D.invalidates == 0 && D.kit_live && B.s.state == 1, "a refused BEGIN touched the kit");
  B.send(INFO);
  expect({info_reply(synthetic_info)}, "INFO after the refusals");
  B.send(begin_msg(256 + FULL, 0));
  expect_status(1, 0, 0, "BEGIN of the whole capacity");
  B.send(ABORT);
  expect_status(4, 0, 0, "ABORT it");

  // an image CRC that isn't BEGIN's: 9 at COMMIT
  context = "image CRC";
  fresh(false);
  B.send(begin_msg(synthetic.size(), ref_crc(synthetic) ^ 1));
  for (size_t i = 1; i + 1 < synthetic_frames.size(); i++) B.send(synthetic_frames[i]);
  D.sent.clear();
  B.send(COMMIT);
  expect_status(3, 9, 0, "COMMIT, BEGIN's CRC wrong");
  CHECK(!B.open() && !D.kit_live && ask_info().state == 0, "after a CRC mismatch");

  // a page that doesn't read back: 8, the upload ended with no kit
  context = "read-back";
  fresh(true);
  start_upload(10);
  D.corrupt_next_program = true;
  const long programs = D.programs;
  B.send(synthetic_frames[1 + 10]);   // sound bytes 1,984 to 2,207: page 7 fills, and is programmed
  expect_status(2, 8, 0, "a page not reading back");
  CHECK(D.programs == programs + 1, "%ld programs", D.programs - programs);
  B.send(synthetic_frames[1 + 11]);
  expect_status(2, 5, 0, "the next piece, the upload ended");
  CHECK(!B.open() && !D.muted && !D.kit_live && ask_info().state == 0, "after a read-back mismatch");
  // and the header page at COMMIT
  context = "read-back of the header";
  fresh(true);
  for (size_t i = 0; i + 1 < synthetic_frames.size(); i++) B.send(synthetic_frames[i]);
  D.sent.clear();
  long before = D.programs;
  D.corrupt_next_program = true;   // the last page (it's partial), at COMMIT
  B.send(COMMIT);
  CHECK(D.programs == before + 1, "COMMIT stopped after %ld programs", D.programs - before);
  expect_status(3, 8, 0, "COMMIT, the last page not reading back");
  // (the header read-back is reached below with an image whose sound is a whole number of pages)

  // CLEAR: with a kit, during an upload, and a CLEAR that isn't one
  context = "CLEAR";
  fresh(true);
  B.send(msg(0x06, {0, 0x43, 0x4D}));
  expect_status(6, 3, 0, "CLEAR without its CL");
  CHECK(B.s.state == 1 && D.kit_live && D.ops == 0, "a bad CLEAR touched the kit");
  B.send(CLEAR);
  expect_status(6, 0, 0, "CLEAR");
  CHECK(!D.kit_live && !D.muted && D.mutes == 1 && D.unmutes == 1 && D.erases == 1, "after CLEAR: kit %d muted %d erases %ld", D.kit_live,
        D.muted, D.erases);
  B.send(INFO);
  expect({info_reply(no_kit(0, 1))}, "INFO after CLEAR");
  power_cycle();
  B.boot();
  CHECK(B.s.state == 0 && B.s.reason == 1, "boot after CLEAR: state %d", B.s.state);
  B.send(CLEAR);
  expect_status(6, 0, 0, "CLEAR with no kit");
  context = "CLEAR in an upload";
  fresh(true);
  start_upload(4);
  B.send(CLEAR);
  expect_status(6, 0, 0, "CLEAR in an upload");
  CHECK(!B.open() && !D.muted && D.ends == 1 && !D.ended_kit && B.s.state == 0, "after CLEAR in an upload");

  // 5 s of silence
  context = "silence";
  fresh(true);
  D.now = 1000;
  start_upload(2);
  D.now += 4999999;
  B.send(synthetic_frames[1 + 2]);   // a DATA: the 5 s start again
  expect_status(2, 0, after_piece(3), "piece 2");
  D.now += 4999999;
  user_kit::poll(B.s);
  CHECK(B.open(), "ended before 5 s of silence");
  D.now += 1;
  user_kit::poll(B.s);
  CHECK(!B.open() && D.sent.empty() && !D.muted && D.ends == 1 && !D.ended_kit && !D.kit_live, "5 s: open %d replies %zu muted %d",
        B.open(), D.sent.size(), D.muted);
  B.send(INFO);
  expect({info_reply(no_kit(0, 1))}, "INFO after the silence");
  B.send(synthetic_frames[1 + 3]);
  expect_status(2, 5, 0, "DATA after the silence");
  // what doesn't start the 5 s again: INFO asked every second (an app polling an upload it abandoned),
  // BEGIN, a DATA that can't be read, a malformed COMMIT or ABORT; what does: a DATA out of order or with
  // a bad CRC, a computer that is there and sending
  context = "silence, INFO every second";
  fresh(true);
  D.now = 1000;
  start_upload(2);
  for (int k = 1; k <= 4; k++) {
    D.now += 1000000;
    B.send(INFO);
    CHECK(B.open() && D.sent.size() == 1 && D.sent[0][5] == 2, "INFO %d s in: open %d", k, B.open());
    D.sent.clear();
  }
  D.now += 999999;
  B.send(INFO);
  CHECK(B.open(), "ended before 5 s");
  D.sent.clear();
  D.now += 1;
  B.send(INFO);   // poll() after it ends the upload, with no reply of its own
  CHECK(!B.open() && D.sent.size() == 1 && D.sent[0][5] == 2 && !D.muted && D.ends == 1 && !D.ended_kit,
        "INFO every second held the upload open: open %d", B.open());
  D.sent.clear();
  B.send(INFO);
  expect({info_reply(no_kit(0, 1))}, "INFO after the silence, polled");
  context = "silence, messages that don't count";
  fresh(false);
  D.now = 1000;
  start_upload(2);
  bytes short_piece = synthetic_frames[1 + 2];
  short_piece.erase(short_piece.end() - 9, short_piece.end() - 1);   // a packed group short: 3
  const std::vector<std::pair<bytes, uint8_t>> idle = {{synthetic_frames[0], 1}, {short_piece, 3}, {msg(0x03, {0, 1}), 3},
                                                        {msg(0x04, {1, 0}), 3}, {msg(0x07, {0, 0}), 3}, {INFO, 0}};
  for (auto &[m, status] : idle) {
    D.now += 800000;
    D.sent.clear();
    B.send(m);
    CHECK(!D.sent.empty() && (m == INFO || D.sent[0][4] == status), "a message %s answered %s", hex(m).substr(0, 10).c_str(),
          D.sent.empty() ? "nothing" : hex(D.sent[0]).c_str());
  }
  D.sent.clear();
  D.now += 5000000 - 6 * 800000 - 1;
  user_kit::poll(B.s);
  CHECK(B.open(), "ended before 5 s");
  D.now += 1;
  user_kit::poll(B.s);
  CHECK(!B.open() && D.ends == 1, "INFO, BEGIN or malformed messages held the upload open");
  context = "silence, DATA that counts";
  fresh(false);
  D.now = 1000;
  start_upload(2);
  D.now += 4000000;
  B.send(synthetic_frames[1 + 0]);   // an old piece: 6
  expect_status(2, 6, after_piece(2), "an old piece");
  D.now += 4000000;
  user_kit::poll(B.s);   // 8 s from the last piece taken, 4 from the old one
  CHECK(B.open(), "a DATA answered 6 didn't start the 5 s again");
  B.send(data_msg(synthetic, after_piece(2), 1));   // a bad CRC: 7
  expect_status(2, 7, after_piece(2), "a bad CRC");
  D.now += 4999999;
  user_kit::poll(B.s);
  CHECK(B.open(), "a DATA answered 7 didn't start the 5 s again");
  B.send(synthetic_frames[1 + 2]);
  expect_status(2, 0, after_piece(3), "piece 2 after them");
  // the clock wrapping in an upload
  context = "silence over the clock's wrap";
  fresh(false);
  D.now = 0xFFFFFFFFu - 1000000;
  start_upload(2);
  D.now += 2000000;   // wrapped
  user_kit::poll(B.s);
  CHECK(B.open(), "ended 2 s in, over the wrap");
  D.now += 3000000;
  user_kit::poll(B.s);
  CHECK(!B.open(), "still open 5 s in, over the wrap");
  D.now = 0;

  // a disabled slot: less than 64 kB of room
  context = "disabled";
  for (uint32_t base : {KIT_TOP, KIT_TOP - 0x8000, KIT_TOP - 0x10000}) {
    fresh(false);
    power_cycle();
    B.boot(base);
    const bool disabled = KIT_TOP - base < 65536;
    if (disabled) {
      CHECK(B.s.state == 4 && B.s.reason == 7, "data_base %08X: state %d reason %d", base, B.s.state, B.s.reason);
      B.send(INFO);
      expect({info_reply(no_kit(4, 7))}, "INFO, disabled");
      B.send(synthetic_frames[0]);
      expect_status(1, 11, 0, "BEGIN, disabled");
      B.send(CLEAR);   // no slot, nothing to erase: the sector may be the firmware's
      expect_status(6, 11, 0, "CLEAR, disabled");
      CHECK(D.ops == 0 && D.mutes == 0 && !B.open(), "a disabled BEGIN or CLEAR touched the flash");
    } else {   // exactly 64 kB: enabled, and the synthetic kit (64.7 kB of sound) is too big for it
      CHECK(B.s.state == 0, "64 kB: state %d", B.s.state);
      B.send(synthetic_frames[0]);
      expect_status(1, 2, 0, "BEGIN, the synthetic kit in 64 kB");
    }
  }

  // data_base moved by a bigger firmware: damaged, reason 4
  context = "data_base moved";
  fresh(true);
  power_cycle();
  reset_counts();
  B.boot(KIT_FLOOR + 65536);
  CHECK(B.s.state == 3 && B.s.reason == 4 && D.applies == 0, "state %d reason %d", B.s.state, B.s.reason);
  B.send(INFO);
  expect({info_reply(no_kit(3, 4))}, "INFO, data_base moved");
  power_cycle();
  B.boot(KIT_FLOOR);   // the old firmware back: the kit is there again
  CHECK(B.s.state == 1, "back on the old data_base: state %d", B.s.state);

  // messages that aren't right
  context = "malformed";
  fresh(true);
  B.send(msg(0x07, {0, 0}));
  expect_status(7, 3, 0, "op 7");
  B.send(msg(0x3F, {0, 0}));
  expect_status(0x3F, 3, 0, "op 3F");
  // a reply's shape, from another minichord or an app that echoes: taken, never answered
  for (uint8_t op : {0x40, 0x42, 0x45, 0x4B, 0x7F}) {
    B.send(msg(op, {0, 0, 0, 0}));
    CHECK(D.sent.empty(), "op %02X answered: %s", op, D.sent.empty() ? "" : hex(D.sent[0]).c_str());
    D.sent.clear();
  }
  B.send(status_reply(2, 0, 224));
  B.send(info_reply(no_kit(1, 0)));
  bytes no_op = {0xF0, 0x7D, 0x6B, 0xF7};   // an F7 where the op should be
  B.send(no_op);
  CHECK(D.sent.empty(), "a reply, or no op, answered: %zu replies", D.sent.size());
  D.sent.clear();
  B.send(msg(0x05, {}));
  expect_status(5, 3, 0, "INFO 5 bytes long");
  B.send(msg(0x05, {1, 0, 0}));
  expect_status(5, 3, 0, "INFO 8 bytes long");
  B.send(msg(0x03, {0, 1}));   // no upload: 5, before the shape (a malformed one in an upload is 3, see silence)
  expect_status(3, 5, 0, "COMMIT 00 01 with no upload");
  B.send(msg(0x04, {1, 0}));
  expect_status(4, 5, 0, "ABORT 01 00 with no upload");
  // not framed: 3 before any op's own rule, so before COMMIT's and DATA's 5 with no upload
  bytes commit_noend = COMMIT;
  commit_noend.back() = 0x00;
  B.send(commit_noend);
  expect_status(3, 3, 0, "COMMIT with no F7 and no upload");
  bytes data_hi = synthetic_frames[1];
  data_hi[20] |= 0x80;
  B.send(data_hi);
  expect_status(2, 3, 0, "DATA with a byte over 7F and no upload");
  bytes hi = INFO;
  hi[4] = 0x81;   // a data byte with bit 7 set
  B.send(hi);
  expect_status(5, 3, 0, "INFO with a byte over 7F");
  bytes noend = INFO;
  noend.back() = 0x00;
  B.send(noend);
  expect_status(5, 3, 0, "INFO with no F7");
  bytes other = {0xF0, 0x7D, 0x6D, 0x01, 0x02, 0x03, 0xF7};   // the pages' ID: not a kit message
  CHECK(!user_kit::receive(B.s, other.data(), other.size()) && D.sent.empty(), "a page message taken as a kit message");
  bytes setting = {0xF0, 0x7D, 0x6B, 0x01, 0xF7, 0xF7};   // 6 bytes beginning like a kit message
  B.send(setting);
  CHECK(D.sent.size() == 1 && D.sent[0].size() == 9 && D.sent[0][4] == 3, "a 6-byte kit message: %s", D.sent.empty() ? "" : hex(D.sent[0]).c_str());
  D.sent.clear();
  CHECK(B.s.state == 1 && D.kit_live, "the malformed messages touched the kit");

  // the strict number and packing rules: a CRC in 35 bits over 0xFFFFFFFF, in BEGIN and in DATA, and a high
  // bit for a byte the last group doesn't have (the raw bytes, and so the piece's CRC, the same without it)
  context = "35-bit CRC and stray high bits";
  fresh(true);
  B.send(begin_msg(synthetic.size(), ref_crc(synthetic) | 1ull << 32));
  expect_status(1, 3, 0, "BEGIN with a CRC over 32 bits");
  CHECK(!B.open() && D.ops == 0 && D.mutes == 0 && D.kit_live, "a BEGIN with a CRC over 32 bits touched the kit");
  for (size_t i = 0; i + 2 < synthetic_frames.size(); i++) B.send(synthetic_frames[i]);
  D.sent.clear();
  {
    const uint32_t last = after_piece(synthetic_frames.size() - 3);
    bytes wide = msg(0x02, {});
    wide.pop_back();
    put_n(wide, last, 3);
    put_n(wide, ref_crc(synthetic.data() + last, piece_len(synthetic, last)) | 1ull << 32, 5);
    bytes p = pack87(synthetic.data() + last, piece_len(synthetic, last));
    wide.insert(wide.end(), p.begin(), p.end());
    wide.push_back(0xF7);
    B.send(wide);
    expect_status(2, 3, last, "DATA with a CRC over 32 bits");
    bytes stray = synthetic_frames[synthetic_frames.size() - 2];
    const size_t packed = stray.size() - 13, group = (packed - 1) / 8 * 8, k = packed - group - 1;
    CHECK(k < 7, "the last piece ends in a full group (%zu bytes)", k);
    stray[12 + group] |= 1 << k;
    B.send(stray);
    expect_status(2, 3, last, "the last piece with a high bit for a byte its last group hasn't");
    B.send(synthetic_frames[synthetic_frames.size() - 2]);
    expect_status(2, 0, synthetic.size(), "the last piece right");
  }
  B.send(COMMIT);
  expect({status_reply(3, 0, 0), info_reply(synthetic_info)}, "COMMIT after them");
  {
    const long reads = D.reads;
    B.send(INFO);
    expect({info_reply(synthetic_info)}, "INFO after COMMIT");
    CHECK(D.reads == reads, "INFO after a COMMIT read the flash %ld times: its CRC wasn't taken from BEGIN's", D.reads - reads);
  }

  // a header sector that holds something not a kit's (an old filesystem's bytes, as the sector was LittleFS's
  // before firmware 77): no kit, state 0 reason 1, not a damaged one; and one that starts MCK1 is a kit's,
  // damaged when it fails a check
  context = "foreign header sector";
  D.mem.fill(0xFF);
  for (uint32_t i = 0; i < 4096; i++) D.mem.header[i] = (uint8_t)(i * 37 + 11);
  power_cycle();
  reset_counts();
  B.boot();
  CHECK(B.s.state == 0 && B.s.reason == 1 && D.applies == 0, "boot over foreign bytes: state %d reason %d", B.s.state, B.s.reason);
  B.send(INFO);
  expect({info_reply(no_kit(0, 1))}, "INFO over foreign bytes");
  CHECK(upload(synthetic), "an upload over foreign bytes");
  expect({status_reply(3, 0, 0), info_reply(synthetic_info)}, "COMMIT over foreign bytes");
  memcpy(D.mem.header.data(), "MCK1", 4);
  std::fill(D.mem.header.begin() + 4, D.mem.header.end(), 0xFF);
  power_cycle();
  B.boot();
  CHECK(B.s.state == 3 && B.s.reason == 2, "MCK1 and the rest FF: state %d reason %d", B.s.state, B.s.reason);
  D.mem.header[0] = 'm';
  power_cycle();
  B.boot();
  CHECK(B.s.state == 0 && B.s.reason == 1, "mCK1 and the rest FF: state %d reason %d", B.s.state, B.s.reason);

  CHECK(!D.muted, "left muted");
}

// ---- 4b. a BEGIN waiting to open ------------------------------------------------------------------

// On the Teensy a BEGIN is only noted in processMIDI and opened from drums_update's next pass, so other
// messages can come in between: ABORT calls it off (0), CLEAR calls it off and clears, a second BEGIN is
// busy (1), DATA and COMMIT find no upload (5), INFO answers the kit stored. None of them opens it early.
static void test_waiting() {
  B.deferred = true;
  // noted, then ABORT: never opens, the stored kit left as it is
  context = "waiting BEGIN, ABORT";
  fresh(true);
  B.send(synthetic_frames[0]);
  CHECK(D.sent.empty() && user_kit::begin_waiting(B.s) && !B.open() && D.ops == 0 && D.mutes == 0 && D.kit_live,
        "a BEGIN noted: replies %zu ops %ld mutes %ld", D.sent.size(), D.ops, D.mutes);
  B.send(INFO);
  expect({info_reply(synthetic_info)}, "INFO, a BEGIN waiting");
  B.send(msg(0x04, {1, 0}));
  expect_status(4, 3, 0, "a malformed ABORT, a BEGIN waiting");
  CHECK(user_kit::begin_waiting(B.s), "a malformed ABORT called the BEGIN off");
  B.send(ABORT);
  expect_status(4, 0, 0, "ABORT, a BEGIN waiting");
  B.pass();
  CHECK(D.sent.empty() && !B.open() && D.ops == 0 && D.mutes == 0 && D.kit_live && B.s.state == 1,
        "after the ABORT the BEGIN opened anyway: replies %zu open %d ops %ld", D.sent.size(), B.open(), D.ops);
  B.send(ABORT);
  expect_status(4, 5, 0, "ABORT again: nothing open or waiting");

  // noted, then CLEAR: called off, and the kit cleared
  context = "waiting BEGIN, CLEAR";
  fresh(true);
  B.send(synthetic_frames[0]);
  B.send(CLEAR);
  expect_status(6, 0, 0, "CLEAR, a BEGIN waiting");
  B.pass();
  CHECK(D.sent.empty() && !B.open() && !D.kit_live && !D.muted && D.mutes == 1 && D.erases == 1 && B.s.state == 0,
        "after CLEAR: replies %zu open %d kit %d erases %ld", D.sent.size(), B.open(), D.kit_live, D.erases);

  // noted, then DATA, COMMIT, a second BEGIN and INFO: answered without opening it; the pass opens the
  // first, whose size and CRC the upload then goes by
  context = "waiting BEGIN, the others";
  fresh(true);
  B.send(synthetic_frames[0]);
  B.send(synthetic_frames[1]);
  expect_status(2, 5, 0, "DATA, a BEGIN waiting");
  B.send(COMMIT);
  expect_status(3, 5, 0, "COMMIT, a BEGIN waiting");
  B.send(begin_msg(small.size(), ref_crc(small)));
  expect_status(1, 1, 0, "a second BEGIN, one waiting");
  B.send(begin_msg(256 + FULL + 4, 0));   // busy comes before too big
  expect_status(1, 1, 0, "a second BEGIN, too big, one waiting");
  B.send(begin_msg(small.size(), ref_crc(small), 2));
  expect_status(1, 4, 0, "a second BEGIN of proto 2, one waiting");
  B.send(begin_msg(small.size(), 1ull << 32));
  expect_status(1, 3, 0, "a second BEGIN with a CRC over 32 bits, one waiting");
  B.send(INFO);
  expect({info_reply(synthetic_info)}, "INFO, a BEGIN waiting");
  CHECK(!B.open() && D.ops == 0 && D.mutes == 0 && D.kit_live, "a BEGIN waiting opened early: ops %ld", D.ops);
  B.pass();
  expect_status(1, 0, 0, "the BEGIN opened");
  CHECK(B.open() && D.muted && D.erases == 1 && !D.kit_live && B.s.up_size == synthetic.size(), "opened: size %u", B.s.up_size);
  bool ok = true;
  for (size_t i = 1; i < synthetic_frames.size(); i++) {
    D.sent.clear();
    B.send(synthetic_frames[i]);
    ok = ok && !D.sent.empty() && D.sent[0][4] == 0;
  }
  CHECK(ok, "the first BEGIN's upload refused");
  expect({status_reply(3, 0, 0), info_reply(synthetic_info)}, "COMMIT of the first BEGIN's upload");

  // noted, and the pass finds the rhythm running, or a CRC over 32 bits: refused then, nothing touched
  context = "waiting BEGIN, refused at the pass";
  fresh(true);
  B.send(synthetic_frames[0]);
  B.pass(true);
  expect_status(1, 1, 0, "BEGIN, the rhythm running at the pass");
  B.send(begin_msg(synthetic.size(), ref_crc(synthetic) | 1ull << 32));
  B.pass();
  expect_status(1, 3, 0, "BEGIN with a CRC over 32 bits, at the pass");
  CHECK(!B.open() && !user_kit::begin_waiting(B.s) && D.ops == 0 && D.mutes == 0 && D.kit_live, "a refused BEGIN touched the kit");
  B.deferred = false;
}

// ---- 5. kits of other sizes -----------------------------------------------------------------------

// A valid image of data_bytes of sound, voice 0 playing all of it (no voice when there's no sound)
static bytes make_image(uint32_t data_bytes, uint32_t seed) {
  bytes img(256 + data_bytes, 0);
  memcpy(&img[0], "MCK1", 4);
  img[4] = 1;
  img[6] = 13;
  img[7] = 4;
  set32(&img[12], data_bytes);
  memcpy(&img[20], "pc size test", 12);
  if (data_bytes) {
    set32(&img[44 + 4], data_bytes / 2);
    img[44 + 8] = 0x22, img[44 + 9] = 0x56;   // 22,050
    img[44 + 10] = 100;
  }
  for (uint32_t i = 0; i < data_bytes; i++) { seed = seed * 1664525 + 1013904223; img[256 + i] = seed >> 24; }
  set32(&img[16], ref_crc(&img[256], data_bytes));
  set32(&img[252], ref_crc(img.data(), 252));
  return img;
}

static void test_sizes() {
  // the whole slot, and data_base moved up 64 and 192 kB by a bigger firmware (192 and 64 kB of room): the
  // sound written from data_base, never below it (the fake flash refuses that) nor past KIT_TOP
  for (uint32_t base : {KIT_FLOOR, KIT_FLOOR + 0x10000, KIT_FLOOR + 0x30000}) {
    const uint32_t cap = KIT_TOP - base;
    std::vector<uint32_t> sizes;
    for (uint32_t n : {0u, 4u, 256u, 4096u, 4100u, 65536u, cap - 4, cap}) if (n <= cap && std::find(sizes.begin(), sizes.end(), n) == sizes.end()) sizes.push_back(n);
    for (uint32_t n : sizes) {
      // over a slot full of old data and over blank flash: every sound sector is erased either way, and
      // nothing past the sound is touched
      for (uint8_t old : {0x00, 0xFF}) {
        char what[80];
        snprintf(what, sizeof what, "size %u, data_base %08X, over %02X", n, base, old);
        context = what;
        bytes img = make_image(n, n);
        CHECK(user_kit::check_image(img.data(), img.size()) == 0, "core check_image %d", user_kit::check_image(img.data(), img.size()));
        D.mem.fill(old);
        power_cycle();
        B.boot(base);
        reset_counts();
        const flash_t before = D.mem;
        bool ok = upload(img);
        info_t want = {1, 0, cap, base, (uint32_t)img.size(), ref_crc(img), 4, (uint16_t)(n ? 1 : 0), "pc size test"};
        CHECK(ok, "an upload refused");
        expect({status_reply(3, 0, 0), info_reply(want)}, "COMMIT");
        CHECK(flash_holds(img, base), "flash");
        const uint32_t sectors = (n + 4095) / 4096;
        CHECK(D.erases == 1 + (long)sectors && D.programs == (long)(n + 255) / 256 + 1, "erases %ld programs %ld", D.erases, D.programs);
        // below data_base, the rest of the sound area and the header sector past the header untouched
        const uint32_t from = base - KIT_FLOOR, end = from + sectors * 4096;
        CHECK(memcmp(D.mem.sound.data(), before.sound.data(), from) == 0, "the flash below data_base changed");
        CHECK(memcmp(&D.mem.sound[end], &before.sound[end], FULL - end) == 0, "the flash past the sound changed");
        CHECK(std::all_of(D.mem.header.begin() + 256, D.mem.header.end(), [](uint8_t b) { return b == 0xFF; }),
              "the header sector past the header isn't erased");
        power_cycle();
        reset_counts();
        B.boot(base);
        CHECK(B.s.state == 1 && D.applies == 1, "reboot: state %d reason %d", B.s.state, B.s.reason);
        const long reads = D.reads;
        B.send(INFO);
        expect({info_reply(want)}, "INFO after reboot");
        CHECK(D.reads == reads, "INFO after a boot read the flash %ld times: its CRC wasn't worked out at boot", D.reads - reads);
      }
    }
    // a word over the room is too big
    context = "data_base " + std::to_string(base - KIT_FLOOR) + " over KIT_FLOOR, too big";
    B.send(begin_msg(256 + cap + 4, 0));
    expect_status(1, 2, 0, "BEGIN a word over the room");
  }
  // the synthetic kit into 192 kB: each voice built at the moved data_base
  context = "synthetic at a moved data_base";
  D.mem.fill(0x00);
  power_cycle();
  B.boot(KIT_FLOOR + 0x10000);
  reset_counts();
  CHECK(upload(synthetic), "the upload");
  info_t moved = synthetic_info;
  moved.capacity = FULL - 0x10000;
  moved.data_base = KIT_FLOOR + 0x10000;
  expect({status_reply(3, 0, 0), info_reply(moved)}, "COMMIT");
  CHECK(flash_holds(synthetic, KIT_FLOOR + 0x10000), "flash");
  check_kit_built();
  power_cycle();
  B.boot(KIT_FLOOR + 0x10000);
  check_kit_built();
  B.send(INFO);
  expect({info_reply(moved)}, "INFO after reboot");
  // the header page not reading back at COMMIT (the sound a whole number of pages, so the header is the
  // only program COMMIT makes): 8, and no kit at the next boot
  context = "read-back of the header";
  bytes img = make_image(4096, 9);
  D.mem.fill(0xFF);
  power_cycle();
  B.boot();
  auto frames = make_frames(img);
  for (size_t i = 0; i + 1 < frames.size(); i++) B.send(frames[i]);
  D.sent.clear();
  reset_counts();
  D.corrupt_next_program = true;
  B.send(COMMIT);
  CHECK(D.programs == 1, "%ld programs at COMMIT", D.programs);
  expect_status(3, 8, 0, "COMMIT, the header not reading back");
  CHECK(!B.open() && !D.muted && !D.kit_live && B.s.state == 0, "after the header's read-back: state %d", B.s.state);
  power_cycle();
  B.boot();
  CHECK(no_kit_state(B.s.state), "boot after the header's read-back: state %d", B.s.state);
}

// ---- kit 13's stand-in --------------------------------------------------------------------------

// With no kit stored, kit 13 plays drum kit 4, studio (section 4): user_kit::STAND_IN_KIT is that kit in
// setting 337's numbering, found here by name in the generated table, drum_kits[] in drum_patterns.h
static void test_stand_in(const std::string &patterns) {
  context = "stand-in kit";
  std::ifstream f(patterns);
  CHECK((bool)f, "can't read %s", patterns.c_str());
  std::vector<std::string> names;
  std::string line;
  bool in_kits = false;
  while (std::getline(f, line)) {
    if (line.rfind("const drum_kit_t drum_kits[]", 0) == 0) { in_kits = true; continue; }
    if (in_kits && line.rfind("};", 0) == 0) break;
    if (in_kits && line.rfind("  {\"", 0) == 0) names.push_back(line.substr(4, line.find('"', 4) - 4));
  }
  CHECK(names.size() == 12, "%zu kits in drum_kits[]", names.size());
  const unsigned k = user_kit::STAND_IN_KIT;
  CHECK(k >= 1 && k <= names.size() && names[k - 1] == "studio", "STAND_IN_KIT %u is %s, not studio", k,
        k >= 1 && k <= names.size() ? names[k - 1].c_str() : "no kit");
  printf("  kit 13 with none stored: drum kit %u, %s\n", k, k >= 1 && k <= names.size() ? names[k - 1].c_str() : "?");
}

int main(int argc, char **argv) {
  if (argc != 4) { fprintf(stderr, "harness <test/kits folder> <voices.txt> <drum_patterns.h>\n"); return 2; }
  const std::string kits = argv[1];
  read_voices(argv[2]);
  synthetic = read_file(kits + "/synthetic.image");
  small = read_file(kits + "/small.image");
  {
    std::ifstream f(kits + "/synthetic.frames");
    std::string line;
    while (std::getline(f, line)) if (!line.empty()) synthetic_frames.push_back(unhex(line));
  }
  printf("user kit core on the PC: %zu-byte image, %zu frames\n", synthetic.size(), synthetic_frames.size());
  test_synthetic();
  printf("  1. synthetic upload, INFO, reboot: done\n");
  test_power_cuts();
  printf("  2. power cuts: %ld boots after a cut\n", cut_runs);
  test_broken(kits);
  printf("  3. broken images: done\n");
  test_edges();
  test_waiting();
  printf("  4. edge cases, and a BEGIN waiting to open: done\n");
  test_sizes();
  printf("  5. sizes 0 to 256 kB: done\n");
  test_stand_in(argv[3]);
  printf("%ld checks, %ld failed\n", checks, failures);
  return failures ? 1 : 0;
}
