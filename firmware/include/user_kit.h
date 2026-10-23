// USER KIT: drum kit 13 (337 = 13), "your own": up to thirteen of the player's own one-shots in place of the
// drum machine's thirteen voices, sent from minicontrol (or minictl) as one image over SysEx and kept in
// flash, played from there. generator/user_kit.md is the contract (the image, where it lives, the messages
// and what the minichord does with each); include/user_kit_core.h is the slot itself, with nothing of the
// Teensy in it so a computer can run it; this is the Teensy's side of it, included by main.cpp after the
// preset store's flash routines (crc32_update, flash_program, flash_erase), which it uses.
//
// Where: the sound from data_base, the first 64 kB boundary after the firmware's image (or 0x601A0000 if
// that's lower), up to 0x601E0000, so up to 256 kB; the header in the 4 kB sector at 0x601E0000, in the
// presets' 64 kB block, which a firmware upload never erases (the presets are at 0x601E8000). A bigger
// firmware moves data_base up and the stored kit no longer checks (its header holds the old data_base):
// INFO says why, and the kit is sent again. With less than 64 kB left there is no slot at all
// (scripts/flash_room.py warns of that, and fails the build when the image reaches the header).
//
// An upload: BEGIN comes in through processMIDI (mts_receive hands every SysEx here first) and is only
// noted; drums_update, from the loop, opens it (user_kit_upload, whose stack holds the upload's buffers, so
// they take no room in RAM2 between uploads), refusing it while the rhythm runs (its timer strikes drums
// and queues MIDI every millisecond) or the looper counts in, records or plays. Until that pass an ABORT
// calls it off and a CLEAR calls it off and clears (user_kit_core.h). Open: the MIDI already queued goes out
// with the drums' note-offs still waiting, the chord notes still waiting to start are called off, the DAC
// is muted as a preset save mutes it, the kit is given up (the drum machine moved off it, its sample
// entries emptied), the old header erased, and the minichord runs the upload loop and nothing else: USB
// MIDI read as fast as it comes, the kit's messages answered, every other message dropped, the bank LED
// pulsing. Each sector is erased as the sound first reaches it and each page programmed as it fills and
// read back; the header goes in last. COMMIT, ABORT, CLEAR, a failure, or 5 s with no DATA (INFO doesn't
// count) ends it, and all of them leave the same way (user_kit::finish): the DAC back on, the drums'
// note-offs still waiting sent (late, which is harmless), the rest of the MIDI output queue emptied (what
// queued meanwhile is stale), any drum hit still queued dropped, the notes held from MIDI in released as
// All Notes Off releases them (their note-offs were dropped with everything else), and the LED green with
// a kit, red without, for a moment. The 5 s are timed by the processor's cycle counter: an erase holds
// interrupts off for tens of milliseconds, which millis() loses. Replies go out with usbMIDI.sendSysEx and
// never send_now(): the USB freeze fixed in ed85cd2 came from a flush in the program meeting the
// start-of-frame interrupt's.
//
// What keeps the audio off the flash while it's written is the drum machine moved off the kit before
// anything is erased (user_kit_invalidate, first in BEGIN and CLEAR), and no other audio object reading the
// kit's flash. The mute's AudioNoInterrupts isn't a guarantee: anything that calls AudioInterrupts lifts it
// (play_single_note does, through set_chord_voice_frequency, which is why the chord's note_timers are ended
// first), so the audio's update may run between flash operations.
//
// The kit: the base kit the header names copied into RAM, and each voice with a hit made a recorded
// stroke's voice (drum_stroke_voice, from generator/drums.py) playing drum_samples[10 + v], which points at
// the hit in flash, at its level, pan and choke group; an empty voice stays the base kit's. Its press bends
// flag is what a press on plate 13 does to it (DRUM PRESS, drum_voice_metal): bend it, or choke it. With no
// kit stored, kit 13 plays the studio kit (drum kit 4, user_kit::STAND_IN_KIT) in its place.
#pragma once
#define USER_KIT_COLD FLASHMEM
#define USER_KIT_CRC32 crc32_update
#include "user_kit_core.h"

extern unsigned long _flashimagelen;   // the linker's: the firmware image's length is this symbol's address
const uint32_t kit_header = 0x601E0000;   // the contract's KIT_HEADER, a sector of its own
const uint32_t kit_top = 0x601E0000;      // the sound ends below it
const uint32_t kit_floor = 0x601A0000;    // and starts no lower: 256 kB at most
static_assert(kit_top <= preset_store_base && kit_header + flash_sector <= preset_store_base, "the user kit runs into the preset store");
static_assert(kit_header % flash_sector == 0 && kit_floor % 65536 == 0 && kit_floor < kit_top, "the user kit's flash is out of line");
static_assert(user_kit::SECTOR == flash_sector, "the user kit's sector is the flash's");
static_assert(sizeof(drum_kits) / sizeof(drum_kit_t) >= 12, "a stored kit's base_kit can be any of 1 to 12");
static_assert(user_kit::STAND_IN_KIT >= 1 && user_kit::STAND_IN_KIT <= sizeof(drum_kits) / sizeof(drum_kit_t), "kit 13's stand-in isn't a kit");

// What lasts between uploads; the upload's own buffers (user_kit::work_t) are user_kit_upload's, on the stack
DMAMEM static user_kit::slot_t user_kit_slot;   // 104 bytes, in RAM2 (set up by user_kit_boot)
DMAMEM static drum_kit_t user_kit_drums;        // the kit the drum machine plays, 1,416 bytes
static bool user_kit_ready = false;             // user_kit_boot has run
static bool user_kit_valid = false;             // user_kit_drums is built from a stored kit
static uint32_t user_kit_built = 0;             // one more each time it's built or given up, for drums_update
static uint16_t user_kit_bend_mask = 0;         // bit v: a press bends voice v rather than choking it
static uint8_t user_kit_cable = 0;              // the cable the last kit message came on: the reply goes there
static bool user_kit_led_shown = false;         // the end's green or red, until user_kit_led_until
static uint32_t user_kit_led_until = 0;

// ---- what the slot asks of the Teensy (user_kit::io_t) ----

FLASHMEM static const uint8_t *user_kit_flash(uint32_t addr) { return (const uint8_t *)addr; }   // through the cache, purged by every write
FLASHMEM static void user_kit_erase(uint32_t addr) { flash_erase((void *)addr, flash_sector); }
FLASHMEM static void user_kit_program(uint32_t addr, const uint8_t *data, uint32_t len) { flash_program((void *)addr, data, len); }
FLASHMEM static void user_kit_send(const uint8_t *message, uint32_t len) { usbMIDI.sendSysEx(len, message, true, user_kit_cable); }

// Microseconds from the cycle counter, which runs on through an erase: its 32 bits wrap every 7.2 s, so
// this keeps up only while it's called more often than that, as it is in the upload loop; between uploads
// its first call can jump, and the slot only measures from a call made after that.
FLASHMEM static uint32_t user_kit_now_us() {
  static uint32_t last = 0, us = 0, spare = 0;
  const uint32_t per_us = F_CPU_ACTUAL / 1000000;
  uint32_t now = ARM_DWT_CYCCNT;
  uint32_t cycles = now - last + spare;
  last = now;
  us += cycles / per_us;
  spare = cycles % per_us;
  return us;
}

// The drums' note-offs still waiting, queued now rather than at their time (drums_update's way), and the ring
// emptied
FLASHMEM static void user_kit_drum_offs_now() {
  noInterrupts();
  while (drum_off_tail != drum_off_head) {
    queue_midi(false, drum_offs[drum_off_tail].note, 0, 10, drum_offs[drum_off_tail].port);
    drum_off_tail = (drum_off_tail + 1) % 32;
  }
  interrupts();
}
// The MIDI output queue sent, all of it: drain_midi_queue sends 16 a call (and 32 calls are more than the
// queue holds, so an interrupt that keeps queueing can't hold this here)
FLASHMEM static void user_kit_drain_midi() {
  for (uint8_t calls = 0; calls < 32 && midi_queue_tail != midi_queue_head; calls++) drain_midi_queue();
}

// As save_config mutes for a preset save: the DAC's mute pin, and the audio library's updates held (see
// above for what that hold doesn't promise). Before the flash changes (an upload opening, or CLEAR), what's
// owed goes first: the chord notes still waiting to start are called off (play_single_note would lift the
// hold, and the upload drops the chord anyway), then the MIDI already queued is sent, and the drums'
// note-offs still waiting with it, so no note the minichord began is left hanging.
FLASHMEM static void user_kit_mute(bool on) {
  if (on) {
    for (uint8_t i = 0; i < 4; i++) note_timer[i].end();
    user_kit_drain_midi();
    user_kit_drum_offs_now();
    user_kit_drain_midi();
    digitalWrite(_MUTE_PIN, LOW);
    AudioNoInterrupts();
  } else {
    AudioInterrupts();
    digitalWrite(_MUTE_PIN, HIGH);
  }
}

// The kit given up before its flash changes: the drum machine off it (setKit stops every voice, and the hits
// still queued for it are dropped) before the sample entries its voices read are emptied. This, not the
// mute, is what keeps the audio from reading the flash being written.
FLASHMEM static void user_kit_invalidate() {
  user_kit_valid = false;
  user_kit_built++;
  if (drum_machine.currentKit() == &user_kit_drums) drum_machine.setKit(&drum_kits[user_kit::STAND_IN_KIT - 1]);
  drum_machine.clearHits();
  for (uint8_t v = 0; v < DRUM_VOICES; v++) drum_samples[drum_sample_builtin + v] = {nullptr, 0, 0, 0.0f};
  user_kit_bend_mask = 0;
}

// The kit built from a stored header that passed every check (user_kit::refresh): the base kit, and a
// recorded stroke's voice for each voice with a hit. drums_update hands it to the drum machine.
FLASHMEM static void user_kit_apply(const uint8_t *h) {
  user_kit_invalidate();
  memcpy(&user_kit_drums, &drum_kits[h[7] - 1], sizeof user_kit_drums);   // its grit (bits, hold, drive) too
  user_kit_drums.name = "your own";
  const uint32_t data_base = user_kit_slot.data_base;
  for (uint8_t v = 0; v < DRUM_VOICES; v++) {
    const uint8_t *e = h + 44 + 16 * v;
    const uint32_t length = user_kit::rd32(e + 4);
    if (!length) continue;   // the base kit's own voice: never an entry of length 0 (update() reads length - 1)
    drum_samples[drum_sample_builtin + v] = {(const int16_t *)(data_base + user_kit::rd32(e)), length, user_kit::rd16(e + 8),
                                             user_kit::rd16(e + 14) / 10.0f};
    drum_voice_t &p = user_kit_drums.v[v];
    p = drum_stroke_voice;
    p.sample = drum_sample_builtin + v;
    p.level = e[10] / 100.0f;
    p.pan = (int8_t)e[11] / 100.0f;
    p.choke = e[12];
    p.follow = 0;
    if (e[13] & 0x02) user_kit_bend_mask |= 1 << v;
  }
  user_kit_valid = true;
  user_kit_built++;
}

// Upload mode left (user_kit::finish, after the DAC is back on): the drums' note-offs still waiting are sent
// (late, which is harmless: a note on the other end left hanging isn't), what else queued meanwhile goes
// unsent, a drum hit still queued is dropped (it would sound now, seconds late, its MIDI note-on gone with
// the queue), the notes MIDI in holds are let go (the upload loop dropped their note-offs), and the LED
// says how it went
FLASHMEM static void user_kit_ended(bool kit) {
  noInterrupts();
  midi_queue_tail = midi_queue_head;
  interrupts();
  user_kit_drum_offs_now();   // on the queue, for the loop's drain_midi_queue
  drum_machine.clearHits();
  if (midi_in_plays) midi_in_release_all();   // as All Notes Off does
  set_led_color(kit ? 120 : 0, 1.0, 1 - led_attenuation);
  user_kit_led_shown = true;
  user_kit_led_until = millis() + 800;
}

static const user_kit::io_t user_kit_io PROGMEM = {user_kit_flash, user_kit_erase, user_kit_program, user_kit_send, user_kit_now_us,
                                                   user_kit_mute, user_kit_invalidate, user_kit_apply, user_kit_ended};

// ---- what main.cpp calls ----

// At boot, after the presets load: where the slot is for this firmware, and the stored kit checked in full
FLASHMEM void user_kit_boot() {
  const uint32_t image_end = 0x60000000 + (uint32_t)&_flashimagelen;
  uint32_t data_base = (image_end + 65535) & ~65535u;
  if (data_base < kit_floor) data_base = kit_floor;
  const uint32_t capacity = data_base < kit_top ? kit_top - data_base : 0;
  user_kit::init(user_kit_slot, &user_kit_io, kit_header, data_base, capacity);
  user_kit_ready = true;
  Serial.printf("User kit: %lu kB from 0x%08lX, state %d (reason %d)\n", capacity / 1024, data_base, user_kit_slot.state, user_kit_slot.reason);
}

// Every SysEx, from mts_receive before anything else looks at it: whether it was a kit message
FLASHMEM bool user_kit_message(const byte *data, uint32_t length) {
  if (!user_kit_ready) return false;
  user_kit_cable = usbMIDI.getCable();
  return user_kit::receive(user_kit_slot, data, length);
}

// the kit 13 plays: the player's own, or the studio kit standing in while none is stored
const drum_kit_t *user_kit_playing() { return user_kit_valid ? &user_kit_drums : &drum_kits[user_kit::STAND_IN_KIT - 1]; }
uint32_t user_kit_generation() { return user_kit_built; }
bool user_kit_bends(uint8_t v) { return user_kit_bend_mask >> v & 1; }

// What would have the rhythm timer strike drums or queue MIDI while the flash is written, or the looper
// record or play through it. MIDI clock out (266) goes from the rhythm timer only, so it's in the first.
FLASHMEM static bool user_kit_busy() {
  return rythm_timer_running || looper_state == LOOPER_COUNTIN || looper_state == LOOPER_RECORDING || looper_state == LOOPER_PLAYING;
}

// A BEGIN noted run, then, if it opened, the upload loop until the upload ends. The upload's buffers are
// this function's, on the stack (512 bytes of the 22 kB the stack has), lent to the slot for as long as
// it's open: the loop doesn't return while it is. noinline, so drums_update's every pass doesn't carry them.
FLASHMEM __attribute__((noinline)) static void user_kit_upload() {
  user_kit::work_t work;
  user_kit::run_begin(user_kit_slot, user_kit_busy(), work);
  uint32_t lit = user_kit_now_us() - 20000;
  while (user_kit::is_open(user_kit_slot)) {
    while (usbMIDI.read()) {
      if (usbMIDI.getType() != usbMIDI.SystemExclusive) continue;   // dropped: nothing else runs meanwhile
      user_kit_cable = usbMIDI.getCable();
      user_kit::receive(user_kit_slot, usbMIDI.getSysExArray(), usbMIDI.getSysExArrayLength());
      if (!user_kit::is_open(user_kit_slot)) break;   // what follows goes to processMIDI again
    }
    if (!user_kit::is_open(user_kit_slot)) break;
    uint32_t now = user_kit_now_us();
    if (now - lit >= 20000) {   // the bank LED pulsing, twice a second
      lit = now;
      float pulse = 0.5f + 0.5f * sinf((now % 500000) * (2 * 3.14159265f / 500000));
      set_led_color(bank_led_hue, bank_led_saturation(), (0.15f + 0.85f * pulse) * (1 - led_attenuation));
    }
    user_kit::poll(user_kit_slot);
  }
}

// From drums_update, every pass of the loop: the end's LED put back, and a BEGIN noted run
FLASHMEM void user_kit_update() {
  if (user_kit_led_shown && (int32_t)(millis() - user_kit_led_until) >= 0) {
    user_kit_led_shown = false;
    set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);   // an animation running takes it on its next step
  }
  if (user_kit_ready && user_kit::begin_waiting(user_kit_slot)) user_kit_upload();
}
