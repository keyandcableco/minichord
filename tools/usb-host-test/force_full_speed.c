/* TEST ONLY. Copy into firmware/src/ for a test build, and delete it after.
 *
 * Makes the Teensy connect at USB full speed (12 Mbit/s), the speed most
 * hardware USB MIDI host boxes use, without needing a USB 1.1 hub.
 *
 * usb_init() has already attached at high speed by the time this runs, and a
 * controller reset inside usb_init() would clear the force bit if it were set
 * any earlier. So this detaches, sets the port's force-full-speed bit, and
 * attaches again; the host re-enumerates the device at 12 Mbit/s.
 *
 * The reconnect happens during boot, so the boot-time parameter dump is likely
 * sent before the host has configured the device again. Don't read boot
 * behaviour from a build that includes this file.
 */
#include <Arduino.h>

void startup_late_hook(void)
{
	USB1_USBCMD &= ~USB_USBCMD_RS;            // detach: the host sees a disconnect
	uint32_t until = millis() + 250;
	while (millis() < until) ;
	USB1_PORTSC1 |= USB_PORTSC1_PFSC;         // never chirp for high speed
	USB1_USBCMD |= USB_USBCMD_RS;             // attach again, at full speed
}
