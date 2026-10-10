// The drum machine: thirteen synthesized drum voices in one audio object, stereo out (see DRUMS in
// main.cpp, and generator/drums.py for the kits and grooves). Each voice is a pitched body (a sine
// whose pitch falls from tone_bend times its own to it, for a kick's thump or a tom's) and a burst of
// noise and metal (white noise, and two or four square waves at bell or cymbal ratios) through a
// filter, each with its own decay; a clap's noise is struck again a few times, quickly, before its
// tail; hats in one choke group cut each other off. A kit is the thirteen voices' settings, and the
// kit's grit: fewer bits and held samples for a lo-fi or broken sound, and a drive. Hits come from
// the rhythm's timer and from the loop (the harp's pads), so they go through a little queue the audio
// update takes them from, each with a delay in samples (the humanize). Nothing sounding, nothing runs.
#ifndef DRUM_MACHINE_H
#define DRUM_MACHINE_H
#include <Arduino.h>
#include <AudioStream.h>

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
  uint8_t follow;                                           // 1: the body takes the pitch the hit gives (an 808 bass)
};

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
      s.active = false;
    }
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
    if (!any) return;
    audio_block_t *l = allocate(), *r = allocate();
    if (!l || !r) { if (l) release(l); if (r) release(r); return; }
    float out_l[AUDIO_BLOCK_SAMPLES] = {0}, out_r[AUDIO_BLOCK_SAMPLES] = {0};
    for (uint8_t i = 0; i < DRUM_VOICES; i++) {
      voice_t &s = voice[i];
      if (!s.active) continue;
      const drum_voice_t &p = kit->v[i];
      for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
        float x = 0;
        if (s.tone_amp > 0.0001f) {
          float f = s.tone_hz * (1.0f + (p.tone_bend - 1.0f) * s.bend);
          s.bend *= s.bend_decay;
          s.phase += f / AUDIO_SAMPLE_RATE_EXACT;
          s.phase -= (int32_t)s.phase;
          float idx = s.phase * 256.0f;
          int32_t i0 = (int32_t)idx;
          float fr = idx - i0;
          float sine = (AudioWaveformSine[i0] + (AudioWaveformSine[i0 + 1] - AudioWaveformSine[i0]) * fr) * (1.0f / 32767.0f);
          x += sine * s.tone_amp;
          s.tone_amp *= s.tone_decay;
        }
        if (s.retrigs && ++s.retrig_count >= s.retrig_samples) {   // a clap: struck again
          s.retrig_count = 0;
          s.retrigs--;
          s.noise_amp = s.noise_start;
        }
        if (s.noise_amp > 0.0001f) {
          float burst = 0;
          if (p.noise_level > 0) {
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            burst += (int32_t)noise * (1.0f / 2147483648.0f) * p.noise_level;
          }
          if (p.metal_level > 0) {
            float m = 0;
            uint8_t count = p.metal_count == 4 ? 4 : 2;
            for (uint8_t k = 0; k < count; k++) {
              s.metal_phase[k] += s.metal_inc[k];
              if (s.metal_phase[k] >= 1.0f) s.metal_phase[k] -= 1.0f;
              m += s.metal_phase[k] < 0.5f ? 1.0f : -1.0f;
            }
            burst += m / count * p.metal_level;
          }
          float v3 = burst - s.ic2;
          float v1 = s.a1 * s.ic1 + s.a2 * v3;
          float v2 = s.ic2 + s.a2 * s.ic1 + s.a3 * v3;
          s.ic1 = 2 * v1 - s.ic1;
          s.ic2 = 2 * v2 - s.ic2;
          float y = p.filter == 0 ? v2 : p.filter == 1 ? v1 * s.k : burst - s.k * v1 - v2;
          x += y * s.noise_amp;
          s.noise_amp *= s.noise_decay;
        }
        out_l[n] += x * s.left;
        out_r[n] += x * s.right;
      }
      if (s.tone_amp <= 0.0001f && s.noise_amp <= 0.0001f && !s.retrigs) s.active = false;
    }
    // the kit's grit and drive, the level, and a soft clip so a pile of hits rounds off rather than cracks
    float drive = kit->drive, g = gain * 26000.0f;
    uint8_t hold = kit->hold > 1 ? kit->hold : 1;
    for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
      float a = out_l[n] * drive, b = out_r[n] * drive;
      if (hold > 1) {
        if (hold_count == 0) { held_l = a; held_r = b; }
        a = held_l; b = held_r;
        hold_count = (hold_count + 1) % hold;
      }
      if (crush_step > 0) { a = roundf(a / crush_step) * crush_step; b = roundf(b / crush_step) * crush_step; }
      a = a / (1.0f + fabsf(a)) * 1.6f;
      b = b / (1.0f + fabsf(b)) * 1.6f;
      l->data[n] = (int16_t)constrain(a * g, -32767.0f, 32767.0f);
      r->data[n] = (int16_t)constrain(b * g, -32767.0f, 32767.0f);
    }
    transmit(l, 0);
    transmit(r, 1);
    release(l);
    release(r);
  }

private:
  static const uint8_t QUEUE = 32, PENDING = 32;
  struct hit_t { uint8_t voice; float velocity, pitch; uint32_t delay; };
  struct voice_t {
    bool active;
    float tone_hz, tone_amp, tone_decay, bend, bend_decay, phase;
    float noise_amp, noise_start, noise_decay;
    float metal_phase[4], metal_inc[4];
    float ic1, ic2, k, a1, a2, a3;
    float left, right;
    uint8_t retrigs;
    uint32_t retrig_count, retrig_samples;
  };
  static float decay(float ms, float fs) { return expf(-6.9078f / fmaxf(ms * fs / 1000.0f, 1.0f)); }   // to -60 dB in ms

  void start(const hit_t &h) {
    const drum_voice_t &p = kit->v[h.voice];
    voice_t &s = voice[h.voice];
    if (p.choke) {
      for (uint8_t i = 0; i < DRUM_VOICES; i++) {
        if (i != h.voice && kit->v[i].choke == p.choke && voice[i].active) { voice[i].noise_amp *= 0.05f; voice[i].tone_amp *= 0.05f; }
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
    s.active = s.tone_amp > 0 || s.noise_amp > 0;
  }

  const drum_kit_t *kit = nullptr;
  voice_t voice[DRUM_VOICES] = {};
  volatile uint8_t head = 0, tail = 0;
  hit_t queue[QUEUE];
  hit_t pending[PENDING];
  uint8_t waiting = 0;
  uint32_t noise = 22222;
  float gain = 0.7f, crush_step = 0, held_l = 0, held_r = 0;
  uint8_t hold_count = 0;
};

#endif
