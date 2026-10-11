"""Instructions in words, for the live sound or for any banks: a port of minicontrol's commands.js.

"turn off hover", "assign the mod pot to key signature on banks 2 and 4", "set the chord attack to
200 ms", "raise the harp release a lot", "make bank 5 like bank 2", "reset the reverb on every bank".
The grammar of an instruction lives here: its verbs, which banks it is for, numbers and their units,
and the words a dropdown offers. What a setting is called, and every description of a sound
("warmer", "plucky harp", a famous song), is interpret.py's, as describe.js is its port: anything
this doesn't read as an instruction goes to the describer as it is, once for each bank it is meant
for, so "darker" is darker than what each bank holds.

Kept line for line with commands.js, so the page and the command line read every instruction
alike; tests/parity_test.py runs both over the same instructions. Where JavaScript and Python
differ (Math.round rounds halves up, a number prints as JavaScript prints it), the JavaScript is
followed. Nothing here talks to the minichord: plan() changes working copies of stored values, one
per place the words name ("live", or "b0".."b11"), and the caller shows and sends them.
"""
import base64, json, math, re, unicodedata

from .settings import pm, interpret

D = None          # describe_data.json, as interpret.export_data makes it
PARAMS = None     # address -> the setting
OPTIONS = {}      # address -> [{value, label}], the page's dropdown words
NAMES = None      # a setting's names -> its addresses, in order
SECTION = None    # a section word -> "chord" or "harp"
NAMER = None      # interpret's Interpreter, for setting names in conflict notes
SELECTORS = None

BANKS = 12
# 256 (the looper) and 287 (chord memory) are actions: writing one records, plays or stops, so
# nothing here may write them, in a place or in every bank
LOCKED = {0, 1, 2, 3, 4, 5, 6, 7, 241, 242, 243, 244, 256, 287, 382, 383, 510, 511}
ACTIONS = {256, 287}
RHYTHM_STEPS = (220, 235)
KNOBS = {"mod": [14, 15], "mod_alt": [16, 17], "chord_knob": [10, 11], "harp_knob": [12, 13]}
TAPS = [[200, 201], [209, 210], [211, 212]]
CONTROL_NAME = {"mod": "mod knob", "mod_alt": "modifier + mod knob", "chord_knob": "modifier + chord knob",
                "harp_knob": "modifier + harp knob", "hover": "hover", "tap": "double tap"}
# groups a bare name most likely means: "attack" is the envelope's before the filter's
CORE_GROUPS = {"Envelope", "Oscillator", "Effects", "General", "Notes", "Key and tuning", "Buttons",
               "Voicing", "Device", "MIDI", "Potentiometer", "Hover", "Double tap"}
NUMBER_WORDS = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8, "nine": 9,
                "ten": 10, "eleven": 11, "twelve": 12, "first": 1, "second": 2, "third": 3, "fourth": 4, "fifth": 5,
                "sixth": 6, "seventh": 7, "eighth": 8, "ninth": 9, "tenth": 10, "eleventh": 11, "twelfth": 12}


# ---- JavaScript's arithmetic and printing ----

def js_round(x):
    """Math.round: halves go up"""
    return math.floor(x + 0.5)


def js_num(v):
    """a number as JavaScript prints it: 2 not 2.0"""
    if isinstance(v, float) and v.is_integer():
        return str(int(v))
    return str(v) if isinstance(v, int) else repr(v)


def is_integer(v):
    return isinstance(v, int) or (isinstance(v, float) and v.is_integer())


def fmt(v):
    if is_integer(v):
        return str(int(v))
    return js_num(float(f"{v:.2f}"))


def setup(options=None, data=None):
    """data: describe_data.json's contents (interpret.export_data of this checkout by default);
    options: address -> {value: label}, the dropdowns' words"""
    global D, PARAMS, OPTIONS, NAMES, SECTION, NAMER, SELECTORS
    params = pm.load_parameters()
    D = data or json.loads(json.dumps(interpret.export_data(params, pm.firmware_version(params))))
    PARAMS = {int(a): p for a, p in D["params"].items()}
    OPTIONS = {int(a): [{"value": int(v), "label": label} for v, label in o.items()] for a, o in (options or {}).items()}
    SECTION = {}
    for s, ws in D["section_words"].items():
        for w in ws:
            SECTION[w] = s
    NAMES = {}

    def add(key, a):
        key = " ".join(words(key))
        if not key:
            return
        NAMES.setdefault(key, [])
        if a not in NAMES[key]:
            NAMES[key].append(a)

    for a in sorted(PARAMS):
        if not settable(a):
            continue
        p = PARAMS[a]
        n, g = p["name"].strip(), p["group"]
        add(n, a)
        add(f"{g} {n}", a)
        if p["section"] != "global":
            for s in (p["section"], p["section"] + "s"):
                add(f"{s} {n}", a)
                add(f"{s} {g} {n}", a)
    NAMER = interpret.Interpreter(params)
    SELECTORS = set(D["selectors"])


# ---- words ----

def normalise(text):
    t = unicodedata.normalize("NFKD", text)
    t = re.sub("[̀-ͯ]", "", t).lower()
    t = re.sub("[’‘]", "'", t)
    t = re.sub("[“”]", '"', t)
    t = re.sub(r"→|->|=>", " to ", t)
    t = t.replace("&", " and ").replace("+", " plus ")
    t = re.sub(r"(\d)\s*%", r"\1 percent ", t)
    t = t.replace("%", " percent ")
    t = re.sub(r"\b([a-g])\s*#", r"\1 sharp", t)
    t = re.sub(r"(\d)\s*-\s*(\d)", r"\1 to \2", t)
    t = re.sub(r"(?<=[a-z])-(?=[a-z])", " ", t)
    t = re.sub(r"'s\b", "", t).replace("'", "")
    return t


def words(text):
    return re.findall(r"@\d+|[a-z0-9#]+(?:\.\d+)?|\.\d+", text.lower())


def single(w):
    return w[:-1] if len(w) > 3 and w.endswith("s") and not w.endswith("ss") else w


def near(a, b):
    """Levenshtein within one, for a slip of the fingers on a long word"""
    if a == b:
        return True
    if len(a) < 5 or abs(len(a) - len(b)) > 1:
        return False
    i = j = edits = 0
    while i < len(a) and j < len(b):
        if a[i] == b[j]:
            i += 1
            j += 1
            continue
        edits += 1
        if edits > 1:
            return False
        if len(a) > len(b):
            i += 1
        elif len(b) > len(a):
            j += 1
        else:
            i += 1
            j += 1
    return edits + (len(a) - i) + (len(b) - j) <= 1


def settable(a):
    p = PARAMS.get(a)
    return bool(p) and a not in LOCKED and p["group"] != "hidden" and not (RHYTHM_STEPS[0] <= a <= RHYTHM_STEPS[1])


PLAIN_GROUPS = ["Effects", "General", "Notes", "Oscillator", "Envelope", "Settings", "Potentiometer", "Device",
                "Buttons", "Voicing", "Key and tuning", "MIDI", "Double tap", "Hover", "hidden", "Rythm"]


def name_of(a):
    """"chord attack", "harp low pass filter resonance": what a setting is called in what this says back"""
    p = PARAMS[a]
    name = re.sub(r"^default bpm$", "tempo", p["name"].strip().replace("_", " "))
    if p["group"] == "Rythm":
        return f"rhythm {name}"
    if p["group"] not in PLAIN_GROUPS and re.sub(r"s+$", "", p["group"].lower()) not in name.lower():
        name = f"{p['group'].lower()} {name}"
    if p["section"] == "global" or p["section"] in name.lower():
        return name
    return f"{p['section']} {name}"


# ---- values: the describer keeps stored values; words use the setting's own units ----

def units_of(a, values):
    """the setting a value is in: a double tap or hover value is in its target's units"""
    p = PARAMS[a]
    if p.get("follows_target") is None:
        return p
    t = values[p["follows_target"]]
    return PARAMS.get(t) or p


def human(a, values):
    p = units_of(a, values)
    v = values[a] or 0
    return js_round(v) / 100 if p["data_type"] == "float" else v


def is_time(p):
    n = (p["group"] + " " + p["name"]).lower()
    return p["data_type"] == "int" and p["max_value"] >= 250 and \
        bool(re.search(r"attack|hold|decay|release|delay length|glide|duration|palm mute release", n)) and \
        not re.search(r"frequency", n)


def is_selector(p):
    return p["data_type"] != "float" and p["max_value"] - p["min_value"] <= 30


def option_label(a, v):
    for o in OPTIONS.get(a, []):
        if o["value"] == v:
            return o["label"]
    return None


TARGET_ADDRESSES = {10, 12, 14, 16, 200, 209, 211, 249}


def shown(a, v, values):
    if a in TARGET_ADDRESSES:
        return name_of(v) if v else "nothing"
    label = option_label(a, v)
    if label:
        return label
    p = units_of(a, values)
    if p["min_value"] >= 1000 and re.search(r"tuning", p["name"]):
        return fmt(v / 10) + " Hz"
    return fmt(v) + (" ms" if is_time(p) else " bpm" if re.search(r" bpm$|default_bpm", p["name"]) else "")


def key_choice(text, a):
    """A key name to the key signature's choice: "e flat" is Eb; a minor key is its relative major's"""
    m = re.match(r"^([a-g])\s*(sharp|flat|#|b)?(?:\s+(major|minor|maj|min|m))?$", text)
    if not m:
        return None
    opts = OPTIONS.get(a, [])
    acc = "#" if m.group(2) in ("sharp", "#") else "b" if m.group(2) in ("flat", "b") else ""
    if (m.group(3) and m.group(3).startswith("min")) or m.group(3) == "m":
        semis = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}[m.group(1)] + (1 if acc == "#" else -1 if acc == "b" else 0)
        major = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"][((semis + 3) % 12 + 12) % 12]
        o = next((x for x in opts if x["label"] == major), None)
        return o["value"] if o else None
    want = m.group(1).upper() + acc
    o = next((x for x in opts if x["label"] == want), None)
    return o["value"] if o else None


def _num(s):
    """parseFloat of a whole number-ish string: an int when it is one, as JavaScript can't tell"""
    v = float(s)
    return int(v) if v.is_integer() else v


def parse_value(text, a, values):
    """Words for a value of setting a, in its units: a number with or without units, a choice its
    dropdown offers, or max, min, half, default, on and off. {"value"} or {"error"}, or None."""
    p = units_of(a, values)
    t = re.sub(r"^(?:to|at|=|be|is|of|around|about|roughly|exactly|equal to)\s+", "", text)
    t = re.sub(r"\s+(?:please|instead|now)$", "", t)
    t = re.sub(r"^(?:the|a|an)\s+", "", t)
    t = re.sub(r"\s+(?:one|mode|setting|option|choice)$", "", t).strip()
    if not t:
        return None
    lo, hi = p["min_value"], p["max_value"]
    if re.match(r"^(?:max|maximum|full|fully|all the way(?: up)?|highest|the most|top|100 percent)$", t):
        return {"value": hi}
    if re.match(r"^(?:min|minimum|lowest|the least|bottom|all the way down)$", t):
        return {"value": lo}
    if re.match(r"^(?:zero|nothing|none|0 percent)$", t):
        return {"value": max(lo, 0)}
    if re.match(r"^(?:half|halfway|half way|the middle|middle|centre|center|mid)$", t):
        return {"value": (lo + hi) / 2}
    if re.match(r"^(?:default|its default|the default|factory|normal|stock|original)(?: value| setting)?$", t):
        return {"value": p["default_value"]}
    if hi - lo == 1 and p["data_type"] != "float":
        if re.match(r"^(?:on|yes|true|enabled?|active)$", t):
            return {"value": D["on_value"][str(a)] if str(a) in D["on_value"] else hi}
        if re.match(r"^(?:off|no|false|disabled?|inactive)$", t):
            return {"value": lo}
        # a choice of two: "the alternate one", "the standard one"
        if re.match(r"^(?:alt|alternate|alternative|other|second|2nd)$", t):
            return {"value": hi}
        if re.match(r"^(?:standard|regular|usual|first|1st|classic)$", t):
            return {"value": lo}
    if re.match(r"^(?:off|silent)$", t):
        return {"value": max(lo, 0)}
    if a == 20 and D.get("colors") and D["colors"].get(t) is not None:
        return {"value": D["colors"][t]}
    # a dropdown's own words, or a key, or a waveform's name as the describer knows them
    if units_of(a, values) is p:
        oa = a if PARAMS[a] is p else values[PARAMS[a]["follows_target"]]
    else:
        oa = a
    opts = OPTIONS.get(oa, [])
    if re.search(r"key signature", p["name"]):
        k = key_choice(re.sub(r"^(?:the\s+)?key\s+of\s+", "", t), a)
        if k is not None:
            return {"value": k}
    if re.search(r"waveform", p["name"]) and D.get("wave_names"):
        w = D["wave_names"].get(t)
        if w is None:
            w = D["wave_names"].get(re.sub(r"\s+wave$", "", t))
        if w is not None:
            return {"value": w}
    if opts:
        tw = [single(w) for w in words(t)]
        exact = next((o for o in opts if " ".join(single(w) for w in words(o["label"])) == " ".join(tw)), None)
        if exact:
            return {"value": exact["value"]}
        partial = [o for o in opts if all(any(near(w, x) for x in [single(y) for y in words(o["label"])]) for w in tw)]
        if len(partial) == 1:
            return {"value": partial[0]["value"]}
        if len(partial) > 1 and not re.match(r"^\d", t):
            return {"error": f'"{t}" could be {", ".join(o["label"] for o in partial)}; say which'}
    m = re.match(r"^(-?\d*\.?\d+)\s*(percent|per cent|pc|ms|milliseconds?|millis|s|secs?|seconds?|hz|hertz|khz|kilohertz|"
                 r"cm|centimet(?:er|re)s?|bpm|semitones?|steps?|octaves?|cents?)?$", t)
    if not m:
        return None
    v = _num(m.group(1))
    unit = m.group(2) or ""
    if unit.startswith("per") or unit == "pc":
        v = lo + (hi - lo) * v / 100
    elif re.match(r"^(?:s|secs?|seconds?)$", unit):
        v = v * 1000 if is_time(p) else v
    elif re.match(r"^(?:khz|kilohertz)$", unit):
        v *= 1000
    elif not unit:
        # tuning is kept in tenths of a hertz; a float setting given a number over its top is a percentage
        if lo >= 1000 and lo <= v * 10 <= hi:
            v *= 10
        elif p["data_type"] == "float" and v > hi and v <= 100 and hi <= 5:
            v = lo + (hi - lo) * v / 100
    if opts and not is_integer(v):
        v = js_round(v)
    return {"value": v}


# ---- which banks: "on banks 2 and 4", "every bank but 3", "this bank", "from bank 6" ----

BANKNUM = ("(?:1[0-2]|[1-9]|one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|first|second|third|"
           "fourth|fifth|sixth|seventh|eighth|ninth|tenth|eleventh|twelfth)")
BANKLIST = rf"{BANKNUM}(?:\s*(?:,\s*(?:and\s+)?|and|or|to|through|thru|until)\s*{BANKNUM})*"
PREP = r"(?:(on|in|for|to|into|onto|across|at|from|of|with)\s+)?(?:the\s+|my\s+)?"
SCOPE_PATTERNS = [
    ("all", re.compile(rf"\b{PREP}(?:(?:all|every|each)\s+(?:(?:of\s+)?(?:the|my)\s+)?(?:twelve\s+|12\s+)?(?:banks?|presets?|slots?)|(?:all|every|each)\s+(?:twelve|12)|(?:banks?|presets?)\s+all)"
                       rf"(?:\s+(?:except|but(?:\s+not)?|apart\s+from|other\s+than|besides|excluding)\s+(?:(?:for\s+)?(?:banks?|presets?)\s+)?({BANKLIST}))?")),
    ("list", re.compile(rf"\b{PREP}(?:banks?|presets?|slots?)\s+(?:number\s+|numbers\s+|#\s*)?({BANKLIST})\b")),
    ("ordinal", re.compile(rf"\b{PREP}({BANKLIST})(?:st|nd|rd|th)?\s+(?:and\s+({BANKLIST})(?:st|nd|rd|th)?\s+)?(?:banks?|presets?|slots?)\b")),
    ("this", re.compile(r"\b(?:(on|in|for|to|into|onto|at|from|of)\s+)?(?:this|the\s+current|current|the\s+active|active|the\s+loaded|my\s+current|the\s+selected)\s+(?:bank|slot|saved\s+preset)\b")),
    ("live", re.compile(r"\b(?:(on|in|for|to|into|onto|at|from|of)\s+)?(?:the\s+)?(?:live(?:\s+(?:sound|settings?|preset|edits?))?|current\s+sound|sound\s+(?:i(?:m|\s+am)\s+)?(?:hearing|playing)\s+now|what\s+(?:is|s)\s+playing(?:\s+now)?|right\s+now)\b")),
]


def bank_numbers(lst):
    toks = [t for t in re.split(r"\s+", lst.replace(",", " , ")) if t]
    out = []
    prev, rng = None, False
    for t in toks:
        if re.match(r"^(?:to|through|thru|until)$", t):
            rng = True
            continue
        if not re.match(r"^\d+$", t) and t not in NUMBER_WORDS:
            continue
        n = int(t) if re.match(r"^\d+$", t) else NUMBER_WORDS[t]
        if rng and prev is not None:
            out += range(min(prev, n) + 1, max(prev, n))
        out.append(n)
        prev, rng = n, False
    seen = []
    for n in out:
        if n not in seen:
            seen.append(n)
    return [n - 1 for n in seen if 1 <= n <= BANKS]


def spaced(name):
    """a name as a pattern, any run of spaces between its words"""
    return r"\s+".join(re.escape(w) for w in name.split(" "))


def mark_scopes(text, current_bank, bank_names):
    """Each place words name, taken out of the text and left as a marker «n», so a list like
    "2, 4 and 6" isn't split into clauses. A place is {keys: ["live"] or ["b1", "b3"], from, text}."""
    scopes = []
    # a bank by the name typed for it: "my Drone bank", "the bank called Drone", "bank Drone"
    for b, name in enumerate(bank_names or []):
        n = " ".join(words(normalise(name))) if name else ""
        if not n or re.match(r"^\d+$", n):
            continue
        nm = spaced(n)
        pattern = re.compile(rf"\b{PREP}(?:(?:banks?|presets?|slots?)\s+(?:called\s+|named\s+)?{nm}|{nm}\s+(?:bank|slot))\b")

        def by_name(m, b=b):
            prep = m.group(1)
            scopes.append({"keys": [f"b{b}"], "from": prep in ("from", "of", "with"), "text": m.group(0).strip()})
            return f" «{len(scopes) - 1}» "
        text = pattern.sub(by_name, text)
    for kind, pattern in SCOPE_PATTERNS:
        def place(m, kind=kind):
            groups = m.groups()
            prep = groups[0]
            a = groups[1] if len(groups) > 1 else None
            b = groups[2] if len(groups) > 2 else None
            if kind == "all":
                except_ = set(bank_numbers(a) if a else [])
                keys = [f"b{n}" for n in range(BANKS) if n not in except_]
            elif kind == "list":
                keys = [f"b{n}" for n in bank_numbers(a)]
            elif kind == "ordinal":
                keys = [f"b{n}" for n in bank_numbers(a + (" , " + b if b else ""))]
            elif kind == "this":
                keys = [f"b{current_bank}"] if current_bank >= 0 else ["live"]
            else:
                keys = ["live"]
            if not keys:
                return m.group(0)
            scopes.append({"keys": keys, "from": prep in ("from", "of", "with"), "text": m.group(0).strip()})
            return f" «{len(scopes) - 1}» "
        text = pattern.sub(place, text)
    return text, scopes


def scope_name(keys):
    if len(keys) == 1 and keys[0] == "live":
        return "live"
    n = [int(k[1:]) + 1 for k in keys]
    if len(n) == BANKS:
        return "every bank"
    return ("bank " if len(n) == 1 else "banks ") + \
        (", ".join(map(str, n[:-1])) + " and " + str(n[-1]) if len(n) > 1 else str(n[0]))


# ---- clauses ----

VERB = ("(?:turn|switch|set|make|put|assign|map|route|link|unassign|clear|disable|enable|activate|deactivate|"
        "raise|lower|increase|decrease|reduce|boost|bump|double|halve|reset|restore|revert|copy|duplicate|change|"
        "give|remove|mute|unmute|kill|bring|drop|use|have|let|crank|shorten|lengthen|cut|add)")
LEAD = re.compile(rf"^(?:(?:and|also|then|now|so|ok|okay|please|plus|next|finally|lastly|first|after that|and then|can you|could you|would you|will you|i want(?: to)?|i d like(?: to)?|id like(?: to)?|i would like(?: to)?|lets|let us|go ahead and|try|maybe|just(?=\s+{VERB}\b))\s+)+")
TAIL = re.compile(r"\s+(?:please|too|as well|for me|thanks|thank you|if you can|instead)$")
VERB_START = re.compile(rf"^{VERB}\b")


def clauses(text):
    """Sentences, then clauses at commas, semicolons and "then", and at an "and" that starts a new
    instruction ("turn off hover and set the reverb to 40"); an "and" inside a description stays."""
    out = []
    for sentence in re.split(r"(?<!\d)\.(?!\d)|[;!?\n]+", text):
        parts = re.split(r",|\bthen\b|\band also\b|\bafter that\b", sentence)
        sentence_out = []
        for part in parts:
            for piece in re.split(rf"\band\s+(?=(?:also\s+|then\s+|please\s+)?{VERB}\b)", part):
                if piece.strip():
                    sentence_out.append(piece.strip())
        if sentence_out:
            out.append(sentence_out)
    return out


# ---- naming a setting: "the chord attack", "harp filter resonance", "key signature" ----

CONTROL_RE = [
    ("mod_alt", re.compile(r"\b(?:(?:the|my)\s+)?(?:alt(?:ernate)?|second(?:ary)?|shift(?:ed)?|modifier(?:\s+plus)?|modified|other)\s+mod(?:ulation)?(?:\s+(?:knob|pot|potentiometer|wheel|dial|control))?\b|\bmod(?:ulation)?\s+(?:knob|pot|potentiometer|dial)\s+(?:alt(?:ernate)?|with\s+(?:the\s+)?modifier|shifted)\b")),
    ("mod", re.compile(r"\b(?:(?:the|my)\s+)?(?:mod(?:ulation)?\s+(?:knob|pot|potentiometer|wheel|dial|control)|mod)\b")),
    ("chord_knob", re.compile(r"\b(?:(?:the|my)\s+)?(?:(?:alt(?:ernate)?|modifier(?:\s+plus)?|shift(?:ed)?)\s+)?chords?\s+(?:knob|pot|potentiometer|dial)\b")),
    ("harp_knob", re.compile(r"\b(?:(?:the|my)\s+)?(?:(?:alt(?:ernate)?|modifier(?:\s+plus)?|shift(?:ed)?)\s+)?harp\s+(?:knob|pot|potentiometer|dial)\b")),
    ("hover", re.compile(r"\b(?:(?:the|my)\s+)?(?:hover(?:ing)?|proximity)(?:\s+(?:control|sensor|sensing|assignment|target))?\b(?!\s+(?:reach|distance|range|height|value))")),
    ("tap", re.compile(r"\b(?:(?:the|my)\s+)?(?:(first|second|third|1st|2nd|3rd)\s+)?double\s*tap(?:\s+(?:slot\s+)?(1|2|3|one|two|three))?(?:\s+(?:control|slot|assignment))?\b")),
]
SLOTS = {"first": 0, "1st": 0, "1": 0, "one": 0, "second": 1, "2nd": 1, "2": 1, "two": 1, "third": 2, "3rd": 2,
         "3": 2, "three": 2}


def find_control(s):
    for kind, pattern in CONTROL_RE:
        m = pattern.search(s)
        if not m:
            continue
        slot = None
        if kind == "tap":
            w = m.group(1) or m.group(2)
            if w:
                slot = SLOTS[w]
        return {"kind": kind, "slot": slot, "index": m.start(), "length": len(m.group(0)), "text": m.group(0)}
    return None


ARTICLES = re.compile(r"^(?:the|my|a|an|its|their|our|this|that|some|all the|all of the|both|both the)\s+")


def strip_articles(s):
    """JavaScript's s.replace(ARTICLES, ""): once"""
    return ARTICLES.sub("", s, count=1)


def resolve(phrase, section=None):
    """A setting's name to its addresses: {"addrs"} when it is clear, {"choices"} when it could be
    several, None when it names none. section narrows it ("chord", "harp")."""
    s = re.sub(r"\s+(?:setting|settings|parameter|param|amount|knob)$", "", phrase.strip()).strip()
    while ARTICLES.search(s):
        s = strip_articles(s)
    if not s:
        return None
    direct = re.match(r"^@(\d+)$", s)
    if direct and settable(int(direct.group(1))):
        return {"addrs": [int(direct.group(1))]}
    toks = words(s)
    # a section named in the phrase: "harp release", "the release on the chords", "chords and harp"
    sections = []
    rest = []
    whole_key = " ".join(toks)
    i = 0
    while i < len(toks):
        two = " ".join(toks[i:i + 2])
        if two in SECTION and whole_key not in NAMES:
            if SECTION[two] not in sections:
                sections.append(SECTION[two])
            i += 2
            continue
        if toks[i] in SECTION and whole_key not in NAMES and not re.match(r"^(?:string|strings|strum)$", toks[i]):
            if SECTION[toks[i]] not in sections:
                sections.append(SECTION[toks[i]])
            i += 1
            continue
        if re.match(r"^(?:on|in|for|of|the|and|both)$", toks[i]):
            i += 1
            continue
        rest.append(toks[i])
        i += 1
    if section and not sections:
        sections.append(section)

    def in_section(a):
        return not sections or PARAMS[a]["section"] == "global" or PARAMS[a]["section"] in sections

    key = " ".join(rest)
    # the describer's words for a setting: "filter" is the cutoff, "echo" the delay, "attack" the envelope's
    role = target_role(key) if key else None
    # the looper (256) and chord memory (287) are actions, not settings: the describer gives a control
    # to one, with the step its double tap takes, so neither is read as one of their settings
    if role and D["roles"].get(role) and any(a in ACTIONS for a in D["roles"][role].values()):
        return None
    if role and D["roles"].get(role):
        where = D["roles"][role]
        if "global" in where:
            addrs = [where["global"]]
        else:
            addrs = [a for sec, a in where.items() if not sections or sec in sections]
        addrs = [a for a in addrs if a is not None and settable(a)]
        if addrs:
            return {"addrs": addrs}
    # the whole name as the page has it
    whole = NAMES.get(whole_key)
    if whole:
        addrs = [a for a in whole if in_section(a)]
        if addrs:
            return pick(addrs, sections)
    if not rest:
        return None
    named = NAMES.get(key)
    if named:
        addrs = [a for a in named if in_section(a)]
        if addrs:
            return pick(addrs, sections)
    # every word found in a setting's section, group and name; the fewest words left over wins
    want = [single(w) for w in rest]
    best, best_score = [], math.inf
    for a in sorted(PARAMS):
        if not settable(a) or not in_section(a):
            continue
        p = PARAMS[a]
        have = [single(w) for w in words(f"{p['section']} {p['group']} {p['name']}")]
        if not all(any(near(w, h) for h in have) for w in want):
            continue
        score = len([h for h in have if h not in want and h != p["section"] and h != "global"]) + \
            (0 if p["group"] in CORE_GROUPS else 1.5)
        if score < best_score:
            best, best_score = [a], score
        elif score == best_score:
            best.append(a)
    if not best:
        return None
    # "the midi channel for the chords": a global setting named for a section
    if len(sections) == 1 and len(best) > 1:
        sec = sections[0]
        named = [a for a in best if sec in PARAMS[a]["name"].lower()]
        if named:
            best = named
    return pick(best, sections)


def group_addrs(phrase):
    """Every setting in a group: "the envelope", "the harp low pass filter", "the vibrato" """
    toks = words(strip_articles(phrase))
    sections = set()
    kept = []
    for t in toks:
        if t in SECTION and not re.match(r"^(?:string|strings|strum)$", t):
            sections.add(SECTION[t])
        else:
            kept.append(t)
    want = " ".join(single(t) for t in kept)
    if not want:
        return None
    addrs = [a for a in sorted(PARAMS) if settable(a) and " ".join(single(w) for w in words(PARAMS[a]["group"])) == want
             and (not sections or PARAMS[a]["section"] in sections)]
    return addrs if len(addrs) > 1 else None


def target_role(key):
    for role, lst in D["targets"].items():
        if any(w.strip() == key for w in lst.split(",")):
            return role
    return None


def pick(addrs, sections):
    """One setting, or the chords' and the harp's of the same name together; otherwise a question"""
    if len(addrs) == 1:
        return {"addrs": addrs}
    # the envelope's attack before the filter's or the vibrato's
    core = [a for a in addrs if PARAMS[a]["group"] in CORE_GROUPS]
    if core and len(core) < len(addrs):
        return pick(core, sections)
    ps = [PARAMS[a] for a in addrs]
    twins = len(addrs) == 2 and ps[0]["name"] == ps[1]["name"] and ps[0]["group"] == ps[1]["group"] and \
        len({p["section"] for p in ps}) == 2 and all(p["section"] in ("chord", "harp") for p in ps)
    if twins:
        return {"addrs": sorted(addrs, key=lambda x: PARAMS[x]["section"])}
    return {"choices": addrs[:8]}


# ---- the instructions ----

UP = "raise|increase|boost|bump up|bump|turn up|bring up|push up|push|crank up|crank|lengthen|extend|up|higher|more|add more|add"
DOWN = "lower|decrease|reduce|turn down|bring down|cut back|cut|drop|shorten|lessen|dial back|back off|tame|less|down"
COMPARE_UP = "longer|higher|louder|bigger|faster|wider|stronger|deeper|brighter|more"
COMPARE_DOWN = "shorter|lower|quieter|softer|smaller|slower|narrower|weaker|shallower|darker|less"
MUCH = {"a bit": 0.5, "a little": 0.5, "a touch": 0.5, "slightly": 0.5, "a tad": 0.5, "a little bit": 0.5,
        "a lot": 2, "much": 2, "way": 2, "loads": 2, "heaps": 2, "a ton": 2, "way more": 2, "lots": 2}
MUCH_RE = "(a little bit|a little|a bit|a touch|a tad|slightly|a lot|much|way|loads|heaps|a ton|lots)"
UP_RE = re.compile(rf"^(?:make\s+)?({UP}|{DOWN})\s+(?:the\s+)?(.+?)(?:\s+by\s+(.+?))?(?:\s+{MUCH_RE})?$")
AFTER_RE = re.compile(rf"^(?:turn\s+|bring\s+|push\s+|move\s+)?(.+?)\s+(up|down|higher|lower)(?:\s+by\s+(.+?))?(?:\s+{MUCH_RE})?$")
COMPARE_RE = re.compile(rf"^(?:make|give|set)\s+(?:the\s+)?(.+?)\s+(?:{MUCH_RE}\s+)?({COMPARE_UP}|{COMPARE_DOWN})$")


def to_human_all(values):
    return {a: pm.to_human(PARAMS[a], values[a] if a < len(values) and values[a] is not None else 0) for a in PARAMS}


def conflicts(before, after):
    """the conflicts a change of stored values brings about"""
    return interpret.conflict_notes(NAMER.params, SELECTORS, to_human_all(before), to_human_all(after), NAMER.setting_name)


def describe_interpret(text, values, first=False, preset_names=()):
    r = interpret.interpret(text, list(values), NAMER.params, list(preset_names), first=first)
    return r


class Question:
    def __init__(self, phrase, keys, clause, choices):
        self.phrase, self.keys, self.clause, self.choices = phrase, keys, clause, choices


class Context:
    """values(key) -> the stored values of a place, changed in place; why(key, addr, reason)"""

    def __init__(self, values, current_bank=-1, preset_names=(), presets=(), bank_names=(), why=None):
        self.values = values
        self.current_bank = current_bank
        self.preset_names = list(preset_names)
        self.presets = list(presets)
        self.bank_names = list(bank_names)
        self.why = why or (lambda key, a, reason: None)


class Planner:
    def __init__(self, ctx):
        self.ctx = ctx
        self.out = {"understood": [], "notes": [], "unknown": [], "questions": [], "heard_as": []}

    def say(self, keys, text):
        where = "" if len(keys) == 1 and keys[0] == "live" else scope_name(keys) + ": "
        self.out["understood"].append(where + text)

    def note(self, text):
        if text not in self.out["notes"]:
            self.out["notes"].append(text)

    def ask(self, phrase, keys, clause, addrs):
        self.out["questions"].append(Question(phrase, keys, clause, [{"addr": a, "name": name_of(a)} for a in addrs]))

    def change(self, keys, changes, reason):
        """Changes in the words' units, through apply_changes, in every place named"""
        made = []
        for key in keys:
            values = self.ctx.values(key)
            before = list(values)
            _, r_made, r_notes = pm.apply_changes(values, changes, NAMER.params, report_all=True)
            for n in r_notes:
                self.note(n)
            for a, _, v in r_made:
                values[a] = v
                self.ctx.why(key, a, reason)
            for n in conflicts(before, values):   # what the change runs into
                self.note(n)
            made += r_made
        return made

    def run(self, text):
        marked, scopes = mark_scopes(self.mark_presets(normalise(text)), self.ctx.current_bank, self.ctx.bank_names)
        self.scopes = scopes
        for sentence in clauses(marked):
            # a place that starts a clause goes with the clauses after it, "on bank 2, turn off hover,
            # raise the reverb"; one that ends a sentence, with all of it: "turn off hover, raise the
            # reverb on bank 2"; one that ends another clause, with that clause alone
            own = [self.places_in(c) for c in sentence]
            first_placed = next((i for i, o in enumerate(own) if o["to"]), -1)
            at_end = first_placed == len(own) - 1 and bool(re.search(r"»\s*$", sentence[first_placed]))
            last = own[first_placed]["to"] if at_end else None
            pending = []
            last_verb = None

            def flush():
                if not pending:
                    return
                keys = pending[0]["keys"]
                self.describe_text(", ".join(p["text"] for p in pending), keys)
                pending.clear()

            for i, c in enumerate(sentence):
                places = own[i]
                copying = bool(re.search(r"^(?:copy|duplicate|clone|transfer|bring|paste|take)\b|\b(?:like|same as|match|copy of)\b", c))
                if not places["to"] and places["from"] and not copying:
                    places["to"] = places["from"]
                keys = places["to"] or last or ["live"]
                # "on bank 2, …" carries on to what follows; "… on bank 2" stays with its own clause
                if places["to"] and re.match(r"^\s*«\d+»", c):
                    last = places["to"]
                s = re.sub(r"\s+", " ", re.sub(r"«\d+»", " ", c)).strip()
                s = TAIL.sub("", LEAD.sub("", s, count=1), count=1).strip()
                if not s and not places["copy"]:
                    continue
                done = self.preset(s, keys)
                if not done:
                    s = self.unmark_presets(s)
                    done = self.command(s, keys, places, last_verb)
                if done:
                    flush()
                    last_verb = None if done is True else done
                    continue
                last_verb = None
                if pending and ",".join(pending[0]["keys"]) != ",".join(keys):
                    flush()
                pending.append({"text": s, "keys": keys})
            flush()
        return self.out

    def places_in(self, c):
        marks = [self.scopes[int(m)] for m in re.findall(r"«(\d+)»", c)]
        to = [m for m in marks if not m["from"]]
        frm = [m for m in marks if m["from"]]
        to_keys = []
        for m in to:
            for k in m["keys"]:
                if k not in to_keys:
                    to_keys.append(k)
        return {"to": to_keys or None, "from": frm[0]["keys"] if frm else None,
                "copy": len(marks) >= 2 or len(frm) > 0 or (len(to) == 1 and len(to[0]["keys"]) == 2), "marks": marks}

    def describe_text(self, text, keys):
        """A description, as the describer reads it, over each place's own values"""
        said = None
        for key in keys:
            values = self.ctx.values(key)
            t = re.sub(r"(\d)\s+percent\b", r"\1%", text)
            r = describe_interpret(t, values, False, self.ctx.preset_names)
            base_name, base = None, values
            if r.base:
                shared = next((p for p in self.ctx.presets if p["name"] == r.base), None) or \
                    next((p for p in pm.load_shared_presets() if p["name"] == r.base), None)
                if shared:
                    base = pm.decode(shared["value"], NAMER.params)
                    base[pm.VERSION_ADDRESS] = D["firmware_version"]
                    base_name = r.base
                    r = describe_interpret(t, base, True, self.ctx.preset_names)
            nxt, _, applied_notes = pm.apply_changes(base, r.changes, NAMER.params)
            r.notes = r.notes + applied_notes
            r.base = base_name
            reasons = {c["address"]: c.get("reason") for c in r.changes}
            for a in range(2, len(nxt)):
                if a == 7 or nxt[a] == values[a] or a in LOCKED:
                    continue
                values[a] = nxt[a]
                self.ctx.why(key, a, reasons.get(a) or (f"from {r.base}" if r.base else "described"))
            if said is None:
                said = r
        if said is None:
            return
        if said.base:
            self.say(keys, f"starting from {said.base}")
        for u in said.understood:
            self.say(keys, u)
        for n in said.notes:
            self.note(n)
        for w in said.unknown:
            if w not in self.out["unknown"] and not re.fullmatch(VERB, w):
                self.out["unknown"].append(w)
        self.out["heard_as"] += list(said.heard_as)

    def command(self, s, keys, places, last_verb):
        """One clause as an instruction. True (or the verb, for a list that carries on in the next
        clause) when it was one; False to hand it to the describer."""
        if places["copy"] and self.copy(s, keys, places):
            return True
        # a list that carries on: "turn off the reverb, the delay and the chorus"
        if last_verb and not VERB_START.search(s):
            items = self.items(s)
            if items and all(it.get("addrs") or it.get("control") for it in items):
                return self.command(f"{last_verb} {s}", keys, places, None)
        # two settings in one: "set the delay length to 300 and the delay mix to 30 percent"
        m = re.match(r"^(.+?\s(?:to|at|=)\s.+?)\s+and\s+(.+?\s(?:to|at|=)\s.+)$", s)
        if m and not find_control(s):
            lead = re.match(rf"^({VERB})\s", m.group(1))
            right = f"{lead.group(1)} {m.group(2)}" if lead and not VERB_START.search(m.group(2)) else m.group(2)
            if self.command(m.group(1), keys, places, None):
                if not self.command(right, keys, places, None):
                    self.describe_text(right, keys)
                return True
        if self.control(s, keys):
            return True
        # muting a section: "mute the harp", "unmute the chords"
        m = re.match(r"^(mute|silence|unmute)\s+(?:the\s+)?(.+)$", s)
        if m and m.group(2) in SECTION and D["roles"].get("level"):
            sec = SECTION[m.group(2)]
            a = D["roles"]["level"].get(sec)
            if a is not None:
                on = m.group(1) == "unmute"
                self.change(keys, [{"address": a, "value": PARAMS[a]["default_value"] if on else 0,
                                    "reason": "unmuted" if on else "muted"}], "unmuted" if on else "muted")
                self.say(keys, f"{name_of(a)} → {'default' if on else '0'}")
                return True
        # reset
        m = re.match(r"^(?:reset|restore|revert|default)\s+(.+?)(?:\s+(?:to|back to)\s+(?:its\s+|their\s+|the\s+)?(?:defaults?|factory(?:\s+settings?)?|normal|original|stock)(?:\s+(?:values?|settings?))?)?$", s) or \
            re.match(r"^(?:put|set|bring|turn|change)\s+(.+?)\s+back(?:\s+to\s+(?:its\s+|their\s+|the\s+)?(?:defaults?|normal|original|factory|stock)(?:\s+(?:values?|settings?))?)?$", s) or \
            re.match(r"^(?:set\s+|put\s+|return\s+)?(.+?)\s+(?:back\s+)?to\s+(?:its\s+|their\s+|the\s+)?(?:defaults?|factory\s+settings?|factory)(?:\s+(?:values?|settings?))?$", s)
        if m:
            return "reset" if self.reset(m.group(1), keys) else False
        # off and on
        m = re.match(r"^(?:turn|switch|shut)\s+off\s+(.+)$", s) or re.match(r"^(?:turn|switch|shut)\s+(.+?)\s+off$", s) or \
            re.match(r"^(?:disable|deactivate|bypass|kill|get rid of|no more|lose|ditch)\s+(.+)$", s) or \
            re.match(r"^(.+?)\s+(?:off|disabled)$", s)
        if m:
            return "turn off" if self.on_off(m.group(1), False, keys) else False
        m = re.match(r"^(?:turn|switch)\s+on\s+(.+)$", s) or re.match(r"^(?:turn|switch)\s+(.+?)\s+on$", s) or \
            re.match(r"^(?:enable|activate)\s+(.+)$", s) or re.match(r"^(.+?)\s+(?:on|enabled)$", s)
        if m:
            return "turn on" if self.on_off(m.group(1), True, keys) else False
        # double and halve
        m = re.match(r"^(double|halve|triple)\s+(.+)$", s) or re.match(r"^(?:cut|reduce)\s+(.+?)\s+(in half|by half)$", s)
        if m:
            g1, g2 = m.group(1), m.group(2)
            factor = 2 if g1 == "double" else 3 if g1 == "triple" else 0.5
            phrase = (g2 or g1) if (g1.startswith("in") or g1 == "halve") else g2
            word = "doubled" if g1 == "double" else "tripled" if g1 == "triple" else "halved"
            return True if self.scale(phrase, factor, keys, word) else False
        # up and down, by an amount or by a step
        m = UP_RE.match(s)
        if m:
            direction = 1 if re.fullmatch(f"(?:{UP})", m.group(1)) else -1
            # "more reverb" and "less delay" are the describer's, which knows what each effect needs
            if not m.group(3) and re.match(r"^(?:more|less|add|add more)$", m.group(1)):
                return False
            return True if self.step(m.group(2), direction, m.group(3), MUCH.get(m.group(4)) or 1, keys) else False
        m = COMPARE_RE.match(s)
        if m:
            direction = 1 if re.fullmatch(f"(?:{COMPARE_UP})", m.group(3)) else -1
            return True if self.step(m.group(1), direction, None, MUCH.get(m.group(2)) or 1, keys, True) else False
        m = AFTER_RE.match(s)
        if m and not re.match(r"^(?:turn|bring|push|move)$", m.group(1)):
            direction = 1 if re.match(r"^(?:up|higher)$", m.group(2)) else -1
            return True if self.step(m.group(1), direction, m.group(3), MUCH.get(m.group(4)) or 1, keys, True) else False
        # set
        m = re.match(r"^(?:set|make|change|put|adjust|turn|bring|move|give|switch|have)\s+(.+?)\s+(?:to|at|=|into|up to|down to|as)\s+(.+)$", s)
        pair = (m.group(1), m.group(2)) if m else None
        if not pair:
            m = re.match(r"^(?:use|choose|pick|select)\s+(.+?)\s+(?:for|as|on)\s+(?:the\s+)?(.+)$", s)
            pair = (m.group(2), m.group(1)) if m else None
        if not pair:
            m = re.match(r"^(.+?)\s+(?:to|at|=|is|should be|equals|of)\s+(.+)$", s)
            pair = (m.group(1), m.group(2)) if m else None
        if pair and self.set(pair[0], pair[1], keys):
            return True
        # "chord attack 200 ms", "harp waveform triangle", "set reverb 40 percent"
        bare = re.sub(r"^(?:set|make|change|put|adjust)\s+", "", s, count=1)
        if bare != s or re.search(r"\d", s):
            toks = bare.split(" ")
            for k in range(len(toks) - 1, 0, -1):
                if self.set(" ".join(toks[:k]), " ".join(toks[k:]), keys, True):
                    return True
        return False

    def items(self, phrase):
        """"the reverb, the delay and the chorus": each a setting or a control, or None"""
        parts = [p for p in re.split(r"\s*(?:,|\band\b|\bplus\b|\bas well as\b)\s*", phrase) if p]
        out = []
        for part in parts:
            c = find_control(part)
            if c and strip_articles(part.replace(c["text"], "", 1).strip()) == "":
                out.append({"control": c})
                continue
            r = resolve(part)
            if not r:
                return None
            out.append(r)
        return out

    def setting(self, phrase, keys, clause_for):
        """a setting named, or a question when it could be several"""
        r = resolve(phrase)
        if not r:
            return None
        if r.get("choices"):
            self.ask(phrase.strip(), keys, clause_for, r["choices"])
            return {"asked": True}
        return r

    def set(self, phrase, value_text, keys, bare=False):
        r = resolve(phrase)
        if find_control(phrase) and not (r and r.get("addrs") and len(r["addrs"]) == 1):
            return False
        if not r:
            return False
        if r.get("choices"):
            if bare:
                return False
            return bool(self.setting(phrase, keys, lambda a: f"set @{a} to {value_text}"))
        values = self.ctx.values(keys[0])
        parsed = [(a, parse_value(value_text, a, values)) for a in r["addrs"]]
        if any(not v for _, v in parsed):
            return False
        bad = next((v for _, v in parsed if "error" in v), None)
        if bad:
            self.note(bad["error"])
            return True
        changes = [{"address": a, "value": v["value"], "reason": f"set to {value_text}"} for a, v in parsed]
        self.change(keys, changes, f"set to {value_text}")
        v = self.ctx.values(keys[0])
        self.say(keys, ", ".join(f"{name_of(a)} → {shown(a, human(a, v) if v[a] is not None else 0, v)}" for a in r["addrs"]))
        return True

    def on_off(self, phrase, on, keys):
        items = self.items(phrase)
        if not items:
            return False
        # "turn on the delay": the describer knows what an effect needs to be heard
        if on and any(it.get("addrs") and any(not binary(a) for a in it["addrs"]) for it in items):
            return False
        # "turn on the harp drums": a switch with words of its own in the describer, which knows what
        # else it needs (a drum level above 0) and what to say about it
        if on and all(it.get("addrs") for it in items):
            addrs = [a for it in items for a in it["addrs"]]
            probe = describe_interpret(phrase, self.ctx.values(keys[0]))
            if not probe.unknown and all(any(ch["address"] == a and ch["value"] > 0 for ch in probe.changes) for a in addrs):
                self.describe_text(phrase, keys)
                return True
        for it in items:
            if it.get("control"):
                if on:
                    name = CONTROL_NAME[it["control"]["kind"]]
                    self.note(f'say what {name} should control: "{name} to the reverb"')
                    continue
                self.clear_control(it["control"], keys)
                continue
            if it.get("choices"):
                self.ask(phrase, keys, lambda a: f"turn {'on' if on else 'off'} @{a}", it["choices"])
                continue
            changes = []
            for a in it["addrs"]:
                p = PARAMS[a]
                v = (D["on_value"][str(a)] if str(a) in D["on_value"] else p["max_value"]) if on else max(p["min_value"], 0)
                changes.append({"address": a, "value": v, "reason": "turned on" if on else "turned off"})
            self.change(keys, changes, "turned on" if on else "turned off")
            self.say(keys, " and ".join(name_of(a) for a in it["addrs"]) + (" on" if on else " off"))
        return True

    def reset(self, phrase, keys):
        if re.match(r"^(?:everything|all|all settings|every setting|the whole (?:preset|bank|sound)|the (?:preset|sound|bank)|it all)$",
                    strip_articles(phrase)):
            changes = [{"address": a, "value": 0 if a in TARGET_ADDRESSES else PARAMS[a]["default_value"], "reason": "default"}
                       for a in sorted(PARAMS) if settable(a)]
            self.change(keys, changes, "back to its default")
            self.say(keys, "every setting back to its default")
            return True
        group = group_addrs(phrase)
        items = [{"addrs": group}] if group else self.items(phrase)
        if not items:
            return False
        for it in items:
            if it.get("control"):
                self.clear_control(it["control"], keys)
                continue
            if it.get("choices"):
                self.ask(phrase, keys, lambda a: f"reset @{a}", it["choices"])
                continue
            self.change(keys, [{"address": a, "value": PARAMS[a]["default_value"], "reason": "default"} for a in it["addrs"]],
                        "back to its default")
            self.say(keys, (strip_articles(phrase) if group else " and ".join(name_of(a) for a in it["addrs"])) + " back to default")
        return True

    def scale(self, phrase, factor, keys, word):
        verb = "halve" if word == "halved" else "double" if word == "doubled" else "triple"
        r = self.setting(phrase, keys, lambda a: f"{verb} @{a}")
        if not r:
            return False
        if r.get("asked"):
            return True
        for key in keys:
            v = self.ctx.values(key)
            changes = []
            for a in r["addrs"]:
                p = units_of(a, v)
                nxt = max(p["min_value"], min(p["max_value"], human(a, v) * factor))
                changes.append({"address": a, "value": js_round(nxt * 100) / 100 if p["data_type"] == "float" else js_round(nxt),
                                "reason": word})
            self.change([key], changes, word)
        self.say(keys, " and ".join(name_of(a) for a in r["addrs"]) + " " + word)
        return True

    def step(self, phrase, direction, by, much, keys, strict=False):
        """up or down: by an amount ("by 20", "by 10 percent", "by 2 semitones"), or a step that suits it"""
        phrase = re.sub(r"\s+(?:up|down)$", "", phrase)
        items = self.items(phrase)
        if not items or any(it.get("control") for it in items):
            return False
        if strict and any(it.get("choices") for it in items):
            return False
        for it in items:
            if it.get("choices"):
                self.ask(phrase, keys, lambda a: f"{'raise' if direction > 0 else 'lower'} @{a}{' by ' + by if by else ''}",
                         it["choices"])
                continue
            for key in keys:
                v = self.ctx.values(key)
                changes = []
                for a in it["addrs"]:
                    p, cur = units_of(a, v), human(a, v)
                    if by:
                        pct = re.match(r"^(\d*\.?\d+)\s*percent$", by)
                        if pct:
                            nxt = cur + direction * (p["max_value"] - p["min_value"]) * _num(pct.group(1)) / 100
                        else:
                            amount = parse_value(by, a, v)
                            if not amount or "error" in amount:
                                return False
                            nxt = cur + direction * amount["value"]
                    elif is_selector(p):
                        nxt = cur + direction * max(1, js_round(much))
                    elif is_time(p):
                        nxt = cur * math.pow(1.5, direction * much) if cur else (p["max_value"] * 0.05 * much if direction > 0 else 0)
                    else:
                        nxt = cur + direction * (p["max_value"] - p["min_value"]) * 0.1 * much
                    nxt = max(p["min_value"], min(p["max_value"], nxt))
                    changes.append({"address": a, "value": js_round(nxt * 100) / 100 if p["data_type"] == "float" else js_round(nxt),
                                    "reason": "raised" if direction > 0 else "lowered"})
                self.change([key], changes, "raised" if direction > 0 else "lowered")
            v = self.ctx.values(keys[0])
            self.say(keys, ", ".join(f"{name_of(a)} {'up' if direction > 0 else 'down'} to {shown(a, human(a, v), v)}"
                                     for a in it["addrs"]) + (" in the first; each bank from its own" if len(keys) > 1 else ""))
        return True

    # ---- the knobs, hover and the double tap ----

    def control_addrs(self, c):
        if c["kind"] == "hover":
            return [[249, 250]]
        if c["kind"] == "tap":
            return TAPS if c["slot"] is None else [TAPS[c["slot"]]]
        return [KNOBS[c["kind"]]]

    def clear_control(self, c, keys):
        pairs = self.control_addrs(c)
        self.change(keys, [{"address": ca, "value": 0, "reason": "does nothing"} for ca, _ in pairs], "does nothing")
        name = f"double tap {c['slot'] + 1}" if c["kind"] == "tap" and c["slot"] is not None else CONTROL_NAME[c["kind"]]
        self.say(keys, f"{name} does nothing")

    def control(self, s, keys):
        """"assign the mod pot to key signature", "hover controls the filter", "put the reverb on the
        harp knob", "double tap 2 switches chromatic mode on", "the chord knob does nothing" """
        c = find_control(s)
        if not c:
            return False
        before = s[:c["index"]].strip()
        after = s[c["index"] + c["length"]:].strip()
        # clearing
        if (re.match(r"^(?:turn off|switch off|disable|unassign|clear|free|reset|remove|disconnect|unmap|deactivate|kill)$", before) and not after) or \
                (not before and re.match(r"^(?:off|does nothing|do nothing|to nothing|to none|unassigned|disabled|nothing|none|cleared|free)$", after)) or \
                (re.match(r"^(?:turn|switch)$", before) and after == "off"):
            self.clear_control(c, keys)
            return True
        target, value_text = None, None
        verb = (r"(?:to|onto|on|=|for|at|controls?|moves?|sweeps?|changes?|does|do|is|adjusts?|sets?|switch(?:es)?|toggles?|flips?|"
                r"opens?|plays?|handles?|runs?|bends?|affects?|modulates?|drives?|works|should\s+(?:control|move|change|do|be|set|switch|toggle|sweep|adjust))")
        m = None
        if re.match(r"^(?:(?:assign|map|route|link|set|make|have|let|use|give|point|connect|turn|change|put|switch)\s*)?$", before):
            m = re.match(rf"^(?:so\s+(?:it|that\s+it)\s+)?(?:to\s+(?:control|move|change|sweep|do|adjust|set)|{verb})\s+(?:to\s+|the\s+|my\s+)*(.+)$", after)
        if m:
            target = m.group(1)
        else:
            m = re.match(r"^(?:assign|map|route|link|put|set|give|connect|attach|bind|move)\s+(.+?)\s+(?:to|on|onto|with|for|under)(?:\s+the)?$", before)
            if m and not after:
                target = m.group(1)
            else:
                m2 = re.match(r"^(?:use|let)$", before) and re.match(r"^(?:for|to\s+(?:control|move|change|sweep))\s+(.+)$", after)
                if m2:
                    target = m2.group(1)
                else:
                    return False
        # "… to 0", "… toward 5000", "… to max": the value hover goes to, or the double tap sets
        if c["kind"] in ("hover", "tap"):
            v = re.match(r"^(.+?)\s+(?:to|toward|towards|at|up to|down to)\s+(.+)$", target) or re.match(r"^(.+?)\s+(on|off)$", target)
            if v and resolve(v.group(1)):
                target, value_text = v.group(1), v.group(2)
        target = re.sub(r"^(?:turn|switch|toggle|flip)\s+", "", target, count=1)
        target = re.sub(r"\s+(?:on and off|up and down)$", "", target, count=1)
        if re.match(r"^(?:nothing|none|no(?:thing)?\s+at\s+all|off)$", target):
            self.clear_control(c, keys)
            return True
        r = resolve(target)
        if not r:
            return False
        # the describer's reading first, when it lands on one of these settings: it knows a sensible
        # sweep, and which section a word like "filter" means here
        if not value_text and c["kind"] != "tap":
            probe = describe_interpret(s, self.ctx.values(keys[0]))
            ca = self.control_addrs(c)[0][0]
            options = r.get("choices") or r.get("addrs")
            if any(ch["address"] == ca and js_round(ch["value"]) in options for ch in probe.changes):
                self.describe_text(s, keys)
                return True
        vt = f" to {value_text}" if value_text else ""
        if r.get("choices"):
            verb_word = f"double tap{' ' + str(c['slot'] + 1) if c['slot'] is not None else ''}" if c["kind"] == "tap" else CONTROL_NAME[c["kind"]]
            self.ask(target, keys, lambda a: f"{verb_word} to @{a}{vt}", r["choices"])
            return True
        if len(r["addrs"]) > 1:
            # a control moves one setting: the chords' and the harp's are two
            word = "double tap" if c["kind"] == "tap" else CONTROL_NAME[c["kind"]]
            self.ask(target, keys, lambda a: f"{word} to @{a}{vt}", r["addrs"])
            return True
        t = r["addrs"][0]
        p = PARAMS[t]
        knob = c["kind"] in KNOBS or c["kind"] == "hover"
        if knob and p["controls"] != "all":
            self.note(f"{CONTROL_NAME[c['kind']]} can't move {name_of(t)}{'; the double tap can' if p['controls'] == 'tap' else ''}")
            return True
        if not knob and p["controls"] == "none":
            self.note(f"the double tap can't change {name_of(t)}")
            return True
        for key in keys:
            v = self.ctx.values(key)
            pairs = [TAPS[c["slot"] if c["slot"] is not None else self.free_tap(v)]] if c["kind"] == "tap" else self.control_addrs(c)
            ca, ra = pairs[0]
            changes = [{"address": ca, "value": t, "reason": f"{CONTROL_NAME[c['kind']]} → {name_of(t)}"}]
            if c["kind"] in KNOBS:
                if not v[ra]:
                    changes.append({"address": ra, "value": 100, "reason": "the knob's range"})
            else:
                # what hover reaches with a hand close, or the double tap sets: said, or the far end
                tv = None
                if value_text:
                    parsed = parse_value(value_text, t, v)
                    if parsed and "error" not in parsed:
                        tv = parsed["value"]
                if tv is None:
                    cur = (v[t] / 100 if p["data_type"] == "float" else v[t]) if v[t] is not None else p["min_value"]
                    if is_selector(p) and p["max_value"] - p["min_value"] == 1:
                        tv = p["min_value"] if cur else p["max_value"]
                    else:
                        tv = p["min_value"] if cur > (p["min_value"] + p["max_value"]) / 2 else p["max_value"]
                changes.append({"address": ra, "value": tv, "reason": "hover value" if c["kind"] == "hover" else "double tap value"})
            self.change([key], changes, f"{CONTROL_NAME[c['kind']]} → {name_of(t)}")
        v = self.ctx.values(keys[0])
        if c["kind"] in KNOBS:
            self.say(keys, f"{CONTROL_NAME[c['kind']]} → {name_of(t)}")
        elif c["kind"] == "hover":
            self.say(keys, f"hover → {name_of(t)}, toward {shown(250, human(250, v), v)} with a hand close")
        else:
            slot = next((i for i, (ca, _) in enumerate(TAPS) if v[ca] == t), -1)
            pair = TAPS[max(slot, 0)]
            self.say(keys, f"double tap{' ' + str(slot + 1) if slot > 0 else ''} → {name_of(t)} to {shown(pair[1], human(pair[1], v), v)}, and back")
        return True

    def free_tap(self, values):
        """the first double tap slot that is free, or the first"""
        return next((i for i, (ca, _) in enumerate(TAPS) if not values[ca]), 0)

    # ---- a minishop preset by name: "load Twin Green", "Lonely Nights on bank 5", "copy Ice Cream
    # to bank 2", "make it like Moonlight but darker". The preset replaces the place's sound, then
    # anything else said goes on top. A name that is also a word for a sound ("Jump", the song)
    # and has no cue asks which.

    def mark_presets(self, text):
        """A preset's name becomes a marker ‹n› before anything else reads the text, so "Default
        Preset 3" isn't taken for bank 3 and "Lonely Nights" isn't read as a mood"""
        index = [{"p": p, "i": i, "key": " ".join(words(normalise(p["name"])))} for i, p in enumerate(self.ctx.presets)]
        index = sorted([x for x in index if x["key"]], key=lambda x: -len(x["key"]))
        for x in index:
            text = re.sub(rf"\b{spaced(x['key'])}\b", f" ‹{x['i']}› ", text)
        return text

    def unmark_presets(self, text):
        t = re.sub(r"‹(\d+)›", lambda m: " ".join(words(normalise(self.ctx.presets[int(m.group(1))]["name"]))), text)
        return re.sub(r"\s+", " ", t).strip()

    def preset(self, s, keys):
        m = re.search(r"(^|\s)(?:the\s+)?‹(\d+)›(?:\s+preset)?(?=\s|$)", s)
        if not m:
            return False
        p = self.ctx.presets[int(m.group(2))]
        key = " ".join(words(normalise(p["name"])))
        s = self.unmark_presets(s[:m.start()]) + m.group(0) + " " + self.unmark_presets(s[m.end():])
        s = re.sub(r"\s+", " ", s)
        hit_text = m.group(0).strip()
        hit_index = s.find(hit_text)
        rest = re.sub(r"\s+", " ", s[:hit_index] + " " + s[hit_index + len(hit_text):]).strip()
        cue = bool(re.search(r"\b(?:load|loads|use|using|start(?:ing)?\s+(?:from|with)|from|switch\s+to|go\s+to|apply|pick|choose|select|try|give\s+me|play|like|copy|put|preset|based\s+on|base|make\s+it|turn\s+it\s+into|change\s+to|minishop)\b", s))
        alone = not rest or bool(re.match(r"^(?:the|a|preset|the preset)$", rest))
        if not cue and not alone and len(key.split(" ")) < 2:
            return False
        if not cue:
            # "Jump" alone could be the song: ask
            probe = describe_interpret(key, self.ctx.values(keys[0]))
            if probe.understood and not probe.unknown:
                tail = ", " + rest if rest else ""
                self.out["questions"].append(Question(
                    p["name"], keys,
                    lambda a: f"load the {key} preset{tail}" if a == "preset" else f"sound like {key}{tail}",
                    [{"addr": "preset", "name": f"the {p['name']} preset"}, {"addr": "words", "name": "; ".join(probe.understood)}]))
                return True
        try:
            values = preset_values(p["value"])
        except ValueError:
            self.note(f"couldn't read the {p['name']} preset's code")
            return True
        for key_ in keys:
            v = self.ctx.values(key_)
            for a in range(2, len(v)):
                if a == 7 or a in LOCKED or values[a] == v[a]:
                    continue
                v[a] = values[a]
                self.ctx.why(key_, a, f"from {p['name']}")
        preset_word = "" if re.search(r"preset", p["name"], re.I) else " preset"
        self.say(keys, f"the {p['name']}{preset_word}{' by ' + p['author'] if p.get('author') else ''}")
        # what follows the name, on top of it: "… but darker", "… with the mod knob on the filter"
        after = re.sub(r"^(?:but|with|and|then|plus|only)\s+", "", s[hit_index + len(hit_text):].strip(), count=1)
        if after and not re.match(r"^(?:preset|from the minishop|on|to|in|instead)$", after):
            self.describe_text(after, keys)
        return True

    # ---- copying: "make bank 4 like bank 2", "copy the reverb from bank 1 to banks 3 and 5" ----

    def copy(self, s, keys, places):
        frm, to = places["from"], places["to"]
        every = [a for a in sorted(PARAMS) if settable(a) or RHYTHM_STEPS[0] <= a <= RHYTHM_STEPS[1]]
        if re.match(r"^(?:swap|exchange|trade|switch)(?:\s+(?:round|around|over|places))?$", s) and to and len(to) == 2 and "live" not in to:
            x, y = (self.ctx.values(k) for k in to)
            xs = list(x)
            for a in every:
                if x[a] != y[a]:
                    x[a] = y[a]
                    self.ctx.why(to[0], a, f"from {scope_name([to[1]])}")
                if y[a] != xs[a]:
                    y[a] = xs[a]
                    self.ctx.why(to[1], a, f"from {scope_name([to[0]])}")
            self.say(to, "swapped")
            self.note("swapping here moves the sounds; to move banks with their names, drag them in reorder and bulk edit")
            return True
        m = re.match(r"^(?:copy|duplicate|clone|transfer|take|bring over|bring|use|apply|paste)\s*(.*?)\s*(?:over|across|onto|into|to|on)?$", s) or \
            re.match(r"^(?:make|set|turn)\s*(.*?)\s*(?:like|the same as|same as|match|a copy of|identical to|copy)$", s) or \
            re.match(r"^(?:make|set)\s*(.*?)\s*(?:match|equal|copy)$", s)
        if not m:
            return False
        marks = places["marks"]
        if not frm:
            # "make bank 4 like bank 2": the bank after like is the one copied
            if len(marks) < 2:
                return False
            a, b = marks[0], marks[1]
            if re.match(r"^(?:make|set|turn)", s):
                to, frm = a["keys"], b["keys"]
            else:
                frm, to = a["keys"], b["keys"]
        if not to or not frm or len(frm) != 1:
            return False
        to = [k for k in to if k != frm[0]]
        if not to:
            return False
        what = strip_articles(re.sub(r"\s+(?:settings?|values?)$", "", m.group(1))).strip()
        src = self.ctx.values(frm[0])
        if not what or re.match(r"^(?:everything|all|it|the sound|sound|the preset|preset|the whole thing|whole bank|the bank|bank|all of it)$", what):
            addrs = every
        else:
            group = group_addrs(what)
            items = [{"addrs": group}] if group else self.items(what)
            if not items or any(it.get("choices") for it in items):
                return False
            addrs = [a for it in items for a in ([x for pair in self.control_addrs(it["control"]) for x in pair]
                                                  if it.get("control") else it["addrs"])]
        for key in to:
            v = self.ctx.values(key)
            for a in addrs:
                if v[a] == src[a]:
                    continue
                v[a] = src[a]
                self.ctx.why(key, a, f"copied from {scope_name(frm)}")
        named = what if what and not re.match(r"^(?:everything|all|it)$", what) else "every setting"
        self.say(to, f"{named} copied from {scope_name(frm)}")
        return True


def preset_values(code):
    """A shared preset's stored values. A code a field short (one in the list is) is padded first."""
    try:
        return pm.decode(code, NAMER.params)
    except ValueError:
        fields = base64.b64decode(re.sub(r"\s+", "", code) + "==").decode("ascii", "replace").split(";")
        if 250 <= len(fields) < 256:
            fields += [""] * (256 - len(fields))
            return pm.decode(base64.b64encode(";".join(fields).encode()).decode(), NAMER.params)
        raise


def binary(a):
    p = PARAMS[a]
    return p["data_type"] != "float" and p["max_value"] - p["min_value"] == 1


def places_named(text, current_bank, bank_names):
    """The places a text names, so the caller can read those banks before planning"""
    _, scopes = mark_scopes(normalise(text), current_bank, bank_names)
    keys = ["live"]
    for s in scopes:
        for k in s["keys"]:
            if k not in keys:
                keys.append(k)
    return keys


def plan(text, ctx):
    """Read a text into changes to the working values. Returns what was understood, what wasn't,
    notes, and questions (Question: phrase, keys, clause(addr) -> words, choices [{addr, name}])."""
    return Planner(ctx).run(text)
