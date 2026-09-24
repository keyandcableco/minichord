#ifndef HARP_H
#define HARP_H
#include "Arduino.h" 
//1=ATQT2120, 0=MPR121
#define CAP_CHIP 0

#if CAP_CHIP==1
  #include <AT42QT2120.h>
#else
  #include <MPR121.h>
#endif
#include <debouncer.h>

class harp{
  public:
  harp();

  void setup();
  void recalibrate();
  void update(debouncer (&data_array)[12]);

  // For the tripwire build (include/diagnostics_checks.h). Reading the sensor's
  // own view of a pad is how a release from a lifted finger is told apart from
  // one caused by the baseline creeping toward a finger still on the pad.
  bool diag_delta(uint8_t string, int16_t &delta);   // baseline minus filtered, per string
  bool diag_raw(uint8_t string, uint16_t &filtered, uint16_t &baseline);
  bool diag_communicating();
  uint8_t diag_touch_threshold();
  uint8_t diag_release_threshold();
  // A/B switches for the stress test. Both stop and restart the electrodes,
  // which reloads every baseline from the current reading, so they are meant
  // to be used with no finger on the harp.
  void diag_set_touched_filter(bool frozen);
  bool diag_touched_filter_frozen = false;
  bool diag_read_touched_filter(uint8_t &nhd, uint8_t &ncl, uint8_t &fdl);
  void diag_set_release_threshold(uint8_t release);
  volatile uint32_t diag_overcurrent_count = 0;

  private:
  #if CAP_CHIP==1
    AT42QT2120 touch_sensor;
    int remap_array[12]={3,4,5,6,7,8,9,10,11,2,1,0};
    int calibrated_val_array[12]={0,0,0,0,0,0,0,0,0,0,0,0};
    int threshold=12;
    float hysteresis=1.6;
  #else
    MPR121 touch_sensor;
    int remap_array[12]={11,9,7,6,8,10,0,2,4,5,3,1};
    //int remap_array[12]={1,3,5,4,2,0,10,8,7,11,9,6};
    const uint8_t touch_threshold = 40;
    const uint8_t release_threshold = 20;
    uint8_t diag_live_release = release_threshold;   // what the stress test last set
    const uint8_t touch_debounce = 1;
    const uint8_t release_debounce = 1;
    const MPR121::BaselineTracking baseline_tracking = MPR121::BASELINE_TRACKING_INIT_10BIT;
    const uint8_t charge_discharge_current = 63;
    const MPR121::ChargeDischargeTime charge_discharge_time = MPR121::CHARGE_DISCHARGE_TIME_HALF_US;
    const MPR121::FirstFilterIterations first_filter_iterations = MPR121::FIRST_FILTER_ITERATIONS_6;
    const MPR121::SecondFilterIterations second_filter_iterations = MPR121::SECOND_FILTER_ITERATIONS_4;
    const MPR121::SamplePeriod sample_period = MPR121::SAMPLE_PERIOD_1MS;
  #endif


};

#endif