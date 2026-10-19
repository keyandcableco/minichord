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
  looped = 0;
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

// a head's jump (0 A, 1 B), at sample t: it would land WINDOW from where it was; it lands instead up to
// SPLICE_REACH either side of that, wherever the stretch it then reads (SPLICE_LENGTH samples back from
// it, left and right together) is most like the other head's. On a held note the two then read the same
// waveform a whole number of periods apart, and every partial comes out at exactly the ratio: a fixed jump
// lands each one on a grid as wide as the jump rate (14 Hz an octave down), up to half of it off the
// note. rho, how alike they were, sets how the crossfade's level is made up.
FLASHMEM void AudioEffectBend::splice(uint32_t t, uint8_t head) {
  float p = head ? phase + 0.5f : phase;   // the jumping head's place in the window, just after its jump
  if (p >= 1) p -= 1;
  float po = head ? phase : phase + 0.5f;  // the other head's
  if (po >= 1) po -= 1;
  uint32_t other = t - (uint32_t)(SPLICE_DELAY + po * WINDOW + head_off[head ^ 1]);
  uint32_t near = t - (uint32_t)(SPLICE_DELAY + p * WINDOW) + SPLICE_REACH;   // the newest start it may take
  static int16_t a[SPLICE_LENGTH], b[SPLICE_LENGTH + 2 * SPLICE_REACH];
  auto mono = [this](uint32_t s) -> int16_t {
    return (sample(history[0][(s >> 7) & (HISTORY - 1)], s & 127) + sample(history[1][(s >> 7) & (HISTORY - 1)], s & 127)) >> 1;
  };
  float ea = 0;
  for (uint32_t j = 0; j < SPLICE_LENGTH; j++) { a[j] = mono(other - j); ea += (float)a[j] * a[j]; }
  for (uint32_t j = 0; j < SPLICE_LENGTH + 2 * SPLICE_REACH; j++) b[j] = mono(near - j);
  // b[k + j] is the stretch for an offset of k - SPLICE_REACH (k = 0 the newest): every other offset,
  // then the best one's neighbours
  auto score = [&](int32_t k, float *eb_out) {
    float ab = 0, eb = 0;
    for (uint32_t j = 0; j < SPLICE_LENGTH; j++) { float y = b[k + j]; ab += a[j] * y; eb += y * y; }
    if (eb_out) *eb_out = eb;
    return ab / sqrtf(ea * eb + 1.0f);
  };
  int32_t best = SPLICE_REACH; float top = -2;
  for (int32_t k = 0; k <= 2 * SPLICE_REACH; k += 2) { float c = score(k, nullptr); if (c > top) { top = c; best = k; } }
  for (int32_t k = best - 1; k <= best + 1; k += 2) {
    if (k < 0 || k > 2 * SPLICE_REACH) continue;
    float c = score(k, nullptr); if (c > top) { top = c; best = k; }
  }
  if (ea < 1.0f) { head_off[head] = 0; rho = 0; return; }   // silence: where it would have landed
  head_off[head] = (float)(best - SPLICE_REACH);
  // how alike, over a longer stretch: the best of many short matches flatters, and the level made up
  // from it would come out short
  uint32_t mine = near - best;
  float ab = 0, e0 = 0, e1 = 0;
  for (uint32_t j = 0; j < 4 * SPLICE_LENGTH; j++) {
    float x = mono(other - j), y = mono(mine - j);
    ab += x * y; e0 += x * x; e1 += y * y;
  }
  float c = ab / sqrtf(e0 * e1 + 1.0f);
  rho = c < 0 ? 0 : c;
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
  if (wet == 0 && shifting) { phase = 0.5f; ratio = 1; head_off[0] = head_off[1] = 0; rho = 0; }   // in at the window's middle: the live sound 25 ms late

  // the crush
  float cv = crush_target;
  crush_now += (cv - crush_now) * 0.3f;
  if (cv == 0 && crush_now < 1e-4f) crush_now = 0;

  // the bends on one another: the starve's supply takes bits, the drop's sag the sample rate
  float k = interact_amount;
  float bits_bent = k * (1 - supply) * 3;   // all of them gone once the supply is a third down
  bits_bent = bits_bent < 0.001f ? 0 : bits_bent > 1 ? 1 : bits_bent;
  float rate_bent = k * (drop_now < 12 ? drop_now / 12 : 1) * 0.6f;
  if (rate_bent < 0.001f) rate_bent = 0;
  bool crushing = crush_now > 0 || bits_bent > 0 || rate_bent > 0;

  rest = !stuttering && !looping && wet == 0 && !shifting && starve_now == 0 && gain == 1 && crackle == 0 && !crushing;

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
      // held long enough: back to the live sound as on a release, and, still held, a new slice from there
      looped += N;
      uint32_t refresh = stutter_refresh;
      if (stuttering && refresh && looped >= refresh && !stutter_fade) stutter_fade = 1;
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
  // falling behind across the window and jumping back while the other one sounds (to where the other
  // reads: splice), crossfaded
  if (wet > 0 || wet_aim > 0) {
    audio_block_t *out[2] = {allocate(), allocate()};
    if (out[0] && out[1]) {
      const float step = 1.0f / (44.1f * 10);   // the shifter in or out over 10 ms
      for (uint32_t n = 0; n < N; n++) {
        float r = ratio + (ratio_new - ratio) * (n + 1) * (1.0f / N);
        float was = phase;
        phase += (1 - r) * (1.0f / WINDOW);
        // a head jumps while it's silent: A as the phase wraps, B as it crosses the middle; each lands
        // where it reads what the other is reading, so the two stay in step through their crossfade
        if (phase >= 1) { phase -= 1; splice(first + n, 0); }
        else if (phase < 0) { phase += 1; splice(first + n, 0); }
        else if ((was < 0.5f) != (phase < 0.5f)) splice(first + n, 1);
        if (wet < wet_aim) { wet += step; if (wet > 1) wet = 1; }
        else if (wet > wet_aim) { wet -= step; if (wet < 0) wet = 0; }
        float pb = phase + 0.5f;
        if (pb >= 1) pb -= 1;
        float ga = phase < 0.5f ? 2 * phase : 2 - 2 * phase;
        // the crossfade's level held: heads in step (rho 1) add as one; unrelated, as their powers
        float gb = 1 - ga;
        float level = 1.0f / sqrtf(ga * ga + gb * gb + 2 * ga * gb * rho);
        float da = SPLICE_DELAY + phase * WINDOW + head_off[0], db = SPLICE_DELAY + pb * WINDOW + head_off[1];
        uint32_t ia = (uint32_t)da, ib = (uint32_t)db;
        float fa = da - ia, fb = db - ib;
        uint32_t sa = first + n - ia, sb = first + n - ib;   // the sample at or after each head, then the one before it
        for (uint8_t c = 0; c < 2; c++) {
          audio_block_t **h = history[c];
          float a0 = sample(h[(sa >> 7) & (HISTORY - 1)], sa & 127), a1 = sample(h[((sa - 1) >> 7) & (HISTORY - 1)], (sa - 1) & 127);
          float b0 = sample(h[(sb >> 7) & (HISTORY - 1)], sb & 127), b1 = sample(h[((sb - 1) >> 7) & (HISTORY - 1)], (sb - 1) & 127);
          float shifted = ((a0 + (a1 - a0) * fa) * ga + (b0 + (b1 - b0) * fb) * gb) * level;
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
  if (starving || crushing) {
    for (uint8_t c = 0; c < 2; c++) {
      if (own[c]) continue;
      audio_block_t *b = allocate();
      if (!b) { starving = crushing = false; break; }   // no room this block: as it is
      for (uint32_t n = 0; n < N; n++) b->data[n] = sample(cur[c], n);
      if (cur[c]) release(cur[c]);
      cur[c] = b;
      own[c] = true;
    }
  }
  if (own[0] && own[1] && (starving || crushing)) {
    float bits = 0, rate = 0;
    if (crush_mode != 2) bits = crush_now;
    if (crush_mode != 1) rate = crush_now;
    bits = bits > bits_bent ? bits : bits_bent;
    rate = rate > rate_bent ? rate : rate_bent;
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
      if (crushing) {
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

  uint16_t count = 0;
  for (uint8_t c = 0; c < 2; c++) {
    for (uint8_t k = 0; k < HISTORY; k++) count += history[c][k] != nullptr;
    count += slice_blocks;
    if (cur[c]) { transmit(cur[c], c); release(cur[c]); }
  }
  held = count;
}
