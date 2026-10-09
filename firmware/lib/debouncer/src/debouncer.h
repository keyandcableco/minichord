#ifndef DEBOUNCER_H
#define DEBOUNCER_H

#include "Arduino.h" 

class debouncer{
  public:
  debouncer();
  void set(bool set_value);
  uint8_t read_transition();
  bool read_value();
  bool read_raw();   // the latest reading, before it has held: for a caller that must not wait
  void set_debounce(uint16_t microseconds);   // how long a reading must hold, 10 ms unless set
  // A press taken sooner than a release: after `microseconds` closed, except within `relock`
  // microseconds of a release, where it waits the full debounce. Off unless set.
  void set_press(uint16_t microseconds, uint32_t relock);
  private:
  u_int16_t debounce_value=10000;
  u_int16_t press_value=10000;    // how long a press must hold, outside the relock
  uint32_t relock_value=0;        // after a release, presses wait the full debounce this long
  elapsedMicros since_release=0;
  bool flag=false; //flag warns that there has been a change in value that was not yet accounted for 
  elapsedMicros last_update=0;
  bool value=false;
  bool pending=false; //the raw reading, which becomes value once it has held
};

#endif
