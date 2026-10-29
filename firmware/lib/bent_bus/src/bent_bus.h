// The bends (BENDS in main.cpp): what a circuit bender wires into an Omnichord's body-contact
// points, on the whole instrument at once. One stereo object between the last mix (stereo_l_mixer,
// stereo_r_mixer) and the outputs, so the speaker, the headphones and the USB recording all hear
// it, and the host's song and the looper's click don't. In signal order:
//
//   stutter   a slice of the sound from the press on, played over and over while held: shorter the
//             deeper (280 ms at a touch, down to its shortest, 25 ms, at full), or a beat's fraction in
//             sync; the drums keep playing through it unless they are to stutter too
//   pitch     the whole instrument sags in pitch, drums and all, as a slowed clock would: a two-head
//             delay-line pitch shifter; the starve's wobble goes through it too
//   starve    a starved supply: the pitch wobbles and sags, the rails close in (a fuzz that gets
//             quieter), the sound drops out in brown-outs and crackles
//   crush     fewer bits (across the sound's own peak, 4 at full) and a lower sample rate (1.8 kHz)
//
// Each takes a depth from 0 to 1; at 0 they all rest and the blocks go through untouched, bit for
// bit. RAM2 has no room for buffers, so the history is the audio library's own blocks, held rather
// than released (zero copy): 2 x 32 always, for the pitch shifter's window, and up to 2 x 48 more
// while a stutter is held. Settings arrive from the loop; the work is in update(), which is in
// flash (bent_bus.cpp): RAM1's code is a few hundred bytes from a 32 kB step.
#ifndef BENT_BUS_H
#define BENT_BUS_H
#include <Arduino.h>
#include <AudioStream.h>

class AudioEffectBend : public AudioStream {
public:
  AudioEffectBend();   // in flash (bent_bus.cpp): inline, it put its state's zeroing in the static constructors' ITCM
  // Inputs 0 and 1 the mix, left and right; 2 and 3 the drums, added to it here (as the last mix did, at
  // gain 1) before the stutter or, with drums_through, after it, so the beat carries on under a stutter

  // depth 0..1; semitones at depth 1; the time the pitch takes to sag and to come back, in ms (to 95%,
  // three time constants)
  void pitch_drop(float depth, float semitones, float sag_ms) {
    drop_target = depth * semitones;
    sag_coef = sag_ms <= 3 * BLOCK_MS ? 1.0f : 1.0f - expf(-3 * BLOCK_MS / sag_ms);
  }
  void starve(float depth) { starve_target = depth; }
  // mode 0 bits and rate, 1 bits only, 2 rate only
  void crush(float depth, uint8_t mode) { crush_target = depth; crush_mode = mode; }
  // beat_samples: the beat's length while the rhythm runs, for slices in time with it, 0 free; shortest:
  // the slice at full depth, samples; drums_through: the drums keep playing rather than stuttering;
  // refresh: after this many samples of a slice repeating, a held stutter catches a new one (0 never), so
  // one left on rolls with the music rather than holding what it caught (silence, at worst) for ever
  void stutter(float depth, uint32_t beat_samples, uint32_t shortest, bool drums_through, uint32_t refresh = 0) {
    stutter_target = depth; stutter_beat = beat_samples; stutter_drums_through = drums_through; stutter_refresh = refresh;
    stutter_shortest = shortest < 64 ? 64 : shortest > STUTTER_BLOCKS * AUDIO_BLOCK_SAMPLES ? STUTTER_BLOCKS * AUDIO_BLOCK_SAMPLES : shortest;
  }

  // how much the bends act on one another, 0..1 (bends interact): the starve takes bits as its supply
  // sags, all of them in a brown-out; the pitch drop lowers the sample rate as it sags (to 5 kHz at an
  // octave). The clock's side of it is main.cpp's, from what these report.
  void interact(float amount) { interact_amount = amount; }
  float supply_level() { return supply; }        // the starve's supply now, 1 full
  bool browned_out() { return dropout > 0; }
  float drop_semitones() { return drop_now; }    // the pitch drop as sagged so far

  // a harp note on the bent board strikes the bends a touch holds again (bent_strike in main.cpp), taken at
  // the next block: the drop starts again from in tune and sags, the stutter catches a new slice from the
  // note (the one repeating faded back to the live sound over a block first), the starve's supply slumps
  // under it and comes back as it does from a brown-out
  void strike(bool drop, bool stutter, bool starve) {
    if (drop) strike_drop = true;
    if (stutter) strike_stutter = true;
    if (starve) strike_starve = true;
  }
  static constexpr float STRIKE_SLUMP = 0.2f;   // the supply's slump under a struck note, at full starve

  // what it's doing, for the serial report
  bool resting() { return rest; }
  uint16_t held_blocks() { return held; }

  virtual void update(void);

  static const uint16_t WINDOW = 1536;        // the pitch shifter's window, samples (35 ms): its heads jump every
                                              // 35 ms / (1 - speed), a warble of 4.6 Hz 3 semitones down, 14 Hz an octave down
  static const uint8_t HISTORY = 32;          // blocks kept a side for it (4096 samples, a power of 2)
  static const uint16_t SPLICE_REACH = 320;   // how far either side of its jump a head may land (a period of 69 Hz)
  static const uint16_t SPLICE_LENGTH = 128;  // the stretch compared to find where
  static const uint16_t SPLICE_REST = 512;    // samples between one head's splices at the most
  static const uint16_t SPLICE_DELAY = SPLICE_REACH + 1;   // the heads' shortest delay, so one landing short still reads the past
  static const uint8_t STUTTER_BLOCKS = 96;   // the longest slice a side (279 ms)

private:
  static constexpr float BLOCK_MS = AUDIO_BLOCK_SAMPLES * 1000.0f / AUDIO_SAMPLE_RATE_EXACT;
  audio_block_t *inputQueueArray[4];

  // settings, from the loop
  volatile float drop_target = 0, sag_coef = 1, starve_target = 0, crush_target = 0, stutter_target = 0;
  volatile uint8_t crush_mode = 0;
  volatile float interact_amount = 0;
  volatile uint32_t stutter_beat = 0, stutter_shortest = 1102, stutter_refresh = 0;   // 25 ms; never
  volatile bool stutter_drums_through = true;
  volatile bool strike_drop = false, strike_stutter = false, strike_starve = false;   // set by the loop, taken here

  bool rest = true;
  uint16_t held = 0;   // up to 2 x (32 + 96)

  // pitch: the stutter's output kept, block n at history[side][n & 15]
  audio_block_t *history[2][HISTORY] = {};
  uint32_t block_number = 0;
  float semis = 0;               // the shift now, semitones down (the drop sagged, and the starve's wobble)
  float drop_now = 0;            // the drop as sagged so far
  float ratio = 1;               // the read speed it makes, at the end of the last block
  float phase = 0.5f;            // the heads' place in the window
  float head_off[2] = {0, 0};    // where each head landed at its last jump, from where a fixed jump would put it
  float rho = 0;                 // how alike the heads read at the last jump, 0 to 1
  uint8_t splice_due = 0;        // the heads (bit 0 A, 1 B) that have jumped and wait to be matched
  uint16_t splice_rest[2] = {0, 0};   // samples before each may be matched again
  float wet = 0;                 // the shifter's share of the sound, 0 to 1, moved a little each sample

  // starve
  float starve_now = 0;
  float wobble = 0, wobble_v = 0, wobble_aim = 0;   // smoothed random, semitones
  float supply = 1, supply_aim = 1;                 // the rail voltage, 1 full
  uint16_t dropout = 0;                             // blocks of brown-out left
  float gain = 1;                                   // the level the supply allows, per sample toward its aim
  float crackle = 0;                                // the crackle's ringing
  uint32_t seed = 22222;

  // crush
  float crush_now = 0;
  float hold_phase = 1;
  float peak = 0;                // the sound's peak, falling slowly, which the bits are counted across
  int16_t held_sample[2] = {0, 0};

  // stutter
  audio_block_t *slice[2][STUTTER_BLOCKS] = {};
  uint8_t slice_blocks = 0;      // captured so far
  bool stuttering = false;       // pressed
  bool looping = false;          // playing the slice back (once it's as long as the loop)
  uint32_t loop_length = 0, loop_at = 0;
  uint16_t stutter_fade = 0;     // samples into the way back to the live sound, after a release
  uint32_t looped = 0;           // samples the slice has been repeating, toward a refresh

  uint32_t random() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
  float noise() { return (int32_t)random() * (1.0f / 2147483648.0f); }   // -1..1
  uint32_t loop_wanted();
  void release_slice();
  void splice(uint32_t t, uint8_t head);
  static void hold(audio_block_t *b);
  static inline int16_t sample(audio_block_t *b, uint32_t n) { return b ? b->data[n] : 0; }
  static inline int16_t clip16(float x) { return x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : (int16_t)x; }
  void add_drums(audio_block_t **cur, bool *own, audio_block_t **drums);
};

#endif
