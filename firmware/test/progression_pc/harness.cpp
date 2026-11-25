// The progression upload (generator/progression.md) tested on a computer: include/progression_core.h on its
// own, and include/progression.h, the Teensy's side, against a fake of the parts of main.cpp it uses (chord
// memory, the chord tables, the rhythm's bar, USB MIDI). Messages are built here with this file's own CRC and
// 7-bit numbers, not the core's, and every reply is checked byte for byte. See README.md.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

// ---- a fake of what progression.h takes from main.cpp ----
#define FLASHMEM
typedef uint8_t byte;
#define ACC_BEAT 24

static std::vector<std::vector<uint8_t>> sent;
struct fake_usb_midi {
  uint8_t cable = 0;
  void sendSysEx(uint32_t len, const uint8_t *m, bool has_term, uint8_t c) {
    (void)has_term;
    (void)c;
    sent.emplace_back(m, m + len);
  }
  uint8_t getCable() { return cable; }
} usbMIDI;

uint8_t major[7] = {0, 4, 7, 12, 2, 5, 9}, minor[7] = {0, 3, 7, 12, 2, 5, 8}, seventh[7] = {0, 4, 10, 7, 2, 5, 9};
uint8_t maj_sixth[7] = {0, 4, 7, 9, 2, 5, 12}, min_sixth[7] = {0, 3, 7, 9, 2, 5, 12};
uint8_t other[29][7];
uint8_t (*chord_catalogue[29])[7];
const uint8_t chord_catalogue_size = 29;
uint8_t alt_chord_layout = 0;
bool barry_harris_mode = false;
uint8_t alt_slot[7] = {11, 12, 13, 14, 15, 16, 17};
uint8_t (*alt_chord_for(uint8_t slot))[7] { return chord_catalogue[alt_slot[slot]]; }

enum { CHORD_MEMORY_EMPTY, CHORD_MEMORY_RECORDING, CHORD_MEMORY_PLAYING, CHORD_MEMORY_STOPPED };
volatile uint8_t chord_memory_state = CHORD_MEMORY_EMPTY;
struct chord_moment {
  double at;
  int8_t fundamental;
  uint8_t (*chord)[7];
  bool sharp, slashed;
  uint8_t slash;
};
#define CHORD_MEMORY_MAX 64
chord_moment chord_memory[CHORD_MEMORY_MAX];
uint8_t chord_memory_count = 0;
double chord_memory_start = 0, chord_memory_length = 0;
int16_t chord_memory_played = -1;
bool rythm_mode = false;
uint8_t meter_eighths = 8;
uint8_t acc_bar_eighths() { return meter_eighths; }
void set_rythm_mode(bool on) { rythm_mode = on; }
static double next_bar_line = 192;
static double chord_memory_bar_line() { return next_bar_line; }
static int shown = 0;
static void chord_memory_show() { shown++; }

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n) {   // main.cpp's, as zlib's
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

#include "progression.h"

// ---- this file's own message building ----
static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                       \
  do {                                                          \
    checks++;                                                   \
    if (!(cond)) {                                              \
      failures++;                                               \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
      printf(__VA_ARGS__);                                      \
      printf("\n");                                             \
    }                                                           \
  } while (0)

static uint32_t ref_crc(const std::vector<uint8_t> &b) {   // bytewise, table-free, independent of the core's
  uint32_t c = 0xFFFFFFFFu;
  for (uint8_t x : b) {
    c ^= x;
    for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
  }
  return c ^ 0xFFFFFFFFu;
}
static void groups(std::vector<uint8_t> &m, uint64_t v, int n) {
  for (int i = 0; i < n; i++) { m.push_back(v & 0x7F); v >>= 7; }
}
struct E { uint8_t button, type, mods, eighths; };
static std::vector<uint8_t> write_msg(const std::vector<E> &es, uint8_t flags = 0, uint8_t proto = 1) {
  std::vector<uint8_t> body;
  for (auto &e : es) body.insert(body.end(), {e.button, e.type, e.mods, e.eighths});
  std::vector<uint8_t> m = {0xF0, 0x7D, 0x70, 0x01, proto, flags, (uint8_t)es.size()};
  groups(m, ref_crc(body), 5);
  m.insert(m.end(), body.begin(), body.end());
  m.push_back(0xF7);
  return m;
}
static bool send(const std::vector<uint8_t> &m) {
  sent.clear();
  return progression_message(m.data(), m.size());
}
static bool status_is(uint8_t op, uint8_t status, uint8_t count) {
  return sent.size() == 1 && sent[0] == std::vector<uint8_t>{0xF0, 0x7D, 0x70, (uint8_t)(0x40 | op), status, count, 0xF7};
}
static void reset() {
  chord_memory_state = CHORD_MEMORY_EMPTY;
  chord_memory_count = 0;
  chord_memory_length = 0;
  rythm_mode = false;
  alt_chord_layout = 0;
  barry_harris_mode = false;
  meter_eighths = 8;
}

int main() {
  // the catalogue: major, minor, seventh where main.cpp has them (1, 2, 3), the sixths at 8 and 9
  for (int i = 0; i < 29; i++) chord_catalogue[i] = &other[i];
  chord_catalogue[0] = &major;
  chord_catalogue[1] = &minor;
  chord_catalogue[2] = &seventh;
  chord_catalogue[7] = &maj_sixth;
  chord_catalogue[8] = &min_sixth;

  // ---- the contract's example: Dm, G7, C, 8 eighths each, play now ----
  reset();
  CHECK(send(write_msg({{10, 0, 0, 8}, {8, 0, 0, 8}, {3, 0, 0, 8}}, 0x01)), "WRITE not taken as ours");
  CHECK(status_is(1, 0, 3), "WRITE OK reply");
  CHECK(chord_memory_count == 3, "count %d", chord_memory_count);
  CHECK(chord_memory[0].fundamental == 3 && chord_memory[0].chord == &minor, "Dm: line 6-3, minor");
  CHECK(chord_memory[1].fundamental == 4 && chord_memory[1].chord == &seventh, "G7: line 6-2, seventh");
  CHECK(chord_memory[2].fundamental == 5 && chord_memory[2].chord == &major, "C: line 6-1, major");
  CHECK(chord_memory[0].at == 0 && chord_memory[1].at == 96 && chord_memory[2].at == 192, "at in 24ths of a beat");
  CHECK(chord_memory_length == 288, "24 eighths = 3 bars of 4/4: %f", chord_memory_length);
  CHECK(chord_memory_state == CHORD_MEMORY_PLAYING && rythm_mode && chord_memory_start == next_bar_line, "plays from the next bar line");

  // ---- the length rounds up to whole bars; in 7/8 too ----
  reset();
  send(write_msg({{0, 0, 0, 3}, {3, 0, 0, 2}}));
  CHECK(chord_memory_length == 8 * 12, "5 eighths in 4/4 -> one bar");
  CHECK(chord_memory_state == CHORD_MEMORY_STOPPED && !rythm_mode && shown, "no play flag: stopped, waiting");
  reset();
  meter_eighths = 7;
  send(write_msg({{0, 0, 0, 7}, {3, 0, 0, 1}}));
  CHECK(chord_memory_length == 14 * 12, "8 eighths in 7/8 -> two bars");
  CHECK(progression::bar_round(0, 8) == 0 && progression::bar_round(24, 8) == 24 && progression::bar_round(1, 5) == 5, "bar_round");

  // ---- a WRITE while playing keeps playing ----
  reset();
  chord_memory_state = CHORD_MEMORY_PLAYING;
  rythm_mode = true;
  send(write_msg({{0, 0, 0, 8}}));
  CHECK(chord_memory_state == CHORD_MEMORY_PLAYING, "was playing: plays on");

  // ---- type 0 follows the layout as it is; 1-29 the catalogue ----
  reset();
  barry_harris_mode = true;
  send(write_msg({{0, 0, 0, 8}, {1, 0, 0, 8}}));
  CHECK(chord_memory[0].chord == &maj_sixth && chord_memory[1].chord == &min_sixth, "Barry Harris: sixths");
  reset();
  alt_chord_layout = 1;
  send(write_msg({{2, 0, 0, 8}}));
  CHECK(chord_memory[0].chord == chord_catalogue[13], "alternate layout: the seventh row's slot");
  reset();
  send(write_msg({{0, 16, 0, 8}, {0, 29, 0, 8}}));
  CHECK(chord_memory[0].chord == chord_catalogue[15] && chord_memory[1].chord == chord_catalogue[28], "types 16 and 29");

  // ---- sharp and slash: Fmaj7 over D (column 3), sharp on B ----
  reset();
  send(write_msg({{0, 4, (3 + 1) << 1, 8}, {18, 0, 0x01, 8}}));
  CHECK(chord_memory[0].slashed && chord_memory[0].slash == 3 && !chord_memory[0].sharp, "slash D: line 6-3");
  CHECK(chord_memory[1].sharp && !chord_memory[1].slashed && chord_memory[1].fundamental == 0, "B sharp: line 0");
  reset();
  send(write_msg({{0, 0, 7 << 1, 8}}));
  CHECK(chord_memory[0].slashed && chord_memory[0].slash == 0, "slash column 6 (B): line 0");

  // ---- refusals, in the contract's order; chord memory left as it was ----
  reset();
  send(write_msg({{3, 0, 0, 8}}));
  auto bad = [&](const std::vector<uint8_t> &m, uint8_t status, const char *what) {
    send(m);
    CHECK(status_is(1, status, 1), "%s: status %d", what, status);
    CHECK(chord_memory_count == 1 && chord_memory[0].fundamental == 5, "%s: memory kept", what);
  };
  bad(write_msg({{3, 0, 0, 8}}, 0, 2), 4, "proto 2");
  bad(write_msg({{3, 0, 0, 8}}, 0x02), 3, "flag bit 1");
  std::vector<E> many(65, E{0, 0, 0, 1});
  bad(write_msg(many), 2, "65 entries");
  { auto m = write_msg({{3, 0, 0, 8}}); m[6] = 2; bad(m, 3, "count not the length"); }
  { std::vector<uint8_t> m = {0xF0, 0x7D, 0x70, 0x01, 0x01, 0x00, 0x00}; groups(m, ref_crc({}), 5); m.push_back(0xF7); bad(m, 3, "count 0"); }
  { auto m = write_msg({{3, 0, 0, 8}}); m[13] ^= 1; bad(m, 7, "an entry changed after its CRC"); }
  { auto m = write_msg({{3, 0, 0, 8}}); m[11] = 0x7F; bad(m, 3, "crc past 32 bits"); }
  bad(write_msg({{21, 0, 0, 8}}), 3, "button 21");
  bad(write_msg({{0, 30, 0, 8}}), 3, "type 30");
  bad(write_msg({{0, 0, 0x10, 8}}), 3, "modifier bit 4");
  bad(write_msg({{0, 0, 0, 0}}), 3, "length 0");
  { auto m = write_msg({{3, 0, 0, 8}}); m.pop_back(); bad(m, 3, "no F7"); }
  { std::vector<uint8_t> m = {0xF0, 0x7D, 0x70, 0x01, 0x01, 0x00, 0xF7}; bad(m, 3, "too short for a WRITE"); }
  chord_memory_state = CHORD_MEMORY_RECORDING;
  bad(write_msg({{6, 0, 0, 8}}), 1, "while recording");

  // ---- not ours, a reply, an unknown op ----
  CHECK(!send({0xF0, 0x7D, 0x6B, 0x05, 0x00, 0x00, 0xF7}) && sent.empty(), "the kit's INFO is not ours");
  CHECK(send({0xF0, 0x7D, 0x70, 0x41, 0x00, 0x01, 0xF7}) && sent.empty(), "a reply is never answered");
  CHECK(send({0xF0, 0x7D, 0x70, 0x09, 0x00, 0x00, 0xF7}) && status_is(9, 3, 1), "unknown op: 3");

  // ---- INFO ----
  reset();
  send(write_msg({{3, 0, 0, 8}, {8, 0, 0, 8}}));
  meter_eighths = 6;
  send({0xF0, 0x7D, 0x70, 0x05, 0x00, 0x00, 0xF7});
  CHECK(sent.size() == 1 && sent[0] == std::vector<uint8_t>({0xF0, 0x7D, 0x70, 0x45, 1, 64, CHORD_MEMORY_STOPPED, 2, 6, 0xF7}), "INFO's answer");
  send({0xF0, 0x7D, 0x70, 0x05, 0x01, 0x00, 0xF7});
  CHECK(status_is(5, 3, 2), "INFO with a non-zero byte: 3");

  // ---- READ: the progression back, types spelled out, lengths as gaps; a recorded one too ----
  reset();
  send(write_msg({{10, 0, 0, 8}, {8, 0, 0, 8}, {0, 4, (3 + 1) << 1 | 1, 5}}));
  send({0xF0, 0x7D, 0x70, 0x02, 0x00, 0x00, 0xF7});
  {
    std::vector<uint8_t> want = write_msg({{9, 2, 0, 8}, {6, 3, 0, 8}, {0, 4, (3 + 1) << 1 | 1, 8}});
    want[3] = 0x42;
    CHECK(sent.size() == 1 && sent[0] == want, "READ's answer (the last chord stretched to the bar)");
  }
  reset();
  chord_memory_state = CHORD_MEMORY_PLAYING;
  chord_memory_count = 1;
  chord_memory[0] = {0, 6, &major, false, false, 0};
  chord_memory_length = 2000 * 12;   // 2000 eighths: split into 127s
  send({0xF0, 0x7D, 0x70, 0x02, 0x00, 0x00, 0xF7});
  {
    std::vector<E> es(15, E{0, 1, 0, 127});
    es.push_back(E{0, 1, 0, 2000 - 15 * 127});
    std::vector<uint8_t> want = write_msg(es);
    want[3] = 0x42;
    CHECK(sent.size() == 1 && sent[0] == want, "a long chord read back split into 127s");
  }
  reset();
  send({0xF0, 0x7D, 0x70, 0x02, 0x00, 0x00, 0xF7});
  CHECK(sent.size() == 1 && sent[0].size() == 13 && sent[0][6] == 0, "READ with chord memory empty: count 0");

  // ---- a READ's answer, sent back as a WRITE, makes the same progression ----
  reset();
  send(write_msg({{10, 0, 0, 3}, {15, 5, 0x01, 6}, {20, 29, 2 << 1, 9}}));
  chord_moment before[3];
  memcpy(before, chord_memory, sizeof before);
  double before_length = chord_memory_length;
  send({0xF0, 0x7D, 0x70, 0x02, 0x00, 0x00, 0xF7});
  std::vector<uint8_t> again = sent[0];
  again[3] = 0x01;
  reset();
  send(again);
  bool same = chord_memory_count == 3 && chord_memory_length == before_length;
  for (int i = 0; same && i < 3; i++)
    same = before[i].at == chord_memory[i].at && before[i].fundamental == chord_memory[i].fundamental &&
           before[i].chord == chord_memory[i].chord && before[i].sharp == chord_memory[i].sharp &&
           before[i].slashed == chord_memory[i].slashed && (!before[i].slashed || before[i].slash == chord_memory[i].slash);
  CHECK(same, "READ then WRITE: the same progression");

  // ---- the largest WRITE fits the Teensy's SysEx buffer ----
  std::vector<E> full(64, E{20, 29, 0x0F, 127});
  CHECK(write_msg(full).size() == progression::WRITE_MAX && progression::WRITE_MAX <= 290, "64 entries: %zu bytes", write_msg(full).size());
  reset();
  send(write_msg(full));
  CHECK(status_is(1, 0, 64), "64 entries taken");

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
