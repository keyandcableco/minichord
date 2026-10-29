// The switch-test app: a stand-in for the minichord's firmware on the bench, small enough that nothing but
// the boot switch is being tested. It carries the updater (scripts/embed_updater.py puts it here from the
// updater's build) and writes it into its slot at boot when the slot doesn't hold it, answers INFO, times the
// flash (TIME), and on ENTER saves its own sector 1 and points the switch at the updater. No sound, no
// touch, no buttons: the real firmware's ENTER (with the chord-button press that confirms it) comes later.
// Green LED. See include/boot_switch.h for the flash and the messages.
#include <Arduino.h>
#include "boot_switch.h"
#include "updater_blob.h"   // generated: updater_image, updater_boot_page, UPDATER_LENGTH, UPDATER_ENTRY

using namespace boot_switch;

static_assert(UPDATER_LENGTH <= SLOT_SIZE && UPDATER_LENGTH % PAGE == 0, "the updater fits its slot in whole pages");

static uint8_t buffer[SECTOR];     // a sector in RAM: the flash can't be read while it's programmed
static bool installed = false;     // the slot written at this boot
static uint32_t times[4];          // TIME's: erase min and max, programming min and max, microseconds

// The slot holds this updater and nothing else: its bytes, then FF to the slot's end (a bigger updater's
// tail left there would make INFO's slot CRC differ from a clean install's)
static bool slot_matches() {
  if (memcmp((const void *)SLOT, updater_image, UPDATER_LENGTH) != 0) return false;
  for (uint32_t at = UPDATER_LENGTH; at < SLOT_SIZE; at++) if (((const uint8_t *)SLOT)[at] != 0xFF) return false;
  return true;
}

// The updater into its slot, page by page through RAM, the whole slot erased first, when it doesn't already hold it
static void install() {
  if (slot_matches()) return;
  bool ok = true;
  for (uint32_t at = 0; at < SLOT_SIZE; at += SECTOR) ok = ok && erase(SLOT + at);
  for (uint32_t at = 0; at < UPDATER_LENGTH; at += PAGE) {
    memcpy(buffer, updater_image + at, PAGE);
    ok = ok && program(SLOT + at, buffer, PAGE);
  }
  installed = true;
  Serial.println(ok && slot_matches() ? "switch test: updater installed" : "switch test: updater install FAILED");
}

static uint32_t us_since(uint32_t cycles) { return (ARM_DWT_CYCCNT - cycles) / (F_CPU_ACTUAL / 1000000); }

static void handle(const uint8_t *m, uint32_t n) {
  if (is_update_mode_request(m, n)) {
    Serial.println("switch test: restarting into the Teensy's bootloader");
    delay(20);
    _reboot_Teensyduino_();
  }
  uint8_t op = op_of(m, n);
  if (!op) return;
  switch (op) {
  case OP_INFO: {
    static const uint8_t want[] = {0xF0, ID, SUB, OP_INFO, PROTO, 0x00, 0xF7};
    if (!is_exactly(m, n, want, sizeof want)) { status(op, STATUS_BAD_MESSAGE); return; }
    uint8_t out[INFO_LENGTH];
    info(out, WHO_APP, (slot_matches() ? FLAG_SLOT_MATCHES : 0) | (installed ? FLAG_INSTALLED : 0), times);
    usbMIDI.sendSysEx(sizeof out, out, true);
    return;
  }
  case OP_TIME: {
    uint8_t rounds = n == 7 ? m[4] : 0;
    if (n != 7 || m[5] != 0 || rounds < 1 || rounds > 20) { status(op, STATUS_BAD_MESSAGE); return; }
    for (uint32_t i = 0; i < SECTOR; i++) buffer[i] = i * 7 + 1;   // anything not all FF
    times[0] = times[2] = 0xFFFFFFFF;
    times[1] = times[3] = 0;
    bool ok = true;
    for (uint8_t r = 0; r < rounds; r++) {
      uint32_t c = ARM_DWT_CYCCNT;
      ok = ok && erase(SAVED);
      uint32_t e = us_since(c);
      c = ARM_DWT_CYCCNT;
      ok = ok && program(SAVED, buffer, SECTOR);
      uint32_t p = us_since(c);
      times[0] = min(times[0], e); times[1] = max(times[1], e);
      times[2] = min(times[2], p); times[3] = max(times[3], p);
      ok = ok && memcmp((const void *)SAVED, buffer, SECTOR) == 0;
    }
    status(op, ok ? STATUS_OK : STATUS_READ_BACK);
    return;
  }
  case OP_ENTER: {
    static const uint8_t want[] = {0xF0, ID, SUB, OP_ENTER, 0x55, 0x50, 0xF7};
    if (!is_exactly(m, n, want, sizeof want)) { status(op, STATUS_BAD_MESSAGE); return; }
    if (!slot_matches()) { status(op, STATUS_NO_UPDATER); return; }
    uint32_t mine = boot_entry(SWITCH);
    // this app's sector 1, saved where BACK will find it, and read back
    memcpy(buffer, (const void *)SWITCH, SECTOR);
    bool ok = erase(SAVED) && program(SAVED, buffer, SECTOR);
    if (!ok || !mine || memcmp((const void *)SAVED, buffer, SECTOR) != 0 || boot_entry(SAVED) != mine) {
      status(op, STATUS_READ_BACK);
      return;
    }
    Serial.print("switch test: ENTER, the switch to the updater at 0x");
    Serial.println((uint32_t)UPDATER_ENTRY, HEX);
    status(op, STATUS_OK);
    delay(20);                                   // the start-of-frame interrupt sends the reply on
    led(false, false, false);
    memcpy(buffer, updater_boot_page, PAGE);     // in RAM for flip(), whatever section the header put it in
    flip(buffer, PAGE);
  }
  default:
    status(op, op == OP_BACK || op == OP_BLANK ? STATUS_WRONG_PROGRAM : STATUS_BAD_MESSAGE);
  }
}

void setup() {
  pins_start();
  led(false, true, false);
  Serial.begin(9600);
  install();
}

void loop() {
  while (usbMIDI.read()) {
    if (usbMIDI.getType() != usbMIDI.SystemExclusive) continue;
    handle(usbMIDI.getSysExArray(), usbMIDI.getSysExArrayLength());
  }
  static elapsedMillis since;
  if (since >= 2000) {   // a line now and then, so a serial monitor shows which program runs
    since = 0;
    Serial.print("switch test: running, switch entry 0x");
    Serial.print(boot_entry(SWITCH), HEX);
    Serial.println(slot_matches() ? ", updater in its slot" : ", updater NOT in its slot");
  }
}
