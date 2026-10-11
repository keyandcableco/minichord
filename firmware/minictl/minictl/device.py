"""Talking to a minichord over USB: its two MIDI ports (SysEx settings and notes), its serial port, its
USB audio and its flashing, on Linux, macOS and Windows. The protocol is minicontrol's:

    F0 <addr lo> <addr hi> <value lo> <value hi> F7    set a live setting (7 bits each)
    F0 00 00 <command> <parameter> F7                  a control command (address 0)

Commands: 0 report page 0 (512 bytes, no header), 7 report page <parameter> (7D 6D <page> first),
1 wipe memory, 2 save to bank, 3 reset a bank to its factory preset, 4 load a bank (it reports itself),
5 push the live settings, 6 pop them back, 8 rhythm mode (0 off, 1 on, 2 the other way)."""
import glob, os, re, subprocess, sys, threading, time

PAGE = 256
PAGE1_VERSION = 31         # firmware 31 grew the settings to two pages
RESERVED = {382, 383, 510, 511}
VERSION_ADDRESS = 7
PJRC_VID = 0x16C0


class NotFound(RuntimeError):
    pass


def _rtmidi():
    try:
        import rtmidi
        return rtmidi
    except ImportError:
        raise RuntimeError("minictl needs python-rtmidi: pip install python-rtmidi")


SECOND = re.compile(r"(MIDI|Port)\s*2\b|MIDI(IN|OUT)2", re.I)
FIRST = re.compile(r"(MIDI|Port)\s*1\b", re.I)


def pick_port(names, cable=1, want=None):
    """The index of the minichord's port for cable 1 (chords, and every SysEx) or cable 2 (the harp).
    ALSA calls them "minichord MIDI 1/2", macOS "minichord Port 1/2", Windows "minichord" and
    "MIDIIN2 (minichord)". `want` (or MINICTL_PORT) picks by a piece of the name instead."""
    want = want or os.environ.get("MINICTL_PORT")
    if want and cable == 1:
        hits = [i for i, n in enumerate(names) if want.lower() in n.lower()]
        hits = [i for i in hits if not SECOND.search(names[i])] or hits
        return hits[0] if hits else None
    if want:      # the second port of a chosen device: its name with 2 where the first has 1
        first = next((n for n in names if want.lower() in n.lower()), "")
        stem = FIRST.split(first)[0].strip() if FIRST.search(first) else None
        hits = [i for i, n in enumerate(names) if stem and n.startswith(stem) and SECOND.search(n)]
        return hits[0] if hits else None
    ours = [(i, n) for i, n in enumerate(names) if "minichord" in n.lower()]
    if cable == 1:
        hits = [i for i, n in ours if FIRST.search(n)] or [i for i, n in ours if not SECOND.search(n)]
    else:
        hits = [i for i, n in ours if SECOND.search(n)]
    return hits[0] if hits else None


def list_ports():
    rtmidi = _rtmidi()
    o, i = rtmidi.MidiOut(), rtmidi.MidiIn()
    try:
        return o.get_ports(), i.get_ports()
    finally:
        o.delete()
        i.delete()


class Minichord:
    """One minichord, open on both its MIDI ports. Values are stored values (what the SysEx carries)."""

    def __init__(self, port=None):
        rtmidi = _rtmidi()
        self.out, self.inp, self.out2 = rtmidi.MidiOut(), rtmidi.MidiIn(), rtmidi.MidiOut()
        names = self.out.get_ports()
        i = pick_port(names, 1, port)
        j = pick_port(self.inp.get_ports(), 1, port)
        if i is None or j is None:
            seen = ", ".join(names) or "none"
            raise NotFound(f"no minichord found (MIDI ports: {seen}). Plug it in, or name its port with --port")
        try:
            self.out.open_port(i, "minictl")
            self.inp.open_port(j, "minictl")
        except Exception as e:
            raise RuntimeError(f"couldn't open the minichord's port ({e}). Another program may hold it: "
                               f"minicontrol in a browser, or a DAW (Windows lets only one program at a time)")
        self.port_name = names[i]
        k = pick_port(names, 2, port)
        if k is not None:
            try:
                self.out2.open_port(k, "minictl harp")
            except Exception:
                pass
        self.inp.ignore_types(sysex=False, timing=True, active_sense=True)
        self.lock = threading.Lock()
        self.sysex = []
        self.listeners = []
        self.inp.set_callback(self._on_midi)
        self.version = None
        self.bank = None
        self.sent = False

    def close(self):
        if self.sent:
            time.sleep(0.05)      # let the last messages out before the port goes: ALSA delivers after send returns
        try:
            self.inp.cancel_callback()
        except Exception:
            pass
        for p in (self.out, self.inp, self.out2):
            try:
                p.close_port()
                p.delete()          # the ALSA client goes now, not whenever the collector gets to it
            except Exception:
                pass
        self.listeners = []

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def _on_midi(self, ev, _):
        msg, _dt = ev
        if msg and msg[0] == 0xF0:
            with self.lock:
                self.sysex.append(msg)
        for f in self.listeners:
            f(msg)

    # ---- sending ----

    def set(self, address, value):
        value = int(value) & 0x3FFF
        self.sent = True
        self.out.send_message([0xF0, address % 128, address // 128, value % 128, value // 128, 0xF7])

    def command(self, cmd, param=0):
        self.sent = True
        self.out.send_message([0xF0, 0, 0, cmd, param, 0xF7])

    # ---- reading ----

    def _take(self, want, timeout, nudge=()):
        """the first SysEx reply that `want` accepts, waiting up to timeout; nudge: (seconds, fn) to call
        while waiting, as minicontrol asks again in case a report was missed"""
        t0 = time.monotonic()
        nudge = sorted(nudge, key=lambda x: x[0])
        while time.monotonic() - t0 < timeout:
            with self.lock:
                for k, m in enumerate(self.sysex):
                    r = want(m[1:-1])
                    if r is not None:
                        del self.sysex[:k + 1]
                        return r
            while nudge and time.monotonic() - t0 >= nudge[0][0]:
                nudge.pop(0)[1]()
            time.sleep(0.005)
        return None

    @staticmethod
    def _page0(body):
        if len(body) == 2 * PAGE:
            return [body[2 * i] + 128 * body[2 * i + 1] for i in range(PAGE)]
        return None

    @staticmethod
    def _page(page):
        def want(body):
            if len(body) == 3 + 2 * PAGE and body[:3] == [0x7D, 0x6D, page]:
                b = body[3:]
                return [b[2 * i] + 128 * b[2 * i + 1] for i in range(PAGE)]
            return None
        return want

    def read(self, timeout=3.0, bank=None):
        """Every live setting, page 0 and (firmware 31 on) page 1. With bank, only a report of that bank
        counts: a report from the bank before can still be on its way."""
        with self.lock:
            self.sysex.clear()

        def want0(body):
            v = self._page0(body)
            return v if v is not None and (bank is None or v[1] == bank) else None

        p0 = self._take(want0, timeout, [(0.0, lambda: self.command(0)), (0.4, lambda: self.command(0)),
                                         (0.9, lambda: self.command(0))] if bank is None else
                        [(0.08, lambda: self.command(0)), (0.4, lambda: self.command(0)), (0.9, lambda: self.command(0))])
        if p0 is None:
            raise RuntimeError("the minichord didn't answer" + (f" for bank {bank + 1}" if bank is not None else ""))
        self.version, self.bank = p0[VERSION_ADDRESS], p0[1]
        if self.version < PAGE1_VERSION:
            return p0
        self.command(7, 1)
        p1 = self._take(self._page(1), 1.5)
        if p1 is None:
            raise RuntimeError("the minichord sent page 0 but not page 1")
        return p0 + p1

    @property
    def has_page1(self):
        return (self.version or 0) >= PAGE1_VERSION

    def load_bank(self, bank, timeout=3.0):
        """load bank 0-11 and return its settings as stored (the load drops unsaved live edits)"""
        with self.lock:
            self.sysex.clear()
        self.command(4, bank)
        return self.read(timeout, bank=bank)

    def apply(self, values, page1_defaults=None):
        """Sends a whole preset to the live settings, as minicontrol's load does; nothing is saved. A
        page-0 preset gets page 1's defaults rather than keeping what the last preset had there."""
        n = min(len(values), 2 * PAGE if self.has_page1 else PAGE)
        for a in range(2, n):
            if a == VERSION_ADDRESS or a in RESERVED or values[a] is None:
                continue
            self.set(a, values[a])
            if a % 32 == 0:
                time.sleep(0.002)
        if len(values) <= PAGE and self.has_page1 and page1_defaults:
            for a, v in page1_defaults.items():
                if a not in RESERVED:
                    self.set(a, v)
        time.sleep(0.02)

    def save(self, bank):
        self.command(2, bank)
        time.sleep(0.3)                  # a flash write

    def reset(self, bank):
        self.command(3, bank)
        time.sleep(0.4)

    # ---- notes ----

    def port_for(self, cable):
        return self.out2 if cable == 2 and self.out2.is_port_open() else self.out

    def note(self, on, note, vel=100, channel=1, cable=1):
        st = (0x90 if on else 0x80) | ((channel - 1) & 15)
        self.sent = True
        self.port_for(cable).send_message([st, note, vel if on else 0])

    def panic(self):
        self.sent = True
        for port in {id(self.out): self.out, id(self.out2): self.out2}.values():
            if not port.is_port_open():
                continue
            for ch in range(16):
                port.send_message([0xB0 | ch, 64, 0])     # sustain up
                port.send_message([0xB0 | ch, 123, 0])    # all notes off
                port.send_message([0xB0 | ch, 120, 0])    # all sound off


# ---- serial ----------------------------------------------------------------------------------------

def serial_port():
    """the minichord's serial port (Teensy's vendor id), or None"""
    try:
        from serial.tools import list_ports
    except ImportError:
        raise RuntimeError("the serial log needs pyserial: pip install pyserial")
    ports = [p for p in list_ports.comports() if p.vid == PJRC_VID]
    ours = [p for p in ports if "minichord" in f"{p.product} {p.description}".lower()]
    pick = (ours or ports or [None])[0]
    return pick.device if pick else None


# ---- USB audio -------------------------------------------------------------------------------------

def audio_input(want=None):
    """The minichord's USB audio input for sounddevice: a device index, or "pulse" with PULSE_SOURCE
    set when the desktop's sound server holds the card (Linux, USB audio mode 1)."""
    try:
        import sounddevice as sd
    except ImportError:
        raise RuntimeError("recording needs sounddevice: pip install sounddevice")
    want = want or os.environ.get("MINICTL_AUDIO")
    if want:
        return int(want) if want.isdigit() else want
    devs = sd.query_devices()
    hits = [i for i, d in enumerate(devs) if "minichord" in d["name"].lower() and d["max_input_channels"] >= 2]
    if sys.platform.startswith("linux"):
        try:
            src = subprocess.run(["pactl", "list", "short", "sources"], capture_output=True, text=True,
                                 timeout=5).stdout
            m = re.search(r"^\d+\s+(\S*minichord\S*analog\S*)", src, re.M | re.I)
        except (OSError, subprocess.TimeoutExpired):
            m = None
        if m and any(d["name"] == "pulse" for d in devs):
            os.environ["PULSE_SOURCE"] = m.group(1)
            return "pulse"
    if hits:
        return hits[0]
    raise NotFound("no minichord audio input found (is USB audio on? `minictl get usb-audio`); "
                   "name one with --device (python -m sounddevice lists them)")


def record(path, seconds, device=None, rate=44100):
    import sounddevice as sd, wave
    dev = audio_input(device)
    frames = []
    with sd.RawInputStream(device=dev, samplerate=rate, channels=2, dtype="int16") as s:
        left = int(seconds * rate)
        while left > 0:
            data, _ = s.read(min(left, 4096))
            frames.append(bytes(data))
            left -= min(left, 4096)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(frames))
    return dev


# ---- flashing --------------------------------------------------------------------------------------

def teensy_tools():
    """the folder holding PJRC's teensy_post_compile, from PlatformIO or the Arduino IDE"""
    exe = "teensy_post_compile" + (".exe" if os.name == "nt" else "")
    env = os.environ.get("MINICTL_TEENSY_TOOLS")
    home = os.path.expanduser("~")
    places = [env] if env else []
    places += [os.path.join(home, ".platformio", "packages", "tool-teensy")]
    for arduino in (os.path.join(home, ".arduino15"), os.path.join(home, "Library", "Arduino15"),
                    os.path.join(os.environ.get("LOCALAPPDATA", home), "Arduino15")):
        places += sorted(glob.glob(os.path.join(arduino, "packages", "teensy", "tools", "teensy-tools", "*")),
                         reverse=True)
    for p in places:
        if p and os.path.exists(os.path.join(p, exe)):
            return p, os.path.join(p, exe)
    raise NotFound("can't find PJRC's teensy tools (teensy_post_compile). Install PlatformIO or the Arduino IDE "
                   "with Teensyduino, or point MINICTL_TEENSY_TOOLS at the folder")


def flash_hazards():
    """reasons not to flash now, on Linux: the kernel with the MIDI unplug hang, another upload running,
    or a USB stack already stuck"""
    if not sys.platform.startswith("linux"):
        return []
    out = []
    k = os.uname().release
    if k.startswith("6.1.0-53"):
        out.append(f"kernel {k} (6.1.187) hangs USB when a MIDI device drops off; boot 6.1.0-50 first")
    busy = subprocess.run(["pgrep", "-f", "teensy_post_compile|teensy_loader_cli|pio run -t upload|pio run --target upload"],
                          capture_output=True, text=True).stdout.split()
    busy = [p for p in busy if int(p) != os.getpid()]
    if busy:
        out.append(f"another upload is running (pids {', '.join(busy)})")
    d = subprocess.run("ps -eLo stat,wchan:40 | awk '$1 ~ /D/' | grep -c snd_use_lock", shell=True,
                       capture_output=True, text=True).stdout.strip()
    if d not in ("", "0"):
        out.append("threads are stuck in snd_use_lock: the USB stack is already hung")
    return out


def flash(hexfile, wait=60, log=print):
    """Puts a .hex on the minichord through PJRC's loader (it presses the program button by itself),
    then waits for the minichord's MIDI port to come back. Close every MIDI program first."""
    tools, post = teensy_tools()
    hexfile = os.path.abspath(hexfile)
    folder, name = os.path.split(hexfile)
    stem = name[:-4] if name.lower().endswith(".hex") else name
    r = subprocess.run([post, f"-file={stem}", f"-path={folder}", f"-tools={tools}", "-board=TEENSY40", "-reboot"],
                       capture_output=True, text=True, timeout=120)
    said = (r.stdout + r.stderr).strip()
    if r.returncode != 0:
        raise RuntimeError(f"the loader failed ({r.returncode}): {said[-300:]}")
    t = time.monotonic()
    time.sleep(3)
    while time.monotonic() - t < wait:
        outs, _ = list_ports()
        if pick_port(outs) is not None:
            time.sleep(2)
            return time.monotonic() - t
        time.sleep(0.5)
    raise RuntimeError(f"flashed, but the minichord's MIDI port didn't come back within {wait} s")
