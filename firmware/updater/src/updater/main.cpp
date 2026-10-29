// The updater, spike form: a second program in the minichord's flash, at 0x601E1000 (updater.ld), that the
// boot ROM runs when the boot pointer in sector 1 names it. Here it only proves the boot switch: it answers
// INFO, puts the app's saved boot pointer back (BACK), and, for the bench's worst case, erases sector 1
// (BLANK). The real one will take a firmware over SysEx. Blue LED; red once sector 1 is blank. See
// include/boot_switch.h for the flash and the messages.
#include <Arduino.h>
#include "boot_switch.h"

using namespace boot_switch;

static uint8_t saved_copy[SECTOR];   // the saved sector 1, in RAM: the flash can't be read while it's written
static bool blanked = false;

static void handle(const uint8_t *m, uint32_t n) {
  if (is_update_mode_request(m, n)) {
    Serial.println("updater: restarting into the Teensy's bootloader");
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
    info(out, WHO_UPDATER, 0, nullptr);
    usbMIDI.sendSysEx(sizeof out, out, true);
    return;
  }
  case OP_BACK: {
    static const uint8_t want[] = {0xF0, ID, SUB, OP_BACK, 0x42, 0x4B, 0xF7};
    if (!is_exactly(m, n, want, sizeof want)) { status(op, STATUS_BAD_MESSAGE); return; }
    uint32_t entry = boot_entry(SAVED);
    if (!entry || in_slot(entry)) {
      Serial.println("updater: BACK refused, the saved copy is no boot pointer to the app");
      status(op, STATUS_NO_SAVED);
      return;
    }
    memcpy(saved_copy, (const void *)SAVED, SECTOR);
    // the copy checked as the flash was (boot_entry reads values, so a RAM copy passes as sector 1 would)
    if (memcmp(saved_copy, (const void *)SAVED, SECTOR) != 0 || boot_entry((uint32_t)saved_copy) != entry) {
      status(op, STATUS_READ_BACK);
      return;
    }
    Serial.print("updater: BACK, the switch to 0x");
    Serial.println(entry, HEX);
    // the updater's code is all in its slot, so it writes sector 1 as any flash, checks it, and only then
    // restarts; a write that won't read back whole leaves it running, red, for the bench to see
    if (!write_switch(saved_copy, SECTOR)) {
      Serial.println("updater: BACK FAILED, sector 1 doesn't read back");
      led(true, false, false);
      status(op, STATUS_READ_BACK);
      return;
    }
    status(op, STATUS_OK);
    delay(20);                                   // the start-of-frame interrupt sends the reply on
    restart();
  }
  case OP_BLANK: {
    static const uint8_t want[] = {0xF0, ID, SUB, OP_BLANK, 0x42, 0x4C, 0x41, 0x4E, 0x4B, 0xF7};
    if (!is_exactly(m, n, want, sizeof want)) { status(op, STATUS_BAD_MESSAGE); return; }
    Serial.println("updater: BLANK, sector 1 erased: nothing will start at the next power on");
    eepromemu_flash_erase_sector((void *)SWITCH);   // the updater's own code is all in its slot
    blanked = true;
    led(true, false, false);
    status(op, STATUS_OK);
    return;
  }
  default:
    status(op, op == OP_ENTER || op == OP_TIME ? STATUS_WRONG_PROGRAM : STATUS_BAD_MESSAGE);
  }
}

void setup() {
  pins_start();
  led(false, false, true);
  Serial.begin(9600);
}

void loop() {
  while (usbMIDI.read()) {
    if (usbMIDI.getType() != usbMIDI.SystemExclusive) continue;
    handle(usbMIDI.getSysExArray(), usbMIDI.getSysExArrayLength());
  }
  static elapsedMillis since;
  if (since >= 2000) {   // a line now and then, so a serial monitor shows which program runs
    since = 0;
    Serial.print("updater: running from the slot, switch entry 0x");
    Serial.print(boot_entry(SWITCH), HEX);
    Serial.println(blanked ? ", sector 1 blank" : "");
  }
}
