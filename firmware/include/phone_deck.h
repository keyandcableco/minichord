// The phone deck: the song coming in over USB on its way to the speaker (see PHONE DJ in main.cpp).
// It passes the song through as it comes, and keeps the last stretch of it, mono and at a third of
// the sample rate, in a ring: for the harp to play when it is grabbed (grab), and for the tape stop,
// which plays the song on from the ring ever slower until it stands still, and silence after, until
// start() brings the song back in over 40 ms. The ring is given at begin(), in RAM2.
#ifndef PHONE_DECK_H
#define PHONE_DECK_H
#include <Arduino.h>
#include <AudioStream.h>

class AudioPhoneDeck : public AudioStream {
public:
  AudioPhoneDeck() : AudioStream(2, inputQueueArray) {}
  static const uint8_t DECIMATE = 3;   // the ring's rate: 44117.6 / 3, 14706 Hz

  void begin(int16_t *ring_buffer, uint32_t length) { ring = ring_buffer; size = length; }

  // the song slows to a stop over ms, from where it is now
  void stop(uint32_t ms) {
    __disable_irq();
    if (state == LIVE) {
      read = (float)((write + size - 1) % size);
      length = max<uint32_t>(1, ms * (uint32_t)(AUDIO_SAMPLE_RATE_EXACT / 1000));
      elapsed = 0;
      state = STOPPING;
    }
    __enable_irq();
  }
  void start() {
    __disable_irq();
    if (state != LIVE) { state = LIVE; fade = 0; }
    __enable_irq();
  }
  bool live() { return state == LIVE; }

  // the last n samples kept, oldest first, into out (at most the ring's length); with audio interrupts held
  uint32_t grab(int16_t *out, uint32_t n) {
    if (!ring) return 0;
    if (n > size) n = size;
    uint32_t from = (write + size - n) % size;
    for (uint32_t k = 0; k < n; k++) out[k] = ring[(from + k) % size];
    return n;
  }

  virtual void update(void) {
    audio_block_t *l = receiveReadOnly(0), *r = receiveReadOnly(1);
    // the ring: the song in mono, through two one-pole low passes (about 5 kHz) and every third sample kept
    if (ring && (l || r)) {
      for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
        int32_t x = ((l ? l->data[n] : 0) + (r ? r->data[n] : 0)) << 7;   // 8 bits of headroom kept below
        lp1 += (x - lp1) >> 1;
        lp2 += (lp1 - lp2) >> 1;
        if (++phase >= DECIMATE) {
          phase = 0;
          ring[write] = (int16_t)(lp2 >> 8);
          write = write + 1 >= size ? 0 : write + 1;
        }
      }
    }
    if (state == LIVE && fade >= FADE) {
      if (l) { transmit(l, 0); release(l); }
      if (r) { transmit(r, 1); release(r); }
      return;
    }
    if (state == LIVE) {   // back in, faded up
      for (uint8_t c = 0; c < 2; c++) {
        audio_block_t *in = c ? r : l;
        if (!in) continue;
        audio_block_t *out = allocate();
        if (out) {
          for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
            uint32_t g = min<uint32_t>(fade + n, FADE);
            out->data[n] = (int16_t)((int32_t)in->data[n] * (int32_t)g / (int32_t)FADE);
          }
          transmit(out, c);
          release(out);
        }
      }
      fade += AUDIO_BLOCK_SAMPLES;
      if (l) release(l);
      if (r) release(r);
      return;
    }
    if (l) release(l);
    if (r) release(r);
    if (state == STOPPED || !ring) return;   // silence
    audio_block_t *out = allocate();
    if (!out) return;
    for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
      float left = 1.0f - (float)elapsed / (float)length;   // the speed, falling to nothing
      if (left <= 0) { out->data[n] = 0; state = STOPPED; continue; }
      elapsed++;
      uint32_t i = (uint32_t)read;
      float f = read - (float)i;
      int32_t a = ring[i % size], b = ring[(i + 1) % size];
      out->data[n] = (int16_t)(a + (b - a) * f);
      read += left / DECIMATE;
      if (read >= size) read -= size;
    }
    transmit(out, 0);
    transmit(out, 1);
    release(out);
  }

private:
  enum { LIVE, STOPPING, STOPPED };
  static const uint32_t FADE = 1764;   // 40 ms
  audio_block_t *inputQueueArray[2];
  int16_t *ring = nullptr;
  uint32_t size = 0, write = 0, length = 1, elapsed = 0, fade = FADE;
  uint8_t phase = 0;
  volatile uint8_t state = LIVE;
  int32_t lp1 = 0, lp2 = 0;
  float read = 0;
};

#endif
