# USB host test tools

Tools for looking into [#72](https://github.com/BenjaminPoilve/minichord/issues/72), where the minichord misbehaves on hardware USB MIDI hosts, using nothing more than a Linux computer.

None of this is firmware and none of it is needed to build the minichord.

## What's here

| File | What it does |
| --- | --- |
| `capture.sh` | Captures a cold boot with `usbmon`, opening the MIDI port the moment it appears the way a host box would, and prompts you to press preset up and strum. |
| `minichord_usb.py` | Reads a `usbmon` capture and lists what the minichord sent on its MIDI port: every sysex (complete, cut off, or unfinished) and the notes, timed from when the device was configured. |
| `virtual_box.py` | Acts as a naive host box: takes the MIDI interface from Linux as soon as the device appears, reads it immediately, and records every packet. It then replays the stream through five receiver models and reports phantom and lost notes. |
| `force_full_speed.c` | Test-only: dropped into `firmware/src/`, it makes the Teensy connect at full speed (12M), the speed most host boxes use, without a USB 1.1 hub. |
| `captures/` | The recordings described below, replayable without a minichord. |

The receiver models in `virtual_box.py` test mechanisms, not the firmware of any real product:

- **spec** flattens USB-MIDI to a byte stream and parses it exactly as the MIDI spec says.
- **rs-encoder** is a box that uses running status on its output and doesn't reset it after a sysex, feeding a spec receiver.
- **small-sysex** is a receiver with a 128-byte sysex buffer that keeps the running status from before the sysex and, on overflow, reads the rest of the dump as data for it.
- **waits-for-F7** is a receiver that stays in sysex until it sees `F7`, ignoring status bytes, so an unfinished sysex swallows what follows.
- **merged** is a box that merges both minichord ports into one stream in USB order.

## Running

Linux only.

```bash
sudo apt-get install -y tshark alsa-utils python3-usb
sudo modprobe usbmon
lsusb | grep -i 16c0          # with the minichord on: note the bus number
```

Capture a cold boot. The script tells you when to plug in the headphones, press preset up, and strum.

```bash
./capture.sh stock-v9 3       # label, bus number
```

Run the virtual host box. It needs root for the raw USB device, and writes its log to `/tmp`.

```bash
sudo python3 virtual_box.py --out /tmp/stock-v9.json
sudo python3 virtual_box.py --pause-ms 500 --out /tmp/stock-v9-slow.json   # stop reading for 500 ms at the first sysex
```

Replay a saved recording through the models. This needs no minichord and no root.

```bash
python3 virtual_box.py --replay captures/ben-v9-fullspeed-slowbox.json
```

To test at full speed, copy `force_full_speed.c` into `firmware/src/`, build and upload, then unplug and replug the headphones. `lsusb -t` should show `12M` for the minichord. Delete the file afterwards. Its reconnect happens during boot, so boot-time behaviour isn't meaningful in that build; preset changes are.

## Recordings

All made with `virtual_box.py`'s prompts: power on, strum, preset up, strum again. "Slow box" runs stop reading for 500 ms when the first sysex arrives.

| Recording | Firmware | Speed | Sysex received | Models with problems |
| --- | --- | --- | --- | --- |
| `ben-v9-highspeed-vbox.json` | stock v9 (`b4154aa`) | 480M | 514 bytes at boot, 514 at the preset press, both complete | small-sysex (191 phantom notes) |
| `ben-v9-highspeed-slowbox.json` | stock v9, slow box | 480M | same, both complete; the Teensy held the rest of the boot dump until reads resumed | small-sysex (191 phantom notes) |
| `ben-v9-fullspeed-vbox.json` | stock v9 + `force_full_speed.c` | 12M | 514 bytes at the preset press, complete | small-sysex (191 phantom notes) |
| `ben-v9-fullspeed-slowbox.json` | stock v9 + `force_full_speed.c`, slow box | 12M | **240 of 514 bytes at the preset press, never finished** | small-sysex (55 phantom notes), **waits-for-F7 (all following chord notes lost)** |
| `test-allFeatures-highspeed-vbox.json` | `test-allFeatures` (dump gated) | 480M | none | none |
| `test-allFeatures-fullspeed-vbox.json` | `test-allFeatures` + `force_full_speed.c` | 12M | none | none |
| `test-allFeatures-fullspeed-slowbox.json` | `test-allFeatures` + `force_full_speed.c`, slow box | 12M | none | none |

Why the full-speed slow box loses the tail: the Teensy 4 USB MIDI driver queues at most 4 transfers for the host (`TX_NUM` in `usb_midi.c`) and gives up on a write after 40 ms (`TX_TIMEOUT_MSEC`). At 480M that's 4 × 512 bytes, enough for the whole dump; at 12M it's 4 × 64 bytes, so a host that stops reading for more than 40 ms partway through receives an unfinished sysex.
