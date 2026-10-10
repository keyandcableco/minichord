#pragma once
#include "Arduino.h"
#define AUDIO_BLOCK_SAMPLES 128
#define AUDIO_SAMPLE_RATE_EXACT 44117.64706f
struct audio_block_t { int16_t data[AUDIO_BLOCK_SAMPLES]; };
class AudioStream {
public:
  AudioStream(int, audio_block_t **) {}
  virtual void update() = 0;
  audio_block_t *out[2] = {nullptr, nullptr};
protected:
  audio_block_t *allocate() { return new audio_block_t(); }
  void transmit(audio_block_t *b, int ch) { out[ch] = new audio_block_t(*b); }
  void release(audio_block_t *b) { delete b; }
};
