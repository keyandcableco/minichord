#include "plucked_string.h"

// The delay that sounds freq: a period, less the loop filter's own delay at that
// pitch, so the string is in tune however damped. The filter is a one-pole lowpass,
// (1 - d) / (1 - d z^-1), whose phase delay at w is atan2(d sin w, 1 - d cos w) / w.
// The loss per trip round the loop makes the note fall 60 dB in t60 seconds, at
// any pitch.
void AudioSynthPluckedString::tune(float freq) {
  tuned_freq = freq;
  float w = 2.0f * (float)M_PI * freq / AUDIO_SAMPLE_RATE_EXACT;
  float filter_delay = atan2f(damp * sinf(w), 1.0f - damp * cosf(w)) / w;
  delay_samples = constrain(AUDIO_SAMPLE_RATE_EXACT / freq - filter_delay, 2.0f, (float)(length - 2));
  loss = powf(10.0f, -3.0f / (freq * t60));
}

// Fills one period behind the write point with noise, lowpassed as dark as the
// pluck is soft, without its average (which would only thump), and levelled so its
// body sits beside a full-scale sawtooth's, so loudness is amplitude()'s alone.
// The lowpass is set from the note, from its own pitch (a soft pluck) to two
// octaves above (a firm one): its harmonics fall away as a sawtooth's do, where
// a fixed lowpass left the low notes' plucks white noise. The body is what a
// lowpass at the note keeps, the part the string's damping and the harp's filter
// leave: levelled to its whole RMS, a pluck spent most of its level on highs
// that died within a few periods or never got past the filter, and turning up
// string model lost the harp 16-20 dB.
void AudioSynthPluckedString::pluck(float brightness) {
  tune(base_freq);
  uint16_t n = (uint16_t)ceilf(delay_samples) + 1;
  const float w = 2.0f * (float)M_PI * tuned_freq / AUDIO_SAMPLE_RATE_EXACT;
  float alpha = min(1.0f - expf(-w * powf(4.0f, constrain(brightness, 0.0f, 1.0f))), 1.0f);
  float y = 0, mean = 0;
  for (uint16_t k = 0; k < n; k++) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    float noise = (int32_t)seed / 2147483648.0f;
    y += alpha * (noise - y);
    buffer[(write_index + length - n + k) % length] = y;
    mean += y;
  }
  mean /= n;
  for (uint16_t k = 0; k < n; k++) buffer[(write_index + length - n + k) % length] -= mean;
  // The body: through a lowpass at the note, round the period twice, measured the
  // second time, once the filter has settled as it would on the ringing string.
  // A 0.58-RMS sawtooth's body through the same lowpass is 0.34.
  const float body_alpha = 1.0f - expf(-w);
  float body = 0, power = 1e-12f;
  for (uint8_t pass = 0; pass < 2; pass++) {
    for (uint16_t k = 0; k < n; k++) {
      body += body_alpha * (buffer[(write_index + length - n + k) % length] - body);
      if (pass) power += body * body;
    }
  }
  float level = 0.34f / sqrtf(power / n);
  for (uint16_t k = 0; k < n; k++) buffer[(write_index + length - n + k) % length] *= level;
  lowpass = 0;
  ringing = true;
}

void AudioSynthPluckedString::update(void) {
  audio_block_t *fm = receiveReadOnly(0);
  if (mix <= 0.0f) {   // no string in the blend: the oscillator as it is
    if (fm) release(fm);
    audio_block_t *osc = receiveReadOnly(1);
    if (osc) {
      transmit(osc);
      release(osc);
    }
    return;
  }
  if (!ringing) {
    // No string sounding, so only the oscillator's share of the blend to pass on: scaled in
    // place, two integer operations a sample where the loop below spends a few dozen in float,
    // and with nearly every string silent nearly all the time, that was most of the model's
    // cost. A block still goes out, silent if need be, rather than none: the envelope after
    // this only moves on through its stages while blocks arrive.
    if (fm) release(fm);
    audio_block_t *osc = receiveWritable(1);
    if (!osc) {
      osc = allocate();
      if (!osc) return;
      memset(osc->data, 0, sizeof(osc->data));
    } else {
      int32_t gain = (int32_t)((1.0f - mix) * 65536.0f);   // 1 - mix, in 16 fractional bits
      for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        int32_t v = osc->data[i] * gain;
        osc->data[i] = v >= 0 ? v >> 16 : -(-v >> 16);   // towards zero, as the float loop's cast
      }
    }
    transmit(osc);
    release(osc);
    return;
  }
  audio_block_t *osc = receiveReadOnly(1);
  float freq = base_freq;
  if (fm) {
    int32_t sum = 0;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) sum += fm->data[i];
    release(fm);
    freq *= exp2f(sum / (AUDIO_BLOCK_SAMPLES * 32768.0f) * fm_octaves);   // moves once a block, as vibrato needs
  }
  audio_block_t *out = allocate();
  if (!out) {
    if (osc) release(osc);
    return;
  }
  if (fabsf(freq - tuned_freq) > 0.0002f * freq) tune(freq);
  float oscillator = 1.0f - mix;
  float string = mix * amp;
  float peak = 0;
  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
    float s = 0;
    if (ringing) {
      float read = write_index - delay_samples;
      if (read < 0) read += length;
      uint16_t i0 = (uint16_t)read;
      uint16_t i1 = i0 + 1 >= length ? 0 : i0 + 1;
      float fraction = read - i0;
      s = buffer[i0] + fraction * (buffer[i1] - buffer[i0]);
      lowpass += (1.0f - damp) * (s - lowpass);
      buffer[write_index] = lowpass * loss;
      if (++write_index >= length) write_index = 0;
      peak = max(peak, fabsf(s));
    }
    float v = string * s * 32767.0f + (osc ? oscillator * osc->data[i] : 0.0f);
    out->data[i] = (int16_t)constrain(v, -32767.0f, 32767.0f);
  }
  if (ringing && peak < 0.00002f) ringing = false;   // died away: nothing to compute until the next pluck
  if (osc) release(osc);
  transmit(out);
  release(out);
}
