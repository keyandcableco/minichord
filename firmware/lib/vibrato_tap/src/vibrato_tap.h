#ifndef VIBRATO_TAP_H
#define VIBRATO_TAP_H

#include "Arduino.h"
#include "AudioStream.h"
#include "lean_mixer.h"

// Listens to a pitch-modulation signal, the one a modulated oscillator's frequency input takes,
// and hands its block average on as a ratio of frequency, for voices that have no such input: a
// sample player is only ever given setFrequency. It sends nothing on. The ratio is
// exp2(average * octaves), the curve AudioSynthWaveformModulated follows, reached once a block as
// AudioSynthPluckedString does; octaves() is to be given what the oscillators' frequencyModulation()
// is (the harp's vibrato intensity, 76), and keeps to the same 0.1-12 they do. The hook is called
// from the audio interrupt, each block while the signal moves the pitch and once more when it
// stops (ratio 1), so a voice left where it was is put back on its note.
class AudioVibratoTap : public AudioStream
{
public:
  AudioVibratoTap() : AudioStream(1, inputQueueArray) {}
  void octaves(float o) { fm_octaves = o < 0.1f ? 0.1f : o > 12.0f ? 12.0f : o; }
  void on_ratio(void (*hook)(float ratio)) { ratio_hook = hook; }
  float ratio() const { return last_ratio; }
  virtual void update(void) {
    audio_block_t *in = receiveReadOnly(0);
    int32_t sum = 0;
    if (in) {
      for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) sum += in->data[i];
      release(in);
    }
    if (sum == 0) {
      if (!moving) return;
      moving = false;
      last_ratio = 1;
    } else {
      moving = true;
      last_ratio = exp2f(sum / (AUDIO_BLOCK_SAMPLES * 32768.0f) * fm_octaves);
    }
    if (ratio_hook) ratio_hook(last_ratio);
  }

private:
  float fm_octaves = 8;   // AudioSynthWaveformModulated's own, until frequencyModulation() is called
  volatile float last_ratio = 1;
  bool moving = false;
  void (*ratio_hook)(float) = nullptr;
  audio_block_t *inputQueueArray[1];
};

// The same for a chord voice, whose pitch input (voiceN_vibrato_mixer) also carries the glide, which its
// sample player is already given by setFrequency. This one listens only to
// the vibrato's two inputs there, the LFO's envelope (0) and the pitch envelope (1), and weighs them
// by that mixer's own gains (175 sets them), so it moves the pitch as the oscillators are moved by
// those two and no further. The hook is told which voice.
class AudioChordVibratoTap : public AudioStream
{
public:
  AudioChordVibratoTap() : AudioStream(2, inputQueueArray) {}
  void octaves(float o) { fm_octaves = o < 0.1f ? 0.1f : o > 12.0f ? 12.0f : o; }
  void gains_of(const AudioMixer4Lean *m) { mixer = m; }
  void on_ratio(void (*hook)(uint8_t voice, float ratio), uint8_t v) { ratio_hook = hook; voice = v; }
  float ratio() const { return last_ratio; }
  virtual void update(void) {
    float sum = 0;
    for (int k = 0; k < 2; k++) {
      audio_block_t *in = receiveReadOnly(k);
      if (!in) continue;
      int32_t g = mixer ? mixer->gain_fixed(k) : 65536;
      if (g) {
        int32_t s = 0;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) s += in->data[i];
        sum += s * (g / 65536.0f);
      }
      release(in);
    }
    if (sum == 0) {
      if (!moving) return;
      moving = false;
      last_ratio = 1;
    } else {
      moving = true;
      last_ratio = exp2f(sum / (AUDIO_BLOCK_SAMPLES * 32768.0f) * fm_octaves);
    }
    if (ratio_hook) ratio_hook(voice, last_ratio);
  }

private:
  float fm_octaves = 8;
  const AudioMixer4Lean *mixer = nullptr;
  volatile float last_ratio = 1;
  bool moving = false;
  uint8_t voice = 0;
  void (*ratio_hook)(uint8_t, float) = nullptr;
  audio_block_t *inputQueueArray[2];
};

#endif
