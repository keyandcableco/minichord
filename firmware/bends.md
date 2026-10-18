# The bends (firmware 102)

What a circuit bender wires into an Omnichord's body-contact points, on the whole minichord. Each is a
depth from 0 to 100: its setting (kept in the preset, movable by a knob, hover or the double tap) or
live from a control change (never kept), the deeper of the two counting. At 0 the instrument is
exactly as it was, bit for bit: no stock control does anything new.

| bend | setting | CC (any channel, 0-127) | what it does |
|---|---|---|---|
| pitch drop | 430, range 431 (semitones at 100, 12), sag 432 (ms to 95%, 150) | 112 | everything sags in pitch, drums too, and comes back |
| voltage starve | 433 | 113 | pitch wobbles and sags, fuzz as the rails close in, brown-outs, crackle |
| bitcrush | 434, mode 435 (0 both, 1 bits, 2 rate) | 114 | 16 to 4 bits (across the sound's own peak), 44.1 to 1.8 kHz |
| clock bend | 436, direction 437 (0 slower, 1 faster) | 115 | rhythm mode, its drums and the MIDI clock out to 1/4 or 4x |
| stutter | 438 (a preset always loads it at 0), sync 439 | 116 | a slice from the press repeats: 139 ms at a touch, 9 ms at full |

CC 121 (reset all controllers) lets every CC depth go.

## Where it sits
`lib/bent_bus` (AudioEffectBend), one stereo object between `stereo_l_mixer`/`stereo_r_mixer` and the
outputs: the speaker, headphones and USB recording hear it; the host's song and the looper click don't.
Chain: stutter -> pitch shifter (drop + starve wobble) -> starve (rails, level, crackle) -> crush.
Clock bend is one multiply in `acc_tick` (does nothing while the rhythm follows an external MIDI clock;
the looper keeps its own time). Pitch drop is a two-head delay-line shifter (1536-sample window): a
warble of a few Hz (4.6 Hz at -3 semitones, 14 Hz an octave down) and 17 ms of latency while it bends.

## Cost
- RAM2 had 5 kB free, so no buffers: the object keeps audio pool blocks by reference. 32 always, up to
  96 more while a stutter is held (pool 900; worst measured use before was 689).
- RAM1: variables +736 B (free for the stack 23264 -> 22528); code in flash, and the generated
  `parameter_range` moved to flash too: ITCM padding 1268 -> 4180 B. Flash +6 kB.
- CPU on the minichord (fw102_test.py cpu, 16 voices sounding, 2026-10-18): 26.2% with no bends; held
  deep, pitch drop +2.0 points, starve +3.1, crush +1.5, drop + starve + crush +3.4, all four with a
  stutter +4.2. At rest it only passes blocks on (not measured against 99). Audio blocks in use at
  most 243 -> 258 (pool 900).

## Trying each one (once 102 is flashed)
In minicontrol the Bends group has every setting; or from a DAW / `minictl`, send the CCs:
1. Pitch drop: hold a chord, send CC 112 at 127 and back to 0: an octave sag over 150 ms and back.
   Set 432 to 1000 for a slow battery droop; 431 to 2 for a gentle wow.
2. Voltage starve: CC 113 at 50 for a sick supply, 127 for brown-outs and fuzz.
3. Bitcrush: CC 114 swept 0 -> 127; 435 = 1 for bits only, 2 for rate only.
4. Clock bend: rhythm mode on (long press on hold), CC 115 at 127 drags to a quarter; 437 = 1 rushes.
5. Stutter: play anything, CC 116 at 30 for a beat-repeat, 127 for a buzz; 439 = 1 with rhythm mode
   for slices of the beat (1/4 to 1/64).
A knob works too: point the mod knob's alternate at 430-438 (knob targets reach 511).

## Checks
- On a PC: `tools/bent_render` (see its header) renders each bend to wav and checks rest is bit for
  bit, no blocks leak, pool use, and the drop's pitch (-3.00/-5.99/-11.98 st).
- On the minichord: `~/minichord-bench/fw102_test.py` (settings, rest, pitch, starve, crush, clock,
  stutter, cpu with a bench build, takes). Run 2026-10-18 on the flashed bench build: every check
  passes. Pitch drop -2.98/-6.04/-12.02 st; starve -3.7 dB with 17 brown-out frames at full, back
  to 0.5 dB; crush images +23 dB; clock 28.8 -> 7.2 / 115.2 ticks a second and back; stutter
  correlation 0.97/1.00 at the slice while held, ~0 after; banks untouched by the flash and the tests
  (only the version and the two new defaults filled in). Take: sample-demos/bends/device-bends-take.wav.
  Still worth: a soak with stutter pressed and released repeatedly (pool blocks), a DIAG build's stack
  high-water (RAM1 lost 736 B), the reload check (BANK=n), and playing it by hand.
