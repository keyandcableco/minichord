// The bends' work, once a block (see bent_bus.h). In flash: RAM1's code is near a 32 kB step, and the
// cache holds these loops after their first pass. On a PC FLASHMEM is nothing (tools/bent_render).
#include "bent_bus.h"
#ifndef FLASHMEM
#define FLASHMEM
#endif

FLASHMEM AudioEffectBend::AudioEffectBend() : AudioStream(4, inputQueueArray) {}

// the drums into the mix, as the last mix's AudioMixer4 added them (gain 1, saturating), so with no bend
// the sum is the one it was; each block's reference passed on or let go
FLASHMEM void AudioEffectBend::add_drums(audio_block_t **cur, bool *own, audio_block_t **drums) {
  for (uint8_t c = 0; c < 2; c++) {
    if (!drums[c]) continue;
    if (!cur[c]) { cur[c] = drums[c]; own[c] = false; drums[c] = nullptr; continue; }
    audio_block_t *b = own[c] ? cur[c] : allocate();
    if (b) {
      for (uint32_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
        int32_t y = cur[c]->data[n] + drums[c]->data[n];
        b->data[n] = y > 32767 ? 32767 : y < -32768 ? -32768 : y;
      }
      if (b != cur[c]) { release(cur[c]); cur[c] = b; own[c] = true; }
    }
    release(drums[c]);
    drums[c] = nullptr;
  }
}

// one more reference to a block kept (the library's release() takes one away), as transmit adds one
// for each cord; the USB interrupt allocates and releases too, so with interrupts held
FLASHMEM void AudioEffectBend::hold(audio_block_t *b) {
  if (!b) return;
  __disable_irq();
  b->ref_count++;
  __enable_irq();
}

FLASHMEM void AudioEffectBend::release_slice() {
  for (uint8_t c = 0; c < 2; c++) {
    for (uint8_t k = 0; k < slice_blocks; k++) {
      if (slice[c][k]) release(slice[c][k]);
      slice[c][k] = nullptr;
    }
  }
  slice_blocks = 0;
  looping = false;
  stutter_fade = 0;
}

// the slice's length for the stutter's depth now: from the longest at a touch to the shortest at full,
// evenly in octaves; in sync, half a beat halving down to a 32nd as it deepens, none shorter than the
// shortest (doubled up to it) nor longer than the longest
FLASHMEM uint32_t AudioEffectBend::loop_wanted() {
  float x = (stutter_target - 0.03f) / 0.97f;
  x = x < 0 ? 0 : x > 1 ? 1 : x;
  const uint32_t longest = STUTTER_BLOCKS * AUDIO_BLOCK_SAMPLES;
  uint32_t beat = stutter_beat;
  if (beat) {
    uint32_t len = beat >> (1 + (uint8_t)(x * 4.0f + 0.5f));
    while (len > longest) len >>= 1;
    while (len < stutter_shortest && len * 2 <= longest) len <<= 1;
    return len < 64 ? 64 : len;
  }
  return (uint32_t)(longest * powf((float)stutter_shortest / longest, x));
}

FLASHMEM void AudioEffectBend::update(void) {
  const uint32_t N = AUDIO_BLOCK_SAMPLES;
  audio_block_t *cur[2] = {receiveReadOnly(0), receiveReadOnly(1)};   // each holds one reference
  audio_block_t *drums[2] = {receiveReadOnly(2), receiveReadOnly(3)};
  bool own[2] = {false, false};   // a block of this update's own, which nothing else holds: written in place

  // ---- the settings, once a block
  // the stutter pressed past 3%, let go under 1%
  float st = stutter_target;
  bool press = stuttering ? st > 0.01f : st > 0.03f;
  if (press && !stuttering) { release_slice(); stuttering = true; }
  if (!press && stuttering) { stuttering = false; if (looping) stutter_fade = 1; else release_slice(); }

  // the starve: a supply that sags with the depth and wanders, gives way now and then, and comes back slowly
  float sv = starve_target;
  starve_now += (sv - starve_now) * 0.15f;   // ~20 ms
  if (sv == 0 && starve_now < 1e-4f) starve_now = 0;
  float gain_aim = 1, rail = 32767, crackle_odds = 0;
  if (starve_now > 0) {
    if ((random() & 31) == 0) wobble_aim = noise();   // a new aim every 90 ms or so
    wobble_v += (wobble_aim - wobble_v) * 0.08f;
    wobble += (wobble_v - wobble) * 0.08f;
    if ((random() & 15) == 0) supply_aim = 1 - starve_now * (0.25f + 0.25f * (noise() * 0.5f + 0.5f));
    if (dropout) dropout--;
    else if ((random() & 0xFFFF) < (uint32_t)(starve_now * starve_now * 0.02f * 65536)) dropout = 2 + random() % 23;   // 6 to 70 ms
    supply += ((dropout ? 0.1f : supply_aim) - supply) * (dropout ? 0.3f : 0.05f);   // gives way quickly, comes back slowly
    float s = (supply - 0.1f) / 0.9f;
    s = s < 0 ? 0 : s;
    rail = 32767.0f * (0.03f + 0.97f * s * s * s);
    gain_aim = (supply - 0.15f) / 0.45f;
    gain_aim = gain_aim < 0 ? 0 : gain_aim > 1 ? 1 : gain_aim;
    crackle_odds = starve_now * starve_now * 0.0012f * (1 + 4 * (1 - supply));   // ~50 a second at full, more as it browns out
  } else {
    wobble = wobble_v = wobble_aim = 0;
    supply = supply_aim = 1;
    dropout = 0;
  }
  float starve_semis = starve_now > 0 ? (1 - supply) * 3.0f + starve_now * 0.8f * wobble : 0;

  // the pitch: the drop sags toward its depth, and the starve's wobble rides on it
  float drop_t = drop_target;
  drop_now += (drop_t - drop_now) * sag_coef;
  if (drop_t == 0 && drop_now < 0.05f) drop_now = 0;   // 5 cents, then the shifter fades out
  semis = drop_now + starve_semis;
  bool shifting = drop_t > 0 || starve_now > 0 || drop_now > 0;
  float wet_aim = shifting ? 1 : 0;
  float ratio_new = shifting ? exp2f(-semis / 12.0f) : 1;
  if (wet == 0 && shifting) { phase = 0.5f; ratio = 1; }   // in at the window's middle: the live sound 17 ms late

  // the crush
  float cv = crush_target;
  crush_now += (cv - crush_now) * 0.3f;
  if (cv == 0 && crush_now < 1e-4f) crush_now = 0;

  rest = !stuttering && !looping && wet == 0 && !shifting && starve_now == 0 && gain == 1 && crackle == 0 && crush_now == 0;

  // ---- the drums: into the mix now, unless a stutter is on and they're to keep playing through it
  bool drums_after = stutter_drums_through && (stuttering || looping);
  if (!drums_after) add_drums(cur, own, drums);

  // ---- the stutter: from the press on, the blocks kept; once there's a slice's worth, it plays over and over
  if (stuttering && slice_blocks < STUTTER_BLOCKS) {
    for (uint8_t c = 0; c < 2; c++) { hold(cur[c]); slice[c][slice_blocks] = cur[c]; own[c] = false; }
    slice_blocks++;
  }
  uint32_t captured = slice_blocks * N;
  if (stuttering && !looping && captured >= loop_wanted()) {   // from the next block
    looping = true;
    loop_length = loop_wanted();
    loop_at = 0;
  } else if (looping) {
    audio_block_t *out[2] = {allocate(), allocate()};
    if (out[0] && out[1]) {
      uint32_t at = 0, length = 0;
      uint16_t fade = 0;
      uint32_t next_length = loop_wanted();
      if (next_length > captured) next_length = captured;
      for (uint8_t c = 0; c < 2; c++) {
        at = loop_at; length = loop_length; fade = stutter_fade;
        audio_block_t *live = cur[c];
        for (uint32_t n = 0; n < N; n++) {
          // each repeat faded in and out over a millisecond, so the seam doesn't click
          uint32_t f = length / 4 < 48 ? length / 4 : 48;
          float e = 1;
          if (at < f) e = (at + 1) / (float)f;
          else if (length - at <= f) e = (length - at) / (float)f;
          float y = slice[c][at >> 7] ? slice[c][at >> 7]->data[at & 127] * e : 0;
          if (fade) {   // let go: back to the live sound over 128 samples
            float k = fade / 128.0f;
            y = y * (1 - k) + sample(live, n) * k;
            if (fade < 128) fade++;
          }
          out[c]->data[n] = clip16(y);
          if (++at >= length) { at = 0; length = next_length; }   // a new depth takes the slice from its next repeat
        }
      }
      loop_at = at; loop_length = length; stutter_fade = fade;
      for (uint8_t c = 0; c < 2; c++) {
        if (cur[c]) release(cur[c]);
        cur[c] = out[c];
        own[c] = true;
      }
      if (stutter_fade >= 128) release_slice();
    } else {
      if (out[0]) release(out[0]);
      if (out[1]) release(out[1]);
    }
  }

  if (drums_after) add_drums(cur, own, drums);

  // ---- the pitch shifter's history: what the stutter left, kept (so it is no longer this update's own)
  uint8_t slot = block_number & (HISTORY - 1);
  for (uint8_t c = 0; c < 2; c++) {
    if (history[c][slot]) release(history[c][slot]);
    hold(cur[c]);
    history[c][slot] = cur[c];
    own[c] = false;
  }
  const uint32_t first = block_number * N;   // this block's first sample, counted from the start (wraps after a day)
  block_number++;

  // ---- the pitch: two heads reading the history a little slower (or quicker) than it's written, each
  // falling behind across the window and jumping back while the other one sounds, crossfaded
  if (wet > 0 || wet_aim > 0) {
    audio_block_t *out[2] = {allocate(), allocate()};
    if (out[0] && out[1]) {
      const float step = 1.0f / (44.1f * 10);   // the shifter in or out over 10 ms
      for (uint32_t n = 0; n < N; n++) {
        float r = ratio + (ratio_new - ratio) * (n + 1) * (1.0f / N);
        phase += (1 - r) * (1.0f / WINDOW);
        if (phase >= 1) phase -= 1;
        if (phase < 0) phase += 1;
        if (wet < wet_aim) { wet += step; if (wet > 1) wet = 1; }
        else if (wet > wet_aim) { wet -= step; if (wet < 0) wet = 0; }
        float pb = phase + 0.5f;
        if (pb >= 1) pb -= 1;
        float ga = phase < 0.5f ? 2 * phase : 2 - 2 * phase;
        float da = 1 + phase * WINDOW, db = 1 + pb * WINDOW;
        uint32_t ia = (uint32_t)da, ib = (uint32_t)db;
        float fa = da - ia, fb = db - ib;
        uint32_t sa = first + n - ia, sb = first + n - ib;   // the sample at or after each head, then the one before it
        for (uint8_t c = 0; c < 2; c++) {
          audio_block_t **h = history[c];
          float a0 = sample(h[(sa >> 7) & (HISTORY - 1)], sa & 127), a1 = sample(h[((sa - 1) >> 7) & (HISTORY - 1)], (sa - 1) & 127);
          float b0 = sample(h[(sb >> 7) & (HISTORY - 1)], sb & 127), b1 = sample(h[((sb - 1) >> 7) & (HISTORY - 1)], (sb - 1) & 127);
          float shifted = (a0 + (a1 - a0) * fa) * ga + (b0 + (b1 - b0) * fb) * (1 - ga);
          out[c]->data[n] = clip16(sample(cur[c], n) + (shifted - sample(cur[c], n)) * wet);
        }
      }
      ratio = ratio_new;
      for (uint8_t c = 0; c < 2; c++) {
        if (cur[c]) release(cur[c]);
        cur[c] = out[c];
        own[c] = true;
      }
    } else {
      if (out[0]) release(out[0]);
      if (out[1]) release(out[1]);
    }
  } else {
    ratio = 1;
  }

  // ---- the starve's rails, level and crackle, then the crush: a sample at a time, both sides together
  bool starving = starve_now > 0 || gain != 1 || crackle != 0;
  if (starving || crush_now > 0) {
    for (uint8_t c = 0; c < 2; c++) {
      if (own[c]) continue;
      audio_block_t *b = allocate();
      if (!b) { starving = false; crush_now = 0; break; }   // no room this block: as it is
      for (uint32_t n = 0; n < N; n++) b->data[n] = sample(cur[c], n);
      if (cur[c]) release(cur[c]);
      cur[c] = b;
      own[c] = true;
    }
  }
  if (own[0] && own[1] && (starving || crush_now > 0)) {
    float bits = 0, rate = 0;
    if (crush_mode != 2) bits = crush_now;
    if (crush_mode != 1) rate = crush_now;
    // the bits counted across the sound's own peak, so a quiet preset crushes as a loud one does rather
    // than falling silent between steps: 16 at 0, 10 at a quarter, 7.5 at half, 4 at full
    const float steps = exp2f(16.0f - 12.0f * sqrtf(bits)) * 0.5f;
    const float hold_step = 1.0f / (1 + 23.0f * powf(rate, 1.6f));   // 44.1 kHz down to 1.8
    const float rail_up = rail * 0.8f, rail_down = -rail;      // a starved stage clips one side first
    const uint32_t odds = (uint32_t)(crackle_odds * 4294967295.0f);
    for (uint32_t n = 0; n < N; n++) {
      float y[2] = {(float)cur[0]->data[n], (float)cur[1]->data[n]};
      if (starving) {
        gain += (gain_aim - gain) * 0.02f;   // ~1 ms
        if (gain_aim == 1 && gain > 0.9999f) gain = 1;
        if (odds && random() < odds) crackle = noise() * 2500.0f * (0.3f + starve_now);   // ticks to about -20 dBFS
        for (uint8_t c = 0; c < 2; c++) {
          float x = y[c];
          if (x > rail_up) x = rail_up + (x - rail_up) * 0.1f;
          else if (x < rail_down) x = rail_down + (x - rail_down) * 0.1f;
          y[c] = x * gain + crackle;
        }
        crackle *= 0.7f;
        if (crackle < 1 && crackle > -1) crackle = 0;
      }
      if (crush_now > 0) {
        hold_phase += hold_step;
        float a = fmaxf(fabsf(y[0]), fabsf(y[1]));
        peak = a > peak ? a : peak * 0.99992f;   // falls 3 dB in a quarter of a second
        if (hold_phase >= 1) {
          hold_phase -= 1;
          float q = fmaxf(peak, 512.0f) / steps;
          for (uint8_t c = 0; c < 2; c++) held_sample[c] = clip16(bits > 0 ? floorf(y[c] / q + 0.5f) * q : y[c]);
        }
        y[0] = held_sample[0];
        y[1] = held_sample[1];
      }
      cur[0]->data[n] = clip16(y[0]);
      cur[1]->data[n] = clip16(y[1]);
    }
  }

  uint8_t count = 0;
  for (uint8_t c = 0; c < 2; c++) {
    for (uint8_t k = 0; k < HISTORY; k++) count += history[c][k] != nullptr;
    count += slice_blocks;
    if (cur[c]) { transmit(cur[c], c); release(cur[c]); }
  }
  held = count;
}
