# preset_maker

A minichord preset from a description in words, typed or spoken. It runs entirely on your computer, with no internet and no account. Built-in rules turn the words into settings, and the script checks every value against `generator/parameters.json` and prints a share code you can paste into minicontrol ("load preset code"). Then say what to change and it revises the same preset.

```
python3 -m venv ~/.venvs/minichord-preset
~/.venvs/minichord-preset/bin/pip install -r requirements.txt
~/.venvs/minichord-preset/bin/python preset_maker.py "warm pad chords, plucky harp spread across the stereo field, the mod knob opens the filter, double tap starts the looper"
```

```
Understood:
  chords: warmer
  chords: pad
  harp: plucky
  harp: stereo spread
  mod knob → chord low pass filter base frequency, about 150 to 4000
  double tap → the looper: record, play, stop, then a new recording
...
Preset code:
MDswOzUwOzUw...

What should change? > darker and less reverb
```

What it understands (`--words` lists everything):

- **Instruments and kinds of sound:** pad, pluck, organ, electric piano, piano, choir, string quartet, pizzicato, guitar, bells, music box, kalimba, marimba, flute, brass, accordion, supersaw, chiptune, bass, and more. The piano, choir and strings use the sampled voices.
- **Qualities:** dark/bright, warm/cold, soft/punchy, short/long, spacey/dry, lo-fi, detuned, fat/thin, eerie, calm and more. Each one works with "very", "slightly", "more", "less" and "not".
- **Effects:** reverb, delay, distortion, fuzz, tremolo, vibrato, ensemble chorus, stereo spread, glide, vocoder, singing vowels, touch velocity, pressure, palm mute and pluck on lift. Each one works with "lots of", "a touch of", "more", "less" and "no".
- **Settings:** pentatonic or blues harp, chromatic, jazzy (Barry Harris), voice leading, sus chords, voicings, temperaments and EDOs, octave up or down, key of E flat, 120 bpm, tuned to 432 Hz, and a colour name for the bank LED.
- **Rhythm mode** (firmware 42): the accompaniment styles, among them Alberti bass, waltz, boom-chick ("country rhythm"), walking bass ("jazz rhythm"), boogie, Travis picking, strummed guitar, bossa nova, arpeggios, ballad, reggae, 6/8 and habanera; and how they play: "no bass", "held bass", "held chords over the bass", "just the bass", "only while I hold a chord", "sync start", "chord changes on the bar", "staccato rhythm", "even rhythm", "swing". They play when rhythm mode is on, which is switched at the instrument. "Send midi clock" or "drive my drum machine" sends rhythm mode's tempo as MIDI clock (firmware 43); "follow my DAW" turns it off, since the minichord follows a clock coming in by itself.
- **Controls:** "the mod knob opens the filter", "holding the modifier the mod knob adds vibrato", "hover makes the chords sing", "double tap starts the looper and adds the crunch".

- **Songs:** about 180 of them (see below).

A word goes to the chords or the harp according to which one is named next to it ("plucky harp", "dark chords"). Otherwise it goes where it usually belongs, or to both. Words it doesn't know are listed back to you, not guessed at, and near-misses ("brigter", a mishearing) are taken as the word they're close to and reported. Without `--base`, or a shared preset named in the description ("like Neon Sunset"), it starts from a plain synth.

`--claude` uses Claude over the internet instead of the rules. It understands any wording, but needs an API key and costs a few cents a preset.

## Songs

Name a song and it sets the chords and harp to the closest the minichord gets. Examples: "Jump by Van Halen", "Africa", "a Herbie Hancock Chameleon bass", "Blade Runner", "the Halloween theme". Say a song for one section to take only that part, as in "Van Halen chords with an Africa harp". Add words to tune it, as in "Twin Peaks but darker" or "Purple Rain with less reverb". `--words` lists every song it knows.

Most songs live in `songs.json`, one line each, with the chords and harp described in the same words a description uses:

```json
{"song": "Don't You Want Me", "by": "The Human League", "chords": "OB-Xa, punchy", "harp": "synth pluck, bright"}
```

That line answers to "Don't You Want Me", "Don't You Want Me by The Human League" and "Human League Don't You Want Me". A one-word title ("Clocks") also needs its artist, unless the line says `"alone": true`, so an ordinary word can't trigger a song. Other options:

- `"words"`: more names for the song ("miami vice").
- `"note"`: a tip shown with it.
- `"base"`: a shared preset to start from.
- `"both"`: a description that applies to both sections.
- `"no_title"`: answer only to `words`.

The songs are approximations, built from the minichord's own oscillators, filters and samples. Tune them by ear.

After editing the list, run `parity_test.py`. It checks that every song is found by its name, that every recipe uses only known words, and that no name is taken twice. Then export the data for minicontrol (below).

## Bulk edit profiles

With `--profile` it describes a bulk edit profile instead of a preset. That's a file for minicontrol's bulk edit sheet ("reorder and bulk edit", then "import a profile file") that sets the same settings in every bank and leaves the rest of each bank's sound alone.

```
preset_maker.py --profile "attenuate all LEDs to 0.95, the mod knob controls the filter, the alternate pots for the chord and the harp turn ribbon mode and pluck on lift on and off, hover to chord crunch with the hover distance at 10 and the value at 100, my second double tap turns MPE on. Call it Controls and MIDI"
```

It writes `controls-and-midi.profile.json` (or `--out`), and rewrites the file after each revision or undo. On top of the preset vocabulary it reads:

- **A setting by name with a number:** "ribbon glide 30", "the chord attack to 200", "led attenuation 0.95", "hover distance 10", "crunch type 2". A plain name means the main setting, so "chord attack" is the envelope's attack and not the filter's.
- **Settings switched on or off:** "make sure ribbon mode is on", "turn off MPE", "scalar harp mode on".
- **The double tap's three slots:** "double tap 1", "my second double tap", "double tap control 2". Several things for one double tap fill the next slots. One double tap flips all three slots together.
- **The values a hover or double tap reaches:** "and the value is set to 100". A number past a setting's range is read as a percentage, so 100 on crunch level is full crunch.

A profile puts the same number in every bank, so anything relative to a bank's own sound ("darker", "more reverb", "an octave up") can't go in one. The tool says what it left out, and a knob given an ordinary setting sweeps from 0 to twice each bank's own value. Something the instrument doesn't have, like a second hover, is reported as such, and so is a fourth double tap slot.

| Option | |
|---|---|
| `--base X` | Start from a preset code, a file holding one, a shared preset's name (`--list-bases`), or `defaults`, instead of letting Claude pick. Use this to revise a preset you already have. |
| `--send` | Play each version on a plugged-in minichord as it's made. Nothing is saved, and the bank comes back when you change preset. Close minicontrol first, since it holds the port. |
| `--save BANK` | With `--send`: save the final version to bank 1–12 (it asks first). |
| `--once` | Make one preset and stop. |
| `--words` | List every word and phrase the rules know. |
| `--profile` | Describe a bulk edit profile instead of a preset (above). `--name` names it, as does "call it ..." in the description. `--out` says where to write it. |
| `--voice` | Speak the description and the revisions: press Enter, talk, press Enter. You see what it heard, and can take it, say it again, or type a correction. Typing works at every prompt too. |
| `--mic X` | The microphone for `--voice` (`--list-mics` lists them). Without it, the tool takes the default input unless that's the minichord: a desktop often makes the minichord's USB audio the default input, and that would record the instrument instead of you. |
| `--whisper-model` | `tiny.en`, `base.en` (default), `small.en` or `medium.en`. Larger hears better and is slower. The first use downloads it (about 150 MB for base). |
| `--apply FILE` | No model: apply a JSON list of `{"address", "value"}` changes to the base, in the same units Claude uses (floats as floats, knob and double tap targets as addresses). The script checks the values the same way. |

Speech is turned into text on your computer by Whisper (faster-whisper), with a hint of the instrument's vocabulary so "mod knob" and "vocoder" come through. Only the text goes to Claude.

What the script guards:

- **Out-of-range values:** they're clamped. Claude is told about each one in its next turn, and you see a `!` line.
- **Off-limits settings:** a preset can't change the instrument's own setup (USB audio, the harp plate and its thresholds), the knob memories, or the looper's action.
- **Controls:** a knob or hover can only be given a setting that a knob may move, and the double tap a setting it may set, by the same `controls` rule the firmware follows. Double tap and hover values are converted in their target setting's units.
- **Old codes:** codes from before firmware 39 carry no version. Settings newer than such a code get their defaults where a 0 can't have been meant, so a shared preset arrives sounding as it was made.

The code is written the way minicontrol writes one. It holds page 0 alone when page 1 is all at its defaults, and both pages otherwise. A code with page 1 needs this fork's firmware and minicontrol.

The rules can't hear the result either: a description gets you a sound in the right family with the controls wired as asked, and the rest is tuning by ear.
