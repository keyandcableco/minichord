"""The minichord's settings by name: parameters.json, preset codes and the describer, borrowed from
preset_maker so the command line and the describer never disagree about a setting."""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(os.path.dirname(HERE))
PRESET_MAKER = os.path.join(FIRMWARE, "preset_maker")
if not os.path.isdir(PRESET_MAKER):
    raise SystemExit(f"minictl: can't find preset_maker at {PRESET_MAKER}. minictl runs from the minichord "
                     f"firmware checkout: install it with `pip install -e firmware/minictl`")
sys.path.insert(0, PRESET_MAKER)
import preset_maker as pm      # noqa: E402
import interpret               # noqa: E402

BANKS = 12
NAME_FIRST, NAME_SLOTS = 290, 12
RHYTHM_FIRST, RHYTHM_LAST = 220, 235
TARGETS = set(pm.KNOB_TARGETS) | set(pm.TAP_TARGETS)


# Names for the controls, shorter than parameters.json's (the generated name stays an alias)
RENAMES = {10: "chord-knob-alt", 11: "chord-knob-alt-range", 12: "harp-knob-alt", 13: "harp-knob-alt-range",
           14: "mod-knob", 15: "mod-knob-range", 16: "mod-knob-alt", 17: "mod-knob-alt-range",
           249: "hover", 250: "hover-value", 251: "hover-reach", 325: "hover-alt", 326: "hover-alt-value",
           200: "double-tap", 201: "double-tap-value", 209: "double-tap-2", 210: "double-tap-value-2",
           211: "double-tap-3", 212: "double-tap-value-3", 4: "chord-knob-memory", 5: "harp-knob-memory",
           6: "mod-knob-memory"}
ALIASES = {"key": 35, "volume-chords": 3, "volume-harp": 2, "bpm": None, "tempo": None}


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower().replace("#", " sharp ")).strip("-")


class Settings:
    def __init__(self):
        self.params = pm.load_parameters()
        self.version = pm.firmware_version(self.params)
        self._it = interpret.Interpreter(self.params)
        with open(os.path.join(HERE, "options.json")) as f:
            self.options = {int(a): {int(v): label for v, label in o.items()} for a, o in json.load(f).items()}
        self.names, self.aliases = {}, {}
        for a in sorted(self.params):
            name = slug(self._it.setting_name(a))
            if RHYTHM_FIRST <= a <= RHYTHM_LAST:
                name = f"rhythm-step-{a - RHYTHM_FIRST + 1}"
            self.names[a] = RENAMES.get(a, name)
            self.aliases[name] = a
        self.by_name = {n: a for a, n in self.names.items()}
        for n, a in ALIASES.items():
            if a is not None:
                self.aliases[n] = a
        tempo = [a for a, p in self.params.items() if p["name"].strip().lower() in ("bpm", "tempo", "rhythm bpm")]
        if len(tempo) == 1:
            self.aliases["bpm"] = self.aliases["tempo"] = tempo[0]

    # ---- naming ----

    def title(self, a):
        """the long, readable name: section / group / name"""
        return pm.label(self.params[a]) if a in self.params else f"address {a}"

    def find(self, word):
        """A setting from what was typed: an address, a name (reverb-size, "reverb size"), or the start of
        one if only one name starts so. Returns the address, or raises ValueError listing the candidates."""
        w = word.strip()
        if re.fullmatch(r"\d+", w):
            a = int(w)
            if not 0 <= a < pm.PARAMETER_SIZE:
                raise ValueError(f"{a} isn't an address (0-{pm.PARAMETER_SIZE - 1})")
            return a
        s = slug(w)
        if s in self.by_name:
            return self.by_name[s]
        if s in self.aliases:
            return self.aliases[s]
        # the start of a name, then whole words of one ("key" is chord-key-signature, not a keytrack),
        # then any part of one
        hits = [n for n in self.by_name if n.startswith(s)] or \
               [n for n in self.by_name if f"-{s}-" in f"-{n}-"] or [n for n in self.by_name if s in n]
        if len(hits) == 1:
            return self.by_name[hits[0]]
        if not hits:
            raise ValueError(f"no setting called {word!r} (`minictl list {s.split('-')[0]}` searches the names)")
        shown = ", ".join(sorted(hits)[:12]) + (" ..." if len(hits) > 12 else "")
        raise ValueError(f"{word!r} could be: {shown}")

    def search(self, words):
        """the settings whose names hold every word, then those whose help does"""
        ws = [slug(w) for w in words]
        named, helped = [], []
        for a, n in self.names.items():
            p = self.params[a]
            if all(w in f"{n} {slug(p['group'])}" for w in ws):
                named.append(a)
            elif all(w in slug(p["tooltip"]) for w in ws):
                helped.append(a)
        return named + helped

    # ---- values ----

    def shown(self, a, stored, values=None):
        """a stored value in the units a person reads: Hz as Hz, floats as floats, choices by name"""
        p = self.params.get(a)
        if p is None:
            return str(stored)
        if a in TARGETS:
            return "unused" if stored == 0 or stored not in self.params else self.names[stored]
        if p.get("follows_target") is not None and values is not None:
            t = self.params.get(values[p["follows_target"]])
            if t is not None:
                return self.shown(t["sysex_adress"], stored) if t["sysex_adress"] not in TARGETS else str(stored)
        if a in self.options and stored in self.options[a]:
            label = self.options[a][stored]
            return label if label == str(stored) else f"{stored} ({label})"
        h = pm.to_human(p, stored)
        return f"{h:g}" if isinstance(h, float) else str(h)

    def parse(self, a, text, values):
        """What was typed for setting a, in its own units, as the number preset_maker's checks take: a
        choice by its name, a target setting by its name, a float as a float."""
        t = text.strip()
        if a in TARGETS:
            if t.lower() in ("0", "none", "unused", "off", "nothing"):
                return 0
            return self.find(t)
        p = self.params.get(a)
        if p is not None and p.get("follows_target") is not None:
            target = values[p["follows_target"]] if values else None
            if target in self.options:
                a = target
        if a in self.options and not re.fullmatch(r"-?\d+(\.\d+)?", t):
            low = t.lower()
            hits = [v for v, label in self.options[a].items() if label.lower() == low] or \
                   [v for v, label in self.options[a].items() if label.lower().startswith(low)]
            if len(hits) == 1:
                return hits[0]
            choices = "; ".join(f"{v} {label}" for v, label in self.options[a].items())
            raise ValueError(f"{self.names.get(a, a)} takes one of: {choices}")
        if t.lower() in ("on", "yes", "true"):
            return 1
        if t.lower() in ("off", "no", "false"):
            return 0
        try:
            return float(t)
        except ValueError:
            raise ValueError(f"{t!r} isn't a value for {self.names.get(a, a)}")

    def describe_range(self, a):
        p = self.params[a]
        if a in TARGETS:
            return "the name of a setting, or none"
        if a in self.options:
            return "; ".join(f"{v} {label}" for v, label in self.options[a].items())
        unit = "" if p["data_type"] == "int" else " (decimal)"
        return f"{p['min_value']:g} to {p['max_value']:g}{unit}, default {p['default_value']:g}"

    # ---- names kept in a preset (firmware 50 and on) ----

    @staticmethod
    def preset_name(values):
        out = ""
        for k in range(NAME_SLOTS):
            v = values[NAME_FIRST + k] if NAME_FIRST + k < len(values) else 0
            for c in (v & 127, (v >> 7) & 127):
                if c < 32:
                    return out.strip()
                out += chr(c)
        return out.strip()

    @staticmethod
    def set_preset_name(values, name):
        import unicodedata
        s = unicodedata.normalize("NFD", name)
        s = "".join(c for c in s if 32 <= ord(c) < 127)
        s = re.sub(r"\s+", " ", s).strip()[:2 * NAME_SLOTS]
        for k in range(NAME_SLOTS):
            first = ord(s[2 * k]) if 2 * k < len(s) else 0
            second = ord(s[2 * k + 1]) if 2 * k + 1 < len(s) else 0
            values[NAME_FIRST + k] = first + 128 * second if first else 0

    # ---- codes and the describer ----

    def decode(self, code):
        return pm.decode(code, self.params)

    def encode(self, values):
        return pm.encode(values, self.params)

    def defaults(self):
        return pm.default_values(self.params, self.version)

    def apply(self, values, changes):
        """[(address, value in its units)] onto a copy of values, checked and clamped as preset_maker does.
        Returns (new values, [(address, old, new)], notes)."""
        return pm.apply_changes(values, [{"address": a, "value": v} for a, v in changes], self.params)

    def conflicts(self, values):
        """the setting conflicts that hold in a preset, from the describer's CONFLICTS table"""
        # in the description's own units, as the CONFLICTS rows are written (describe.js's conflicts() too)
        state = {a: pm.to_human(self.params[a], values[a]) if a in self.params else values[a]
                 for a in range(len(values))}
        return interpret.conflict_notes(self.params, self._it.selectors, {}, state, self._it.setting_name)

    def shared_presets(self):
        return pm.load_shared_presets()
