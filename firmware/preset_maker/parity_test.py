#!/usr/bin/env python3
"""parity_test: minicontrol's describe.js against interpret.py, description by description,
and every song in songs.json found by its name with its recipe understood.

    parity_test.py ~/minicontrol

Exports describe_data.json from this side, runs the same descriptions through both (as presets
from a plain start and from a shared preset, and as profiles) and compares what each understood,
didn't understand, noted and changed. Needs node. Exits 1 on any difference.
"""
import json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import preset_maker as pm
import interpret

SENTENCES = [
    "warm pad chords, plucky harp spread across the stereo field, the mod knob opens the filter, double tap starts the looper",
    "a dark eerie drone with lots of reverb and a music box harp, hover makes the chords sing",
    "jazzy electric piano chords in the key of E flat, a touch-sensitive guitar harp with palm mute, the mod knob "
    "controls harp reverb and holding the modifier the mod knob adds vibrato",
    "8-bit chiptune at 140 bpm, no reverb, double tap turns on the delay and the crunch, pentatonic harp, make it red",
    "darker and less reverb", "more vibrato on the harp, slightly brighter chords", "not too bright, a bit warmer",
    "spooky choir chords like Neon Sunset with a kalimba harp", "brigter chords with a shimery harp",
    "attenuate all LEDs to 0.95, set my mod potentiometer to control I think the overall filter maybe. Set the "
    "alternate on the mod pot to control harp mode, maybe? Set the alternate pots for the chord and the harp to turn "
    "ribbon mode on and off, and to turn pluck on lift on and off. Set the hover to chord crunch, and make sure the "
    "chord crunch is set to type 3. Also make sure the hover distance is set to 10, and the value is set to 100. Set "
    "scalar harp mode on. Set my alternate chord layout to double tap 1. Set alternate hover, if there is an "
    "alternate hover, to harp crunch, and make sure harp crunch is set to 3. Set my second double tap to turn MDE "
    "mode on and all of the recent MIDI options.",
    "Then I would like you to build me a separate profile that is based around microtonality. So maybe have it set "
    "to temperament 31 EDO. Uh, make sure the alternate chords are some of the septimal chords and the just chords. "
    "Maybe turn, make sure ribbon mode is on. I would also set double tap control 2 to change the knob layer.",
    "set ribbon glide to 30, turn off MPE, led brightness 20%, hover on harp reverb with the hover value at 0.8, darker chords",
    "double tap starts the looper and also the second double tap turns on chromatic mode, the chord attack to 200, red",
    "tuned to 432 hz, in A minor, transpose up 2, 31 edo, strict voice leading, drop 2, sus chords",
    "very spacey supersaw chords with a touch of delay, a staccato marimba harp an octave up, no vibrato",
    "lo-fi flute chords with slapback, harp: concert harp, wide, ping pong, the chord knob does the reverb",
    "attenuate all LEDs to 0.95, hover to chord crunch and the value is 100, my second double tap turns MPE on",
    "give me a chameleon bass, like a Herbie Hancock chameleon bass, like that, that ARP 2600, like monophonic, wow, "
    "kind of a uh, bark.",
    "something like Africa by Toto", "Jump by Van Halen, with lots of reverb", "twin peaks but darker",
    "the final countdown", "take on me with a mono minimoog", "blade runner", "superstition, the mod knob opens the filter",
    "Purple Rain by Prince", "Jump chords with an Africa harp", "something like Mr. Blue Sky but darker", "Oxygène",
    "Strobe by deadmau5, slightly brighter", "an R.E.M. Losing My Religion harp", "blade runner chords with a tetris harp",
    "Gymnopédie with lots of reverb", "the Halloween theme, the mod knob opens the filter", "kraftwerk the model",
    "a theremin", "sliding harp with delayed vibrato", "arpeggiated chords with swing, waltz",
    "harp in octaves, key click, telephone, wobble harp", "a harp vocoder with clear words, a child voice singing",
    "rolled chords in second inversion, loose, retrigger chords, chords left harp right, dark reverb",
    "fretless harp, no glide", "a glitchy harp with an offbeat rhythm and no swing",
    "turn off hover", "unassign the mod knob, the chord knob does nothing", "turn off the double tap",
    "my second double tap does nothing", "no hover, the mod knob controls the key signature",
    "the mod knob controls key", "the harp knob does the transposition",
    "set reverb to 40%", "harp reverb at 0.2, chord delay 30 percent", "chord waveform 1 sawtooth",
    "oscillator 2 square, harp waveform triangle", "lots of chorus at 80", "vibrato 15 on the harp",
    "dim the leds", "brighter leds and a blue bank colour", "set the bank color to blue", "turn off the leds",
    "assign the mod pot to the key signature", "map the harp knob to the reverb", "set the bank colour to 200",
    "I want the mod knob to control the vibrato",
]
# every song in the list, by its first name, as a preset and a profile
SENTENCES += [e.words[0] for e in interpret.V if e.kind == "song"]


def python_side(params, version, presets):
    names = [p["name"] for p in presets]
    plain = pm.default_values(params, version)
    shared = pm.decode(presets[3]["value"], params)
    out = []
    for text in SENTENCES:
        row = {}
        for label, base in (("plain", plain), ("shared", shared)):
            r = interpret.interpret(text, base, params, names, first=True)
            after, made, notes = pm.apply_changes(base, r.changes, params)
            row[label] = {"understood": r.understood, "unknown": r.unknown, "notes": r.notes + notes,
                          "heard": [list(h) for h in r.heard_as], "base": r.base, "made": [list(m) for m in made]}
        r = interpret.interpret(text, pm.default_values(params, version), params, names, first=True, profile=True)
        after, made, notes = pm.apply_changes(pm.default_values(params, version), r.changes, params, report_all=True)
        row["profile"] = {"understood": r.understood, "unknown": r.unknown, "notes": r.notes + notes,
                          "edits": [[a, v] for a, _, v in made]}
        out.append(row)
    return out, plain, shared


NODE = r"""
const fs = require("fs");
const [describePath, dataPath, sharedPath, inputPath] = process.argv.slice(2);
const d = require(describePath);
const shared = JSON.parse(fs.readFileSync(sharedPath)).shared_presets;
d.setData(JSON.parse(fs.readFileSync(dataPath)), shared);
const input = JSON.parse(fs.readFileSync(inputPath));
const names = shared.map(p => p.name);
const out = input.sentences.map(text => {
  const row = {};
  for (const [label, base] of [["plain", input.plain], ["shared", input.shared]]) {
    const r = d.interpret(text, base, { presetNames: names, first: true });
    const a = d.applyChanges(base, r.changes);
    row[label] = { understood: r.understood, unknown: r.unknown, notes: r.notes.concat(a.notes),
                   heard: r.heardAs, base: r.base, made: a.made };
  }
  const base = d.defaultValues();
  const r = d.interpret(text, base, { presetNames: names, first: true, profile: true });
  const a = d.applyChanges(base, r.changes, true);
  row.profile = { understood: r.understood, unknown: r.unknown, notes: r.notes.concat(a.notes),
                  edits: a.made.map(m => [m[0], m[2]]) };
  return row;
});
process.stdout.write(JSON.stringify(out));
"""


def check_songs(params, version):
    """Every song found by its own name, its recipe all known words, no name taken twice"""
    base = pm.default_values(params, version)
    others = {tuple(interpret.tokens(interpret.normalise(w))): e.label for e in interpret.V if e.kind != "song" for w in e.words}
    seen, bad = {}, 0
    for e in (e for e in interpret.V if e.kind == "song"):
        for w in e.words:
            k = tuple(interpret.tokens(interpret.normalise(w)))
            if k in others:
                print(f"SONG NAME TAKEN: {w!r} ({e.label}) is already {others[k]}"); bad += 1
            if seen.get(k, e.label) != e.label:
                print(f"TWO SONGS CALLED {w!r}: {seen[k]} and {e.label}"); bad += 1
            seen[k] = e.label
        r = interpret.interpret(e.words[0], base, params)
        if not any(e.label in u for u in r.understood) or r.unknown:
            print(f"NOT FOUND BY ITS NAME: {e.label} via {e.words[0]!r}: {r.understood} {r.unknown}"); bad += 1
        if r.problems:
            print(f"RECIPE WORDS NOT KNOWN: {e.label}: {[w for _, w in r.problems]}"); bad += 1
    print(f"{len(seen) and sum(1 for e in interpret.V if e.kind == 'song')} songs: " + ("all found, every recipe understood" if not bad else f"{bad} problems"))
    return bad


def main():
    page = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else "~/minicontrol")
    params = pm.load_parameters()
    version = pm.firmware_version(params)
    presets = pm.load_shared_presets()
    song_problems = check_songs(params, version)
    py, plain, shared = python_side(params, version, presets)
    with tempfile.TemporaryDirectory() as tmp:
        data = os.path.join(tmp, "describe_data.json")
        with open(data, "w") as f:
            json.dump(interpret.export_data(params, version), f)
        inp = os.path.join(tmp, "input.json")
        with open(inp, "w") as f:
            json.dump({"sentences": SENTENCES, "plain": plain, "shared": shared}, f)
        script = os.path.join(tmp, "run.js")
        with open(script, "w") as f:
            f.write(NODE)
        js = json.loads(subprocess.run(["node", script, os.path.join(page, "describe.js"), data, pm.SHARED_PRESETS_JSON, inp],
                                       capture_output=True, text=True, check=True).stdout)
    bad = 0
    for text, p, j in zip(SENTENCES, py, js):
        for mode in p:
            for key in p[mode]:
                a, b = p[mode][key], j[mode][key]
                if json.dumps(a) != json.dumps(b):
                    bad += 1
                    print(f"DIFFERENT [{mode}/{key}] {text[:60]!r}\n  python: {json.dumps(a)[:400]}\n  js:     {json.dumps(b)[:400]}")
    total = len(SENTENCES) * 3
    print(f"{len(SENTENCES)} descriptions, {total} runs: " + ("the same in both" if not bad else f"{bad} differences"))
    sys.exit(1 if bad or song_problems else 0)


if __name__ == "__main__":
    main()
