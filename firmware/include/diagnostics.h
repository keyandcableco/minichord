// Tripwire build for stress testing. Not for submission.
//
// Everything below compiles to nothing unless MINICHORD_DIAG is defined (the
// diagnostic branch adds it in platformio.ini), and every hook in main.cpp is
// wrapped in DIAG(...), so the normal build is byte-for-byte unaffected.
//
// Output is one line per event on USB serial, each starting "DIAG", so
//   pio device monitor | grep DIAG
// separates it from the firmware's own prints. Type h in the monitor for the
// single-letter commands.
//
// This header is the part that has to exist before anything else: logging,
// the recorders that are safe to call from a timer interrupt, the audio
// watchdog (which must be the first audio object constructed), and the loop
// timer. The checks that need the firmware's globals live in
// diagnostics_checks.h, included just before setup().
#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>
#include <Audio.h>

#ifdef MINICHORD_DIAG
#define DIAG(x) x
#else
#define DIAG(x)
#endif

#ifdef MINICHORD_DIAG

#include <stdarg.h>

//>>THRESHOLDS
#define DIAG_AUDIO_MEMORY       1200    // must match AudioMemory() in setup()
#define DIAG_AUDIO_PERIOD_US    2902    // 128 samples at 44.1 kHz
#define DIAG_AUDIO_LATE_US      3600    // an update this far apart started late
#define DIAG_AUDIO_DROPOUT_US   5800    // two periods: the output ran dry
#define DIAG_CPU_LEVELS         {80, 90, 100}
#define DIAG_MEM_LEVELS         {80, 90, 100}   // percent of DIAG_AUDIO_MEMORY
#define DIAG_CLIP_PERMILLE      985     // peak reading counted as clipping
#define DIAG_LOOP_WARN_US       15000   // a slower pass delays the harp and buttons
#define DIAG_LONG_HOLD_MS       1000    // harp holds this long are watched for drift
#define DIAG_CHATTER_MS         150     // re-touch this soon after a long hold
#define DIAG_MARGIN_WARN        30      // touch delta this close to the release threshold
#define DIAG_PITCH_HIGH_HZ      16000.0f
#define DIAG_PITCH_LOW_HZ       16.0f
#define DIAG_GLIDE_LIMIT        0.95f   // the glide DC saturates at 1 with vibrato on top
#define DIAG_STUCK_MS           400
#define DIAG_TAIL_MS            3000
#define DIAG_HEARTBEAT_MS       10000
#define DIAG_SETTLE_MS          4000    // boot spikes are forgotten after this

//>>LOGGING
volatile uint32_t diag_lines_dropped = 0;

static void diag_log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void diag_log(const char *tag, const char *fmt, ...) {
  char buf[240];
  uint32_t now = millis();
  int n = snprintf(buf, sizeof(buf), "DIAG %6lu.%03lu %-6s ",
                   (unsigned long)(now / 1000), (unsigned long)(now % 1000), tag);
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);
  if (m > 0) n += m;
  if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
  buf[n++] = '\n';
  // Never wait on the host. A blocked serial write would cause the very
  // stalls this build is looking for, so with no monitor open, or the buffer
  // full, the line is counted and dropped.
  if (!Serial.dtr() || Serial.availableForWrite() < n) {
    diag_lines_dropped++;
    return;
  }
  Serial.write((const uint8_t *)buf, n);
}

//>>INTERRUPT-SAFE RECORDERS
// Parts of the engine run from timer interrupts (delayed chord notes, the
// rhythm), where printing is not allowed. These only store the last offender
// and a count; diag_tick() reports them from the loop.
struct diag_event_t {
  volatile uint32_t count;
  volatile int32_t a, b, c;
};
static uint32_t diag_event_reported(diag_event_t &e, uint32_t &seen) {
  uint32_t n = e.count - seen;
  seen = e.count;
  return n;
}
static inline void diag_record(diag_event_t &e, int32_t a, int32_t b, int32_t c) {
  e.a = a;
  e.b = b;
  e.c = c;
  e.count++;
}

diag_event_t diag_pitch_high = {0, 0, 0, 0};   // a: voice kind*100+voice, b: note, c: Hz
diag_event_t diag_pitch_low = {0, 0, 0, 0};    // c: centi-Hz
diag_event_t diag_glide_clip = {0, 0, 0, 0};   // a: voice, b: note, c: offset*1000
diag_event_t diag_midi_range = {0, 0, 0, 0};   // a: note, b: channel, c: cable

// kind: 0 chord oscillator, 1 harp string, 2 harp transient
static inline void diag_check_pitch(uint8_t kind, uint8_t voice, int note, float hz) {
  if (hz > DIAG_PITCH_HIGH_HZ) {
    diag_record(diag_pitch_high, kind * 100 + voice, note, (int32_t)hz);
  } else if (hz < DIAG_PITCH_LOW_HZ) {
    diag_record(diag_pitch_low, kind * 100 + voice, note, (int32_t)(hz * 100.0f));
  }
}
static inline float diag_max3(float a, float b, float c) {
  float m = a > b ? a : b;
  return m > c ? m : c;
}
static inline void diag_check_glide(uint8_t voice, int note, float offset) {
  if (offset > DIAG_GLIDE_LIMIT || offset < -DIAG_GLIDE_LIMIT) {
    diag_record(diag_glide_clip, voice, note, (int32_t)(offset * 1000.0f));
  }
}
static inline void diag_check_midi_note(uint8_t note, uint8_t channel, uint8_t cable) {
  if (note > 127) diag_record(diag_midi_range, note, channel, cable);
}

//>>LOOP TIMING
#define DIAG_SECTIONS 8
const char *const diag_section_names[DIAG_SECTIONS] = {
  "midi_in", "buttons", "presets", "pots", "chords", "harp", "midi_out", "diag"};
volatile uint8_t diag_current_section = 0;   // read by the audio watchdog
uint32_t diag_loop_start = 0;
uint32_t diag_mark_time = 0;
uint32_t diag_section_us[DIAG_SECTIONS];

static inline void diag_loop_begin() {
  diag_loop_start = diag_mark_time = micros();
  for (uint8_t s = 0; s < DIAG_SECTIONS; s++) diag_section_us[s] = 0;
  diag_current_section = 0;
}
// Closes the section that just ran and names the one about to start.
static inline void diag_mark(uint8_t finished) {
  uint32_t t = micros();
  diag_section_us[finished] += t - diag_mark_time;
  diag_mark_time = t;
  diag_current_section = finished + 1 < DIAG_SECTIONS ? finished + 1 : finished;
}

//>>AUDIO WATCHDOG
// The audio library runs every object's update() from one interrupt every
// 2.9 ms, in the order the objects were constructed. This object is
// constructed first (audio_definition.h includes this header before its own
// objects), so the time between its updates is the time between interrupts.
// A longer gap means the audio interrupt was held off, by a long
// AudioNoInterrupts() or noInterrupts() section, a flash write, or an
// overrun of the whole graph; past two periods the output has run dry, which
// is a click. It also notes which loop section was running when it finally
// got in, which is usually the section that held it off.
class DiagAudioWatch : public AudioStream {
 public:
  DiagAudioWatch() : AudioStream(1, inputQueueArray) {}
  virtual void update(void) {
    audio_block_t *b = receiveReadOnly(0);
    if (b) release(b);
    uint32_t now = micros();
    if (last_us != 0) {
      uint32_t gap = now - last_us;
      if (gap > worst_gap_us) worst_gap_us = gap;
      if (gap > DIAG_AUDIO_LATE_US) {
        late++;
        last_late_gap_us = gap;
        last_late_section = diag_current_section;
        if (gap > DIAG_AUDIO_DROPOUT_US) dropouts++;
      }
    }
    last_us = now;
  }
  void reset() {
    worst_gap_us = 0;
  }
  volatile uint32_t last_us = 0;
  volatile uint32_t worst_gap_us = 0;
  volatile uint32_t late = 0;
  volatile uint32_t dropouts = 0;
  volatile uint32_t last_late_gap_us = 0;
  volatile uint8_t last_late_section = 0;

 private:
  audio_block_t *inputQueueArray[1];
};
DiagAudioWatch diag_audio_watch;

#endif  // MINICHORD_DIAG
#endif  // DIAGNOSTICS_H
