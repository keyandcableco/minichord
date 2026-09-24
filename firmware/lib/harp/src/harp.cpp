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


  void harp::update(debouncer (&data_array)[12]){
      AT42QT2120::Status status = touch_sensor.getStatus();
      uint8_t key_count= touch_sensor.KEY_COUNT;
      for (uint8_t key=0; key < key_count; ++key){
          data_array[remap_array[key]].set(touch_sensor.touched(status,key));
      }
  }
  bool harp::diag_delta(uint8_t string, int16_t &delta){ (void)string; (void)delta; return false; }
  bool harp::diag_communicating(){ return touch_sensor.communicating(); }
  uint8_t harp::diag_touch_threshold(){ return threshold; }
  uint8_t harp::diag_release_threshold(){ return 0; }
#else
  void harp::setup(){
    touch_sensor.setupSingleDevice(Wire,MPR121::ADDRESS_5A,true);
    touch_sensor.startAllChannels();
    if (!touch_sensor.communicating(MPR121::ADDRESS_5A)){
      Serial.println("Harp not communicating");
      return;
    }
  }

  void harp::recalibrate(){
    Serial.println("Recalibrating Harp");
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
    touch_sensor.startAllChannels();

  }


  void harp::update(debouncer (&data_array)[12]){
      uint16_t touch_status = touch_sensor.getTouchStatus(MPR121::ADDRESS_5A);
      if (touch_sensor.overCurrentDetected(touch_status)){
        Serial.println("Over current detected!\n\n");
        diag_overcurrent_count++;
        touch_sensor.startAllChannels();
        return;
      }
      for (uint8_t key=0; key < 12; key++){
          data_array[remap_array[key]].set(touch_sensor.deviceChannelTouched(touch_status,key));
      }
  }

  bool harp::diag_delta(uint8_t string, int16_t &delta){
    for (uint8_t key=0; key < 12; key++){
      if (remap_array[key] == string){
        uint16_t filtered = touch_sensor.getDeviceChannelFilteredData(MPR121::ADDRESS_5A, key);
        uint16_t baseline = touch_sensor.getDeviceChannelBaselineData(MPR121::ADDRESS_5A, key);
        delta = (int16_t)baseline - (int16_t)filtered;
        return true;
      }
    }
    return false;
  }
  bool harp::diag_communicating(){ return touch_sensor.communicating(MPR121::ADDRESS_5A); }
  uint8_t harp::diag_touch_threshold(){ return touch_threshold; }
  uint8_t harp::diag_release_threshold(){ return release_threshold; }
#endif
