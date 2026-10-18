// host stand-in for the audio library's blocks: a pool of 900 with reference counts, as on the
// minichord (AudioMemory(900)), so a block kept and never let go shows as a leak
#pragma once
#include "Arduino.h"
#include <cstdio>
#define AUDIO_BLOCK_SAMPLES 128
#define AUDIO_SAMPLE_RATE_EXACT 44100.0f
struct audio_block_t { uint8_t ref_count; uint8_t reserved1; uint16_t memory_pool_index; int16_t data[AUDIO_BLOCK_SAMPLES]; };
struct AudioPool {
  static const int SIZE = 900;
  audio_block_t blocks[SIZE];
  int used = 0, most = 0;
  audio_block_t *get() {
    for (int i = 0; i < SIZE; i++) if (blocks[i].ref_count == 0) { blocks[i].ref_count = 1; used++; most = std::max(most, used); return &blocks[i]; }
    return nullptr;
  }
};
inline AudioPool &audio_pool() { static AudioPool p; return p; }
class AudioStream {
public:
  AudioStream(int inputs, audio_block_t **queue) : n_in(inputs), in_queue(queue) { for (int i = 0; i < inputs; i++) queue[i] = nullptr; }
  virtual void update() = 0;
  virtual ~AudioStream() {}
  audio_block_t *out[2] = {nullptr, nullptr};   // what the last update sent, a reference each
  void feed(int i, audio_block_t *b) { in_queue[i] = b; }   // the cord's reference handed over
  static audio_block_t *allocate() { return audio_pool().get(); }
  static void release(audio_block_t *b) {
    if (!b) return;
    if (b->ref_count == 0) { fprintf(stderr, "release of a free block\n"); abort(); }
    if (--b->ref_count == 0) audio_pool().used--;
  }
protected:
  void transmit(audio_block_t *b, unsigned char ch = 0) { if (b) { b->ref_count++; out[ch] = b; } }
  audio_block_t *receiveReadOnly(unsigned int i = 0) { audio_block_t *b = in_queue[i]; in_queue[i] = nullptr; return b; }
private:
  int n_in;
  audio_block_t **in_queue;
};
