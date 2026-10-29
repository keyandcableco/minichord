// The updater's name, so minicontrol and the bench can tell it from the firmware
// (as src/name.c in the firmware: the USB product and manufacturer names, which the computer's MIDI port takes)
#include "usb_names.h"

#define MIDI_NAME   {'m','i','n','i','c','h','o','r','d',' ','u','p','d','a','t','e','r'}
#define MIDI_NAME_LEN  17

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
