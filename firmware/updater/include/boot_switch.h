// The boot switch spike: what the switch-test app (src/switch_test) and the updater (src/updater) share.
//
// The minichord's firmware starts where the Teensy's boot ROM is told to: the boot pointer at 0x60001000
// (sector 1), the IVT and its boot data, whose entry is the program's ResetHandler. The updater is a second,
// small program that lives for good in 24 kB nobody uses, between the user kit's header sector and the
// presets, and runs from there once the boot pointer names it. Flipping that pointer is the boot switch:
// the app points it at the updater (ENTER), the updater puts the app's own back (BACK). This spike proves the
// switch on the bench before the real updater is written: no firmware is sent here, nothing but sector 1 and
// the updater's own slot is ever written, and the presets, the kit's header and the EEPROM are only read
// (their CRCs go in INFO, so the bench can see they never change).
//
// Flash, Teensy 4.0 (the firmware's flash_room.py and main.cpp's preset store agree with these):
//   0x60000000  sector 0, the FlexSPI configuration block: never written here
//   0x60001000  sector 1, the boot pointer and the start of the program's code: the switch
//   0x601E0000  the user kit's header sector: never written here
//   0x601E1000  the updater's slot, 24 kB: written by the app, at boot, when it doesn't hold its updater
//   0x601E7000  the old sector 1, saved by ENTER for BACK
//   0x601E8000  the presets, 32 kB: never written here
//   0x601F0000  the core's EEPROM emulation, 15 sectors: never written here
//
// SysEx, F0 7D 66 <op> ... F7 (7D the fork's own ID, 66 "f" for firmware; the kit is 6B, pages 6D, locks
// 6C). Every message to the minichord is at least 7 bytes, as the kit's are. Ops 01 to 04 are kept for the
// real updater's BEGIN, DATA, COMMIT and ABORT. Neither program ever answers an op of 0x40 or above.
//   05 INFO   01 00             both: the INFO reply below
//   08 ENTER  55 50 ("UP")      app: save sector 1 to 0x601E7000, point the switch at the updater, restart
//   09 TIME   rounds 00         app: time the erase and the programming of 0x601E7000, 1 to 20 rounds
//   0A BACK   42 4B ("BK")      updater: put the saved sector 1 back, restart into the app
//   0B BLANK  42 4C 41 4E 4B    updater, test only: erase sector 1 and stay running, the worst case staged
// Status reply: F0 7D 66 <0x40 | op> status F7. The control command 11 restart (F0 00 00 0B 42 F7) into the
// Teensy's bootloader works in both, and so does the 134 baud request on their serial port.
//
// INFO reply: F0 7D 66 45, then who (0 app, 1 updater), proto (1), flags, then n5 CRC-32s of the regions
// below in that order, n5 the entry the switch names now, n5 the entry the saved copy names, then the app's
// last TIME as n3 microseconds: erase min, erase max, programming (16 pages) min, max (0 before any TIME and
// in the updater), and F7: 65 bytes in all. Numbers go as 7-bit groups, least significant first, as the kit's.
#pragma once
#include <stdint.h>
#include <string.h>

namespace boot_switch {

constexpr uint32_t FLASH = 0x60000000;
constexpr uint32_t SECTOR = 4096;             // the flash's erase size
constexpr uint32_t PAGE = 256;                // and its program size
constexpr uint32_t SWITCH = 0x60001000;       // sector 1
constexpr uint32_t BOOT_DATA = SWITCH + 0x20; // the IVT is 32 bytes, its boot data follows
constexpr uint32_t KIT_HEADER = 0x601E0000;
constexpr uint32_t SLOT = 0x601E1000;
constexpr uint32_t SLOT_SIZE = 0x6000;
constexpr uint32_t SAVED = SLOT + SLOT_SIZE;  // 0x601E7000
constexpr uint32_t PRESETS = 0x601E8000;
constexpr uint32_t PRESETS_SIZE = 0x8000;
constexpr uint32_t EEPROM_AREA = 0x601F0000;
constexpr uint32_t EEPROM_SIZE = 15 * SECTOR;
static_assert(SAVED + SECTOR == PRESETS, "the saved copy is the last sector before the presets");
static_assert(KIT_HEADER + SECTOR == SLOT, "the slot starts right after the kit's header sector");

constexpr uint8_t ID = 0x7D, SUB = 0x66, PROTO = 1;
enum : uint8_t { OP_INFO = 0x05, OP_ENTER = 0x08, OP_TIME = 0x09, OP_BACK = 0x0A, OP_BLANK = 0x0B };
enum : uint8_t { WHO_APP = 0, WHO_UPDATER = 1 };
// the kit's numbers where they mean the same; 12 to 14 are new
enum : uint8_t { STATUS_OK = 0, STATUS_BAD_MESSAGE = 3, STATUS_READ_BACK = 8, STATUS_WRONG_PROGRAM = 12,
                 STATUS_NO_UPDATER = 13, STATUS_NO_SAVED = 14 };
// INFO's flags
enum : uint8_t {
  FLAG_SLOT_MATCHES = 1,     // app: the slot holds the updater this app carries
  FLAG_INSTALLED = 2,        // app: it wrote the slot at this boot
  FLAG_SAVED_BOOTS_APP = 4,  // the saved copy is a boot pointer to a program outside the slot (BACK can use it)
  FLAG_SWITCH_UPDATER = 8,   // the switch names the updater (an entry inside the slot)
  FLAG_SWITCH_BLANK = 16,    // sector 1 holds no boot pointer (BLANK, a flip cut short, or garbage: INFO's CRC tells)
};

struct region_t { uint32_t addr, size; };
// INFO's CRCs, in this order
constexpr region_t REGIONS[] = {
  {FLASH, SECTOR}, {SWITCH, SECTOR}, {KIT_HEADER, SECTOR}, {SLOT, SLOT_SIZE}, {SAVED, SECTOR},
  {PRESETS, PRESETS_SIZE}, {EEPROM_AREA, EEPROM_SIZE},
};
constexpr uint32_t REGION_COUNT = sizeof REGIONS / sizeof REGIONS[0];
constexpr uint32_t INFO_LENGTH = 4 + 3 + 5 * REGION_COUNT + 5 + 5 + 4 * 3 + 1;
static_assert(INFO_LENGTH == 65, "INFO is 65 bytes");

// The only flash this spike changes outside sector 1: the slot and the saved copy, whole pages inside them.
// Sector 1 is written only by flip(), which names it itself.
static inline bool may_write(uint32_t addr, uint32_t len) {
  return len > 0 && addr >= SLOT && addr <= SAVED + SECTOR && len <= SAVED + SECTOR - addr;
}

// CRC-32 (zlib's), as main.cpp's crc32_update
static inline uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

static inline uint8_t *groups(uint8_t *out, uint32_t value, int count) {
  for (int i = 0; i < count; i++, value >>= 7) *out++ = value & 0x7F;
  return out;
}

static inline uint32_t rd32(uint32_t addr) { return *(const volatile uint32_t *)(uintptr_t)addr; }

// A boot pointer at addr (sector 1 or the saved copy): the IVT's tag and length (0xD1, 32 bytes; the core
// writes version 0x43), and its own address and its boot data's where sector 1 has them. Its entry, or 0
// when there's none.
static inline uint32_t boot_entry(uint32_t addr) {
  if ((rd32(addr) & 0x00FFFFFF) != 0x002000D1) return 0;
  if (rd32(addr + 20) != SWITCH || rd32(addr + 16) != BOOT_DATA) return 0;
  return rd32(addr + 4);
}
static inline bool in_slot(uint32_t entry) { return entry >= SLOT && entry < SLOT + SLOT_SIZE; }

// A message from the computer, F0 7D 66 op ... F7: its op, or 0 when it isn't one (or isn't well formed)
static inline uint8_t op_of(const uint8_t *m, uint32_t n) {
  if (n < 7 || m[0] != 0xF0 || m[1] != ID || m[2] != SUB || m[n - 1] != 0xF7) return 0;
  for (uint32_t i = 1; i < n - 1; i++) if (m[i] & 0x80) return 0;
  return m[3] < 0x40 ? m[3] : 0;
}

// The whole message, F0 to F7, against the one expected: the keys (UP, BK, BLANK) can't be hit by a stray byte
static inline bool is_exactly(const uint8_t *m, uint32_t n, const uint8_t *want, uint32_t wn) {
  return n == wn && memcmp(m, want, n) == 0;
}

}  // namespace boot_switch

#ifdef __arm__
#include <Arduino.h>
extern "C" void eepromemu_flash_write(void *addr, const void *data, uint32_t len);
extern "C" void eepromemu_flash_erase_sector(void *addr);

namespace boot_switch {

constexpr int LED_R = 0, LED_G = 1, LED_B = 2, MUTE = 3;   // include/def.h's R/G/B_LED_PIN, _MUTE_PIN

// The DAC muted (LOW, as the firmware mutes it) and the LED one colour, at the main firmware's full levels
static inline void pins_start() {
  pinMode(MUTE, OUTPUT);
  digitalWrite(MUTE, LOW);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
}
static inline void led(bool r, bool g, bool b) {
  analogWrite(LED_R, r ? 200 : 0);
  analogWrite(LED_G, g ? 115 : 0);
  analogWrite(LED_B, b ? 70 : 0);
}

// Pages into the slot or the saved copy, from RAM (the flash can't be read while it's programmed). False
// when the guard refuses. Each erase and page runs with interrupts off and turns them back on after (the
// core's flash_wait), which is fine here: nothing these two programs run between them is in the flash written.
static inline bool erase(uint32_t addr) {
  if ((addr & (SECTOR - 1)) || !may_write(addr, SECTOR)) return false;
  eepromemu_flash_erase_sector((void *)addr);
  return true;
}
static inline bool program(uint32_t addr, const uint8_t *data, uint32_t len) {
  if ((addr & (PAGE - 1)) || (len & (PAGE - 1)) || !may_write(addr, len)) return false;
  for (uint32_t at = 0; at < len; at += PAGE) eepromemu_flash_write((void *)(addr + at), data + at, PAGE);
  return true;
}

// A restart, the computer told first: USB's run/stop off, so the device leaves the bus, then a system reset,
// after which the boot ROM reads the switch again
static inline void restart() {
  USB1_USBCMD = 0;
  delay(20);
  __disable_irq();
  SCB_AIRCR = 0x05FA0004;
  while (1) ;
}

// Sector 1 erased and len bytes (whole pages, from RAM) written into it, the rest left FF, the boot pointer's
// page, page 0, last: a write cut short leaves no boot pointer at all (nothing starts: the pin and a
// computer), never one naming code half written. Up to three tries. True when it reads back whole. For
// the updater, whose code is all in its slot; the app, whose code starts in sector 1, flips with flip().
static inline bool write_switch(const uint8_t *data, uint32_t len) {
  for (int attempt = 0; attempt < 3; attempt++) {
    eepromemu_flash_erase_sector((void *)SWITCH);
    for (uint32_t page = len / PAGE; page-- > 0;)
      eepromemu_flash_write((void *)(SWITCH + page * PAGE), data + page * PAGE, PAGE);
    if (memcmp((const void *)SWITCH, data, len) == 0) return true;
  }
  return false;
}

// The app's flip: write_switch's order and tries, then a restart, with nothing else allowed to run. It
// never returns. Everything it runs is in RAM (FASTRUN; the core's flash routines and memcpy are in ITCM
// too): the app's own code can sit in sector 1 (its ResetHandler and FLASHMEM functions start at
// 0x60001400), and the core's flash_wait turns interrupts back on after every erase and page, so every
// interrupt is switched off at the NVIC and the SysTick's too, not just masked. USB is stopped first, so
// the computer sees the minichord leave. A write that never reads back whole restarts all the same: there
// is nowhere to return to, and what's in sector 1 then is the pin's business.
FASTRUN __attribute__((noinline, noreturn)) static void flip(const uint8_t *data, uint32_t len) {
  if (len > SECTOR) len = SECTOR;         // the callers pass constants; this keeps a later edit inside sector 1
  len &= ~(PAGE - 1);
  __disable_irq();
  SYST_CSR &= ~SYST_CSR_TICKINT;
  for (int i = 0; i < 8; i++) {
    (&NVIC_ICER0)[i] = 0xFFFFFFFF;
    (&NVIC_ICER0)[i + 64] = 0xFFFFFFFF;   // ICPR: nothing left pending either
  }
  USB1_USBCMD = 0;
  for (int attempt = 0; attempt < 3; attempt++) {
    eepromemu_flash_erase_sector((void *)SWITCH);
    for (uint32_t page = len / PAGE; page-- > 0;)
      eepromemu_flash_write((void *)(SWITCH + page * PAGE), data + page * PAGE, PAGE);
    bool whole = true;                    // read back by hand: memcmp is ITCM too, but this needs nothing
    for (uint32_t i = 0; i < len; i++) whole = whole && ((const volatile uint8_t *)SWITCH)[i] == data[i];
    if (whole) break;
  }
  SCB_AIRCR = 0x05FA0004;
  while (1) ;
}

// An INFO reply into out (INFO_LENGTH bytes)
static inline void info(uint8_t *out, uint8_t who, uint8_t flags, const uint32_t *times) {
  uint8_t *p = out;
  *p++ = 0xF0; *p++ = ID; *p++ = SUB; *p++ = 0x40 | OP_INFO;
  uint32_t entry = boot_entry(SWITCH), saved = boot_entry(SAVED);
  if (in_slot(entry)) flags |= FLAG_SWITCH_UPDATER;
  if (!entry) flags |= FLAG_SWITCH_BLANK;
  if (saved && !in_slot(saved)) flags |= FLAG_SAVED_BOOTS_APP;
  *p++ = who; *p++ = PROTO; *p++ = flags & 0x7F;
  for (uint32_t r = 0; r < REGION_COUNT; r++)
    p = groups(p, crc32(0, (const uint8_t *)REGIONS[r].addr, REGIONS[r].size), 5);
  p = groups(p, entry, 5);
  p = groups(p, saved, 5);
  for (int t = 0; t < 4; t++) p = groups(p, times ? times[t] : 0, 3);
  *p++ = 0xF7;
}

static inline void status(uint8_t op, uint8_t code) {
  uint8_t m[6] = {0xF0, ID, SUB, (uint8_t)(0x40 | op), code, 0xF7};
  usbMIDI.sendSysEx(sizeof m, m, true);
}

// Control command 11 with 66, as the firmware's: F0 00 00 0B 42 F7
static inline bool is_update_mode_request(const uint8_t *m, uint32_t n) {
  static const uint8_t want[] = {0xF0, 0x00, 0x00, 0x0B, 0x42, 0xF7};
  return is_exactly(m, n, want, sizeof want);
}

}  // namespace boot_switch
#endif
