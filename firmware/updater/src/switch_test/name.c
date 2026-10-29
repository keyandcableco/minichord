// The switch-test app's name, never "minichord", so no tool takes it for the firmware
// (as src/name.c in the firmware: the USB product and manufacturer names, which the computer's MIDI port takes)
#include "usb_names.h"

#define MIDI_NAME   {'s','w','i','t','c','h',' ','t','e','s','t'}
#define MIDI_NAME_LEN  11

struct usb_string_descriptor_struct usb_string_product_name = {
        2 + MIDI_NAME_LEN * 2,
        3,
        MIDI_NAME
};

struct usb_string_descriptor_struct usb_string_manufacturer_name = {
        2 + MIDI_NAME_LEN * 2,
        3,
        MIDI_NAME
};
