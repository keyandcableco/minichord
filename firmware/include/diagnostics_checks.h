// The checks that need the firmware's own state. Included from main.cpp just
// before setup(), after every global it reads has been declared. See
// diagnostics.h for the overview.
//
// Tags, roughly in order of how much they matter for the sound:
//   AUDIO   the audio interrupt started late or the output ran dry, CPU or
//           block memory crossed a level
//   CLIP    a stage hit full scale
//   HARP    sensor readings on long holds: the touch margin shrinking, the
//           raw baseline and filtered values at each release, re-touches
//           right after one; TRACE prints held pads continuously (t)
//   MARK    typed by the player (m) the moment a click is heard
//   STALL   a loop pass slow enough to delay the harp and buttons
//   STUCK   audio or MIDI still on for something nobody is holding
//   TAIL    a chord voice ringing long after every chord button is up
//   PITCH   a voice asked for a pitch outside the useful range, a glide
//           that saturates, a MIDI note number over 127
//   QUALITY the chord voices do not hold the chord's own pitch classes
//   BEAT    a summary every ten seconds
#ifndef DIAGNOSTICS_CHECKS_H
#define DIAGNOSTICS_CHECKS_H
#ifdef MINICHORD_DIAG

bool diag_verbose = false;
bool diag_settled = false;
bool diag_was_connected = false;

//>>HARP TRACKING
// The first two sessions could not tell a pad that drifts under a resting
// finger from an ordinary lift: the chip's filtered reading is still settling
// when the debounced release arrives, so both leave a delta just under the
// threshold. So every harp line now carries the two raw readings, baseline
// (b) and filtered (f), instead of a verdict, and trace mode (t) prints every
// held pad ten times a second. A drifting baseline shows as b falling toward
// a steady f; a finger lightening shows as f rising toward a steady b.
uint32_t diag_touch_at[12] = {0};
uint32_t diag_release_at[12] = {0};
uint32_t diag_last_hold_ms[12] = {0};
uint16_t diag_release_b[12] = {0};
uint16_t diag_release_f[12] = {0};
uint16_t diag_ref_b[12] = {0};   // first reading of a long hold, one second in
uint16_t diag_ref_f[12] = {0};
int16_t diag_min_delta[12] = {0};
bool diag_margin_warned[12] = {false};
bool diag_long_touch_warned[12] = {false};
uint8_t diag_margin_cursor = 0;
bool diag_trace = false;
uint32_t diag_mark_count = 0;
uint32_t diag_harp_chatter = 0;
uint32_t diag_harp_long_releases = 0;
uint32_t diag_harp_margin_warnings = 0;

// The lowest margin seen during a hold, or "unsampled" when the pad released
// before the round robin reached it.
static const char *diag_min_text(int16_t v) {
  static char buf[4][12];
  static uint8_t slot = 0;
  slot = (slot + 1) % 4;
  if (v == 32767) return "unsampled";
  snprintf(buf[slot], sizeof(buf[slot]), "%d", v);
  return buf[slot];
}

void diag_harp_press(uint8_t i, bool was_sounding) {
  uint32_t now = millis();
  if (diag_release_at[i] != 0 && now - diag_release_at[i] < DIAG_CHATTER_MS &&
      diag_last_hold_ms[i] >= DIAG_LONG_HOLD_MS) {
    diag_harp_chatter++;
    diag_log("HARP", "retouch s=%u %lums after a %lums hold (at release b=%u f=%u)%s",
             i, (unsigned long)(now - diag_release_at[i]), (unsigned long)diag_last_hold_ms[i],
             diag_release_b[i], diag_release_f[i],
             was_sounding ? ", retriggered a sounding voice" : "");
  }
  diag_touch_at[i] = now;
  diag_min_delta[i] = 32767;
  diag_margin_warned[i] = false;
  diag_long_touch_warned[i] = false;
  if (diag_verbose || diag_trace) {
    uint16_t f = 0, b = 0;
    harp_sensor.diag_raw(i, f, b);
    diag_log("harp", "touch s=%u note=%u b=%u f=%u d=%d", i, current_harp_notes[i], b, f, (int)b - (int)f);
  }
}

void diag_harp_release(uint8_t i) {
  uint32_t now = millis();
  uint32_t hold = now - diag_touch_at[i];
  diag_last_hold_ms[i] = hold;
  diag_release_at[i] = now;
  uint16_t f = 0, b = 0;
  bool read = (hold >= DIAG_LONG_HOLD_MS || diag_verbose || diag_trace) && harp_sensor.diag_raw(i, f, b);
  diag_release_b[i] = b;
  diag_release_f[i] = f;
  if (hold >= DIAG_LONG_HOLD_MS) diag_harp_long_releases++;
  if (read && (hold >= DIAG_LONG_HOLD_MS || diag_verbose || diag_trace)) {
    diag_log(hold >= DIAG_LONG_HOLD_MS ? "HARP" : "harp",
             "release s=%u after %lums b=%u f=%u d=%d min_d_during_hold=%s",
             i, (unsigned long)hold, b, f, (int)b - (int)f, diag_min_text(diag_min_delta[i]));
  }
}

// One held pad per call, round robin, so the extra sensor reads stay small
// and spread out.
void diag_harp_margin_step() {
  uint32_t now = millis();
  for (uint8_t k = 0; k < 12; k++) {
    uint8_t i = (diag_margin_cursor + k) % 12;
    if (!harp_array[i].read_value()) continue;
    if (now - diag_touch_at[i] < DIAG_LONG_HOLD_MS) continue;
    diag_margin_cursor = (i + 1) % 12;
    uint16_t f, b;
    if (!harp_sensor.diag_raw(i, f, b)) return;
    int16_t d = (int16_t)b - (int16_t)f;
    if (diag_min_delta[i] == 32767) {
      diag_ref_b[i] = b;
      diag_ref_f[i] = f;
    }
    if (d < diag_min_delta[i]) diag_min_delta[i] = d;
    // With the baseline held still during a touch, a false touch would never
    // release on its own. Nobody rests a finger on one pad this long.
    if (now - diag_touch_at[i] > DIAG_LONG_TOUCH_MS && !diag_long_touch_warned[i]) {
      diag_long_touch_warned[i] = true;
      diag_log("HARP", "long touch s=%u held %lums b=%u f=%u d=%d: if no finger is on it, the pad is stuck",
               i, (unsigned long)(now - diag_touch_at[i]), b, f, d);
    }
    if (d < DIAG_MARGIN_WARN && !diag_margin_warned[i]) {
      diag_margin_warned[i] = true;
      diag_harp_margin_warnings++;
      diag_log("HARP", "margin s=%u d=%d after %lums held: b=%u f=%u (since the first reading b moved %d, f moved %d)",
               i, d, (unsigned long)(now - diag_touch_at[i]), b, f,
               (int)b - (int)diag_ref_b[i], (int)f - (int)diag_ref_f[i]);
    }
    return;
  }
}

// Trace mode: every held pad, ten times a second, on one line.
void diag_harp_trace_step() {
  char line[200];
  int n = 0;
  line[0] = 0;
  for (uint8_t i = 0; i < 12 && n < (int)sizeof(line) - 24; i++) {
    if (!harp_array[i].read_value()) continue;
    uint16_t f, b;
    if (!harp_sensor.diag_raw(i, f, b)) return;
    n += snprintf(line + n, sizeof(line) - n, "%ss%u b%u f%u", n ? " | " : "", i, b, f);
  }
  if (n) diag_log("TRACE", "%s", line);
}

//>>AUDIO LOAD AND CLIPPING
const uint8_t diag_cpu_levels[] = DIAG_CPU_LEVELS;
const uint8_t diag_mem_levels[] = DIAG_MEM_LEVELS;
uint8_t diag_cpu_level_seen = 0;
uint8_t diag_mem_level_seen = 0;
uint32_t diag_watch_late_seen = 0;
uint32_t diag_watch_dropouts_seen = 0;
uint32_t diag_clips = 0;
uint16_t diag_peak_window[4] = {0, 0, 0, 0};   // per-mille, since the last heartbeat
const char *const diag_peak_names[4] = {"strings", "chords", "out_L", "out_R"};
AudioAnalyzePeak *const diag_peaks[4] = {&diag_peak_strings, &diag_peak_chords, &diag_peak_out_l, &diag_peak_out_r};

static uint8_t diag_strings_active() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < 12; i++) n += string_enveloppe_array[i]->isActive();
  return n;
}
static uint8_t diag_chords_active() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < 4; i++) n += chord_envelope_array[i]->isActive();
  return n;
}
static uint8_t diag_pads_held() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < 12; i++) n += harp_array[i].read_value();
  return n;
}

void diag_check_audio() {
  uint32_t late = diag_audio_watch.late;
  uint32_t drops = diag_audio_watch.dropouts;
  if (late != diag_watch_late_seen) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 200) {
      last_log = millis();
      uint8_t s = diag_audio_watch.last_late_section;
      bool sent = diag_log("AUDIO", "%s: update gap %luus (period %u), %lu late / %lu dropouts so far, loop was in %s; strings=%u chords=%u",
               drops != diag_watch_dropouts_seen ? "DROPOUT" : "late update",
               (unsigned long)diag_audio_watch.last_late_gap_us, DIAG_AUDIO_PERIOD_US,
               (unsigned long)late, (unsigned long)drops,
               s < DIAG_SECTIONS ? diag_section_names[s] : "?",
               diag_strings_active(), diag_chords_active());
      if (sent) {
        diag_watch_late_seen = late;
        diag_watch_dropouts_seen = drops;
      }
    }
  }

  float cpu = AudioProcessorUsageMax();
  while (diag_cpu_level_seen < sizeof(diag_cpu_levels) && cpu >= diag_cpu_levels[diag_cpu_level_seen]) {
    diag_log("AUDIO", "cpu peak %d%% (now %d%%) crossed %u%%; strings=%u chords=%u pads=%u",
             (int)cpu, (int)AudioProcessorUsage(), diag_cpu_levels[diag_cpu_level_seen],
             diag_strings_active(), diag_chords_active(), diag_pads_held());
    diag_cpu_level_seen++;
  }
  unsigned mem = AudioMemoryUsageMax();
  while (diag_mem_level_seen < sizeof(diag_mem_levels) &&
         mem * 100 >= (unsigned)diag_mem_levels[diag_mem_level_seen] * DIAG_AUDIO_MEMORY) {
    diag_log("AUDIO", "block memory peak %u of %u crossed %u%%%s; strings=%u chords=%u",
             mem, DIAG_AUDIO_MEMORY, diag_mem_levels[diag_mem_level_seen],
             mem >= DIAG_AUDIO_MEMORY ? " (EXHAUSTED: blocks refused, expect gaps)" : "",
             diag_strings_active(), diag_chords_active());
    diag_mem_level_seen++;
  }

  if (diag_peaks[0]->available()) {
    uint16_t pm[4];
    bool clip = false;
    for (uint8_t k = 0; k < 4; k++) {
      pm[k] = diag_peaks[k]->available() ? (uint16_t)(diag_peaks[k]->read() * 1000.0f) : 0;
      if (pm[k] > diag_peak_window[k]) diag_peak_window[k] = pm[k];
      if (pm[k] >= DIAG_CLIP_PERMILLE) clip = true;
    }
    if (clip) {
      diag_clips++;
      static uint32_t last_log = 0;
      if (millis() - last_log > 1000) {
        last_log = millis();
        diag_log("CLIP", "peaks strings=%u chords=%u out_L=%u out_R=%u (per mille); strings=%u chords=%u, %lu clipping reads so far",
                 pm[0], pm[1], pm[2], pm[3], diag_strings_active(), diag_chords_active(),
                 (unsigned long)diag_clips);
      }
    }
  }
}

//>>LOOP STALLS
uint32_t diag_loop_count = 0;
uint32_t diag_loop_total_us = 0;
uint32_t diag_loop_max_us = 0;
uint32_t diag_stalls = 0;
uint8_t diag_loop_max_section = 0;

void diag_loop_end() {
  uint32_t total = micros() - diag_loop_start;
  diag_loop_count++;
  diag_loop_total_us += total;
  uint8_t worst = 0;
  for (uint8_t s = 1; s < DIAG_SECTIONS; s++) {
    if (diag_section_us[s] > diag_section_us[worst]) worst = s;
  }
  if (total > diag_loop_max_us) {
    diag_loop_max_us = total;
    diag_loop_max_section = worst;
  }
  if (total > DIAG_LOOP_WARN_US && diag_settled) {
    diag_stalls++;
    static uint32_t last_log = 0;
    if (millis() - last_log > 200) {
      last_log = millis();
      diag_log("STALL", "loop took %luus, mostly %s (%luus); midi_in=%lu buttons=%lu presets=%lu pots=%lu chords=%lu harp=%lu midi_out=%lu diag=%lu",
               (unsigned long)total, diag_section_names[worst], (unsigned long)diag_section_us[worst],
               (unsigned long)diag_section_us[0], (unsigned long)diag_section_us[1],
               (unsigned long)diag_section_us[2], (unsigned long)diag_section_us[3],
               (unsigned long)diag_section_us[4], (unsigned long)diag_section_us[5],
               (unsigned long)diag_section_us[6], (unsigned long)diag_section_us[7]);
    }
  }
}

//>>STUCK NOTES AND LONG TAILS
uint32_t diag_harp_off_since[12] = {0};
bool diag_harp_stuck_logged[12] = {false};
uint32_t diag_chord_up_since = 0;
bool diag_chord_stuck_logged = false;
bool diag_chord_tail_logged = false;
uint32_t diag_stuck_count = 0;

void diag_check_stuck() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 12; i++) {
    bool held = harp_array[i].read_value();
    bool audio_on = string_enveloppe_array[i]->isSustain();
    bool midi_on = harp_started_notes[i] != 0;
    if (held || (!audio_on && !midi_on)) {
      diag_harp_off_since[i] = 0;
      diag_harp_stuck_logged[i] = false;
      continue;
    }
    if (diag_harp_off_since[i] == 0) diag_harp_off_since[i] = now;
    if (now - diag_harp_off_since[i] > DIAG_STUCK_MS && !diag_harp_stuck_logged[i]) {
      diag_harp_stuck_logged[i] = true;
      diag_stuck_count++;
      diag_log("STUCK", "harp s=%u not held for %lums but%s%s", i,
               (unsigned long)(now - diag_harp_off_since[i]),
               audio_on ? " audio still sustaining" : "",
               midi_on ? " MIDI note still on" : "");
    }
  }

  bool any_chord_button = false;
  for (uint8_t b = 1; b < 22; b++) any_chord_button |= chord_matrix_array[b].read_value();
  bool expect_silence = !any_chord_button && !continuous_chord && !rythm_mode;
  if (!expect_silence) {
    diag_chord_up_since = 0;
    diag_chord_stuck_logged = false;
    diag_chord_tail_logged = false;
    return;
  }
  if (diag_chord_up_since == 0) diag_chord_up_since = now;
  uint32_t up = now - diag_chord_up_since;

  uint8_t sustaining = 0, active = 0, midi = 0;
  for (uint8_t v = 0; v < 4; v++) {
    sustaining += chord_envelope_array[v]->isSustain();
    active += chord_envelope_array[v]->isActive();
    midi += chord_started_notes[v] != 0;
  }
  if (up > DIAG_STUCK_MS && (sustaining || midi) && !diag_chord_stuck_logged) {
    diag_chord_stuck_logged = true;
    diag_stuck_count++;
    diag_log("STUCK", "chord: no button for %lums but %u voices sustaining, %u MIDI notes on (line=%d)",
             (unsigned long)up, sustaining, midi, current_line);
  }
  // stop_chord_notes() only releases voices already in sustain, so a voice let
  // go during its attack or decay plays that stage out before it starts to
  // release. With a long decay this is the note that will not stop.
  // Only past the preset's own release time, which is the tail it asked for.
  uint32_t allowed = (uint32_t)max((int16_t)0, current_sysex_parameters[141]) + DIAG_TAIL_MS;
  if (up > allowed && active && !diag_chord_tail_logged) {
    diag_chord_tail_logged = true;
    diag_log("TAIL", "chord: %u voices still sounding %lums after release, past its release time (attack=%d hold=%d decay=%d sustain=%d release=%d)",
             active, (unsigned long)up,
             current_sysex_parameters[137], current_sysex_parameters[138], current_sysex_parameters[139],
             current_sysex_parameters[140], current_sysex_parameters[141]);
  }
}

//>>RECORDED EVENTS
uint32_t diag_pitch_high_seen = 0, diag_pitch_low_seen = 0, diag_glide_seen = 0, diag_midi_range_seen = 0;
uint32_t diag_midi_dropped_seen = 0;
uint32_t diag_overcurrent_seen = 0;

static const char *diag_voice_kind(int32_t code) {
  switch (code / 100) {
    case 0: return "chord";
    case 1: return "string";
    default: return "transient";
  }
}

void diag_report_events() {
  uint32_t n;
  if ((n = diag_event_reported(diag_pitch_high, diag_pitch_high_seen)))
    diag_log("PITCH", "%lu x above %dHz, last %s %ld note=%ld at %ldHz (aliasing)", (unsigned long)n,
             (int)DIAG_PITCH_HIGH_HZ, diag_voice_kind(diag_pitch_high.a), (long)(diag_pitch_high.a % 100),
             (long)diag_pitch_high.b, (long)diag_pitch_high.c);
  if ((n = diag_event_reported(diag_pitch_low, diag_pitch_low_seen)))
    diag_log("PITCH", "%lu x below %dHz, last %s %ld note=%ld at %ld.%02ldHz", (unsigned long)n,
             (int)DIAG_PITCH_LOW_HZ, diag_voice_kind(diag_pitch_low.a), (long)(diag_pitch_low.a % 100),
             (long)diag_pitch_low.b, (long)(diag_pitch_low.c / 100), (long)(diag_pitch_low.c % 100));
  if ((n = diag_event_reported(diag_glide_clip, diag_glide_seen)))
    diag_log("PITCH", "%lu x glide offset past the modulation range, last voice %ld note=%ld offset=%ld/1000 (lands off pitch)",
             (unsigned long)n, (long)diag_glide_clip.a, (long)diag_glide_clip.b, (long)diag_glide_clip.c);
  if ((n = diag_event_reported(diag_midi_range, diag_midi_range_seen)))
    diag_log("PITCH", "%lu x MIDI note number over 127, last %ld on channel %ld cable %ld",
             (unsigned long)n, (long)diag_midi_range.a, (long)diag_midi_range.b, (long)diag_midi_range.c);
  if (midi_queue_dropped != diag_midi_dropped_seen) {
    diag_log("MIDI", "output queue full: %lu events dropped so far",
             (unsigned long)midi_queue_dropped);
    diag_midi_dropped_seen = midi_queue_dropped;
  }
  if (harp_sensor.diag_overcurrent_count != diag_overcurrent_seen) {
    diag_log("HARP", "sensor over-current, restarted %lu times so far",
             (unsigned long)harp_sensor.diag_overcurrent_count);
    diag_overcurrent_seen = harp_sensor.diag_overcurrent_count;
  }
}

//>>CHORD QUALITY
// From diagnostic-qualityTripwire: the four voices must contain exactly the
// chord's pitch classes. Called before previous_voicing is overwritten, so the
// voicing it came from is still there to print.
void diag_check_chord_quality() {
  uint8_t expect[4];
  uint8_t expect_count = collect_chord_tones(current_chord, expect);
  int16_t root = get_root_button(key_signature_selection, chord_frame_shift, fundamental)
                 + (chord_context_sharp ? (flat_button_modifier ? -sharp_step : sharp_step) : 0);
  bool covered[4] = {false, false, false, false};
  bool foreign = false;
  for (uint8_t v = 0; v < 4; v++) {
    int16_t pc = (((int16_t)current_chord_notes[v] - root) % EDO + EDO) % EDO;
    bool known = false;
    for (uint8_t k = 0; k < expect_count; k++) {
      if (pc == expect[k]) { covered[k] = true; known = true; }
    }
    if (!known) foreign = true;
  }
  bool missing = false;
  for (uint8_t k = 0; k < expect_count; k++) {
    if (!covered[k]) missing = true;
  }
  if ((foreign || missing) && !chord_context_slashed) {
    diag_log("QUALITY", "%s%s fund=%d line=%d alt=%u sharp=%u vl=%u spacing=%u inv=%u bh=%u EDO=%u notes=%u %u %u %u tones=%u %u %u %u chord=%u %u %u %u prev=%d %d %d %d",
             foreign ? "foreign tone" : "", missing ? (foreign ? ", missing tone" : "missing tone") : "",
             fundamental, current_line, alt_chord_layout, chord_context_sharp, voice_leading,
             chord_spacing, chord_inversion, barry_harris_mode, EDO,
             current_chord_notes[0], current_chord_notes[1], current_chord_notes[2], current_chord_notes[3],
             expect[0], expect_count > 1 ? expect[1] : 0, expect_count > 2 ? expect[2] : 0, expect_count > 3 ? expect[3] : 0,
             (*current_chord)[0], (*current_chord)[1], (*current_chord)[2], (*current_chord)[3],
             previous_voicing[0], previous_voicing[1], previous_voicing[2], previous_voicing[3]);
  }
}

//>>SUMMARY AND COMMANDS
void diag_reset_peaks() {
  AudioProcessorUsageMaxReset();
  AudioMemoryUsageMaxReset();
  diag_audio_watch.reset();
  diag_cpu_level_seen = 0;
  diag_mem_level_seen = 0;
  diag_loop_max_us = 0;
  diag_loop_max_section = 0;
}

void diag_heartbeat() {
  uint32_t avg = diag_loop_count ? diag_loop_total_us / diag_loop_count : 0;
  diag_log("BEAT", "cpu=%d/%d%% mem=%u/%u of %u gap_max=%luus late=%lu drop=%lu loop avg=%luus max=%luus (%s) stalls=%lu",
           (int)AudioProcessorUsage(), (int)AudioProcessorUsageMax(),
           (unsigned)AudioMemoryUsage(), (unsigned)AudioMemoryUsageMax(), DIAG_AUDIO_MEMORY,
           (unsigned long)diag_audio_watch.worst_gap_us, (unsigned long)diag_audio_watch.late,
           (unsigned long)diag_audio_watch.dropouts, (unsigned long)avg, (unsigned long)diag_loop_max_us,
           diag_section_names[diag_loop_max_section], (unsigned long)diag_stalls);
  diag_log("BEAT", "peaks str=%u chd=%u L=%u R=%u clips=%lu | strings=%u chords=%u pads=%u | harp retouch=%lu long_releases=%lu margin=%lu stuck=%lu",
           diag_peak_window[0], diag_peak_window[1], diag_peak_window[2], diag_peak_window[3],
           (unsigned long)diag_clips,
           diag_strings_active(), diag_chords_active(), diag_pads_held(),
           (unsigned long)diag_harp_chatter, (unsigned long)diag_harp_long_releases,
           (unsigned long)diag_harp_margin_warnings, (unsigned long)diag_stuck_count);
  diag_log("BEAT", "midi_drop=%lu lines_dropped=%lu sensor=%s",
           (unsigned long)midi_queue_dropped, (unsigned long)diag_lines_dropped,
           harp_sensor.diag_communicating() ? "ok" : "NOT RESPONDING");
  diag_loop_count = 0;
  diag_loop_total_us = 0;
  for (uint8_t k = 0; k < 4; k++) diag_peak_window[k] = 0;
}

void diag_banner() {
  diag_log("INFO", "tripwire build, firmware version %d, bank %d, EDO %u, temperament %u, voice leading %u, mpe %u",
           version_ID, current_bank_number, EDO, temperament_selection, voice_leading, mpe_mode);
  uint8_t nhd = 0, ncl = 0, fdl = 0;
  harp_sensor.diag_read_touched_filter(nhd, ncl, fdl);
  diag_log("INFO", "harp touch at %u, release under %u; touched baseline filter NHDT=%u NCLT=%u FDLT=%u (%s)",
           harp_sensor.diag_touch_threshold(), harp_sensor.diag_release_threshold(), nhd, ncl, fdl,
           (nhd | ncl | fdl) == 0 ? "frozen" : "tracking");
  diag_log("INFO", "commands: m mark, t trace, f filter A/B, l release threshold, v verbose, r reset, s summary, h help");
}

void diag_commands() {
  while (Serial.available()) {
    int c = Serial.read();
    switch (c) {
      case 'm':
        diag_log("MARK", "#%lu", (unsigned long)++diag_mark_count);
        break;
      case 't':
        diag_trace = !diag_trace;
        diag_log("INFO", "trace %s", diag_trace ? "on" : "off");
        break;
      case 'f': {
        harp_sensor.diag_set_touched_filter(!harp_sensor.diag_touched_filter_frozen);
        uint8_t nhd = 0, ncl = 0, fdl = 0;
        harp_sensor.diag_read_touched_filter(nhd, ncl, fdl);
        diag_log("INFO", "touched baseline filter now %s: NHDT=%u NCLT=%u FDLT=%u (baselines reloaded, keep hands off)",
                 harp_sensor.diag_touched_filter_frozen ? "frozen" : "library default", nhd, ncl, fdl);
        break;
      }
      case 'l': {
        uint8_t r = harp_sensor.diag_release_threshold();
        r = r > 14 ? 14 : (r > 10 ? 10 : 20);
        harp_sensor.diag_set_release_threshold(r);
        diag_log("INFO", "release threshold now %u (touch stays %u; baselines reloaded, keep hands off)",
                 r, harp_sensor.diag_touch_threshold());
        break;
      }
      case 'v':
        diag_verbose = !diag_verbose;
        diag_log("INFO", "verbose harp %s", diag_verbose ? "on" : "off");
        break;
      case 'r':
        diag_reset_peaks();
        diag_log("INFO", "peaks reset");
        break;
      case 's':
        diag_heartbeat();
        break;
      case 'h':
      case '?':
        diag_banner();
        break;
      default:
        break;
    }
  }
}

// Called once per loop pass, after everything else.
void diag_tick() {
  uint32_t now = millis();
  static uint32_t last_margin = 0, last_stuck = 0, last_beat = 0;

  if (!diag_settled && now > DIAG_SETTLE_MS) {
    // Boot (calibration, flash mount, preset load) sets records nobody cares about.
    diag_settled = true;
    diag_reset_peaks();
    diag_audio_watch.late = 0;
    diag_audio_watch.dropouts = 0;
    diag_watch_late_seen = 0;
    diag_watch_dropouts_seen = 0;
  }
  bool connected = Serial.dtr();
  if (connected && !diag_was_connected) diag_banner();
  diag_was_connected = connected;

  diag_commands();
  diag_preset_retry();
  diag_report_events();
  if (diag_settled) diag_check_audio();
  if (now - last_margin >= 50) {
    last_margin = now;
    diag_harp_margin_step();
  }
  static uint32_t last_trace = 0;
  if (diag_trace && now - last_trace >= 100) {
    last_trace = now;
    diag_harp_trace_step();
  }
  if (now - last_stuck >= 100) {
    last_stuck = now;
    diag_check_stuck();
  }
  if (now - last_beat >= DIAG_HEARTBEAT_MS) {
    last_beat = now;
    diag_heartbeat();
  }
}

#endif  // MINICHORD_DIAG
#endif  // DIAGNOSTICS_CHECKS_H
