#!/usr/bin/env python3
"""A pretend USB MIDI host box for the minichord.

It takes the minichord's MIDI interface away from Linux the moment the device
appears, reads the MIDI IN endpoint straight away the way an embedded host box
does, and records every USB-MIDI packet. Afterwards it replays what it received
through a few models of what a cheap host box and the gear behind it might do
with the byte stream, and reports any note messages that come out which the
minichord never sent (phantoms) or that went missing.

The models are deliberately simple and hypothetical. They are not the firmware
of any real product; they test mechanisms.

  spec          flatten USB-MIDI to bytes, parse exactly per the MIDI spec
  rs-encoder    the box saves bytes with running status on its DIN output and
                forgets to reset it after a sysex; the receiver follows the spec
  small-sysex   the receiver has a 128-byte sysex buffer, keeps the running
                status from before the sysex, and on overflow drops back to
                reading the rest of the dump as data for that running status
  waits-for-F7  the receiver stays in sysex until it sees F7, ignoring status
                bytes, so a sysex that never finishes swallows what follows
  merged        the box merges both minichord ports onto one output in USB
                order, receiver follows the spec

Run as root (it needs the raw USB device):
  sudo python3 virtual_box.py                 read like a well-behaved box
  sudo python3 virtual_box.py --pause-ms 500  stop reading for 500 ms when the
                                              first sysex arrives, like a slow box
  sudo python3 virtual_box.py --replay FILE   re-run the models on a saved log
"""
import argparse, json, sys, time
from collections import Counter

TEENSY_VID = 0x16C0
BOOTLOADER_PID = 0x0478
CIN_LEN = {0x2: 2, 0x3: 3, 0x4: 3, 0x5: 1, 0x6: 2, 0x7: 3, 0x8: 3, 0x9: 3, 0xA: 3, 0xB: 3, 0xC: 2, 0xD: 2, 0xE: 3, 0xF: 1}


# ---------------------------------------------------------------- the models
def usb_packets(transfers):
    """(t_ms, cable, cin, bytes) for every USB-MIDI packet, in order."""
    for t, data in transfers:
        for k in range(0, len(data) - len(data) % 4, 4):
            p = data[k:k + 4]
            if p == [0, 0, 0, 0]:
                continue
            cable, cin = p[0] >> 4, p[0] & 0xF
            yield t, cable, cin, p[1:1 + CIN_LEN.get(cin, 3)]


def truth(transfers, cables):
    """The channel messages the minichord actually sent on these cables."""
    out = []
    for t, cable, cin, body in usb_packets(transfers):
        if cable in cables and 0x8 <= cin <= 0xE:
            out.append((t, tuple(body)))
    return out


def flatten(transfers, cables):
    """USB-MIDI to a MIDI byte stream, the way a USB-to-DIN box forwards it."""
    out = []
    for t, cable, cin, body in usb_packets(transfers):
        if cable in cables:
            out.extend((t, b) for b in body)
    return out


def running_status_encoder(stream, reset_on_sysex):
    """A box output that omits repeated channel status bytes."""
    out, last = [], None
    i = 0
    while i < len(stream):
        t, b = stream[i]
        if 0x80 <= b <= 0xEF:
            if b == last:
                i += 1
                continue
            last = b
        elif 0xF0 <= b <= 0xF7 and reset_on_sysex:
            last = None
        out.append((t, b))
        i += 1
    return out


def parse(stream, sysex_max=None, overflow_resumes_running_status=False, sysex_clears_running_status=True,
          sysex_ends_only_on_f7=False):
    """A MIDI receiver. Returns channel messages as (t, (status, d1[, d2]))."""
    msgs, running, in_sysex, sysex_len, data = [], None, False, 0, []
    for t, b in stream:
        if b >= 0xF8:                      # realtime: never interrupts anything
            continue
        if b >= 0x80:
            if in_sysex and b != 0xF7:
                if sysex_ends_only_on_f7:
                    continue               # this receiver waits for F7 no matter what
                in_sysex = False           # any status byte ends a sysex
            if b == 0xF0:
                in_sysex, sysex_len, data = True, 0, []
                if sysex_clears_running_status:
                    running = None
                continue
            if b == 0xF7:
                in_sysex, data = False, []
                if sysex_clears_running_status:
                    running = None
                continue
            if b >= 0xF1:                  # system common clears running status
                running, data = None, []
                continue
            running, data = b, []
            continue
        if in_sysex:
            sysex_len += 1
            if sysex_max is not None and sysex_len > sysex_max:
                in_sysex = False
                if not overflow_resumes_running_status:
                    running = None
            continue
        if running is None:
            continue                       # stray data byte, ignored
        data.append(b)
        need = 1 if (running & 0xF0) in (0xC0, 0xD0) else 2
        if len(data) == need:
            msgs.append((t, (running, *data)))
            data = []
    return msgs


def compare(name, sent, received):
    s = Counter(m for _, m in sent)
    r = Counter(m for _, m in received)
    phantom = r - s
    lost = s - r
    first_phantom = next((t for t, m in received if phantom.get(m)), None)
    return dict(name=name, sent=sum(s.values()), received=sum(r.values()),
                phantom=sum(phantom.values()), lost=sum(lost.values()),
                phantom_examples=[m for m in phantom.elements()][:8],
                lost_examples=[m for m in lost.elements()][:8],
                first_phantom_ms=first_phantom)


def describe(m):
    st = m[0]
    kind = {0x80: "note off", 0x90: "note on", 0xA0: "poly AT", 0xB0: "CC", 0xC0: "program", 0xD0: "chan AT", 0xE0: "pitch bend"}[st & 0xF0]
    return f"{kind} ch{(st & 0xF) + 1} " + " ".join(str(x) for x in m[1:])


def report(transfers):
    packets = list(usb_packets(transfers))
    sysex_packets = sum(1 for _, _, cin, _ in packets if cin in (4, 5, 6, 7))
    print(f"\n== received {len(transfers)} transfers, {len(packets)} USB-MIDI packets, {sysex_packets} of them sysex")
    chord = flatten(transfers, {0})
    both = flatten(transfers, {0, 1})
    results = [
        compare("spec (chord port)", truth(transfers, {0}), parse(chord)),
        compare("rs-encoder (chord port)", truth(transfers, {0}),
                parse(running_status_encoder(chord, reset_on_sysex=False))),
        compare("small-sysex (chord port)", truth(transfers, {0}),
                parse(chord, sysex_max=128, overflow_resumes_running_status=True,
                      sysex_clears_running_status=False)),
        compare("waits-for-F7 (chord port)", truth(transfers, {0}), parse(chord, sysex_ends_only_on_f7=True)),
        compare("merged (both ports)", truth(transfers, {0, 1}), parse(both)),
    ]
    for r in results:
        verdict = "OK" if not r["phantom"] and not r["lost"] else "PROBLEM"
        print(f"\n-- {r['name']}: {verdict}")
        print(f"   minichord sent {r['sent']} channel messages, receiver got {r['received']}; "
              f"phantom {r['phantom']}, lost {r['lost']}")
        if r["phantom"]:
            print(f"   first phantom at {r['first_phantom_ms']:.0f} ms; examples: " +
                  "; ".join(describe(m) for m in r["phantom_examples"]))
        if r["lost"]:
            print("   lost examples: " + "; ".join(describe(m) for m in r["lost_examples"]))
    return results


# ---------------------------------------------------------------- the USB side
def find_minichord(usb):
    def is_midi(dev):
        if dev.idVendor != TEENSY_VID or dev.idProduct == BOOTLOADER_PID:
            return False
        try:
            for cfg in dev:
                for intf in cfg:
                    if intf.bInterfaceClass == 1 and intf.bInterfaceSubClass == 3:
                        return True
        except Exception:
            return False
        return False
    return usb.core.find(custom_match=is_midi)


def capture(args):
    try:
        import usb.core, usb.util
    except ImportError:
        sys.exit("pyusb is missing: sudo apt-get install -y python3-usb")

    print("Unplug the HEADPHONES so the minichord is OFF. Leave the USB cable in.")
    input("Press Enter once it is off... ")
    if find_minichord(usb):
        sys.exit("the minichord is still on; unplug the headphones and run again")
    print(">>> PLUG IN THE HEADPHONES NOW")
    t_wait = time.monotonic()
    dev = None
    while dev is None:
        dev = find_minichord(usb)
        if dev is None:
            if time.monotonic() - t_wait > 30:
                sys.exit("the minichord did not appear within 30 s")
            time.sleep(0.02)
    t0 = time.monotonic()
    speed = {1: "low", 2: "full (12M)", 3: "high (480M)", 4: "super"}.get(dev.speed, f"unknown ({dev.speed})")

    cfg = None
    for _ in range(100):                       # wait for the kernel to finish configuring it
        try:
            cfg = dev.get_active_configuration()
            break
        except usb.core.USBError:
            time.sleep(0.01)
    if cfg is None:
        dev.set_configuration()
        cfg = dev.get_active_configuration()
    intf = next(i for i in cfg if i.bInterfaceClass == 1 and i.bInterfaceSubClass == 3)
    ep = next(e for e in intf if usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
    n = intf.bInterfaceNumber
    detached = False
    if dev.is_kernel_driver_active(n):
        dev.detach_kernel_driver(n)
        detached = True
    usb.util.claim_interface(dev, n)
    first_read_ms = (time.monotonic() - t0) * 1000
    print(f"    got it: {speed} speed, MIDI interface {n}, endpoint 0x{ep.bEndpointAddress:02x}, "
          f"packet size {ep.wMaxPacketSize}, reading {first_read_ms:.0f} ms after it appeared")

    prompts = [(8, ">>> STRUM ONE CHORD NOW"), (13, ">>> PRESS PRESET UP ONCE NOW"),
               (18, ">>> STRUM ONE CHORD AGAIN NOW"), (23, ">>> hands off")]
    transfers, paused_once, pause_until = [], False, 0.0
    while True:
        now = time.monotonic()
        el = now - t0
        while prompts and el >= prompts[0][0]:
            print(prompts.pop(0)[1])
        if el >= 26:
            break
        if now < pause_until:
            time.sleep(0.005)
            continue
        try:
            data = ep.read(ep.wMaxPacketSize, timeout=20)
        except usb.core.USBTimeoutError:
            continue
        except usb.core.USBError as e:
            if getattr(e, "errno", None) in (110, None) and "timeout" in str(e).lower():
                continue
            print(f"!!! USB error while reading: {e}")
            break
        if len(data):
            t_ms = (time.monotonic() - t0) * 1000
            transfers.append((t_ms, list(data)))
            if args.pause_ms and not paused_once and any(
                    (data[k] & 0xF) in (4, 5, 6, 7) for k in range(0, len(data) - len(data) % 4, 4)):
                paused_once = True
                pause_until = time.monotonic() + args.pause_ms / 1000
                print(f"    (first sysex arrived at {t_ms:.0f} ms; pausing reads for {args.pause_ms} ms)")

    try:
        usb.util.release_interface(dev, n)
        if detached:
            dev.attach_kernel_driver(n)
    except Exception:
        print("    (could not hand the port back to Linux; unplug and replug the headphones to reset it)")

    meta = dict(speed=speed, first_read_ms=first_read_ms, pause_ms=args.pause_ms)
    with open(args.out, "w") as f:
        json.dump(dict(meta=meta, transfers=transfers), f)
    print(f"\nsaved the raw packets to {args.out}")
    return transfers


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pause-ms", type=int, default=0, help="stop reading this long when the first sysex arrives")
    ap.add_argument("--out", default="virtual_box_log.json", help="where to save the received packets")
    ap.add_argument("--replay", help="re-run the models on a saved log instead of capturing")
    args = ap.parse_args()
    if args.replay:
        with open(args.replay) as f:
            saved = json.load(f)
        print(f"replaying {args.replay}: {saved['meta']}")
        transfers = saved["transfers"]
    else:
        transfers = capture(args)
    # the timeline of sysex and first notes, for reading alongside the verdicts
    print("\n== timeline")
    sx, shown = {}, 0
    for t, cable, cin, body in usb_packets(transfers):
        if cin in (4, 5, 6, 7):
            if cable not in sx:
                sx[cable] = [t, 0, body[0] == 0xF0]
            sx[cable][1] += len(body)
            if cin != 4:
                start, count, has_start = sx.pop(cable)
                print(f"  {start:8.0f} ms  cable {cable}: sysex {count} bytes" + ("" if has_start else "  (NO F0 START)"))
        elif 0x8 <= cin <= 0xE and shown < 40:
            print(f"  {t:8.0f} ms  cable {cable}: {describe(tuple(body))}")
            shown += 1
    for cable, (start, count, _) in sx.items():
        print(f"  {start:8.0f} ms  cable {cable}: sysex {count} bytes, NEVER FINISHED")
    report(transfers)


if __name__ == "__main__":
    main()
