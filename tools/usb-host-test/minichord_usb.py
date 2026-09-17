#!/usr/bin/env python3
"""Read a usbmon capture (pcapng from dumpcap) and report what the minichord
sent on its MIDI IN endpoint: sysex dumps (complete or cut off), notes, and
anything else, in order, with times relative to the device being configured."""
import struct, sys

TEENSY_VID = 0x16C0
CIN_LEN = {0x2: 2, 0x3: 3, 0x4: 3, 0x5: 1, 0x6: 2, 0x7: 3, 0x8: 3, 0x9: 3, 0xA: 3, 0xB: 3, 0xC: 2, 0xD: 2, 0xE: 3, 0xF: 1}

def packets(path):
    data = open(path, "rb").read()
    pos, end, linktypes = 0, "<", []
    while pos + 12 <= len(data):
        btype, blen = struct.unpack_from(end + "II", data, pos)
        if btype == 0x0A0D0D0A:
            magic = struct.unpack_from("<I", data, pos + 8)[0]
            end = "<" if magic == 0x1A2B3C4D else ">"
            blen = struct.unpack_from(end + "I", data, pos + 4)[0]
        elif btype == 1:
            linktypes.append(struct.unpack_from(end + "H", data, pos + 8)[0])
        elif btype == 6:
            iface, tsh, tsl, caplen = struct.unpack_from(end + "IIII", data, pos + 8)
            yield linktypes[iface], data[pos + 28: pos + 28 + caplen]
        if blen < 12:
            break
        pos += blen

def urbs(path):
    for linktype, raw in packets(path):
        hdr = 64 if linktype == 220 else 48
        if len(raw) < hdr:
            continue
        kind = chr(raw[8]); xfer = raw[9]; ep = raw[10]; dev = raw[11]
        ts = struct.unpack_from("<q", raw, 16)[0] + struct.unpack_from("<i", raw, 24)[0] / 1e6
        setup = raw[40:48]
        yield dict(kind=kind, xfer=xfer, ep=ep, dev=dev, ts=ts, setup=setup, data=raw[hdr:])

def main(path):
    all_urbs = list(urbs(path))
    if not all_urbs:
        sys.exit("no USB packets in the capture")
    # which device numbers are the minichord, from its device descriptor
    mini, config_desc, configured_at, pending = set(), {}, {}, {}
    for u in all_urbs:
        if u["xfer"] != 2:
            continue
        if u["kind"] == "S":
            pending[(u["dev"], u["ep"])] = u["setup"]
            s = u["setup"]
            if s[0] == 0x00 and s[1] == 0x09:
                configured_at.setdefault(u["dev"], u["ts"])
        elif u["kind"] == "C" and u["ep"] & 0x80:
            d = u["data"]
            if len(d) >= 18 and d[0] == 18 and d[1] == 1 and struct.unpack_from("<H", d, 8)[0] == TEENSY_VID:
                mini.add(u["dev"])
            if len(d) >= 9 and d[1] == 2 and len(d) > len(config_desc.get(u["dev"], b"")):
                config_desc[u["dev"]] = d
    if not mini:
        sys.exit("no Teensy (16c0) enumerated during the capture; power the minichord on after starting the capture")
    for dev in sorted(d for d in mini if d):
        # the MIDI streaming interface's IN endpoint, from the configuration descriptor
        cd, i, in_midi, midi_eps = config_desc.get(dev, b""), 0, False, set()
        while i + 2 <= len(cd) and cd[i] > 0:
            ln, dt = cd[i], cd[i + 1]
            if dt == 4 and i + 7 <= len(cd):
                in_midi = cd[i + 5] == 1 and cd[i + 6] == 3
            if dt == 5 and in_midi and cd[i + 2] & 0x80 and (cd[i + 3] & 3) == 2:
                midi_eps.add(cd[i + 2] & 0x7F)
            i += ln
        t0 = configured_at.get(dev) or all_urbs[0]["ts"]
        print(f"minichord at device {dev}, MIDI IN endpoint(s) {sorted(midi_eps) or 'unknown'}")
        if dev not in configured_at:
            print("  warning: SET_CONFIGURATION not in the capture, times are from the start of the capture")
        events, sysex, sysex_start_t, transfers = [], {}, {}, 0
        for u in all_urbs:
            if u["dev"] != dev or u["xfer"] != 3 or u["kind"] != "C" or not (u["ep"] & 0x80):
                continue
            if midi_eps and (u["ep"] & 0x7F) not in midi_eps:
                continue
            d = u["data"]
            if not d:
                continue
            transfers += 1
            t = (u["ts"] - t0) * 1000
            for k in range(0, len(d) - len(d) % 4, 4):
                p = d[k:k + 4]
                cable, cin = p[0] >> 4, p[0] & 0xF
                body = p[1:1 + CIN_LEN.get(cin, 3)]
                if cin in (0x4, 0x5, 0x6, 0x7):
                    buf = sysex.get(cable)
                    if buf is None:
                        if body[0] != 0xF0:
                            events.append((t, f"cable {cable}: SYSEX DATA WITH NO F0 START (cut off) {body.hex(' ')}"))
                            buf = bytearray(b"?")
                        else:
                            buf = bytearray()
                        sysex_start_t[cable] = t
                    buf += body
                    sysex[cable] = buf
                    if 0xF7 in body:
                        whole = bytes(buf)
                        state = "complete" if whole[:1] == b"\xf0" else "TAIL ONLY, start was lost"
                        events.append((sysex_start_t[cable], f"cable {cable}: sysex {len(whole)} bytes, {state}, begins {whole[:6].hex(' ')}"))
                        sysex[cable] = None
                elif cin in (0x8, 0x9):
                    kind = "on " if cin == 0x9 and body[2] else "off"
                    events.append((t, f"cable {cable}: note {kind} ch{(body[0] & 0xF) + 1} n{body[1]} v{body[2]}"))
                elif cin == 0 and p == b"\x00\x00\x00\x00":
                    continue
                else:
                    events.append((t, f"cable {cable}: CIN {cin:X} {body.hex(' ')}"))
        for cable, buf in sysex.items():
            if buf:
                events.append((sysex_start_t[cable], f"cable {cable}: sysex {len(buf)} bytes NEVER FINISHED (no F7)"))
        events.sort(key=lambda e: e[0])
        print(f"  {transfers} MIDI IN transfers")
        if not events:
            print("  no MIDI data sent")
        collapsed, last = [], None
        for t, msg in events:
            key = msg.split(" n")[0] if "note" in msg else msg
            if last and last[1] == key:
                last[2] += 1
            else:
                last = [t, key, 1, msg]
                collapsed.append(last)
        for t, key, n, msg in collapsed:
            print(f"  {t:9.1f} ms  {msg}" + (f"   (+{n - 1} more like this)" if n > 1 else ""))
        dumps = [m for _, m in events if "sysex" in m]
        print(f"  summary: {len(dumps)} sysex message(s); " +
              ("cut-off or unfinished: " + str(sum(('cut off' in m) or ('TAIL' in m) or ('NEVER' in m) for m in dumps)) if dumps else "none"))

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: minichord_usb.py capture.pcapng")
    main(sys.argv[1])
