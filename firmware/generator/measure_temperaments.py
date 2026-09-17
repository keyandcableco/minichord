#!/usr/bin/env python3
"""Measure the temperaments on a real minichord, through its USB audio.

For each temperament the script selects it over MIDI, then listens while you
touch harp strings one at a time. It detects each note, measures its pitch to a
fraction of a cent, works out which of the twelve pitch classes it is, and
compares it with the offset in include/temperament_profiles.h. It keeps asking
until every pitch class has been heard, then moves to the next temperament, and
finally writes a report.

Linux only. Needs:  sudo apt-get install -y alsa-utils python3-numpy

  python3 measure_temperaments.py                 every temperament in the table
  python3 measure_temperaments.py --only 0 4 6    just these
  python3 measure_temperaments.py --selftest      check the pitch measurement on
                                                  synthetic notes, no minichord

Before starting: connect the minichord over USB, close minicontrol and any other
editor, pick a preset with a plain harp sound (no chorus or vibrato on the
strings), and turn the chord volume down so only the harp is heard.

What it changes on the device, temporarily: master tuning (440.0 Hz) and the
temperament, sent as parameter changes and not saved. Press preset up and then
preset down afterwards to reload the preset as it was.

How the numbers are read. Every row of the table has A at 0, and master tuning
puts A4 at 440 Hz, so a note's offset is simply its distance from the nearest
equal-tempered pitch at A = 440. The report shows that raw offset, and also the
offset after removing the run's common shift (the median over all twelve pitch
classes), which is what to compare if the recording clock or the master tuning
is a fraction of a cent out. Tables anchored on another note (older test
builds) only line up after that shift.
"""
import argparse, json, os, re, select, subprocess, sys, time
from math import log2

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "include", "temperament_profiles.h")
NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
TEMPERAMENT_ADDRESS = 237
MASTER_TUNING_ADDRESS = 109
RATE = 44100
READINGS_PER_CLASS = 2
# a chord whose harp strings include each pitch class, to suggest when one is missing
SUGGEST = {0: "C major", 1: "A major", 2: "D major", 3: "C minor", 4: "C major", 5: "F major",
           6: "D major", 7: "G major", 8: "E major", 9: "A major", 10: "Bb major", 11: "G major"}


# ---------------------------------------------------------------- the table
def read_table(path):
    """Rows of twelve cents from the generated header, with their comment names.
    Understands the upstream format ({c, c, ...}) and the older test-build one
    ({division, {c, c, ...}}), where rows for other divisions are skipped."""
    text = open(path, encoding="utf-8").read()
    rows = []
    for line in text.splitlines():
        m = re.match(r"\s*\{\s*(?:(\d+)\s*,\s*\{)?([-\d,\s]+)\}\}?\s*,\s*//\s*(\d+)\s+(.*)$", line)
        if not m:
            continue
        division, cents, number, name = m.group(1), m.group(2), int(m.group(3)), m.group(4).strip()
        values = [int(x) for x in cents.split(",") if x.strip()]
        if len(values) != 12:
            continue
        rows.append(dict(number=number, name=name, cents=values, twelve=(division in (None, "0"))))
    if not rows:
        sys.exit(f"no temperament rows found in {path}")
    return rows


# ---------------------------------------------------------------- pitch measurement
def measure_pitch(np, samples, rate=RATE):
    """Fundamental of one sustained note, in Hz, and a quality figure in cents
    (how far apart its harmonics' own estimates are). None if no clear pitch."""
    x = samples - np.mean(samples)
    if np.max(np.abs(x)) < 1e-4:
        return None
    n = len(x)
    window = np.hanning(n)
    size = 1 << int(np.ceil(np.log2(n * 8)))           # zero padding for fine bins
    spectrum = np.abs(np.fft.rfft(x * window, size))
    freqs = np.fft.rfftfreq(size, 1.0 / rate)
    bin_hz = freqs[1]
    lo, hi = int(40 / bin_hz), int(4000 / bin_hz)
    mag = spectrum[lo:hi]
    if mag.max() <= 0:
        return None

    def peak_near(target, spread=0.03):
        a, b = int(target * (1 - spread) / bin_hz), int(target * (1 + spread) / bin_hz) + 1
        if a < 1 or b >= len(spectrum) - 1:
            return None
        i = a + int(np.argmax(spectrum[a:b]))
        if i <= a or i >= b - 1:
            return None
        y0, y1, y2 = np.log(spectrum[i - 1:i + 2] + 1e-12)
        d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)       # parabolic interpolation on log magnitude
        return (i + d) * bin_hz, spectrum[i]

    strongest = (lo + int(np.argmax(mag))) * bin_hz
    floor = np.median(mag)
    best = None
    # the strongest peak is harmonic k of the fundamental for some small k
    for k in range(1, 7):
        f0 = strongest / k
        if f0 < 40:
            break
        found = [peak_near(f0 * h) for h in range(1, 7)]
        present = [(h, p) for h, p in zip(range(1, 7), found) if p and p[1] > 8 * floor]
        if not present or present[0][0] != 1:
            continue                                     # the fundamental itself must be there
        score = sum(p[1] for _, p in present)
        if best is None or score > best[0] * 1.2:
            best = (score, f0, present)
    if best is None:
        return None
    _, _, present = best
    # each harmonic gives its own estimate; the three strongest are least
    # disturbed by anything else still sounding, so take their median
    strongest_three = sorted(present, key=lambda hp: -hp[1][1])[:3]
    estimates = [p[0] / h for h, p in strongest_three]
    f0 = float(np.median(estimates))
    spread = max(abs(1200 * log2(e / f0)) for e in estimates)
    return f0, spread


def classify(f0):
    """Nearest equal-tempered note at A4 = 440, and the offset from it in cents."""
    midi = 69 + 12 * log2(f0 / 440.0)
    nearest = round(midi)
    return nearest % 12, (midi - nearest) * 100, nearest


class NoteDetector:
    """Finds single notes in a running stream and hands back a steady window of each.

    A note only counts if it starts from near-silence, so the measured window is
    not a mix of the new string and the one before it still ringing. If another
    note starts before the window is over, the first one is dropped."""

    def __init__(self, np, rate=RATE):
        self.np, self.rate = np, rate
        self.hop = int(rate * 0.01)
        self.skip = int(rate * 0.08)          # past the attack and the harp transient
        self.length = int(rate * 0.60)        # the part that gets measured
        self.buffer = np.zeros(0, dtype=np.float64)
        self.offset = 0                        # stream position of buffer[0]
        self.recent = []                       # rms of the last 150 ms
        self.history = []                      # rms of the last 3 s, for the noise floor
        self.pending = []                      # onsets waiting for their window
        self.quiet = True                      # whether the stream has been quiet since the last note
        self.peak = 0.0                        # loudest level of the current note
        self.last_onset = -rate                # an attack spans several hops; count it once

    def feed(self, block):
        np = self.np
        start = self.offset + len(self.buffer)
        self.buffer = np.concatenate([self.buffer, block])
        pos = start - self.offset
        while pos + self.hop <= len(self.buffer):
            frame = self.buffer[pos:pos + self.hop]
            rms = float(np.sqrt(np.mean(frame * frame)))
            here = self.offset + pos
            quietest = min(self.recent) if self.recent else rms
            latest = max(self.recent[-3:]) if self.recent else rms
            floor = min(self.history) if self.history else rms
            if self.quiet and rms > 0.003 and rms > 4 * quietest and rms > 6 * floor:
                # a note starting from quiet: measure it
                self.pending.append(here)
                self.quiet, self.peak, self.last_onset = False, rms, here
            elif not self.quiet and rms > 2.5 * latest and here - self.last_onset > self.rate * 0.06:
                # a second note on top of one still ringing: neither can be trusted
                self.pending = [p for p in self.pending if here >= p + self.skip + self.length]
                self.peak, self.last_onset = max(self.peak, rms), here
            else:
                self.peak = max(self.peak, rms)
                if rms < max(2.5 * floor, 0.05 * self.peak, 0.0005):
                    self.quiet = True
            self.recent = (self.recent + [rms])[-15:]
            self.history = (self.history + [rms])[-300:]
            pos += self.hop
        end = self.offset + len(self.buffer)
        ready = [p for p in self.pending if p + self.skip + self.length <= end]
        notes = []
        for p in ready:
            a = p + self.skip - self.offset
            notes.append((p / self.rate, self.buffer[a:a + self.length]))
        self.pending = [p for p in self.pending if p not in ready]
        keep = self.rate * 2
        if len(self.buffer) > keep:
            cut = len(self.buffer) - keep
            self.buffer = self.buffer[cut:]
            self.offset += cut
        return notes


# ---------------------------------------------------------------- the device
def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def find_midi_port():
    out = run(["amidi", "-l"]).stdout
    for line in out.splitlines():
        if "minichord" in line.lower():
            return line.split()[1]
    sys.exit("no minichord MIDI port in `amidi -l`; is it connected and switched on?")


def find_audio_device():
    out = run(["arecord", "-l"]).stdout
    for line in out.splitlines():
        m = re.match(r"card (\d+): (\S+) \[(.*?)\], device (\d+)", line)
        if m and "minichord" in line.lower():
            return f"hw:{m.group(1)},{m.group(4)}"
    sys.exit("no minichord capture device in `arecord -l`")


def send_parameter(port, address, value):
    data = [0xF0, address % 128, address // 128, value % 128, value // 128, 0xF7]
    result = run(["amidi", "-p", port, "-S", " ".join(f"{b:02X}" for b in data)])
    if result.returncode != 0:
        sys.exit(f"could not send to {port}: {result.stderr.strip()} (close minicontrol or Sound Lab?)")


def key_pressed():
    return select.select([sys.stdin], [], [], 0)[0] and sys.stdin.readline()


def measure_one(np, row, port, device):
    send_parameter(port, MASTER_TUNING_ADDRESS, 4400)
    send_parameter(port, TEMPERAMENT_ADDRESS, row["number"])
    time.sleep(0.2)
    print(f"\n=== {row['number']} {row['name']}")
    print("Touch harp strings ONE AT A TIME. Let each one fade before the next: a note that starts")
    print("while another is still ringing is ignored, since the two would blur the measurement.")
    print("Press Enter to move on early, or type q and Enter to stop.")
    readings = {pc: [] for pc in range(12)}
    rec = subprocess.Popen(["arecord", "-q", "-D", device, "-f", "S16_LE", "-c", "2", "-r", str(RATE), "-t", "raw"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    detector = NoteDetector(np)
    last_status = ""
    try:
        while True:
            raw = rec.stdout.read(RATE // 20 * 4)
            if not raw:
                err = rec.stderr.read().decode(errors="replace").strip()
                sys.exit(f"recording stopped: {err or 'no data'}")
            block = np.frombuffer(raw, dtype="<i2").astype(np.float64).reshape(-1, 2).mean(axis=1) / 32768.0
            for t, window in detector.feed(block):
                result = measure_pitch(np, window)
                if not result:
                    continue
                f0, spread = result
                if spread > 3.0:
                    continue                                   # overlapping notes or a noisy sound
                pc, cents, midi = classify(f0)
                readings[pc].append(dict(freq=f0, cents=cents, midi=midi, spread=spread))
            missing = [pc for pc in range(12) if len(readings[pc]) < READINGS_PER_CLASS]
            status = "  heard: " + " ".join(f"{NAMES[pc]}{'✓' if pc not in missing else len(readings[pc])}" for pc in range(12))
            if missing:
                status += f"   still need {NAMES[missing[0]]} (try {SUGGEST[missing[0]]})"
            if status != last_status:
                print("\r" + status.ljust(110), end="", flush=True)
                last_status = status
            if not missing:
                print()
                break
            typed = key_pressed()
            if typed:
                print()
                if typed.strip().lower() == "q":
                    return readings, True
                break
    finally:
        rec.terminate()
        rec.wait()
    return readings, False


# ---------------------------------------------------------------- the report
def summarise(np, row, readings):
    measured = {}
    for pc, rs in readings.items():
        if rs:
            measured[pc] = float(np.median([r["cents"] for r in rs]))
    if not measured:
        return None
    diffs = [measured[pc] - row["cents"][pc] for pc in measured]
    shift = float(np.median(diffs))
    worst_raw = max(abs(d) for d in diffs)
    worst_shifted = max(abs(d - shift) for d in diffs)
    return dict(measured=measured, shift=shift, worst_raw=worst_raw, worst_shifted=worst_shifted,
                classes=len(measured), readings=sum(len(r) for r in readings.values()))


def report(np, rows, results, path):
    lines = ["# Temperament measurements", "",
             "Measured from the minichord's USB audio, harp strings, master tuning 440.0 Hz.",
             "Cents are offsets from equal temperament at A4 = 440. *expected* is the firmware table,",
             "*measured* the median of the readings for that pitch class. The last two columns are the",
             "largest difference from the table, raw and after removing the run's common shift.", ""]
    header = "| Temperament | | " + " | ".join(NAMES) + " | shift | worst | worst after shift |"
    lines += [header, "|---|---|" + "---|" * 12 + "---|---|---|"]
    for row in rows:
        s = results.get(row["number"])
        if not s:
            continue
        exp = " | ".join(f"{c:+d}" for c in row["cents"])
        meas = " | ".join(f"{s['measured'][pc]:+.1f}" if pc in s["measured"] else "–" for pc in range(12))
        lines.append(f"| {row['number']} {row['name']} | expected | {exp} | | | |")
        lines.append(f"| | measured | {meas} | {s['shift']:+.2f} | {s['worst_raw']:.2f} | {s['worst_shifted']:.2f} |")
    text = "\n".join(lines) + "\n"
    open(path, "w", encoding="utf-8").write(text)
    return text


# ---------------------------------------------------------------- self-test
def synthetic_run(np, rng, row, midis, gap, decay, noise):
    """Harp-like notes at the table's pitches, each ringing on under the next, as
    a recording would contain them. Returns (expected, measured) note lists."""
    n = int(RATE * (gap * len(midis) + 2))
    audio = np.zeros(n)
    expected = []
    for j, midi in enumerate(midis):
        f = 440.0 * 2 ** ((midi - 69 + row["cents"][midi % 12] / 100.0) / 12)
        start = int(RATE * (0.3 + j * gap))
        t = np.arange(min(n - start, int(RATE * 7 / decay))) / RATE     # until it is 60 dB down
        env = np.exp(-t * decay) * np.minimum(1.0, t / 0.004)
        wave = sum((0.8 ** h) / h * np.sin(2 * np.pi * f * h * t + rng.uniform(0, 6.28)) for h in range(1, 10))
        click = rng.standard_normal(len(t)) * np.exp(-t * 60) * 0.015     # a transient, as the harp has
        audio[start:start + len(t)] += 0.2 * env * wave + click
        expected.append((midi % 12, row["cents"][midi % 12]))
    audio += noise * rng.standard_normal(n)
    detector = NoteDetector(np)
    measured = []
    for i in range(0, n, RATE // 20):
        for _, window in detector.feed(audio[i:i + RATE // 20]):
            r = measure_pitch(np, window)
            if r and r[1] <= 3.0:
                measured.append(classify(r[0])[:2])
    return expected, measured


def score(expected, measured):
    """Match measured notes to expected ones in order; count wrong pitch classes."""
    i, matched, wrong, worst = 0, 0, 0, 0.0
    for pc, cents in measured:
        k = i
        while k < len(expected) and expected[k][0] != pc:
            k += 1
        if k < len(expected):
            matched += 1
            worst = max(worst, abs(cents - expected[k][1]))
            i = k + 1
        else:
            wrong += 1
    return matched, wrong, worst


def selftest(np, rows):
    """The live run's own detector and measurement, on synthetic recordings."""
    rng = np.random.default_rng(7)
    twelve = [r for r in rows if r["twelve"]]
    ok = True
    worst_all = 0.0
    total = 0
    # every temperament, notes that fade before the next
    for row in twelve:
        expected, measured = synthetic_run(np, rng, row, list(range(45, 81)), 1.2, 3.0, 0.0005)
        matched, wrong, worst = score(expected, measured)
        total += matched
        worst_all = max(worst_all, worst)
        if matched != len(expected) or wrong:
            print(f"selftest: {row['name']}: {matched}/{len(expected)} notes measured, {wrong} misclassified")
            ok = False
    print(f"selftest: {total} notes across {len(twelve)} temperaments, all measured and classified, "
          f"largest error {worst_all:.3f} cents")
    # harder recordings, on the temperament with the largest offsets
    row = max(twelve, key=lambda r: max(abs(c) for c in r["cents"]))
    cases = [
        ("long ring, player waits for each note", list(range(40, 64)), 3.5, 1.0, 0.002, True),
        ("low strings", list(range(28, 52)), 3.5, 1.0, 0.002, True),
        ("noise at -26 dB", list(range(40, 64)), 1.5, 3.0, 0.01, True),
        ("notes played over each other", list(range(40, 64)), 0.7, 1.0, 0.002, False),
    ]
    for name, midis, gap, decay, noise, all_expected in cases:
        expected, measured = synthetic_run(np, rng, row, midis, gap, decay, noise)
        matched, wrong, worst = score(expected, measured)
        good = wrong == 0 and worst < 0.2 and (matched == len(expected) if all_expected else True)
        ok &= good
        print(f"selftest: {name} ({row['name']}): {matched}/{len(expected)} measured, "
              f"{wrong} misclassified, largest error {worst:.3f} cents{'' if good else '  FAIL'}")
    return ok and worst_all < 0.2


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", type=int, nargs="+", help="temperament numbers to measure")
    ap.add_argument("--header", default=HEADER, help="the generated temperament table to compare against")
    ap.add_argument("--out", default="temperament-measurements", help="report file name, without extension")
    ap.add_argument("--selftest", action="store_true", help="check the measurement on synthetic notes")
    args = ap.parse_args()
    try:
        import numpy as np
    except ImportError:
        sys.exit("numpy is missing: sudo apt-get install -y python3-numpy")
    rows = read_table(args.header)
    if args.selftest:
        sys.exit(0 if selftest(np, rows) else 1)
    rows = [r for r in rows if r["twelve"] and (not args.only or r["number"] in args.only)]
    if not rows:
        sys.exit("nothing to measure")
    port, device = find_midi_port(), find_audio_device()
    print(f"minichord MIDI {port}, audio {device}; measuring {len(rows)} temperament(s)")
    results, raw = {}, {}
    for row in rows:
        readings, stop = measure_one(np, row, port, device)
        raw[row["number"]] = readings
        s = summarise(np, row, readings)
        if s:
            results[row["number"]] = s
            print(f"  {s['classes']}/12 pitch classes, shift {s['shift']:+.2f} c, "
                  f"worst {s['worst_raw']:.2f} c raw, {s['worst_shifted']:.2f} c after shift")
        if stop:
            break
    send_parameter(port, TEMPERAMENT_ADDRESS, 0)
    text = report(np, rows, results, args.out + ".md")
    with open(args.out + ".json", "w", encoding="utf-8") as f:
        json.dump(dict(table=rows, readings={str(k): v for k, v in raw.items()}), f, indent=1)
    print("\n" + text)
    print(f"saved {args.out}.md and {args.out}.json")
    print("Press preset up and then preset down on the minichord to reload the preset as it was.")


if __name__ == "__main__":
    main()
