# The bent board (harp plate 21, firmware 111)

A full-face plate on the stock plate's twelve lines (designs: the plates repo, `full face plate/make_bentboard.py`).
Its harp is thirteen places on six lines; the other six lines are touch areas that move settings, the bends by
default. This is what the firmware does with it, how it was checked before the board existed, and what to do the
day it arrives.

## What the firmware does

**Harp** (T10 T9 T8 T7 T4 T2 bottom to top, T10's island above T2: the split plate's strip). Thirteen places, read
as positions in every harp mode: the cores, the bands between two lines (a finger there touches both), the wrap
band and the island. Chord mode plays the chord's tones up all thirteen (the thirteenth carries the twelve
strings' pattern on from the bottom). Pluck on lift, palm mute (the six lines only, in their own order), touch
velocity, pressure and strum velocity work; harp drums strike the kit's pads (places 0 and 12 both the first).
Hammer-on, koto press and the harp ribbon don't (positions, as on zipper 24).

**Touch areas**, each with a target setting and a value (472-485), as hover has, any setting a knob can sweep:

| Area | Lines | Target / value | Default | Goes by |
|---|---|---|---|---|
| rail point 1 (leftmost) | T1 | 472 / 473 | pitch drop, 100 | how firmly pressed |
| rail point 2 | T1 + T3 | 474 / 475 | voltage starve, 100 | how firmly |
| rail point 3 | T3 | 476 / 477 | bitcrush, 100 | how firmly |
| rail point 4 | T3 + T5 | 478 / 479 | clock bend, 100 | how firmly |
| rail point 5 (rightmost) | T5 | 480 / 481 | stutter, 100 | how firmly |
| palm pad | T6 | 482 / 483 | bends interact, 100 | how firmly |
| thumb ribbon | T11, T12 | 484 / 485 | bitcrush, 100 | where the thumb is (486 switch 1: how firmly) |

A setting moves from the preset's own value toward the area's value as the area is pressed, and back as it lets go
(the ribbon: its left end is the preset's own, its right end the value, held where the thumb rests). On a bend's
depth a touch is a third source beside the setting and control change 112-116 (`bend_touch` in BENDS): the deepest
counts, it never springs back (443), and it can't undo a bend a knob made. Two areas on one setting: the deepest
moves it. On every other plate the areas don't exist and nothing changes.

Why the defaults: the rail is the board's five bends, as drawn. The palm rests on its pad while the left hand
plays chords, so it shouldn't change the sound by itself: bends interact does nothing until a bend is pressed, then
drags the others with it (the bent Omnichord's one clock and supply), so palm and rail are played together. The
ribbon reads a place, a value the thumb can hold while the fingers play, not a press: a pot rather than a button,
so it takes the one bend that is good held steady, bitcrush.

**Two areas together** (bent board options, 486: switches added up, all off at 0; 1 is the ribbon by pressure, as
486 at 1 was before). No new setting numbers: these only matter on this plate.

- **Catch (2)**: press a rail point, then put the thumb on the ribbon. The thumb takes the point over as deep as the
  finger had it, and the finger can lift. Slide the thumb right for deeper (full at the right end), left for
  lighter (none at the left end), from wherever it landed. Lift it and the point lets go; tap it (under 250 ms)
  and the point stays on until a finger lands on it again and lifts. The thumb landing first catches nothing, and
  the ribbon does its own job; while it has a point, the ribbon's own area is let go. Why: rail pressure is shaky
  and the ribbon steady, and two points held together where they share a line read badly, so a point caught or
  left on frees the rail for the next finger. A thumb or finger reading off for under 40 ms hasn't lifted (the
  chip at its threshold, or the border guard taking the ribbon's right end). A finger landing on a point left on
  picks it up once settled; a thumb landing while it holds it takes it over. A load, a panic, another plate and
  catch switched off let go of everything left on. `bentboard::catch_update`, tested in `tools/bent_board` (`./run.sh`, the catch checks).
- **The harp strikes (4)**: each harp note on the board strikes again the bends a touch holds (a rail point
  pressed, caught or left on, or the ribbon's own bend): the pitch drop starts in tune and sags, the stutter
  catches a new slice from the note, the starve's supply slumps under it (0.2 of the supply at full starve, a
  guess to tune by ear). Bitcrush and clock bend have nothing to strike; a bend only a knob or control change holds
  is left alone. Notes within 40 ms of the last are one strum, struck once. The bends are on the whole mix, so the
  chords and drums start again in tune too. `bent_strike` in main.cpp and `strike` in lib/bent_bus, rendered by
  `tools/bent_render` (the strike checks).

- **The second knob (8)**: press a rail point, then put the thumb on the ribbon: while both stay, the thumb sets how
  that bend goes, the finger how deep. Pitch drop: its range, the preset's at the left end to two octaves at the
  right. Starve: how far its brown-out drags the other bends (bends interact), the preset's to fully. Crush: bits
  in the left half, rate in the right. Clock bend: both ways from where the thumb landed, dragging to the left and
  rushing to the right, four times either way at the ends. Stutter: the slice, longest at the left end, shortest at
  the right. For the clock bend and the stutter the finger only holds it on. The same gesture as catch: with both
  on, catch has it. `bentboard::shape_update`, the overrides in BENDS (`range_touch`, `interact_touch`,
  `crush_mode_touch`, `clock_touch`).
- **Rolls (16)**: with the stutter on (by any touch), a finger landing on the clock bend's point rolls it: the slice
  halves every few repeats (every 4 at a touch, every one pressed fully) down to 2.9 ms, a buzz, and stays when the
  finger lifts. On the pitch drop's point it falls: each repeat a step lower (0.1 to 1 semitone, as pressed) down to
  the drop's range, staying when the finger lifts. The point's own bend is left out meanwhile; the stutter letting
  go starts both again. The other order (the point first) plays the two bends together, as ever. Without a rhythm
  the roll still works on the free slice, so the clock bend's point does something in the banks without one.
  `bentboard::pairs_update`, `AudioEffectBend::roll`.
- **The plug (32)**: the rail's two ends pressed together (landing within 60 ms; the one pair the rail reads pressed at
  once) pull the plug: the pitch drop, the starve and the clock bend all at the shallower finger's depth, the bends
  interacting fully, the ends' own bends left out. Let go and the power comes back; a finger left behind does
  nothing until it lifts too. Landed further apart, the ends are their own bends (or a roll). It gives the three
  bends together, which the rail can't read pressed as three fingers (they share lines).
- **The palm hold (64)**: a rail point pressed with the palm on its pad, then the palm lifted, then the finger: the
  bend stays as deep as it was until the palm lands again. Pressed with the palm already off, it springs back as
  ever. Untried by hand: if reaching the rail lifts the palm anyway, every bend would stay, so it's off by default.
  `bentboard::palm_hold_update`; a panic lets go too.
- **The chain (128)**: the bends a touch holds run in the order they were pressed, ahead of the rest in their own
  order (stutter, pitch drop, starve, crush). A crush pressed before a pitch drop has its grit pitched down with
  it; a starve before a stutter has its crackle repeated in the slice. `AudioEffectBend::chain` runs the stages
  (`stage_stutter`, `stage_pitch`, `stage_starve_crush`) in that order; in their own order the output is the same,
  bit for bit, as before the stages were split (`tools/bent_render`'s renders compared).

The two compose: catch a bend or leave it on, and both hands play while the harp strikes it.

**Decoding** is `include/bent_board.h` (plain C++, its code and tables in flash): the harp's line with T10's two
ends (a touch already at an end, else T9 against T2, else the end last left), runs split at the valley between two
fingers, each core's place spread so a band reads its full width; the rail's fingers each fitted by their own
profile, a newcomer found in what the others leave at their steady forces; the palm's depth; the ribbon's place
through a table; a guard where the ribbon's right end meets the harp's left edge, 1.5 mm apart.

**Sizes** against feature-bentEffects: RAM1 variables +640 bytes (free for locals 20128 -> 19488), ITCM code +352
(padding 4180 -> 3828), RAM2 unchanged.

## Checked before the board existed

`tools/bent_board` (`./run.sh`) runs the decoder over 7850 touches modelled from the board's copper
(~/minichord-bench/bentboard/model.py: each zone reads a round contact's area over its copper, plus a thin fringe,
light to firm, with the chip's noise). All its checks pass. What it found:

- **Harp**: places 1-11 read right 97.9% of the time 1 mm or more inside the place; every touch within one place;
  two fingers with an untouched line between, two runs each within a place 99.8%.
- **T10's two ends**: a touch already at an end, 100%. A cold tap wholly on an end core (6-9 mm fingertip) reads
  no lean either way: it goes to the end last left. Modelled, 70-78% right cold. Real fingertips flatten wider than
  the model's disc and should lean more: measure it.
- **Rail**: a finger 1 mm or more inside a point, 97.9%; two points, the second landing after the first, 96.7%,
  and the second stays when the first lifts, 98.2%. Pressed in the same instant only points 1 and 5 (no line
  shared) separate (99%); any other two read as one.
- **Ribbon**: the "wedge" is a triangle (5.6 mm tall at the left end, 29 at the right), T11's spine along its top
  edge, T12's along its bottom. A thumb 4 mm or more below the top edge reads within 2.9 mm (rms); right against
  the top edge it reads ~11 mm to the left. A slide never steps back.
- **Border**: a thumb at the ribbon's right end reads up to 0.85 of itself on T9/T10 (10-11 mm thumbs); a harp
  finger on the left edge up to 0.45 on T12. Guarded: 98.2% of thumbs touch nothing else. Not guarded: the
  ribbon's top right corner, where a thumb on T11's spine also covers T7's corner.
- **Keys** (a finger flat where the keycap stands, pessimistic: the keycap's top is ~2.4 mm above the board):
  4% touch a zone, on four keys: beside the island (T10), past the rail's right end (T5), the # by the palm pad
  (T6) and by the harp's left edge half way down (T8, T9, T12).

## The day it arrives

Ask Greg before each flash. Close minicontrol and every MIDI app first; back up the banks (`bank_backup.py`): a
flash wipes them.

0. **Which firmware**: feature-bentBoard is firmware 111 on the bends (102) alone; the plates-arrival build (110:
   the places, the joystick harp, the harp fader) isn't in it, so flashing it takes those off the minichord until
   the two are merged (plates-arrival sits within 500 bytes of an ITCM block: the merge has to move code to flash).
1. **Teach it**: flash a TOUCHLOG build of it (`TOUCHLOG=1 TOUCHMS=10 ./build.sh bentboard111cal <commit>`), fit
   the board, choose harp plate 21 with plate detect on, hands off for two seconds: three white flashes.
2. **Record**: `bentboard/record.py` (each step says what to touch). Then `bentboard/fit.py <rec.json>` prints
   the decoder's constants from the real board, and `tools/bent_board/replay <rec.txt>` shows what the decoder
   reads. Look for:
   - the rail's profiles, gains and the edges between points (`rail_bound`); `rail_firm`: is a normal firm press
     full depth?
   - the palm's resting and firm strengths (`palm_soft`, `palm_firm`): a resting palm should sit near the soft
     end
   - the ribbon's share range and table; where the thumb naturally rests (the top edge reads left)
   - `core_reads` from the harp slides; the cold end taps' lean (`island_margin`, `end_doubt`)
   - the border shares; the keys' strongest reading (under 40: none counts; else raise the thresholds or note it)
   - the harp's touch strength against the stock strip (taps on cores): `harp_plate_strength[21]`
3. **Hover**: `hover_cal.py` on the board for `hover_plates[21]`. Expect the left hand playing chords, and the palm
   resting on its pad, to read as a hand near the plate.
4. Paste the fitted constants into `include/bent_board.h`, rebuild plain, `./run.sh` (the modelled checks may move:
   the recording is the judge), flash.
5. **Device checks**: `bentboard/fw111_test.py` (settings, quiet), then `fw111_test.py rail palm ribbon harp
   keys` with the board fitted.
6. **The Bent Board set** (minicontrol feature-bentBoard, `presets/bentboard`, stamp 111) to all twelve banks, and
   by hand: each bank's palm and ribbon; the rail's combinations (two points together, palm + a rail point);
   strums across bands; a palm laid flat on the harp (mute); cold taps at both ends; the thumb at the ribbon's
   right end with a finger on the harp's left edge; the chord buttons beside the copper; upside down (never tried).

Nothing here is pushed: the board and this branch stay local until Greg unveils them.
