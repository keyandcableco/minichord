// The drum machine: thirteen drum voices, mostly synthesized, in one audio object, stereo out (see DRUMS in
// main.cpp, and generator/drums.py for the kits and grooves). Each voice is a pitched body (a sine
// whose pitch falls from tone_bend times its own to it, for a kick's thump or a tom's) and a burst of
// noise and metal (white noise, and two or four square waves at bell or cymbal ratios) through a
// filter, each with its own decay; a clap's noise is struck again a few times, quickly, before its
// tail; hats in one choke group cut each other off. A kit is the thirteen voices' settings, and the
// kit's grit: fewer bits and held samples for a lo-fi or broken sound, and a drive. Hits come from
// the rhythm's timer and from the loop (the harp's pads), so they go through a little queue the audio
// update takes them from, each with a delay in samples (the humanize). Nothing sounding, nothing runs.
// A voice can instead play a recorded stroke (drum_samples.h, from generator/drum_samples.py): the
// tabla's, which no sine and noise can be. A stroke choked fades in a few milliseconds rather than
// stopping dead, as a hand laid on a skin does.
#ifndef DRUM_MACHINE_H
#define DRUM_MACHINE_H
#include <Arduino.h>
#include <AudioStream.h>
#include "tanpura.h"

#define DRUM_VOICES 13
enum { DRUM_KICK, DRUM_SNARE, DRUM_RIM, DRUM_CLAP, DRUM_HAT, DRUM_OPEN, DRUM_CYMBAL, DRUM_TOM_LO, DRUM_TOM_HI,
       DRUM_CONGA_LO, DRUM_CONGA_HI, DRUM_BELL, DRUM_SHAKER };

struct drum_voice_t {
  float tone_hz, tone_bend, bend_ms, tone_ms, tone_level;   // the body
  float noise_level, metal_level, metal_hz, metal_ratio;    // the burst: noise, and squares at metal_hz and ratios of it
  uint8_t metal_count;                                      // 2: metal_hz and metal_ratio times it (a bell); 4: a cymbal's cluster
  uint8_t filter;                                           // the burst's filter: 0 low pass, 1 band pass, 2 high pass
  float filter_hz, filter_q, noise_ms;
  uint8_t retrigs;                                          // a clap's: the burst struck again this many times,
  float retrig_ms;                                          // ... this far apart
  float level, pan;                                         // pan -1 left to 1 right
  uint8_t choke;                                            // a voice struck cuts off the others in its group (not 0)
  uint8_t follow;                                           // 1: the body takes the pitch the hit gives (an 808 bass),
                                                            // or a recorded stroke is retuned to it (a dayan)
  int8_t sample;                                            // a recorded stroke in drum_samples to play, or -1
};

struct drum_sample_t {
  const int16_t *data;
  uint32_t length;   // samples
  uint32_t rate;     // Hz
  float pitch_hz;    // what it was tuned to, for a voice that follows; 0 untuned
};
extern const drum_sample_t drum_samples[];

struct drum_kit_t {
  const char *name;
  uint8_t bits;      // 16 clean, fewer for grit
  uint8_t hold;      // 1 clean, more to hold each sample that many times (a lower rate)
  float drive;       // 1 clean, more into a soft clip
  drum_voice_t v[DRUM_VOICES];
};

extern const int16_t AudioWaveformSine[257];

// interrupts' mask as it is, to put back as it was (a caller may have them off already)
static inline uint32_t drum_primask() {
#if defined(__arm__)
  uint32_t m; __asm__ volatile("mrs %0, primask" : "=r"(m)); return m;
#else
  return 0;   // off the Teensy (generator/render tests): no interrupts to mind
#endif
}

class AudioDrumMachine : public AudioStream {
public:
  AudioDrumMachine() : AudioStream(0, NULL) {}

  void setKit(const drum_kit_t *k) {
    const float fs = AUDIO_SAMPLE_RATE_EXACT;
    __disable_irq();
    kit = k;
    for (uint8_t i = 0; i < DRUM_VOICES; i++) {
      const drum_voice_t &p = k->v[i];
      voice_t &s = voice[i];
      s.tone_decay = decay(p.tone_ms, fs);
      s.noise_decay = decay(p.noise_ms, fs);
      s.bend_decay = expf(-1.0f / fmaxf(p.bend_ms * fs / 1000.0f, 1.0f));
      // the burst's filter, a state variable filter in its topology-preserving form: stable to the top
      float g = tanf(3.14159265f * fminf(p.filter_hz, fs * 0.45f) / fs);
      s.k = 1.0f / fmaxf(p.filter_q, 0.3f);
      s.a1 = 1.0f / (1.0f + g * (g + s.k));
      s.a2 = g * s.a1;
      s.a3 = g * s.a2;
      s.metal_inc[0] = p.metal_hz / fs;
      static const float cluster[4] = {1.0f, 1.4829f, 1.8003f, 2.5461f};   // the 808's cymbal squares, as ratios
      for (uint8_t m = 1; m < 4; m++) s.metal_inc[m] = p.metal_hz * (p.metal_count == 4 ? cluster[m] : m == 1 ? p.metal_ratio : 0) / fs;
      float a = (p.pan + 1.0f) * 0.25f * 3.14159265f;
      s.left = cosf(a) * p.level;
      s.right = sinf(a) * p.level;
      s.retrig_samples = (uint32_t)(p.retrig_ms * fs / 1000.0f);
      // what the per-sample loops read, here in RAM rather than from the kit in flash
      s.bend_span = p.tone_bend - 1.0f;
      s.noise_level = p.noise_level;
      s.metal_level = p.metal_level;
      s.metal_count = p.metal_count == 4 ? 4 : 2;
      s.filter = p.filter;
      s.sample = p.sample >= 0 ? &drum_samples[p.sample] : nullptr;
      s.sample_base_inc = s.sample ? s.sample->rate / fs : 0;
      s.sample_amp = 0;
      s.active = false;
    }
    choke_damp = expf(-1.0f / (0.004f * fs));   // a choked stroke: a 4 ms fall, -60 dB in about 28 ms
    crush_step = k->bits < 16 ? (float)(1 << (16 - k->bits)) / 32768.0f : 0;
    __enable_irq();
  }
  const drum_kit_t *currentKit() { return kit; }

  // A hit on voice v, as hard as velocity (0-1), the body at pitch_hz if the voice follows, after delay samples
  void hit(uint8_t v, float velocity, float pitch_hz = 0, uint32_t delay = 0) {
    if (v >= DRUM_VOICES || !kit) return;
    uint32_t primask = drum_primask();   // from the rhythm's timer or the loop: as it was after
    __disable_irq();
    uint8_t next = (head + 1) % QUEUE;
    if (next != tail) {
      queue[head] = {v, velocity, pitch_hz, delay};
      head = next;
    }
    if (!primask) __enable_irq();
  }
  void level(float g) { gain = g; }
  // The reverb's share of each, out of output 2 (mono) into the reverb's free input: they come out
  // after the reverb, so without it the drums and the drone stay dry beside whatever it fills
  void drumReverb(float s) { send_drums = s; }
  void droneReverb(float s) { send_drone = s; }
  Tanpura drone;   // rendered here with the drums, at its own level (see tanpura.h)
  bool sounding() { for (uint8_t i = 0; i < DRUM_VOICES; i++) if (voice[i].active) return true; return head != tail || waiting; }

  virtual void update(void) {
    if (!kit) return;
    // hits due: waiting ones counted down a block, new ones taken from the queue
    while (tail != head) {
      pending[waiting < PENDING ? waiting++ : PENDING - 1] = queue[tail];
      tail = (tail + 1) % QUEUE;
    }
    for (uint8_t q = 0; q < waiting; q++) {
      if (pending[q].delay > AUDIO_BLOCK_SAMPLES) { pending[q].delay -= AUDIO_BLOCK_SAMPLES; continue; }
      start(pending[q]);
      pending[q--] = pending[--waiting];
    }
    bool any = false;
    for (uint8_t i = 0; i < DRUM_VOICES; i++) any |= voice[i].active;
    if (!any && !drone.active()) return;
    audio_block_t *l = allocate(), *r = allocate();
    if (!l || !r) { if (l) release(l); if (r) release(r); return; }
    const float sd = send_drums * 0.5f, st = send_drone * 0.5f;   // each side's half of the mono send
    audio_block_t *v = (sd > 0 || st > 0) ? allocate() : nullptr;
    float out_l[AUDIO_BLOCK_SAMPLES] = {0}, out_r[AUDIO_BLOCK_SAMPLES] = {0};
    const float inv_fs = 1.0f / AUDIO_SAMPLE_RATE_EXACT;
    float x[AUDIO_BLOCK_SAMPLES];
    for (uint8_t i = 0; i < DRUM_VOICES; i++) {
      voice_t &s = voice[i];
      if (!s.active) continue;
      for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) x[n] = 0;
      // the body, a block at a time, while it's above -60 dB
      if (s.tone_amp > SILENT) {
        float phase = s.phase, amp = s.tone_amp, bend = s.bend;
        const float base = s.tone_hz * inv_fs, span = s.bend_span * base, td = s.tone_decay, bd = s.bend_decay;
        for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
          phase += base + span * bend;
          bend *= bd;
          phase -= (int32_t)phase;
          float idx = phase * 256.0f;
          int32_t i0 = (int32_t)idx;
          float a = AudioWaveformSine[i0], b = AudioWaveformSine[i0 + 1];
          x[n] = (a + (b - a) * (idx - i0)) * (1.0f / 32767.0f) * amp;
          amp *= td;
        }
        s.phase = phase; s.tone_amp = amp; s.bend = bend;
      }
      // the burst: noise and metal through the filter, a clap's strikes again
      if (s.noise_amp > SILENT || s.retrigs) {
        float amp = s.noise_amp, ic1 = s.ic1, ic2 = s.ic2;
        const float a1 = s.a1, a2 = s.a2, a3 = s.a3, k = s.k, nd = s.noise_decay;
        const float nl = s.noise_level * (1.0f / 2147483648.0f), ml = s.metal_level / s.metal_count;
        const uint8_t mc = s.metal_level > 0 ? s.metal_count : 0, type = s.filter;
        uint32_t r = noise;
        for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
          if (s.retrigs && ++s.retrig_count >= s.retrig_samples) { s.retrig_count = 0; s.retrigs--; amp = s.noise_start; }
          float burst = 0;
          if (nl != 0) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; burst = (int32_t)r * nl; }
          if (mc) {
            float m = 0;
            for (uint8_t j = 0; j < mc; j++) {
              float ph = s.metal_phase[j] + s.metal_inc[j];
              if (ph >= 1.0f) ph -= 1.0f;
              s.metal_phase[j] = ph;
              m += ph < 0.5f ? 1.0f : -1.0f;
            }
            burst += m * ml;
          }
          float v3 = burst - ic2;
          float v1 = a1 * ic1 + a2 * v3;
          float v2 = ic2 + a2 * ic1 + a3 * v3;
          ic1 = 2 * v1 - ic1;
          ic2 = 2 * v2 - ic2;
          float y = type == 0 ? v2 : type == 1 ? v1 * k : burst - k * v1 - v2;
          x[n] += y * amp;
          amp *= nd;
        }
        noise = r; s.noise_amp = amp; s.ic1 = ic1; s.ic2 = ic2;
      }
      // a recorded stroke, read between its samples at the rate it was recorded
      if (s.sample_amp > 0) {
        const int16_t *d = s.sample->data;
        const uint32_t last = s.sample->length - 1;
        float pos = s.sample_pos, amp = s.sample_amp * (1.0f / 32768.0f);
        const float inc = s.sample_inc, damp = s.sample_choked ? choke_damp : 1.0f;
        for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
          uint32_t i0 = (uint32_t)pos;
          if (i0 >= last) { amp = 0; break; }
          float a = d[i0], b = d[i0 + 1];
          x[n] += (a + (b - a) * (pos - i0)) * amp;
          pos += inc;
          amp *= damp;
        }
        s.sample_pos = pos;
        s.sample_amp = amp * 32768.0f;
        if (s.sample_amp <= SILENT) s.sample_amp = 0;
      }
      const float left = s.left, right = s.right;
      for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) { out_l[n] += x[n] * left; out_r[n] += x[n] * right; }
      if (s.tone_amp <= SILENT && s.noise_amp <= SILENT && !s.retrigs && s.sample_amp <= 0) s.active = false;
    }
    // the drone, clean, after the kit's grit
    float drone_l[AUDIO_BLOCK_SAMPLES] = {0}, drone_r[AUDIO_BLOCK_SAMPLES] = {0};
    drone.render(drone_l, drone_r, AUDIO_BLOCK_SAMPLES);
    // the kit's grit and drive, the level, and a soft clip so a pile of hits rounds off rather than cracks
    float drive = kit->drive, g = 26000.0f;
    uint8_t hold = kit->hold > 1 ? kit->hold : 1;
    for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
      float a = out_l[n] * drive, b = out_r[n] * drive;
      if (hold > 1) {
        if (hold_count == 0) { held_l = a; held_r = b; }
        a = held_l; b = held_r;
        hold_count = (hold_count + 1) % hold;
      }
      if (crush_step > 0) { a = roundf(a / crush_step) * crush_step; b = roundf(b / crush_step) * crush_step; }
      a = a / (1.0f + fabsf(a)) * 1.6f * gain;
      b = b / (1.0f + fabsf(b)) * 1.6f * gain;
      if (v) v->data[n] = (int16_t)constrain(((a + b) * sd + (drone_l[n] + drone_r[n]) * st) * g, -32767.0f, 32767.0f);
      a += drone_l[n];
      b += drone_r[n];
      l->data[n] = (int16_t)constrain(a * g, -32767.0f, 32767.0f);
      r->data[n] = (int16_t)constrain(b * g, -32767.0f, 32767.0f);
    }
    transmit(l, 0);
    transmit(r, 1);
    release(l);
    release(r);
    if (v) { transmit(v, 2); release(v); }
  }

private:
  static const uint8_t QUEUE = 32, PENDING = 32;
  static constexpr float SILENT = 0.001f;   // -60 dB: a voice under it is done
  struct hit_t { uint8_t voice; float velocity, pitch; uint32_t delay; };
  struct voice_t {
    bool active;
    float tone_hz, tone_amp, tone_decay, bend, bend_decay, phase;
    float noise_amp, noise_start, noise_decay;
    float metal_phase[4], metal_inc[4];
    float ic1, ic2, k, a1, a2, a3;
    float left, right;
    float bend_span, noise_level, metal_level;
    uint8_t metal_count, filter;
    uint8_t retrigs;
    const drum_sample_t *sample;
    float sample_pos, sample_base_inc, sample_inc, sample_amp;
    bool sample_choked;
    uint32_t retrig_count, retrig_samples;
  };
  static float decay(float ms, float fs) { return expf(-6.9078f / fmaxf(ms * fs / 1000.0f, 1.0f)); }   // to -60 dB in ms

  void start(const hit_t &h) {
    const drum_voice_t &p = kit->v[h.voice];
    voice_t &s = voice[h.voice];
    if (p.choke) {
      for (uint8_t i = 0; i < DRUM_VOICES; i++) {
        if (i != h.voice && kit->v[i].choke == p.choke && voice[i].active) { voice[i].noise_amp *= 0.05f; voice[i].tone_amp *= 0.05f; voice[i].sample_choked = true; }
      }
    }
    float vel = constrain(h.velocity, 0.0f, 1.0f);
    s.tone_hz = (p.follow && h.pitch > 0) ? h.pitch : p.tone_hz;
    s.tone_amp = p.tone_hz > 0 ? p.tone_level * vel : 0;
    s.bend = 1.0f;
    s.phase = 0;   // from the sine's zero crossing: no click
    s.noise_start = s.noise_amp = (p.noise_level > 0 || p.metal_level > 0) ? vel : 0;
    s.retrigs = p.retrigs;
    s.retrig_count = 0;
    s.ic1 = s.ic2 = 0;
    s.sample_pos = 0;
    s.sample_amp = s.sample ? vel : 0;
    // a stroke that follows, at the pitch the hit gives, in whichever octave is nearest its own
    float ratio = 1.0f;
    if (s.sample && p.follow && h.pitch > 0 && s.sample->pitch_hz > 0) {
      ratio = h.pitch / s.sample->pitch_hz;
      while (ratio > 1.4142f) ratio *= 0.5f;
      while (ratio < 0.7071f) ratio *= 2.0f;
    }
    s.sample_inc = s.sample_base_inc * ratio;
    s.sample_choked = false;
    s.active = s.tone_amp > 0 || s.noise_amp > 0 || s.sample_amp > 0;
  }

  const drum_kit_t *kit = nullptr;
  voice_t voice[DRUM_VOICES] = {};
  volatile uint8_t head = 0, tail = 0;
  hit_t queue[QUEUE];
  hit_t pending[PENDING];
  uint8_t waiting = 0;
  uint32_t noise = 22222;
  float gain = 0.7f, choke_damp = 1.0f, crush_step = 0, held_l = 0, held_r = 0;
  float send_drums = 0, send_drone = 0;   // drum reverb (378), drone reverb (379)
  uint8_t hold_count = 0;
};

#endif
