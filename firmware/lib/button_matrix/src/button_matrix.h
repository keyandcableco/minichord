#ifndef BUTTONMATRIX_H
#define BUTTONMATRIX_H

#include "Arduino.h" 
#include <debouncer.h>

class button_matrix{
  public:
  /**
  * @param d_in the data to send to the register
  * @param storage_clock the clock to store the data 
  * @param shift_clock the clock to shift the data to the register
  * @param read_pin_1 the first pin to read
  * @param read_pin_2 the second pin to read
  * @param read_pin_3 the third pin to read
  */
  button_matrix(uint8_t d_in, uint8_t storage_clock, uint8_t shift_clock, uint8_t read_pin_1, uint8_t read_pin_2,uint8_t read_pin_3);

  void setup();
  void write_bit(bool data);
  void write_byte(byte data);
  void update(debouncer (&data_array)[22]);
  // Upside down, for a left-handed player: the 21 chord buttons turned round,
  // so the button at the top left as the player holds it is F major, as it is
  // the right way up (button i becomes 22 - i: the columns and the rows both
  // reverse). The sharp button is where it is either way.
  bool upside_down = false;


  private:
  uint8_t d_in;
  uint8_t storage_clock;
  uint8_t shift_clock;
  uint8_t read_pin_1;
  uint8_t read_pin_2;
  uint8_t read_pin_3;
  int registersBits=8;
};

#endif