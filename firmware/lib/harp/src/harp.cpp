#include "harp.h"


harp::harp(){}  


#if CAP_CHIP==1
  void harp::setup(){
    touch_sensor.begin();
    if (!touch_sensor.communicating()){
      Serial.println("Harp not communicating");
      return;
    }
  }

  void harp::recalibrate(){
      Serial.println("reseting...");
    touch_sensor.reset();
    delay(1000);
    Serial.println("triggerCalibration");
    touch_sensor.triggerCalibration();
    delay(50);
    while (touch_sensor.calibrating())
    {
      Serial.println("calibrating...");
      delay(50);
    }
    Serial.println("finished calibrating"); 
    touch_sensor.setMeasurementIntervalCount(1);
    touch_sensor.setDetectionIntegrator(2); 
    touch_sensor.setTowardsDriftCompensationDuration(26);
    touch_sensor.setAwayDriftCompensationDuration(8);
    touch_sensor.setRecalibrationDelay(0);
    AT42QT2120::KeyPulseScale key_pulse_scale;
    uint8_t pulse = 0x0;
    uint8_t scale = 0x0;
    key_pulse_scale.pulse = pulse;
    key_pulse_scale.scale = scale;
    for (uint8_t key=0; key < touch_sensor.KEY_COUNT; ++key){
      touch_sensor.setKeyDetectThreshold(key,threshold);
      touch_sensor.setKeyPulseScale(key,key_pulse_scale);
    }
  }


  // This chip's readings aren't used for positions: a touched key reads as
  // full strength and the rest as none.
  void harp::read_strength(int16_t (&strength)[12]){
      AT42QT2120::Status status = touch_sensor.getStatus();
      for (uint8_t key=0; key < 12; ++key){
          strength[remap_array[key]] = touch_sensor.touched(status,key) ? 100 : 0;
      }
  }

  void harp::set_thresholds(uint8_t touch, uint8_t release){}
  void harp::set_strength_scale(uint16_t percent){}
  void harp::apply_thresholds(){}
  uint16_t harp::read_proximity(){ return 0; }
  void harp::read_rest(uint16_t (&rest)[12], bool &touched){ for (uint8_t k = 0; k < 12; k++) rest[k] = 0; touched = true; }
  void harp::read_levels(uint16_t (&filtered)[12], uint16_t (&baseline)[12]){ for (uint8_t k = 0; k < 12; k++) filtered[k] = baseline[k] = 0; }

  void harp::update(debouncer (&data_array)[12]){
      AT42QT2120::Status status = touch_sensor.getStatus();
      uint8_t key_count= touch_sensor.KEY_COUNT;
      for (uint8_t key=0; key < key_count; ++key){
          data_array[remap_array[key]].set(touch_sensor.touched(status,key));
      }
  }
#else
  void harp::setup(){
    touch_sensor.setupSingleDevice(Wire,MPR121::ADDRESS_5A,true);
    touch_sensor.startAllChannels(MPR121::COMBINE_CHANNELS_0_TO_11);
    if (!touch_sensor.communicating(MPR121::ADDRESS_5A)){
      Serial.println("Harp not communicating");
      return;
    }
  }

  void harp::recalibrate(){
    Serial.println("Recalibrating Harp");
    // stopped once, and given time to finish its scan, so every setting below lands while it is stopped
    // (the chip takes writes only then; each library write stops and starts it again straight away)
    touch_sensor.stopAllChannels(MPR121::ADDRESS_5A);
    delay(2);
    touch_sensor.setAllChannelsThresholds(touch_threshold,
    release_threshold);
    touch_sensor.setDebounce(MPR121::ADDRESS_5A,
    touch_debounce,
    release_debounce);
    touch_sensor.setBaselineTracking(MPR121::ADDRESS_5A,
    baseline_tracking);
    touch_sensor.setChargeDischargeCurrent(MPR121::ADDRESS_5A,
    charge_discharge_current);
    touch_sensor.setChargeDischargeTime(MPR121::ADDRESS_5A,
    charge_discharge_time);
    touch_sensor.setFirstFilterIterations(MPR121::ADDRESS_5A,
    first_filter_iterations);
    touch_sensor.setSecondFilterIterations(MPR121::ADDRESS_5A,
    second_filter_iterations);
    touch_sensor.setSamplePeriod(MPR121::ADDRESS_5A,
    sample_period);
    touch_sensor.startAllChannels(MPR121::COMBINE_CHANNELS_0_TO_11);
    set_proximity_charge();
  }

  // Hover's charge. The proximity channel (the chip's thirteenth: all twelve zones charged together as one
  // antenna) reads 1024 x V / VDD, where V = I x T / C is what charging the antenna's capacitance C at a
  // current I for a time T builds (datasheet eqn 4). That is a measurement only between 0.7 V and
  // VDD - 0.7 V, 217 to 807 at 3.3 V (AN3889): past the top the charge source runs out of headroom and the
  // antenna clips to the supply, about 1016 whatever it holds, so a hand hardly moves it. A hand's pull
  // grows with the reading, so the fit brings the idle reading near the top of the range, inside it.
  //
  // The fit before this one doubled the time, scaled the current from the step before and kept whatever
  // it got, unchecked: on 2026-10-14 twin rows booted at "4.0 us at 39 uA, reads 1016", clipped, and hover
  // was dead on it. Every plate measured, twin rows' 2 us step included, is about 68 pF (the minichord's
  // own wiring, not the pads), where 4 us at 39 uA reads about 705: the chip never took the last write
  // (the library ignores the I2C status and reads nothing back), as the stock strip once did too. So now
  // every write is read back from the chip and made again until it holds, the reading is measured after
  // every write, and the fit ends only on a setting it has measured inside the range.
  //
  // The charge I x T runs, in units of 1 uA x 0.5 us, from 1 to 63 x 64 (CDC 1-63 uA, CDT 0.5-32 us in
  // doublings). The fit starts at 1 us and 63 uA (about 290 on any plate the minichord has; past the top
  // only under 25 pF, where its own wiring is 64) and steps by target / reading. It never scales from a
  // reading past the top (it halves instead); a reading the step before didn't predict, or a landing
  // nothing predicted, is measured again. About 60 ms on any plate, under 400 ms at worst. Worked out and
  // tested against a simulated chip (lost writes, noise, clipping, a hand moving) before it was built.
  struct ProxCharge { uint8_t cdt, cdc; };       // CDT 1-7 (0.5-32 us), CDC 1-63 uA
  static const uint32_t prox_most_charge = 63u << 6;   // 63 uA for 32 us, in 1 uA x 0.5 us

  FLASHMEM static uint32_t prox_charge(ProxCharge c){ return (uint32_t)c.cdc << (c.cdt - 1); }

  // The nearest charge the chip can set: the shortest time that needs no more than 63 uA, so the current,
  // in whole microamps, has the most steps (from 1 us on at least 32 of them: within 1.6%).
  FLASHMEM static ProxCharge prox_settings(uint32_t charge){
    charge = constrain(charge, 1u, prox_most_charge);
    uint8_t cdt = 1;
    while (cdt < 7 && charge > (63u << (cdt - 1))) cdt++;
    uint32_t unit = 1u << (cdt - 1);
    return { cdt, (uint8_t)constrain((charge + unit / 2) / unit, 1u, 63u) };
  }

  // One of the chip's registers, read straight over the bus (the library's reads are its own): -1 if
  // the chip didn't answer.
  FLASHMEM static int prox_register(uint8_t reg){
    Wire.beginTransmission(MPR121::ADDRESS_5A);
    Wire.write(reg);
    if (Wire.endTransmission(false)) return -1;
    if (Wire.requestFrom((int)MPR121::ADDRESS_5A, 1) != 1) return -1;
    return Wire.read();
  }

  // The chip takes register writes only while stopped (datasheet 5.1), and each library write stops it and
  // starts it straight away, so a write can land while it is still finishing a scan. So it is stopped
  // first and given time to finish (a scan is under half a millisecond even at 32 us), both writes land in
  // the one stop, they are read back (CDCPROX 0x6B, CDTPROX 0x72), and written again, up to three times,
  // until the chip holds them. Gives how many times it had to write again.
  FLASHMEM static uint8_t prox_write(MPR121 &chip, uint8_t channel, ProxCharge c){
    uint8_t rewrites = 0;
    for (uint8_t tries = 0; tries < 3; tries++){
      chip.stopAllChannels(MPR121::ADDRESS_5A);
      delay(2);
      chip.setDeviceChannelChargeDischargeCurrent(MPR121::ADDRESS_5A, channel, c.cdc);
      chip.setDeviceChannelChargeDischargeTime(MPR121::ADDRESS_5A, channel, (MPR121::ChargeDischargeTime)c.cdt);
      int cdc = prox_register(0x6B), cdt = prox_register(0x72);
      bool held = cdc >= 0 && cdt >= 0 && (cdc & 0x3F) == c.cdc && (cdt & 0x07) == c.cdt;
      chip.startAllChannels(MPR121::ADDRESS_5A, MPR121::COMBINE_CHANNELS_0_TO_11);
      if (held) break;
      rewrites++;
    }
    return rewrites;
  }

  // The reading after a restart: the chip refreshes it every SFI x ESI (4 x 1 ms), so this waits out the
  // first refreshes and averages four more; if they spread (something moving near the plate, or a reading
  // not yet settled), four more again.
  FLASHMEM static uint16_t prox_measure(MPR121 &chip, uint8_t channel){
    delay(12);
    uint32_t sum = 0;
    for (uint8_t tries = 0; tries < 2; tries++){
      uint16_t lo = 0xFFFF, hi = 0;
      sum = 0;
      for (uint8_t k = 0; k < 4; k++){
        if (k || tries) delay(5);
        uint16_t r = chip.getDeviceChannelFilteredData(MPR121::ADDRESS_5A, channel);
        sum += r;
        if (r < lo) lo = r;
        if (r > hi) hi = r;
      }
      if (hi - lo <= 16) break;
    }
    return (sum + 2) / 4;
  }

  FLASHMEM void harp::set_proximity_charge(){
    const int target = 700;            // 0.87 of the top: as much pull as 720 within 3%, and room to drift
    const int tolerance = 15;          // lands 685-715
    const int top = 780;               // the range ends at 807: nothing past 780 is scaled from or kept
    const int bottom = 217;            // 0.7 V: below it the reading is no measurement either
    const int too_few = 40;            // too coarse to scale by: eight times the charge instead
    const uint8_t most_steps = 10;
    const uint16_t budget_ms = 180;    // no new step after this: with the last one and the way back, < 400 ms
    if (!touch_sensor.communicating(MPR121::ADDRESS_5A)) return;
    elapsedMillis took;
    ProxCharge now = prox_settings(126);   // 63 uA for 1 us
    ProxCharge best = {0, 0};
    int reading = 0, best_reading = 0;
    uint32_t expect = 0;               // what this setting should read, by the step before; 0 if nothing says
    bool again = false;                // this pass wrote the same setting again
    uint8_t steps = 0, rewrites = 0;
    for (;;){
      rewrites += prox_write(touch_sensor, proximity_channel, now);
      reading = prox_measure(touch_sensor, proximity_channel);
      steps++;
      bool over = reading > top;
      bool agrees = expect && (uint32_t)reading * 8 <= expect * 9 && (uint32_t)reading * 9 >= expect * 8;   // within 12%
      if (!over && (!best.cdt || abs(reading - target) < abs(best_reading - target))){ best = now; best_reading = reading; }
      bool landed = !over && abs(reading - target) <= tolerance;
      if (landed && agrees) break;
      if (steps >= most_steps || took >= budget_ms) break;
      uint32_t charge = prox_charge(now), next;
      if (over) next = charge / 2;                       // past the top says only "less"
      else if (reading < too_few) next = charge * 8;
      else next = charge * target / reading;             // linear: lands on the target
      ProxCharge to = prox_settings(next);
      bool stuck = to.cdt == now.cdt && to.cdc == now.cdc;   // as close as the chip's steps go
      // The same setting again, to see whether the chip took it: a reading the step before didn't predict,
      // or one to end on that nothing predicted. A second surprise is believed.
      if (!again && ((expect && !agrees) || (!expect && (landed || stuck)))){
        again = true;
        if (!expect) expect = reading;
        continue;
      }
      if (stuck) break;
      again = false;
      expect = (over || reading < too_few) ? 0 : (uint32_t)reading * prox_charge(to) / charge;
      now = to;
    }
    // End on a setting measured inside the range: if this one is past the top, or worse than the best, back
    // to the best (or the least charge there is), checked against what it read before; past the top now,
    // something changed, so half of it.
    bool elsewhere = best.cdt && (best.cdt != now.cdt || best.cdc != now.cdc);
    if (reading > top || (elsewhere && abs(reading - target) > abs(best_reading - target))){
      ProxCharge want = best.cdt ? best : prox_settings(1);
      int want_reading = best.cdt ? best_reading : 0;
      for (uint8_t tries = 0; tries < 3; tries++){
        now = want;
        rewrites += prox_write(touch_sensor, proximity_channel, now);
        reading = prox_measure(touch_sensor, proximity_channel);
        steps++;
        if (reading <= top && (!want_reading || (reading * 8 <= want_reading * 9 && reading * 9 >= want_reading * 8))) break;
        if (reading > top){ want = prox_settings(prox_charge(want) / 2); want_reading /= 2; }
      }
    }
    proximity_fits++;
    // what the chip holds, read back, not what was asked for
    int cdc = prox_register(0x6B) & 0x3F, cdt = prox_register(0x72) & 0x07;
    float us = cdt > 0 ? (1 << (cdt - 1)) * 0.5f : 0;
    Serial.printf("Hover charge: %.1f us at %d uA, reads %d (target %d), about %.0f pF, %d steps, %d writes again, in %d ms%s\n",
      us, cdc, reading, target, reading ? cdc * us * 1024 / (3.3f * reading) : 0.0f, steps, rewrites, (int)took,
      reading > top ? (prox_charge(now) == 1 ? ", past the top even at the least charge: hover can't work" : ", PAST THE TOP")
      : reading < bottom ? (prox_charge(now) == prox_most_charge ? ", below the range even at the most charge: hover will be weak" : ", below the range") : "");
  }

  uint16_t harp::read_proximity(){
    return touch_sensor.getDeviceChannelFilteredData(MPR121::ADDRESS_5A, proximity_channel);
  }

  void harp::read_strength(int16_t (&strength)[12]){
    uint16_t filtered[12];
    uint16_t baseline[12];
    touch_sensor.getDeviceAllChannelsData(MPR121::ADDRESS_5A, filtered, baseline);
    for (uint8_t key=0; key < 12; key++){
      int16_t delta = (int16_t)baseline[key] - (int16_t)filtered[key];
      strength[remap_array[key]] = delta > 0 ? (int32_t)delta * 100 / strength_scale : 0;
    }
  }

  FLASHMEM void harp::read_rest(uint16_t (&rest)[12], bool &touched){
    uint16_t filtered[12];
    uint16_t baseline[12];
    touch_sensor.getDeviceAllChannelsData(MPR121::ADDRESS_5A, filtered, baseline);
    touched = false;
    for (uint8_t key=0; key < 12; key++){
      uint8_t zone = remap_array[key];
      if (upside_down) zone = 11 - zone;
      rest[zone] = filtered[key];
      touched |= (int16_t)baseline[key] - (int16_t)filtered[key] >= (int16_t)release_threshold;
    }
  }

  FLASHMEM void harp::read_levels(uint16_t (&filtered)[12], uint16_t (&baseline)[12]){
    uint16_t f[12], b[12];
    touch_sensor.getDeviceAllChannelsData(MPR121::ADDRESS_5A, f, b);
    for (uint8_t key=0; key < 12; key++){
      uint8_t zone = remap_array[key];
      if (upside_down) zone = 11 - zone;
      filtered[zone] = f[key];
      baseline[zone] = b[key];
    }
  }

  // Measured 2026-10-14: twin rows' pads, three times a stock pad, read 2-3 times as strongly (a normal tap
  // about 400 against 150) and a hard press near the chip's floor, so touch velocity and pressure were full
  // from a normal tap on; at 240 they spread as on the stock strip. The plate's own scale times a player's
  // trim (main.cpp, apply_harp_touch_scale), so up to 960. 0 is taken as 100.
  FLASHMEM void harp::set_strength_scale(uint16_t percent){
    strength_scale = percent ? constrain(percent, 25, 1000) : 100;
  }

  void harp::set_thresholds(uint8_t touch, uint8_t release){
    uint8_t new_touch = touch ? touch : stock_touch_threshold;
    uint8_t new_release = release ? release : stock_release_threshold;
    // a pad has to let go below where it touches, or it never would
    if (new_release >= new_touch) new_release = new_touch > 1 ? new_touch - 1 : 1;
    if (new_touch == touch_threshold && new_release == release_threshold) return;
    touch_threshold = new_touch;
    release_threshold = new_release;
    thresholds_changed = true;
  }

  // Pausing the chip to write them restarts its baselines from the present
  // readings, so this waits for the harp to be clear of fingers (see main).
  void harp::apply_thresholds(){
    thresholds_changed = false;
    touch_sensor.setAllChannelsThresholds(touch_threshold, release_threshold);
  }

  void harp::update(debouncer (&data_array)[12]){
      uint16_t touch_status = touch_sensor.getTouchStatus(MPR121::ADDRESS_5A);
      if (touch_sensor.overCurrentDetected(touch_status)){
        // It used to start the channels again without clearing the flag (and without hover's proximity
        // channel), and the chip, stopped until the flag is cleared, measured nothing: a silent harp, this
        // line printed every pass. Now the flag is cleared and the caller recalibrates.
        touch_sensor.clearOverCurrent(MPR121::ADDRESS_5A);
        if (!over_current) Serial.println("Harp: over current flagged, cleared");
        over_current = true;
        over_current_count++;
        return;
      }
      for (uint8_t key=0; key < 12; key++){
          data_array[remap_array[key]].set(touch_sensor.deviceChannelTouched(touch_status,key));
      }
  }
#endif

// Both chips keep their order in remap_array, so turning it over is turning
// that over: every reading lands on the zone at the other end.
void harp::set_upside_down(bool flipped){
  if (flipped == upside_down) return;
  upside_down = flipped;
  for (uint8_t key=0; key < 12; key++) remap_array[key] = 11 - remap_array[key];
}
