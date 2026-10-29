# The bends (firmware 121; 102 on its own branch)

What a circuit bender wires into an Omnichord's body-contact points, on the whole minichord. Each is a
depth from 0 to 100: its setting (kept in the preset, movable by a knob, hover or the double tap) or
live from a control change (never kept), the deeper of the two counting. At 0 the instrument is
exactly as it was, bit for bit: no stock control does anything new.

| bend | setting | CC (any channel, 0-127; 112-116 on firmware 102's branch) | what it does |
|---|---|---|---|
| pitch drop | 430, range 431 (semitones at 100, 12), sag 432 (ms to 95%, 150) | 75 | everything sags in pitch, drums too, and comes back |
| voltage starve | 433 | 76 | pitch wobbles and sags, fuzz as the rails close in, brown-outs, crackle |
| bitcrush | 434, mode 435 (0 both, 1 bits, 2 rate) | 77 | 16 to 4 bits (across the sound's own peak), 44.1 to 1.8 kHz |
| clock bend | 436, direction 437 (0 slower, 1 faster) | 78 | rhythm mode, its drums and the MIDI clock out to 1/4 or 4x |
| stutter | 438 (a preset always loads it at 0), sync 439, shortest 440 (25 ms), drums 441 | 79 | a slice from the press repeats: 280 ms at a touch down to the shortest at full; the drums play on through it (441 = 1 catches them too) |

Bends interact (442, 0-100) makes them act on one another, as a bent Omnichord's one clock and one
supply would: the pitch drop drags the rhythm's clock as it sags (at 100 exactly as far as the pitch)
and lowers the sample rate; the voltage starve drags the clock as its supply sags, nearly stops it in a
brown-out, and takes bits away. At 0 each bend is on its own.

Spring back (443: 0 none, 1 pitch drop, clock bend and stutter, 2 every bend; off by default) makes a bend
set by a knob (or an editor) come back by itself, as a bender's touch point does when the finger lifts: it
counts fully while the knob moves and, 150 ms after the knob stops, eases back to nothing over spring time
(444, 1000 ms); a stutter lets go at the end of it. The CCs hold as long as the controller does; hover comes
back with the hand. Stutter refresh (445, 2000 ms; 0 never): a stutter held that long goes back to the live
sound and, still held, catches a new slice, so one left on rolls along with the music and never holds a
slice of silence for ever.

CC 121 (reset all controllers) lets every CC depth go.

## Where it sits
`lib/bent_bus` (AudioEffectBend), one stereo object between `stereo_l_mixer`/`stereo_r_mixer` and the
outputs: the speaker, headphones and USB recording hear it; the host's song and the looper click don't.
Chain: stutter -> pitch shifter (drop + starve wobble) -> starve (rails, level, crackle) -> crush.
Clock bend is one multiply in `acc_tick` (does nothing while the rhythm follows an external MIDI clock;
the looper keeps its own time). Pitch drop is a two-head delay-line shifter (1536-sample window, 25 ms
of latency while it bends). Each head jumps while the other sounds (every 35 ms / (1 - speed): 4.6 Hz
at -3 semitones, 14 Hz an octave down), and lands within 320 samples of a whole window back, wherever
what it reads best matches the other head (splice): on a held note the two read the same waveform, so
every partial comes out at exactly the depth's ratio and their crossfade keeps its level. Before
(firmware 102 to 2d72dbd) the jump was a fixed window, which puts each partial on a grid as wide as
the jump rate, up to half a step off: on A3, 105.1 Hz an octave down rather than 110 (-12.77
semitones), -3.14 and -6.32 at 3 and 6, notes elsewhere sharp; and the level moved by up to 1.4 dB
from note to note. The crossfade's level is made up by how alike the heads were at the jump (rho):
nothing for a note they match, up to 3 dB at the crossing for sound they can't (noise).

## Cost
- RAM2 had 5 kB free, so no buffers: the object keeps audio pool blocks by reference. 64 always (32
  before the splice), up to 192 more while a stutter is held (pool 900; worst measured use before was 689).
- RAM1: variables +736 B (free for the stack 23264 -> 22528); code in flash, and the generated
  `parameter_range` moved to flash too: ITCM padding 1268 -> 4180 B. Flash +6 kB.
- CPU on the minichord (fw102_test.py cpu, 16 voices sounding, 2026-10-18): 26.2% with no bends; held
  deep, pitch drop +2.0 points, starve +3.1, crush +1.5, drop + starve + crush +3.4, all four with a
  stutter +4.2. At rest it only passes blocks on (not measured against 99). Audio blocks in use at
  most 243 -> 258 (pool 900).
- With the splice (2026-10-19, firmware 103 bench build arrival103t, the same 16 voices): 27.9% with
  no bends; pitch drop +3.7 points (blocks at most 39.0%), starve +4.0, crush +0.9, drop + starve +
  crush +4.4, all four with a stutter +4.9 (at most 41.5%). The first splice, every offset on every
  sample, took ~0.8 ms a splice (+5.7 for a drop, blocks to 58.7%). RAM1 +1.9 kB (its buffers).

## Trying each one (once 102 is flashed)
In minicontrol the Bends group has every setting; or from a DAW / `minictl`, send the CCs:
1. Pitch drop: hold a chord, send CC 75 at 127 and back to 0: an octave sag over 150 ms and back.
   Set 432 to 1000 for a slow battery droop; 431 to 2 for a gentle wow.
2. Voltage starve: CC 76 at 50 for a sick supply, 127 for brown-outs and fuzz.
3. Bitcrush: CC 77 swept 0 -> 127; 435 = 1 for bits only, 2 for rate only.
4. Clock bend: rhythm mode on (long press on hold), CC 78 at 127 drags to a quarter; 437 = 1 rushes.
5. Stutter: play anything, CC 79 at 30 for a long beat-repeat, 127 for a fast roll; 440 under 20 ms
   for a buzz at the end; 439 = 1 with rhythm mode for slices of the beat (1/2 to 1/32); 441 = 1 to
   freeze the drums with it.
From a Linux terminal, `amidi` sends the CCs (channel 1; `amidi -l` gives the port, hw:3,0,0 on 2026-10-18):

    amidi -p hw:3,0,0 -S 'B0 4B 7F'    # CC 75 (0x4B) pitch drop at 127; 'B0 4B 00' lets go
    amidi -p hw:3,0,0 -S 'B0 4C 50'    # CC 76 (0x4C) voltage starve at 80
    amidi -p hw:3,0,0 -S 'B0 4D 7F'    # CC 77 (0x4D) bitcrush
    amidi -p hw:3,0,0 -S 'B0 4E 7F'    # CC 78 (0x4E) clock bend (rhythm mode on)
    amidi -p hw:3,0,0 -S 'B0 4F 40'    # CC 79 (0x4F) stutter at 64
    amidi -p hw:3,0,0 -S 'B0 79 00'    # CC 121: every CC depth let go

A knob: `minictl set --raw 16 433` puts voltage starve on the mod knob's alternate (the modifier held);
it sweeps the bend's whole 0-100 (firmware 102 from 0ef047e on; before it, a knob on a bend at 0 did
nothing). Hover: `minictl set --raw 249 438 250 100` makes a hand over the harp a stutter, deeper as
it comes closer. Both are live settings: `minictl revert` after a `minictl snapshot`, or reload the bank.

## A setup for playing them (live, any bank)
Every bend on a control at once, over a rock groove, nothing saved (the user's "much cooler" setup,
2026-10-18, on bank 7):

    minictl snapshot
    minictl set --raw 117 0 14 430 10 434 12 436 16 438 249 433 250 100 325 0 200 442 201 100 334 1 335 1 187 100 442 0
    minictl rhythm on

Mod knob: pitch drop. A hand over the harp: voltage starve, deeper the closer. With the modifier held:
chord knob bitcrush, harp knob clock bend, mod knob stutter (alternates hold where they're left: back
to the bottom to let go). Double tap of the modifier: bends interact on (100) and off. `minictl revert`
puts the bank back.

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
