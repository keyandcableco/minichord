# Harp plate numbering: plan (2026-10-17)

Harp plate (243) numbers for every plate that will be made, so the firmware, plate detect (422)
and the minichord-lab arcade's drawings agree. Talked through with Greg; nothing built yet.

## Rules

- **One number per distinct copper.** Plate detect keeps one print per number, so two boards with
  different copper on one number would overwrite each other's print when taught, and the arcade
  could not draw them apart.
- **Silkscreen variants share their copper's number.** Their resting readings are the same, so
  plate detect can't tell them apart anyway (fretless 31 = 10, joystick piano = 20).
- 0 stays the default: the stock strip, and any plate of twelve separate zones with no number.
- Not being made, so no number: long teeth, strum base, raga, arcade (d-pad), arcade strip,
  zipper strip, veena, veena tala, violin. 5-7 stay free.

## Table

| # | Plate | Reading | Where it is |
|---|---|---|---|
| 0 | stock strip | stock, strip guard 417 | released |
| 1 | Keymaster | stock thresholds, no guard | released |
| 2 | piano | neighbour guard 416 | released |
| 3 | twin rows | stock, strength 240 | released |
| 4 | framed strings | stock, strip guard | released |
| 5-7 | free | | |
| 8 | zipper 24 | 24 places | advanced, fw 99 |
| 9 | arcade wheel | ring bearing CC 23/24 | advanced, fw 99 |
| 10 | fretless wrap (+ fretless 31) | wrap island, ribbon | advanced |
| 11 | joystick | analog stick, A, B, click | feature-joystick(Drums), fw 90/91, says 4: move to 11 |
| 12 | piano glide | piano guard + strip run rule | feature-places (local) |
| 13 | drum pads | 28 places, harp drums | feature-places |
| 14 | xy pad | always a ribbon, x sweep 420/421 | feature-places |
| 15 | torus | corner chords 423 | feature-places |
| 16 | tesseract | corner chords 423 | feature-places |
| 17 | split | pads + 13-note strip | feature-places |
| 18 | split xy | pads + xy | feature-places |
| 19 | celtic plait | corner chords at its 12 crossings | to build |
| 20 | joystick harp (+ joystick piano) | 12 places from 6 pads + analog stick | to build |
| 21 | fretless (ribbon) | stock; the ribbon stays a setting to turn on | number only |
| - | LED zipperlin | needs its downlink firmware | parked |

**Joystick = 11:** apply_preset_version already moves 243 = 1-4 up by 7 in presets stamped
before 93, so a fw 90/91 preset saying 4 (the joystick) lands on 11 with no new migration.
The Lab must then read the joystick as `243 === 11` on the firmware that numbers it so, and
keep `243 === 4` as the joystick only on 90-92 (controller.js `jsOn`, rhythm.js `rhPlate`,
core/minichord.js).

**Plate detect:** prints at EEPROM 320 + 2 + 14 a plate; with 22 numbers that ends at 630, under
the Teensy 4.0's 1080 bytes. Plates with the same copper are one plate to it (teach one).

## Firmware pieces, in order

1. **Rebase feature-places onto advanced** as firmware 100 (it still calls itself 98; advanced
   is 99 with the wheel and zipper fixes already in). Keep 12-18 as they are.
2. **Joystick to 11:** bring feature-joystick / feature-joystickDrums (396-415) onto advanced,
   every `harp_plate == 4` there becomes `plate_joystick` (11), tooltips likewise. Version bump.
3. **Numbers only:** 21 fretless (strength, hover, strip family as stock). harp_plate_count,
   strength/hover tables, parameters.json max_value and the 243 tooltip in the same commit;
   regenerate with generate.py, version_ID bump with a note.
4. **Celtic plait (19):** corner chords 423 extended to its crossings (over arc + both ends of
   the under arc); model-check like torus/tesseract.
5. **Joystick harp (20):** 12 places from the 6 strip pads (T12+T8 island = bottom place) and
   the stick read as one pot per axis on T1/T3, shared on T9.
6. **On each board's arrival:** plate_cal.py touch scale, hover profile, teach plate detect,
   refit the xy fit and drum seam share from real readings.

Each piece: nothing flashed or pushed without Greg's OK; plates stay secret until unveiled.
