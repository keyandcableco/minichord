#include "plucked_string.h"

// The cubic (Lagrange) interpolation's four weights, reading x (0-1) of the way from
// the second sample to the third.
static inline void lagrange(double x, double h[4]) {
  h[0] = -x * (x - 1) * (x - 2) / 6;
  h[1] = (x + 1) * (x - 1) * (x - 2) / 2;
  h[2] = -(x + 1) * x * (x - 2) / 2;
  h[3] = (x + 1) * x * (x - 1) / 6;
}

// The delay that sounds freq, and what the loop keeps each trip round it so that the
// fundamental falls 60 dB in t60 seconds at any pitch.
//
// The loop loses three ways each trip: the damping filter, a one-pole lowpass
// (1 - d) / (1 - d z^-1); reading the delay between samples, itself a gentle lowpass;
// and loss, the gain the rest is made up with. The filter and the reading cost a
// fixed share each trip, so a string making a thousand trips a second paid it a
// thousand times: with linear reading and loss set from t60 alone, the 1 kHz string
// at the default damping died in under a second, not 3, and each string by a different amount
// as its delay fell between samples. Read with the cubic, the reading costs little;
// loss now makes up what the filter and the reading take from the fundamental.
//
// loss has to stay below 1: every part of the loop passes the average (0 Hz) whole,
// so above 1 it would grow. The filter is therefore let take at most a share of the
// fundamental's decay, which on the low notes it never nears (the damping there
// is as it was); on the high ones its d gives way, smoothly, so the damping still
// darkens them more the more it is turned up.
//
// The delay is a period less the filter's own delay at that pitch,
// atan2(d sin w, 1 - d cos w) / w, so the string is in tune however damped; the
// cubic's own delay is what it reads, to well within a cent. In double: the losses
// are a few parts in a million a trip.
void AudioSynthPluckedString::tune(float freq) {
  tuned_freq = freq;
  const double w = 2.0 * M_PI * freq / AUDIO_SAMPLE_RATE_EXACT;
  const double sw = sin(w), cw = cos(w);
  const double s2 = (1 - cw) / 2;   // sin^2(w/2)
  const double budget = log(1000.0) / (freq * t60);   // the fundamental's decay a trip, in nepers
  const double share = 0.8;                            // of it the filter may take, at most
  // the filter's loss at w for d: |H|^-2 = 1 + 4 d sin^2(w/2) / (1 - d)^2
  double d = damp;
  double filter_loss = 0.5 * log1p(4 * d * s2 / ((1 - d) * (1 - d)));
  const double asked = filter_loss / budget;
  const double allowed = share * -expm1(-asked / share);   // ~asked while small, never past share
  if (allowed < asked) {
    filter_loss = allowed * budget;
    double a = expm1(2 * filter_loss) / (4 * s2);          // d / (1 - d)^2
    d = 2 * a / ((2 * a + 1) + sqrt(4 * a + 1));            // its root below 1
  }
  loop_damp = d;
  double filter_delay = atan2(d * sw, 1 - d * cw) / w;
  delay_samples = constrain((float)(AUDIO_SAMPLE_RATE_EXACT / freq - filter_delay), 3.0f, (float)(length - 3));
  // the cubic's loss at w, where this delay falls between samples
  double read = -(double)delay_samples, x = read - floor(read), h[4];
  lagrange(x, h);
  // at the taps -1, 0, 1 and 2 samples from the read point
  double re = h[0] * cw + h[1] + h[2] * cw + h[3] * (2 * cw * cw - 1);
  double im = -h[0] * sw + h[2] * sw + h[3] * 2 * sw * cw;
  double read_loss = -0.5 * log(re * re + im * im);
  loss = (float)min(exp(-budget + filter_loss + read_loss), 0.99999);
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
  uint16_t n = (uint16_t)ceilf(delay_samples) + 2;   // the cubic reads a sample either side
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
      uint16_t i1 = (uint16_t)read;
      float x = read - i1;
      uint16_t i0 = i1 == 0 ? length - 1 : i1 - 1;
      uint16_t i2 = i1 + 1 >= length ? i1 + 1 - length : i1 + 1;
      uint16_t i3 = i1 + 2 >= length ? i1 + 2 - length : i1 + 2;
      // the cubic through the four samples about the read point, in Horner form
      float y0 = buffer[i0], y1 = buffer[i1], y2 = buffer[i2], y3 = buffer[i3];
      float c1 = y2 - (1.0f / 3.0f) * y0 - 0.5f * y1 - (1.0f / 6.0f) * y3;
      float c2 = 0.5f * (y0 + y2) - y1;
      float c3 = (1.0f / 6.0f) * (y3 - y0) + 0.5f * (y1 - y2);
      s = y1 + x * (c1 + x * (c2 + x * c3));
      lowpass += (1.0f - loop_damp) * (s - lowpass);
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
