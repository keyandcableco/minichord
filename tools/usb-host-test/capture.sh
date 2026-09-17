#!/bin/bash
# usage: capture.sh <label> <usb bus number>
# Captures a cold boot of the minichord with usbmon and reports what it sent on
# its MIDI port. Linux only; needs tshark (for dumpcap) and alsa-utils (amidi).
label="$1"; bus="$2"
[ -n "$label" ] && [ -n "$bus" ] || { echo "usage: capture.sh <label> <usb bus number>"; exit 1; }
here="$(cd "$(dirname "$0")" && pwd)"
dir="$here/captures"
mkdir -p "$dir"
out="$dir/$label.pcapng"
log="$dir/$label.dumpcap.log"
midilog="$dir/$label.amidi.txt"
sudo -v || exit 1
rm -f "$out" "$midilog"
echo
echo "Unplug the HEADPHONES so the minichord is OFF. Leave the USB cable in."
read -r -p "Press Enter once it is off... "
# dumpcap writes the capture to its stdout and this shell writes the file, so
# the file is created by you, not root (root is refused in this folder)
sudo dumpcap -i "usbmon$bus" -a duration:24 -w - >"$out" 2>"$log" &
pid=$!
sleep 2
if ! kill -0 "$pid" 2>/dev/null; then
  echo "!!! dumpcap stopped straight away. Its output:"
  cat "$log"
  exit 1
fi
# Linux only reads a USB MIDI device's input once something opens the port, and
# an embedded host box reads it as soon as the device is configured. So open the
# port the moment it appears, the way a box would.
(
  for i in $(seq 1 200); do
    port=$(amidi -l 2>/dev/null | grep -i minichord | awk '{print $2}' | head -1)
    if [ -n "$port" ]; then
      echo "# opened $port after $((i * 50)) ms of polling" >"$midilog"
      exec timeout 21 amidi -p "$port" -d >>"$midilog" 2>&1
    fi
    sleep 0.05
  done
  echo "# the minichord MIDI port never appeared" >"$midilog"
) &
echo ">>> PLUG IN THE HEADPHONES NOW"
for s in 8 7 6 5 4 3 2 1; do echo "    $s"; sleep 1; done
echo ">>> PRESS PRESET UP ONCE NOW"
for s in 5 4 3 2 1; do echo "    $s"; sleep 1; done
echo ">>> STRUM ONE CHORD NOW (proves the MIDI input is being read)"
for s in 5 4 3 2 1; do echo "    $s"; sleep 1; done
echo ">>> hands off, finishing the capture"
wait "$pid"
status=$?
sleep 1
if [ ! -s "$out" ]; then
  echo "!!! no capture file was written (dumpcap exit $status). Its output:"
  cat "$log"
  exit 1
fi
echo
echo "== link speed (12M = full speed, 480M = high speed)"
lsusb -t | grep -i "snd-usb-audio" | head -3
echo
echo "== MIDI port"
head -1 "$midilog"
grep -qi "busy\|error\|cannot" "$midilog" && grep -i "busy\|error\|cannot" "$midilog" | head -2
echo
echo "== what the minichord sent"
python3 "$here/minichord_usb.py" "$out" 2>&1 | tee "$dir/$label.txt"
