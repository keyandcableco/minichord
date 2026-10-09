#!/usr/bin/env python3
"""preset_maker: a minichord preset from a description in words, typed or spoken.

Built-in rules (interpret.py) turn the words into settings, with nothing sent anywhere; the
script checks every value against parameters.json and prints a share code to paste into
minicontrol ("load preset code"). Then say what to change and it revises the same preset.

    preset_maker.py "warm pad chords, plucky harp spread across the stereo field"
    preset_maker.py                          # asks for the description
    preset_maker.py --voice                  # say it instead: Enter to speak, Enter to stop
    preset_maker.py --base CODE "less reverb" # revise a preset you already have
    preset_maker.py --apply changes.json     # apply a list of exact changes
    preset_maker.py --words                  # what the rules understand
    preset_maker.py --profile "..."          # a bulk edit profile for minicontrol instead of a preset
    preset_maker.py --claude "..."           # Claude over the internet instead (needs an API key)

A code with page 1 settings needs this fork's firmware (39 and on) and its minicontrol.
--voice needs faster-whisper and sounddevice (speech becomes text on this computer);
--send needs python-rtmidi and a minichord plugged in; --claude needs the anthropic package.
"""
import argparse, base64, json, os, re, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(HERE)
PARAMETERS_JSON = os.path.join(FIRMWARE, "generator", "parameters.json")
SHARED_PRESETS_JSON = os.path.join(FIRMWARE, "minicontrol", "json", "shared_presets.json")
MAIN_CPP = os.path.join(FIRMWARE, "src", "main.cpp")

MODEL = "claude-opus-5-5"
PARAMETER_SIZE = 512
PAGE_SIZE = 256
FLOAT_MULTIPLIER = 100
VERSION_ADDRESS = 7
RESERVED = {382, 383, 510, 511}                 # can't be written: their low byte is a universal SysEx id
# Settings a preset has no business changing: the bank and version stamps, the knob memories and
# volumes, the instrument's own setup (harp thresholds, plate, USB audio) and the looper's action.
LOCKED = {0, 1, 2, 3, 4, 5, 6, 7, 241, 242, 243, 244, 256}
# Settings whose value is the address of another setting, and which kind of control each one is
KNOB_TARGETS = {10: "chord knob, alternate", 12: "harp knob, alternate", 14: "mod knob, main",
                16: "mod knob, alternate", 249: "hover"}
TAP_TARGETS = {200: "double tap", 209: "double tap, 2nd", 211: "double tap, 3rd"}
SECTIONS = {"global_parameter": "global", "harp_parameter": "harp", "chord_parameter": "chord"}


# ---- parameters ---------------------------------------------------------------------------------

def load_parameters():
    with open(PARAMETERS_JSON) as f:
        d = json.load(f)
    params = {}
    for key, section in SECTIONS.items():
        for p in d[key]:
            p = dict(p, section=section)
            p["controls"] = p.get("controls") or "all"
            params[p["sysex_adress"]] = p
    return params


def firmware_version(params):
    try:
        with open(MAIN_CPP) as f:
            m = re.search(r"int\s+version_ID\s*=\s*(\d+)", f.read())
        if m:
            return int(m.group(1))
    except OSError:
        pass
    return max(p.get("introduction_version", 0) for p in params.values())


def is_float(p):
    return p["data_type"] == "float"


def to_stored(p, value):
    return int(round(float(value) * (FLOAT_MULTIPLIER if is_float(p) else 1)))


def to_human(p, stored):
    if is_float(p):
        return round(stored / FLOAT_MULTIPLIER, 2)
    return int(stored)


def stored_bounds(p):
    return to_stored(p, p["min_value"]), to_stored(p, p["max_value"])


def stored_default(p):
    return to_stored(p, p["default_value"])


def label(p):
    return f'{p["section"]} / {p["group"]} / {p["name"].strip()}'


def editable(params):
    return {a: p for a, p in params.items()
            if a not in LOCKED and a not in RESERVED and p["group"] != "hidden"}


# ---- share codes --------------------------------------------------------------------------------

def decode(code, params):
    """A share code to 512 stored values. A page-0 code gets page 1's defaults, as minicontrol does,
    and a code older than a setting gets that setting's default, as the firmware does on load."""
    s = re.sub(r"\s+", "", code)
    s += "=" * (-len(s) % 4)
    try:
        fields = base64.b64decode(s).decode("ascii").split(";")
    except Exception:
        raise ValueError("that isn't a minichord preset code")
    if len(fields) not in (PAGE_SIZE, PARAMETER_SIZE):
        raise ValueError(f"malformed preset code: {len(fields)} values, not 256 or 512")
    values = [int(round(float(v))) if v.strip() else 0 for v in fields]
    if len(values) == PAGE_SIZE:
        values += [0] * PAGE_SIZE
        for a, p in params.items():
            if a >= PAGE_SIZE:
                values[a] = stored_default(p)
    version = values[VERSION_ADDRESS]
    if 0 < version < 18 and values[237] == 11:   # 24-EDO moved 31-EDO from temperament 11 to 12
        values[237] = 12
    for a, p in params.items():
        if a == VERSION_ADDRESS or p.get("introduction_version", 0) <= version:
            continue
        if version == 0:
            # Codes from before firmware 39 stamped nothing here, so 0 says only "older". Defaulting
            # everything, as the firmware does for a factory bank, would wipe the preset: fill in only
            # the 0s that can't be meant, out of range or in a setting from version 20 on.
            if values[a] != 0 or stored_default(p) == 0:
                continue
            if stored_bounds(p)[0] <= 0 and p.get("introduction_version", 0) < 20:
                continue
        values[a] = stored_default(p)
    return values


def encode(values, params):
    """512 stored values to a share code, the way minicontrol writes one: page 0 alone when page 1 is
    all at its defaults, and the last value left off (minicontrol's export does, and reads it as 0)."""
    page1_default = all(values[a] == stored_default(p) for a, p in params.items() if a >= PAGE_SIZE)
    n = PAGE_SIZE if page1_default else PARAMETER_SIZE
    text = "".join(f"{values[i]};" for i in range(n - 1))
    return base64.b64encode(text.encode("ascii")).decode("ascii")


def default_values(params, version):
    values = [0] * PARAMETER_SIZE
    for a, p in params.items():
        values[a] = stored_default(p)
    values[2] = values[3] = 50
    values[4] = values[5] = values[6] = 512
    values[VERSION_ADDRESS] = version
    return values


def load_shared_presets():
    with open(SHARED_PRESETS_JSON) as f:
        return json.load(f)["shared_presets"]


# ---- what the model reads -----------------------------------------------------------------------

def show_value(p, stored, params):
    a = p["sysex_adress"]
    if a in KNOB_TARGETS or a in TAP_TARGETS:
        return "unused" if stored == 0 or stored not in params else f"{stored} ({params[stored]['name'].strip()})"
    return str(to_human(p, stored))


def reference(params):
    lines = []
    for a, p in sorted(editable(params).items()):
        kind = p["data_type"]
        if a in KNOB_TARGETS or a in TAP_TARGETS:
            kind = "address of a setting, 0 for unused"
        elif p.get("follows_target") is not None:
            kind = f"a value of the setting at address {p['follows_target']}, in that setting's units"
        else:
            kind = f"{kind} {p['min_value']}..{p['max_value']}"
        if p["controls"] == "none":
            reach = "; no knob, hover or double tap can move it"
        elif p["controls"] == "tap":
            reach = "; the double tap can set it, the knobs and hover can't"
        else:
            reach = ""
        lines.append(f"{a} | {label(p)} | {kind}, default {p['default_value']}{reach} | {p['tooltip'].strip()}")
    return "\n".join(lines)


def listing(values, params):
    lines = []
    for a, p in sorted(editable(params).items()):
        lines.append(f"{a} {label(p)} = {show_value(p, values[a], params)}")
    return "\n".join(lines)


SYSTEM = """You design presets for the minichord, a small handheld synthesizer in the spirit of the Omnichord. The player describes a sound and how the controls should behave; you choose the settings.

How the instrument is played:
- Chord buttons, left hand: 7 columns (the chord roots, C to B) and 3 rows (major, minor, seventh). Pressed together the rows make more chords (maj+7th, min+7th, maj+min, all three). A sharp button raises the root. The chord section plays 4 voices.
- The harp, right hand: a touch strip of 12 zones strummed or tapped, playing notes of the held chord (or a scale, or chromatic, depending on the Notes settings).
- Three knobs. The chord knob and harp knob are those sections' volumes as their main function; the mod knob's main function is whatever setting 14 names. Holding the modifier button turns each knob to its alternate function (settings 10, 12, 16). With "knob layer" at 1 this is the other way round.
- A knob sweeping an ordinary setting goes from (preset value x (1 - range%)) at one end to (preset value x (1 + range%)) at the other, so its centre is the value the preset stores. To sweep a filter from 200 to 2000 Hz store 1100 with a range of 82. Range is settings 11, 13, 15, 17 for the knobs; a selector (a setting with a short list of choices, such as a waveform) sweeps its whole list whatever the range.
- Double tap of the modifier button toggles up to three settings at once (200/201, 209/210, 211/212) to the given values, and a second double tap puts them back. The looper is double tap control 256 with value 7: record, play, stop, then a new recording.
- Hover: a hand held over the harp moves setting 249 from its preset value (hand away) to the value in 250 (hand 2 cm above the plate). 251 is how high hover starts.
- Rhythm mode is turned on at the instrument, not by a preset; the rhythm settings only matter there. Each of the 16 steps (addresses 220-235, two steps a beat) is a bitmask of the chord voices that play on that step: bits 0-3 voices 1-4, bits 4-6 three extra voices an octave up.
- Sound sources: the chords have three oscillators (amplitude, waveform, frequency multiplier) plus noise, or a sampled voice (chord voice). The harp has one oscillator with a transient, crossfaded with a plucked string model (string model %), or a sampled voice (harp voice). Each section has an envelope, a low pass filter with its own envelope, tremolo, vibrato, delay, crunch, reverb send and an output filter.
- Waveforms, by number: 0 sine, 1 sawtooth, 2 square, 3 triangle, 4 bandlimited pulse, 5 pulse, 6 reverse sawtooth, 7 sample and hold, 8 triangle variable, 9 bandlimited sawtooth, 10 bandlimited reverse sawtooth, 11 bandlimited square. Prefer the bandlimited ones for bright sounds.

Values: give every value in the units the reference shows (floats as floats, times in milliseconds, frequencies in Hz). The script converts and checks them and tells you of anything out of range. For the knob, hover and double tap settings that name another setting, give that setting's address, and give their values in the target setting's units.

How to work:
- Start from the preset you are given and change what the description needs; leave what already serves it. A preset sounds as a whole, so when you change the character of a section, look at its envelope, filter, levels and effects together, not one setting alone.
- Assign the controls the description asks for. If it says nothing about controls, give the mod knob and the double tap something expressive that suits the sound.
- Keep the levels sane: the two sections should balance, and stacked oscillator amplitudes, high resonance, crunch and the output amplifiers can clip.
- Set the bank color (0-360, a hue) to suit the sound, and give the preset a short name.
- Leave MIDI routing, tuning and the harp plate alone unless asked.
- In a revision, return only what changes in that revision.
- You can't hear the result, so say plainly in caveats which choices are guesses the player should check by ear.

The settings (address | section / group / name | kind and range, default | what it does):
"""


def output_schema():
    return {
        "type": "object",
        "properties": {
            "name": {"type": "string", "description": "a short name for the preset"},
            "summary": {"type": "string", "description": "the sound, in a sentence or two"},
            "controls": {"type": "string", "description": "what each knob, the double tap and hover do, one line each"},
            "changes": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "address": {"type": "integer"},
                        "value": {"type": "number"},
                        "reason": {"type": "string"},
                    },
                    "required": ["address", "value", "reason"],
                    "additionalProperties": False,
                },
            },
            "caveats": {"type": "string", "description": "what to check by ear, or what couldn't be done"},
        },
        "required": ["name", "summary", "controls", "changes", "caveats"],
        "additionalProperties": False,
    }


# ---- applying changes ---------------------------------------------------------------------------

def apply_changes(values, changes, params, report_all=False):
    """Applies the model's changes to a copy of values. Returns the new values, the changes as made
    (address, old, new) and notes on anything adjusted or refused, for the player and the model."""
    values = list(values)
    allowed = editable(params)
    made, notes = [], []
    # the settings naming a target first, so the values that follow them use the new target's units
    ordered = sorted(changes, key=lambda c: 0 if int(c["address"]) in KNOB_TARGETS or int(c["address"]) in TAP_TARGETS else 1)
    for c in ordered:
        a = int(c["address"])
        if a not in allowed:
            notes.append(f"address {a} isn't a setting a preset can change; left as it was")
            continue
        p = allowed[a]
        if a in KNOB_TARGETS or a in TAP_TARGETS:
            target = int(round(c["value"]))
            if target != 0:
                t = params.get(target)
                ok = t is not None and target not in LOCKED and t["group"] != "hidden" or target == 256
                if a in KNOB_TARGETS:
                    ok = ok and target != 256 and t["controls"] == "all"
                else:
                    ok = ok and (target == 256 or t["controls"] in ("all", "tap"))
                if not ok:
                    notes.append(f"{label(p)}: {target} can't be moved by that control; left as it was")
                    continue
            new = target
        else:
            source = p
            if p.get("follows_target") is not None:
                source = params.get(values[p["follows_target"]], p)
                if values[p["follows_target"]] == 256:   # the looper's action, an int 0-7
                    source = params[256]
            new = to_stored(source, c["value"])
            lo, hi = stored_bounds(source)
            if new < lo or new > hi:
                clamped = max(lo, min(hi, new))
                notes.append(f"{label(p)}: {c['value']} is outside {source['min_value']}..{source['max_value']}; "
                             f"set to {to_human(source, clamped)}")
                new = clamped
        if new != values[a] or report_all:
            made.append((a, values[a], new))
            values[a] = new
    return values, made, notes


def show_changes(made, params, before, after):
    for a, old, new in made:
        p = params[a]
        print(f"  {label(p)}: {show_value_for(p, old, before, params)} -> {show_value_for(p, new, after, params)}")


def show_value_for(p, stored, values, params):
    if p.get("follows_target") is not None:
        target = params.get(values[p["follows_target"]])
        if target is not None:
            return str(to_human(target, stored))
    return show_value(p, stored, params)


# ---- the model ----------------------------------------------------------------------------------

class Designer:
    def __init__(self, params, model, effort):
        import anthropic
        self.anthropic = anthropic
        self.client = anthropic.Anthropic()
        self.params = params
        self.model = model
        self.effort = effort
        self.system = SYSTEM + reference(params)
        self.messages = []

    def _call(self, messages, schema, effort, system):
        r = self.client.beta.messages.create(
            model=self.model,
            max_tokens=16000,
            betas=["server-side-fallback-2026-07-01"],
            fallbacks="default",
            thinking={"type": "adaptive"},
            output_config={"effort": effort, "format": {"type": "json_schema", "schema": schema}},
            cache_control={"type": "ephemeral"},
            system=system,
            messages=messages,
        )
        if r.stop_reason == "refusal":
            raise RuntimeError("the model declined that request")
        if r.stop_reason == "max_tokens":
            raise RuntimeError("the answer ran past its length limit; try a shorter description")
        text = next(b.text for b in r.content if b.type == "text")
        return r, json.loads(text)

    def pick_base(self, description, presets):
        catalogue = "\n".join(f"- {p['name']} (by {p['author']}): {p['description'].strip()}" for p in presets)
        names = [p["name"] for p in presets]
        schema = {
            "type": "object",
            "properties": {"base": {"type": "string", "enum": names}, "reason": {"type": "string"}},
            "required": ["base", "reason"],
            "additionalProperties": False,
        }
        prompt = (f"A player wants this minichord preset:\n\n{description}\n\n"
                  f"Which of these existing presets is the best one to start from and adapt? "
                  f"Pick the closest sound; the controls can all be reassigned.\n\n{catalogue}")
        _, out = self._call([{"role": "user", "content": prompt}], schema, "low",
                            "You know the minichord, a small handheld chord synthesizer, and its presets.")
        return out["base"], out["reason"]

    def design(self, request, values=None, notes=()):
        """The first call shows the whole preset; a revision sends only the request and any notes."""
        if values is not None:
            content = (f"The preset to start from:\n\n{listing(values, self.params)}\n\n"
                       f"What the player wants:\n\n{request}")
        else:
            content = request
            if notes:
                content = "The script adjusted your last changes:\n" + "\n".join(f"- {n}" for n in notes) + f"\n\n{request}"
        self.messages.append({"role": "user", "content": content})
        r, out = self._call(self.messages, output_schema(), self.effort, self.system)
        self.messages.append({"role": "assistant", "content": r.content})
        return out


# ---- the minichord ------------------------------------------------------------------------------

class Device:
    def __init__(self):
        import rtmidi
        self.out = rtmidi.MidiOut()
        for i, name in enumerate(self.out.get_ports()):
            if "minichord" in name and ("MIDI 1" in name or "Port 1" in name or name.strip() == "minichord"):
                self.out.open_port(i, "preset_maker")
                return
        raise RuntimeError("no minichord found: plug it in, and close minicontrol if it has the port")

    def set(self, address, value):
        value &= 0x3FFF
        self.out.send_message([0xF0, address % 128, address // 128, value % 128, value // 128, 0xF7])

    def apply(self, values):
        """Plays the preset on the live settings, as minicontrol's load does; nothing is saved."""
        for a in range(2, PARAMETER_SIZE):
            if a in RESERVED or a in LOCKED:
                continue
            self.set(a, values[a])
            if a % 32 == 0:
                time.sleep(0.005)
        self.out.send_message([0xF0, 0, 0, 0, 0, 0xF7])   # the minichord reports back, so an open editor follows

    def save(self, bank):
        self.out.send_message([0xF0, 0, 0, 2, bank - 1, 0xF7])

    def close(self):
        self.out.close_port()


# ---- the command --------------------------------------------------------------------------------

def report(out, made, notes, before, after, params):
    print(f"\n{out['name']}\n{out['summary']}\n")
    if out["controls"].strip():
        print("Controls:")
        for line in out["controls"].strip().splitlines():
            print(f"  {line.strip()}")
    print(f"\n{len(made)} settings changed:")
    show_changes(made, params, before, after)
    for n in notes:
        print(f"  ! {n}")
    if out["caveats"].strip():
        print(f"\nCheck by ear: {out['caveats'].strip()}")
    print(f"\nPreset code:\n{encode(after, params)}\n")


def resolve_base(arg, params, presets, version):
    if arg is None:
        return None, None
    if arg == "defaults":
        return default_values(params, version), "the parameters.json defaults"
    for p in presets:
        if p["name"].lower() == arg.lower():
            return decode(p["value"], params), p["name"]
    if os.path.isfile(arg):
        with open(arg) as f:
            arg = f.read()
    try:
        return decode(arg, params), "the code given"
    except ValueError:
        raise ValueError(f"--base {arg[:40]!r} isn't a preset code, a file holding one, 'defaults' "
                         f"or a shared preset's name (--list-bases lists them)")


def main():
    ap = argparse.ArgumentParser(description="A minichord preset from a description in words.")
    ap.add_argument("description", nargs="*", help="the sound and controls you want")
    ap.add_argument("--base", help="start from this: a preset code, a file holding one, a shared preset's "
                                   "name, or 'defaults' (the plain starting point used when none is given)")
    ap.add_argument("--claude", action="store_true",
                    help="use Claude over the internet instead of the built-in rules (needs an API key)")
    ap.add_argument("--model", default=MODEL, help="with --claude")
    ap.add_argument("--effort", default="high", choices=["low", "medium", "high", "xhigh", "max"], help="with --claude")
    ap.add_argument("--words", action="store_true", help="list the words and phrases the built-in rules know")
    ap.add_argument("--profile", action="store_true",
                    help="describe a bulk edit profile instead of a preset: a file minicontrol's bulk edit sheet "
                         "imports, setting the same values in every bank")
    ap.add_argument("--export-js", metavar="FILE",
                    help="write the vocabulary and the firmware's settings for minicontrol's describe.js")
    ap.add_argument("--name", help="the profile's name (or say \"call it ...\" in the description)")
    ap.add_argument("--out", help="where to write the profile file (default: its name, in this folder)")
    ap.add_argument("--apply", metavar="FILE", help="apply a JSON list of {address, value} changes, no model")
    ap.add_argument("--send", action="store_true", help="play each version on the minichord (live, not saved)")
    ap.add_argument("--save", type=int, metavar="BANK", help="save the final preset to bank 1-12 (asks first)")
    ap.add_argument("--once", action="store_true", help="make one preset and stop, without asking for revisions")
    ap.add_argument("--list-bases", action="store_true", help="list the shared presets that can be a base")
    ap.add_argument("--voice", action="store_true", help="speak the description and the revisions (typing still works)")
    ap.add_argument("--mic", help="the microphone for --voice, by name or number (see --list-mics)")
    ap.add_argument("--list-mics", action="store_true", help="list the inputs --mic can take")
    ap.add_argument("--whisper-model", default="base.en",
                    help="the speech model: tiny.en, base.en (default), small.en, medium.en; larger hears better, slower")
    args = ap.parse_args()

    params = load_parameters()
    version = firmware_version(params)
    presets = load_shared_presets()
    if args.list_mics:
        import voice
        voice.list_mics()
        return
    if args.export_js:
        import interpret
        with open(args.export_js, "w") as f:
            json.dump(interpret.export_data(params, version), f, separators=(",", ":"))
        print(f"Wrote {args.export_js}")
        return
    if args.words:
        import interpret
        interpret.print_vocabulary()
        return
    if args.list_bases:
        for p in presets:
            print(f"{p['name']}  ({p['author']}): {p['description'].strip()}")
        return
    if args.save is not None and not 1 <= args.save <= 12:
        ap.error("--save takes a bank from 1 to 12")

    values, base_name = resolve_base(args.base, params, presets, version)
    device = Device() if args.send or args.save else None

    if args.apply:
        if values is None:
            values, base_name = default_values(params, version), "the parameters.json defaults"
        with open(args.apply) as f:
            changes = json.load(f)
        if isinstance(changes, dict):
            changes = changes.get("changes", [])
        after, made, notes = apply_changes(values, changes, params)
        after[VERSION_ADDRESS] = version
        print(f"Base: {base_name}\n{len(made)} settings changed:")
        show_changes(made, params, values, after)
        for n in notes:
            print(f"  ! {n}")
        print(f"\nPreset code:\n{encode(after, params)}")
        finish(device, after, args.save)
        return

    import voice
    if args.voice:
        asker = voice.VoiceInput(args.mic, args.whisper_model)
    else:
        asker = voice.TextInput()
    description = " ".join(args.description).strip()
    if not description:
        description = asker.ask("Describe the preset", first=True)
        if not description:
            return
    if args.profile:
        profile_mode(description, params, presets, version, asker, args)
        return
    if not args.claude:
        offline(description, values, base_name, params, presets, version, asker, device, args)
        return
    designer = Designer(params, args.model, args.effort)
    if values is None:
        print("Choosing a preset to start from...")
        base_name, reason = designer.pick_base(description, presets)
        values = next(decode(p["value"], params) for p in presets if p["name"] == base_name)
        print(f"Starting from {base_name}: {reason}")
    else:
        print(f"Starting from {base_name}.")
    values[VERSION_ADDRESS] = version

    history = [values]
    print("Designing...")
    out = designer.design(description, values=values)
    undone = False
    while True:
        after, made, notes = apply_changes(history[-1], out["changes"], params)
        report(out, made, notes, history[-1], after, params)
        history.append(after)
        if device:
            device.apply(after)
            print("Playing on the minichord (not saved).")
        if args.once:
            break
        while True:
            request = asker.ask("What should change?")
            if request is None or request.lower() != "undo":
                break
            if len(history) > 2:
                history.pop()
                undone, notes = True, []
                print(f"\nBack to the previous version:\n{encode(history[-1], params)}\n")
                if device:
                    device.apply(history[-1])
            else:
                print("Nothing to undo.")
        if request is None:
            break
        if undone:
            request = "The player undid your last revision; the preset is as it was before it. " + request
            undone = False
        print("Revising...")
        out = designer.design(request, notes=notes)
    finish(device, history[-1], args.save)


def offline(description, values, base_name, params, presets, version, asker, device, args):
    """The built-in rules: nothing leaves the computer"""
    import interpret
    names = [p["name"] for p in presets]
    if values is None:
        values, base_name = default_values(params, version), "a plain starting point"
    values[VERSION_ADDRESS] = version
    history = [values]
    request = description
    while True:
        first = len(history) == 1
        result = interpret.interpret(request, history[-1], params, names, first=first)
        if result.base and first:
            base = next(p for p in presets if p["name"] == result.base)
            history[-1] = decode(base["value"], params)
            history[-1][VERSION_ADDRESS] = version
            base_name = result.base
            result = interpret.interpret(request, history[-1], params, names, first=first)
        if first:
            print(f"Starting from {base_name}.")
        after, made, notes = apply_changes(history[-1], result.changes, params)
        print("\nUnderstood:" if result.understood else "\nNothing in that matched a word I know.")
        for line in result.understood:
            print(f"  {line}")
        for heard, taken in result.heard_as:
            print(f"  (took \"{heard}\" as \"{taken}\")")
        if result.unknown:
            print(f"Not understood: {', '.join(result.unknown)}  (--words lists what I know)")
        for n in result.notes + notes:
            print(f"  ! {n}")
        if made:
            print(f"\n{len(made)} settings changed:")
            show_changes(made, params, history[-1], after)
            history.append(after)
            print(f"\nPreset code:\n{encode(after, params)}\n")
            if device:
                device.apply(after)
                print("Playing on the minichord (not saved).")
        if args.once:
            break
        while True:
            request = asker.ask("What should change?")
            if request is None or request.lower() != "undo":
                break
            if len(history) > 1:
                history.pop()
                print(f"\nBack to the previous version:\n{encode(history[-1], params)}\n")
                if device:
                    device.apply(history[-1])
            else:
                print("Nothing to undo.")
        if request is None:
            break
    finish(device, history[-1], args.save)


PROFILE_EDITS_MAX = 64


def profile_mode(description, params, presets, version, asker, args):
    """A bulk edit profile: the settings named, the same in every bank, for minicontrol's bulk edit sheet"""
    import interpret
    names = [p["name"] for p in presets]
    base = default_values(params, version)      # stands in for a bank while values are checked
    history = [{}]                              # address -> stored value, one dict a version
    name = args.name
    request = description
    while True:
        m = re.search(r"\b(?:call it|name it|named|called)\s+[\"']?([^\"'.,;]{1,40})", request, re.I)
        if m:
            name = m.group(1).strip()
            request = request[:m.start()] + request[m.end():]
        if not name:
            name = "Described profile"
        current = list(base)
        for a, v in history[-1].items():
            current[a] = v
        result = interpret.interpret(request, current, params, names, first=True, profile=True)
        after, made, notes = apply_changes(current, result.changes, params, report_all=True)
        edits = dict(history[-1])
        for a, old, new in made:
            edits[a] = new
        print("\nUnderstood:" if result.understood else "\nNothing in that matched a word I know.")
        for line in result.understood:
            print(f"  {line}")
        for heard, taken in result.heard_as:
            print(f"  (took \"{heard}\" as \"{taken}\")")
        if result.unknown:
            print(f"Not understood: {', '.join(result.unknown)}  (--words lists what I know)")
        for n in result.notes + notes:
            print(f"  ! {n}")
        if len(edits) > PROFILE_EDITS_MAX:
            print(f"  ! a profile holds at most {PROFILE_EDITS_MAX} settings; this has {len(edits)}, so it wasn't written")
        elif edits:
            if edits != history[-1]:
                history.append(edits)
            shown = list(base)
            for a, v in edits.items():
                shown[a] = v
            print(f"\nProfile \"{name}\", {len(edits)} settings, the same in every bank:")
            for a in sorted(edits):
                print(f"  {label(params[a])} = {show_value_for(params[a], edits[a], shown, params)}")
            path = write_profile(name, edits, args.out)
            print(f"\nWritten to {path}: in minicontrol, \"reorder and bulk edit\", then \"import a profile file\".\n")
        if args.once:
            break
        while True:
            request = asker.ask("What should change?")
            if request is None or request.lower() != "undo":
                break
            if len(history) > 1:
                history.pop()
                path = write_profile(name, history[-1], args.out)
                print(f"Back to the previous version ({len(history[-1])} settings), written to {path}")
            else:
                print("Nothing to undo.")
        if request is None:
            break


def write_profile(name, edits, out=None):
    name = name[:40]
    data = {"profiles": [{"name": name, "edits": [{"addr": a, "value": int(v)} for a, v in sorted(edits.items())
                                                  if a >= 2 and a not in RESERVED]}]}
    path = out or (re.sub(r"[^\w.-]+", "-", name).strip("-").lower() or "profile") + ".profile.json"
    with open(path, "w") as f:
        json.dump(data, f, indent=1)
    return path


def finish(device, values, bank):
    if device is None:
        return
    if bank is not None:
        answer = input(f"Save this preset to bank {bank}, replacing what is there? [y/N] ").strip().lower()
        if answer == "y":
            device.apply(values)
            time.sleep(0.2)
            device.save(bank)
            print(f"Saved to bank {bank}.")
        else:
            print("Not saved.")
    device.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
    except (RuntimeError, ValueError) as e:
        sys.exit(f"preset_maker: {e}")
    except Exception as e:
        if type(e).__name__ == "AuthenticationError":
            sys.exit("preset_maker: no working API key: set ANTHROPIC_API_KEY, or run `ant auth login`")
        if type(e).__module__.startswith("anthropic"):
            sys.exit(f"preset_maker: the API said: {e}")
        raise
