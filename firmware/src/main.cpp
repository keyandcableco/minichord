#include "audio_definition.h"
#include "def.h"
#include "temperament_profiles.h"
#include <AT42QT2120.h>
#include <Arduino.h>
#include <Audio.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Wire.h>
#include <button_matrix.h>
#include <debouncer.h>
#include <harp.h>
#include <potentiometer.h>
#include "minichord_samples.h"

//>>SOFWTARE VERSION 
const uint16_t firmware_version_adress = 7;   // where the writing firmware's version is stamped
void apply_preset_version(int bank_number);
void apply_harp_thresholds();
int version_ID=102; //to be read 00.03, stored at adress 7 in memory (18: 24-EDO, the quarter-tone modifier; 19: knob layer; 20: formants; 21: push and pop; 22: palm mute, harp midi notes ring; 23: harp pluck on lift; 24: usb audio, 244; 25: harp plate, touch thresholds, harp ribbon; 26: knobs and double tap reach 236 on; 27: hover, 249-251; 28: touch velocity and pressure, 252-253; 29: MIDI in plays, 8; 30: plucked string model, 217-219; 31: the parameter array grows to 512, page 1 from 256; 32: the looper, 256; 33: string spread and chord ensemble, 257-259; 34: the vocoder, 260-262; 35: strum velocity, 263; 36: sampled instruments, 264-265; 37: the double tap works a looper on the computer, 256 action 7; 38: generator scales, 267-269; 39: a push can replace the one held, and loading another bank drops it; 40: a knob holds its sweep within the setting's range; 41: each chord note's own voice, 270-273; 42: rhythm styles, 274-280; 43: MIDI clock out, 266; 44: tap tempo counts, 281; 45: rhythm mode from MIDI (command 8, CC 102 and 103) and MIDI chords, 282; 46: the looper in time, 283-286; 47: chord memory, 287; 48: looper length, 288; 49: koto press, 289; 50: the preset's name, 290-301; 51: hammer-on, 302; 52: the phone follower, 303-313; 53: rhythm variation B, fills, breaks, intro and ending, 314-319; 54: fingered glide, 320; 55: double tap hold, 321; 56: revert and panic, 322-323; 57: the held chord pressed again, 324; 58: alternate hover, 325-326; 59: the DJ box and the harp that plays the song, 327-333, voice source 5; 60: drums, 334-343; 61: the tabla recorded, preset storage 1 MB to 512 kB; 62: the dayan tuned to the key; 63: the step scale, 344-346, and the custom scale read by name in 19, 24 and 31; 64: koto press in steps and its snap, 347-348, modifier step, 349, ribbon step snap, 350; 65: melodic direction and the down forms, 351-355, the maqams, scalar harp modes 14 and 15, 356-357; 66: purity, 358; 67: your own temperament, 359-370, from MIDI Tuning Standard messages too, and MTS output, 371; 68: the drone, 372-375; 69: hammer-on time, 376, tabla laya, 377, drum and drone reverb, 378-379, speed ramp, 380, the looper keeps harp drums, sampled chord voices glide, the dayan on the drone's Sa; 70: preset storage 512 kB to 128 kB, which held only two presets; 71: back to 512 kB; 72: the twelve presets in one file, presets.bin, preset storage 256 kB; 73: four more sampled instruments, voice source 6-9: vibraphone, bass, sitar, electric piano; 74: the tanpura's jawari, 381; 75: upside down for a left-handed player, 384; 76: the locks, 385; 77: the presets in raw flash, the top 32 kB, and the samples as mu-law; 78: the reed piano, voice source 10, its bark and tremolo, 386-387; 79: kalimba, marimba and the tonewheel organ, voice source 11-13, the organ's drawbars and percussion, 388-389; 80: upright bass and accordion, voice source 14-15; 81: brothers, 390; 84: brothers under every standard chord (82 is the TRS MIDI out, on its own branch; 83 went unused); 85: the cliqhop kit, drum kit 12, and its grooves, 44-47; 86: the time signature, 393; 87: brothers again, 394; 88: brothers strikes the block with retrigger chords on; 89: chords follow the harp's touch, 395; 92: usb audio 2 at full speed, its recording at 44.1 or 48 kHz (90 and 91 are on their own branches); 93: the plates numbered from the released ones, 1 the Keymaster, 2 the piano plate and its neighbour guard, 416, 3 twin rows and 4 framed strings, each with its touch calibration, and harp touch scale, 418; zipper 24, the arcade wheel and the fretless wrap move to 8-10; 95: the strip guard, 417, on the stock strip and framed strings, on by default, and the plates' own touch thresholds (241 and 242 at 0); 94 was the strip guard's first form, off by default, on the bench only; 96: hover per plate, and its charge fit checked; 97: a default lock a newer firmware brings is locked at its default on an instrument that already has its locks, as 416-418 now are on one upgraded from 76-96; 98: plate detect, 422, the plate fitted known by how its zones read at rest, learned on each instrument and shown on the LED, and a plate swapped with the power on waited out and recalibrated; 99: the arcade wheel's bearing from the centre of its readings, corrected, quiet while A or B is pressed, and the neighbour guard on it; zipper 24's wrap island read by a margin, and where that can't tell the ends apart, by the end a touch is at or last left; hover measured on both; 102: the bends, pitch drop, voltage starve, bitcrush, clock bend and stutter, 430-439, and control change 112-116 (100 is the places and 101 the joystick harp, on their own branches))
//>>BUTTON ARRAYS<<
debouncer harp_array[12];
const uint16_t harp_debounce_us = 4000;   // the harp pads' settle, shorter than the buttons' (see setup)
debouncer chord_matrix_array[22];

//>>HARDWARE SETUP<<
harp harp_sensor;
button_matrix chord_matrix(SHIFT_DATA_PIN, SHIFT_STORAGE_CLOCK_PIN, SHIFT_CLOCK_PIN, READ_MATRIX_1_PIN, READ_MATRIX_2_PIN, READ_MATRIX_3_PIN);
debouncer hold_button;
debouncer up_button;
debouncer down_button;
debouncer LBO_flag;
bool flag_save_needed=false; //to know if we need to save the preset
potentiometer chord_pot(POT_CHORD_PIN);
potentiometer harp_pot(POT_HARP_PIN);
potentiometer mod_pot(POT_MOD_PIN);
float color_led_blink_val = 1.0;
bool led_blinking_flag = false;
float led_attenuation = 0.0; 



//>>CHORD DEFINITION<<
//for each chord, we first have the 4 notes of the chord, then decoration that might be used in specific modes
/* ---- temperament ------------------------------------------------------------
 *
 * The instrument is already a meantone interface: seven buttons in flat order
 * B E A D G C F, a modifier that alters by one chromatic semitone, key
 * signatures that add accidentals round the circle of fifths. None of that
 * assumes twelve notes. It assumes seven letters plus alterations, which is what
 * meantone is, so the same layout plays 19 or 31 divisions of the octave without
 * a single extra button.
 *
 * Temperaments 0-3 are twelve-note tunings: the same twelve pitches nudged by a
 * few cents each. 4 and 5 divide the octave differently, which gives notes that
 * do not exist in twelve - C# and Db become separate pitches, a step apart, with
 * C# the LOWER of the two as it was before equal temperament merged them.
 *
 * 24 is the odd one out: not a meantone but twelve with a note between each pair,
 * the quarter-tones. Its tables are twelve's doubled (the just and neutral chords
 * rounded from their ratios, where 24 does well: 11:9, 11:8 and 11:6 land within
 * a few cents), and the modifier moves a letter by one step instead of two, so it
 * plays the note between the frets. Key signatures still sharpen by a semitone.
 *
 * Every table below was generated by gen_edo.py from interval NAMES rather than
 * semitone counts: "a major third is two letters up, unaltered" holds in any
 * meantone, and only the number of steps changes. The generator checks that its
 * 12-EDO output is byte-identical to the tables this firmware already had, so
 * selecting equal temperament is arithmetically the same instrument as before.
 *
 * The custom scale (address 236) is twelve toggles over the chromatic degrees.
 * In 19, 24 and 31 each degree is read by its name, as the chord tables are (b3
 * is a minor third, 8 of 31), so a scale ticked in twelve keeps its sound; the
 * step scale (344-346) picks steps of the division itself, for the notes twelve
 * has no name for, and the generator scale (267-269) builds one from an interval.
 *
 * What does NOT follow the temperament:
 *   - MIDI out. Note numbers are integers and there is nowhere to put a
 *     31st of an octave, so notes are rescaled to the nearest semitone on the
 *     way out. The audio is microtonal and the MIDI is an approximation of it.
 */
const uint8_t edo_steps[4] = {12, 19, 31, 24};
const uint8_t edo_sharp[4] = {1, 1, 2, 2};
// How far the MODIFIER moves a letter. In 12, 19 and 31 it is the chromatic semitone, the same as a
// key signature's sharp. In 24 it is one step, a quarter-tone: 24 holds twelve inside it, so a
// semitone modifier would only double twelve, while a quarter-tone one reaches the twelve notes in
// between. Key signatures still move a letter by the semitone (edo_sharp), so G major is G major.
const uint8_t edo_modifier[4] = {1, 1, 2, 1};
// The custom scale's twelve degrees, 1 b2 2 b3 3 4 b5 5 b6 6 b7 7, as steps of
// each division: the named intervals, a minor second the diatonic semitone and
// b5 the diminished fifth, as the tables below have them.
const uint8_t edo_chromatic_degrees[4][12] = {
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
  {0, 2, 3, 5, 6, 8, 10, 11, 13, 14, 16, 17},
  {0, 3, 5, 8, 10, 13, 16, 18, 21, 23, 26, 28},
  {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22}
};
const int8_t edo_base_notes[4][7] = {
  {11, 4, 9, 2, 7, 0, 5},
  {17, 6, 14, 3, 11, 0, 8},
  {28, 10, 23, 5, 18, 0, 13},
  {22, 8, 18, 4, 14, 0, 10}
};
const int8_t edo_scale_root_offsets[4][21] = {
  {0, 7, 2, 9, 4, 11, 5, 10, 3, 8, 1, 6, 6, 1, 8, 3, 10, 5, 0, 4, 11},
  {0, 11, 3, 14, 6, 17, 8, 16, 5, 13, 2, 10, 9, 1, 12, 4, 15, 7, 18, 7, 18},
  {0, 18, 5, 23, 10, 28, 13, 26, 8, 21, 3, 16, 15, 2, 20, 7, 25, 12, 30, 11, 29},
  {0, 14, 4, 18, 8, 22, 10, 20, 6, 16, 2, 12, 12, 2, 16, 6, 20, 10, 0, 8, 22}
};
const uint8_t edo_major[4][7] = {{0, 4, 7, 12, 2, 5, 9}, {0, 6, 11, 19, 3, 8, 14}, {0, 10, 18, 31, 5, 13, 23}, {0, 8, 14, 24, 4, 10, 18}};
const uint8_t edo_minor[4][7] = {{0, 3, 7, 12, 1, 5, 8}, {0, 5, 11, 19, 2, 8, 13}, {0, 8, 18, 31, 3, 13, 21}, {0, 6, 14, 24, 2, 10, 16}};
const uint8_t edo_maj_sixth[4][7] = {{0, 4, 7, 9, 2, 5, 12}, {0, 6, 11, 14, 3, 8, 19}, {0, 10, 18, 23, 5, 13, 31}, {0, 8, 14, 18, 4, 10, 24}};
const uint8_t edo_min_sixth[4][7] = {{0, 3, 7, 9, 1, 5, 12}, {0, 5, 11, 14, 2, 8, 19}, {0, 8, 18, 23, 3, 13, 31}, {0, 6, 14, 18, 2, 10, 24}};
const uint8_t edo_seventh[4][7] = {{0, 4, 10, 7, 2, 5, 9}, {0, 6, 16, 11, 3, 8, 14}, {0, 10, 26, 18, 5, 13, 23}, {0, 8, 20, 14, 4, 10, 18}};
const uint8_t edo_maj_seventh[4][7] = {{0, 4, 11, 7, 2, 5, 9}, {0, 6, 17, 11, 3, 8, 14}, {0, 10, 28, 18, 5, 13, 23}, {0, 8, 22, 14, 4, 10, 18}};
const uint8_t edo_half_dim[4][7] = {{0, 3, 6, 10, 2, 5, 8}, {0, 5, 10, 16, 3, 8, 13}, {0, 8, 16, 26, 5, 13, 21}, {0, 6, 12, 20, 4, 10, 16}};
const uint8_t edo_sus_fourth[4][7] = {{0, 5, 7, 12, 2, 9, 10}, {0, 8, 11, 19, 3, 14, 16}, {0, 13, 18, 31, 5, 23, 26}, {0, 10, 14, 24, 4, 18, 20}};
const uint8_t edo_sus_second[4][7] = {{0, 2, 7, 12, 5, 9, 4}, {0, 3, 11, 19, 8, 14, 6}, {0, 5, 18, 31, 13, 23, 10}, {0, 4, 14, 24, 10, 18, 8}};
const uint8_t edo_seventh_sus[4][7] = {{0, 5, 10, 7, 2, 9, 4}, {0, 8, 16, 11, 3, 14, 6}, {0, 13, 26, 18, 5, 23, 10}, {0, 10, 20, 14, 4, 18, 8}};
const uint8_t edo_major_ninth[4][7] = {{0, 4, 11, 2, 7, 5, 9}, {0, 6, 17, 3, 11, 8, 14}, {0, 10, 28, 5, 18, 13, 23}, {0, 8, 22, 4, 14, 10, 18}};
// The just sonorities. Steps are round(cents * EDO / 1200) of the ratio; the
// worst tone error in 31 is 5.2 cents, in 19 the septimal tones sit within 22
// cents but the neutral third rounds to the major third, since 19 has no
// neutral intervals -- that chord is only itself in 31.
const uint8_t edo_subminor_seventh[4][7] = {{0, 3, 7, 10, 2, 5, 9}, {0, 4, 11, 15, 3, 7, 14}, {0, 7, 18, 25, 5, 12, 23}, {0, 5, 14, 19, 4, 9, 18}};   // 1 7/6 3/2 7/4 | 9/8 21/16 5/3
const uint8_t edo_utonal_tetrad[4][7] = {{0, 3, 6, 10, 2, 5, 8}, {0, 4, 9, 15, 3, 8, 13}, {0, 7, 15, 25, 5, 13, 21}, {0, 5, 12, 19, 4, 10, 16}};    // 1 7/6 7/5 7/4 | 9/8 4/3 8/5
const uint8_t edo_harmonic_ninth[4][7] = {{0, 2, 4, 10, 7, 6, 8}, {0, 3, 6, 15, 11, 9, 13}, {0, 5, 10, 25, 18, 14, 22}, {0, 4, 8, 19, 14, 11, 17}};  // 1 9/8 5/4 7/4 | 3/2 11/8 13/8
const uint8_t edo_neutral_seventh[4][7] = {{0, 3, 7, 10, 2, 6, 9}, {0, 6, 11, 17, 2, 9, 13}, {0, 9, 18, 27, 4, 14, 22}, {0, 7, 14, 21, 3, 11, 17}};   // 1 11/9 3/2 11/6 | 12/11 11/8 18/11
const uint8_t edo_otonal_hexad[4][7] = {{0, 4, 7, 10, 2, 6, 12}, {0, 6, 11, 15, 3, 9, 19}, {0, 10, 18, 25, 5, 14, 31}, {0, 8, 14, 19, 4, 11, 24}}; // 1 5/4 3/2 7/4 | 9/8 11/8 2
// The just augmented is stacked pure major thirds. Its last extra is 125/64,
// the next third up: in twelve it rounds onto the octave, which is the closure
// that makes the equal tempered chord symmetric; in nineteen and thirty one it
// does not, so the harp plays the note that shows the chord no longer closes.
const uint8_t edo_just_augmented[4][7] = {{0, 4, 8, 12, 2, 6, 12}, {0, 6, 12, 19, 3, 9, 18}, {0, 10, 20, 31, 5, 15, 30}, {0, 8, 15, 24, 4, 12, 23}};  // 1 5/4 25/16 2 | 9/8 45/32 125/64
// In nineteen the septimal fourth 21/16 rounds onto the same step as the 9/7
// third, so the extras take the plain 4/3, as the subminor seventh does 5/3.
const uint8_t edo_supermajor_seventh[4][7] = {{0, 4, 7, 11, 2, 5, 9}, {0, 7, 11, 18, 4, 8, 15}, {0, 11, 18, 29, 6, 13, 24}, {0, 9, 14, 23, 5, 10, 19}}; // 1 9/7 3/2 27/14 | 8/7 4/3 12/7
const uint8_t edo_minor_ninth[4][7] = {{0, 3, 10, 2, 7, 5, 8}, {0, 5, 16, 3, 11, 8, 13}, {0, 8, 26, 5, 18, 13, 21}, {0, 6, 20, 4, 14, 10, 16}};
const uint8_t edo_added_ninth[4][7] = {{0, 4, 7, 2, 5, 9, 11}, {0, 6, 11, 3, 8, 14, 17}, {0, 10, 18, 5, 13, 23, 28}, {0, 8, 14, 4, 10, 18, 22}};
const uint8_t edo_six_nine[4][7] = {{0, 4, 9, 2, 7, 5, 11}, {0, 6, 14, 3, 11, 8, 17}, {0, 10, 23, 5, 18, 13, 28}, {0, 8, 18, 4, 14, 10, 22}};
const uint8_t edo_min_seventh[4][7] = {{0, 3, 10, 7, 1, 5, 8}, {0, 5, 16, 11, 2, 8, 13}, {0, 8, 26, 18, 3, 13, 21}, {0, 6, 20, 14, 2, 10, 16}};
const uint8_t edo_aug[4][7] = {{0, 4, 8, 12, 2, 5, 9}, {0, 6, 12, 19, 3, 8, 14}, {0, 10, 20, 31, 5, 13, 23}, {0, 8, 16, 24, 4, 10, 18}};
// The diminished and augmented tones are the NAMED intervals, like every other
// table here: a diminished fifth is G-flat (16 of 31, 10 of 19), not F-sharp
// (15, 9); a diminished seventh B-double-flat (24, 15), not A (23, 14); an
// augmented fifth G-sharp (20, 12), not A-flat (21, 13). These four had been
// generated by rounding cents, which lands on the neighbouring enharmonic --
// the pitches twelve cannot tell apart and 19 and 31 can.
const uint8_t edo_dim[4][7] = {{0, 3, 6, 12, 2, 5, 9}, {0, 5, 10, 19, 3, 8, 14}, {0, 8, 16, 31, 5, 13, 23}, {0, 6, 12, 24, 4, 10, 18}};
const uint8_t edo_full_dim[4][7] = {{0, 3, 6, 9, 2, 5, 12}, {0, 5, 10, 15, 3, 8, 19}, {0, 8, 16, 24, 5, 13, 31}, {0, 6, 12, 18, 4, 10, 24}};
const uint8_t edo_scale_intervals[4][7][8] = {
  {{0, 2, 4, 5, 7, 9, 11, 0}, {0, 2, 4, 7, 9, 0, 0, 0}, {0, 2, 3, 7, 10, 0, 0, 0}, {0, 2, 4, 5, 7, 8, 9, 11}, {0, 2, 3, 5, 7, 8, 10, 0}, {0, 2, 3, 5, 7, 8, 11, 0}, {0, 2, 3, 7, 10, 0, 0, 0}},
  {{0, 3, 6, 8, 11, 14, 17, 0}, {0, 3, 6, 11, 14, 0, 0, 0}, {0, 3, 5, 11, 16, 0, 0, 0}, {0, 3, 6, 8, 11, 13, 14, 17}, {0, 3, 5, 8, 11, 13, 16, 0}, {0, 3, 5, 8, 11, 13, 17, 0}, {0, 3, 5, 11, 16, 0, 0, 0}},
  {{0, 5, 10, 13, 18, 23, 28, 0}, {0, 5, 10, 18, 23, 0, 0, 0}, {0, 5, 8, 18, 26, 0, 0, 0}, {0, 5, 10, 13, 18, 21, 23, 28}, {0, 5, 8, 13, 18, 21, 26, 0}, {0, 5, 8, 13, 18, 21, 28, 0}, {0, 5, 8, 18, 26, 0, 0, 0}},
  {{0, 4, 8, 10, 14, 18, 22, 0}, {0, 4, 8, 14, 18, 0, 0, 0}, {0, 4, 6, 14, 20, 0, 0, 0}, {0, 4, 8, 10, 14, 16, 18, 22}, {0, 4, 6, 10, 14, 16, 20, 0}, {0, 4, 6, 10, 14, 16, 22, 0}, {0, 4, 6, 14, 20, 0, 0, 0}}
};
// Rows 5, 6, 9, 17 and 18 carry the same named tones as the diminished and
// augmented chords they serve: the whole tone, m7b5 pentatonic and locrian rows
// by their names, and the octatonic (dim) and offset diminished sixth (dim7)
// rows with the chord's own G-flat and B-double-flat in place of F-sharp and A,
// so the harp in modes 8 and 9 plays the chord it is under.
const uint8_t edo_chord_scale_intervals[4][19][8] = {
  {{0, 2, 4, 7, 9, 0, 0, 0}, {0, 2, 4, 6, 9, 0, 0, 0}, {0, 3, 5, 7, 10, 0, 0, 0}, {0, 2, 4, 7, 10, 0, 0, 0}, {0, 3, 5, 7, 9, 0, 0, 0}, {0, 1, 3, 4, 6, 7, 9, 10}, {0, 2, 4, 6, 8, 10, 0, 0}, {0, 2, 4, 5, 7, 8, 9, 11}, {0, 2, 3, 5, 7, 8, 9, 11}, {0, 2, 3, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 11, 0}, {0, 2, 3, 5, 7, 9, 10, 0}, {0, 2, 4, 6, 7, 9, 11, 0}, {0, 2, 4, 5, 7, 9, 10, 0}, {0, 2, 3, 5, 7, 8, 10, 0}, {0, 2, 5, 7, 9, 0, 0, 0}, {0, 2, 5, 7, 10, 0, 0, 0}, {0, 3, 5, 6, 10, 0, 0, 0}, {0, 1, 3, 5, 6, 8, 10, 0}},
  {{0, 3, 6, 11, 14, 0, 0, 0}, {0, 3, 6, 9, 14, 0, 0, 0}, {0, 5, 8, 11, 16, 0, 0, 0}, {0, 3, 6, 11, 16, 0, 0, 0}, {0, 5, 8, 11, 14, 0, 0, 0}, {0, 2, 5, 6, 10, 11, 14, 16}, {0, 3, 6, 9, 12, 15, 0, 0}, {0, 3, 6, 8, 11, 13, 14, 17}, {0, 3, 5, 8, 11, 13, 14, 17}, {0, 3, 5, 6, 10, 11, 15, 17}, {0, 3, 6, 8, 11, 14, 17, 0}, {0, 3, 5, 8, 11, 14, 16, 0}, {0, 3, 6, 9, 11, 14, 17, 0}, {0, 3, 6, 8, 11, 14, 16, 0}, {0, 3, 5, 8, 11, 13, 16, 0}, {0, 3, 8, 11, 14, 0, 0, 0}, {0, 3, 8, 11, 16, 0, 0, 0}, {0, 5, 8, 10, 16, 0, 0, 0}, {0, 2, 5, 8, 10, 13, 16, 0}},
  {{0, 5, 10, 18, 23, 0, 0, 0}, {0, 5, 10, 15, 23, 0, 0, 0}, {0, 8, 13, 18, 26, 0, 0, 0}, {0, 5, 10, 18, 26, 0, 0, 0}, {0, 8, 13, 18, 23, 0, 0, 0}, {0, 3, 8, 10, 16, 18, 23, 26}, {0, 5, 10, 15, 20, 25, 0, 0}, {0, 5, 10, 13, 18, 21, 23, 28}, {0, 5, 8, 13, 18, 21, 23, 28}, {0, 5, 8, 10, 16, 18, 24, 28}, {0, 5, 10, 13, 18, 23, 28, 0}, {0, 5, 8, 13, 18, 23, 26, 0}, {0, 5, 10, 15, 18, 23, 28, 0}, {0, 5, 10, 13, 18, 23, 26, 0}, {0, 5, 8, 13, 18, 21, 26, 0}, {0, 5, 13, 18, 23, 0, 0, 0}, {0, 5, 13, 18, 26, 0, 0, 0}, {0, 8, 13, 16, 26, 0, 0, 0}, {0, 3, 8, 13, 16, 21, 26, 0}},
  {{0, 4, 8, 14, 18, 0, 0, 0}, {0, 4, 8, 12, 18, 0, 0, 0}, {0, 6, 10, 14, 20, 0, 0, 0}, {0, 4, 8, 14, 20, 0, 0, 0}, {0, 6, 10, 14, 18, 0, 0, 0}, {0, 2, 6, 8, 12, 14, 18, 20}, {0, 4, 8, 12, 16, 20, 0, 0}, {0, 4, 8, 10, 14, 16, 18, 22}, {0, 4, 6, 10, 14, 16, 18, 22}, {0, 4, 6, 8, 12, 14, 18, 22}, {0, 4, 8, 10, 14, 18, 22, 0}, {0, 4, 6, 10, 14, 18, 20, 0}, {0, 4, 8, 12, 14, 18, 22, 0}, {0, 4, 8, 10, 14, 18, 20, 0}, {0, 4, 6, 10, 14, 16, 20, 0}, {0, 4, 10, 14, 18, 0, 0, 0}, {0, 4, 10, 14, 20, 0, 0, 0}, {0, 6, 10, 12, 20, 0, 0, 0}, {0, 2, 6, 10, 12, 16, 20, 0}}
};

/* Four chords built from ratios rather than letter names — the intervals twelve
 * notes cannot spell. They are only themselves in 31: a neutral third is 1 cent
 * from 11:9 there, 31 cents off in 19, and 47 cents off in 12, where it simply
 * collapses into a minor third. Included in all three so a slot assignment does
 * not break when the temperament changes, but they are worth reaching for in 31.
 */
const uint8_t edo_neutral[4][7] = {{0, 3, 7, 12, 2, 5, 9}, {0, 6, 11, 19, 3, 8, 14}, {0, 9, 18, 31, 5, 13, 23}, {0, 7, 14, 24, 4, 10, 18}};
const uint8_t edo_harmonic_7th[4][7] = {{0, 4, 10, 7, 2, 5, 9}, {0, 6, 15, 11, 3, 8, 14}, {0, 10, 25, 18, 5, 13, 23}, {0, 8, 19, 14, 4, 10, 18}};
const uint8_t edo_subminor[4][7] = {{0, 3, 7, 12, 2, 5, 9}, {0, 4, 11, 19, 3, 8, 14}, {0, 7, 18, 31, 5, 13, 23}, {0, 5, 14, 24, 4, 10, 18}};
const uint8_t edo_supermajor[4][7] = {{0, 4, 7, 12, 2, 5, 9}, {0, 7, 11, 19, 3, 8, 14}, {0, 11, 18, 31, 5, 13, 23}, {0, 9, 14, 24, 4, 10, 18}};
uint8_t neutral[7]      = {0, 3, 7, 12, 2, 5, 9};
uint8_t harmonic_7th[7] = {0, 4, 10, 7, 2, 5, 9};
uint8_t subminor[7]     = {0, 3, 7, 12, 2, 5, 9};
uint8_t supermajor[7]   = {0, 4, 7, 12, 2, 5, 9};

uint8_t edo_index = 0;        // which of the four divisions is live
uint8_t EDO = 12;             // steps per octave
uint8_t sharp_step = 1;       // how far a key signature's sharp or flat moves a letter
uint8_t modifier_step = 1;    // how far the modifier moves one: the same, but a quarter-tone in 24
// modifier step (349) overrides that: 0 as the division has it, 1 one step of
// the division (the diesis of 31, the quarter-tone of 24, a semitone in 19), 2
// the chromatic semitone (two steps of 24 and 31). Twelve has only the one.
uint8_t modifier_step_choice = 0;
void apply_modifier_step();
bool harp_ribbon_active();
extern uint8_t drone_mode, drone_register;   // see DRONE
void drone_set_mode(uint8_t m);

uint8_t major[7] = {0, 4, 7, 12, 2, 5, 9};  // After the four notes of the chord (fundamental, third, fifth of seven, and octave of fifth, the next notes are the second fourth and sixth)
uint8_t minor[7] = {0, 3, 7, 12, 1, 5, 8};
uint8_t maj_sixth[7] = {0, 4, 7, 9, 2, 5, 12};
uint8_t min_sixth[7] = {0, 3, 7, 9, 1, 5, 12};
uint8_t seventh[7] = {0, 4, 10, 7, 2, 5, 9};
uint8_t maj_seventh[7] = {0, 4, 11, 7, 2, 5, 9};
uint8_t min_seventh[7] = {0, 3, 10, 7, 1, 5, 8};
uint8_t aug[7] = {0, 4, 8, 12, 2, 5, 9};

//>>ALTERNATE CHORD LAYOUT<<
// Reached by tapping the modifier and then holding it, these give the same
// seven button combinations a suspended and extended reading. Four voices, so
// the ninth chords drop their fifth, which is how a player would voice them.
uint8_t sus_fourth[7]  = {0, 5, 7, 12, 2, 9, 10};   // sus4
uint8_t sus_second[7]  = {0, 2, 7, 12, 5, 9, 4};    // sus2
uint8_t seventh_sus[7] = {0, 5, 10, 7, 2, 9, 4};    // 7sus4
uint8_t major_ninth[7] = {0, 4, 11, 2, 7, 5, 9};    // maj9, no fifth
uint8_t minor_ninth[7] = {0, 3, 10, 2, 7, 5, 8};    // min9, no fifth
uint8_t added_ninth[7] = {0, 4, 7, 2, 5, 9, 11};    // add9
uint8_t six_nine[7]    = {0, 4, 9, 2, 7, 5, 11};    // 6/9
uint8_t half_dim[7]    = {0, 3, 6, 10, 2, 5, 8};    // m7b5
// Chords the divided octaves are for. Each is a just sonority, its steps
// rounded per division; the ratios and the rounding are in the edo_ tables
// below. In 12 most of them collapse onto chords already here -- the subminor
// seventh onto m7, the utonal tetrad onto m7b5 -- which is the point: twelve
// cannot tell them apart, nineteen almost can, thirty one can.
// The four voices take the first four entries; the remaining tones of the
// larger sonorities live in the extras, which the rhythm voices and the harp
// spell. Entries ascend, so a harp strum through them ascends too.
uint8_t subminor_seventh[7] = {0, 3, 7, 10, 2, 5, 9};    // 12:14:18:21
uint8_t utonal_tetrad[7]    = {0, 3, 6, 10, 2, 5, 8};    // 1/7:1/6:1/5:1/4
uint8_t harmonic_ninth[7]   = {0, 2, 4, 10, 7, 6, 8};    // 4:5:6:7:9, fifth in the extras
uint8_t neutral_seventh[7]  = {0, 3, 7, 10, 2, 6, 9};    // 1 11/9 3/2 11/6
uint8_t otonal_hexad[7]     = {0, 4, 7, 10, 2, 6, 12};   // 4:5:6:7:9:11
uint8_t just_augmented[7]   = {0, 4, 8, 12, 2, 6, 12};   // 16:20:25
uint8_t supermajor_seventh[7] = {0, 4, 7, 11, 2, 5, 9};  // 14:18:21:27
// Double-tapping the modifier toggles up to three parameters between their
// stored values and chosen ones, and back: a preset's other side, say the
// alternate chord layout, a different harp mode and the knobs' alternates, all
// at once. Which parameters and which values are up to the player, so the
// gesture is not tied to any one feature. Each pair is a control (which
// parameter) and a value; a control of 0 leaves that pair unused.
const uint16_t modifier_tap_max = 250;  // ms: a press longer than this is a hold, not a tap
const uint16_t modifier_tap_gap = 400;  // ms: the second tap must land within this of the first
bool double_tap_engaged = false;
const uint8_t double_tap_pairs = 3;
const uint8_t double_tap_control_adress[double_tap_pairs] = {200, 209, 211};
const uint8_t double_tap_value_adress[double_tap_pairs] = {201, 210, 212};
int16_t double_tap_saved[double_tap_pairs] = {0, 0, 0};
int16_t double_tap_applied[double_tap_pairs] = {0, 0, 0};   // what each pair set, to tell whether anything has changed it since
int16_t double_tap_engaged_adress[double_tap_pairs] = {-1, -1, -1}; // what each pair is holding, latched at engage time; -1 if nothing

uint8_t dim[7] = {0, 3, 6, 12, 2, 5, 9};
uint8_t full_dim[7] = {0, 3, 6, 9, 2, 5, 12};
uint8_t alt_chord_layout = 0;   // 0 = standard chords, 1 = suspended and extended

uint8_t (*alt_chord_for(uint8_t slot))[7];
uint8_t key_signature_selection = 0; // 0=C, 1=G, 2=D, 3=A, 4=E, 5=B, 6=F, 7=Bb, 8=Eb, 9=Ab, 10=Db, 11=Gb
enum KeySig { // Enums for KeySigs
  KEY_SIG_C, KEY_SIG_G, KEY_SIG_D, KEY_SIG_A, KEY_SIG_E, KEY_SIG_B, KEY_SIG_F,
  KEY_SIG_Bb, KEY_SIG_Eb, KEY_SIG_Ab, KEY_SIG_Db, KEY_SIG_Gb,
  // enharmonic keys, reachable through the physical key change combo
  KEY_SIG_Fs, KEY_SIG_Cs, KEY_SIG_Gs, KEY_SIG_Ds, KEY_SIG_As, KEY_SIG_Es, KEY_SIG_Bs,
  KEY_SIG_Fb, KEY_SIG_Cb
};
enum Button { // Button enum in hardware order: B, E, A, D, G, C, F
  BTN_B, BTN_E, BTN_A, BTN_D, BTN_G, BTN_C, BTN_F
};
enum FrameShift { //Enums for chord frame shifts
  FRAMESHIFT_0, FRAMESHIFT_1,FRAMESHIFT_2,FRAMESHIFT_3,FRAMESHIFT_4,FRAMESHIFT_5,FRAMESHIFT_6
};
int8_t base_notes[7] = {11, 4, 9, 2, 7, 0, 5}; // Base note offsets for buttons in key of C (relative to C4 = MIDI 60), in hardware order B, E, A, D, G, C, F
const int8_t key_offsets[12] = {0, 7, 2, 9, 4, 11, 5, 10, 3, 8, 1, 6}; // Circle of fifths: semitone offset for each key’s root note relative to C: C, G, D, A, E, B, F, Bb, Eb, Ab, Db, Gb
const int8_t key_signatures[21] = {
  0, 1, 2, 3, 4, 5,       // C, G, D, A, E, B  (sharps)
  1, 2, 3, 4, 5, 6,       // F, Bb, Eb, Ab, Db, Gb  (flats)
  6, 7,                   // F#, C#  (sharps)
  7, 7, 7, 7, 7,          // G#, D#, A#, E#, B#  (seven sharps plus double sharps)
  8, 7                    // Fb, Cb  (flats, Fb also carries a double flat)
};
const int8_t sharp_notes[7][7] = { // Notes affected by sharps in each key, in hardware order (B, E, A, D, G, C, F)
  {BTN_F, -1, -1, -1, -1, -1, -1},                          // 1 sharp
  {BTN_F, BTN_C, -1, -1, -1, -1, -1},                       // 2 sharps
  {BTN_F, BTN_C, BTN_G, -1, -1, -1, -1},                    // 3 sharps
  {BTN_F, BTN_C, BTN_G, BTN_D, -1, -1, -1},                 // 4 sharps
  {BTN_F, BTN_C, BTN_G, BTN_D, BTN_A, -1, -1},              // 5 sharps
  {BTN_F, BTN_C, BTN_G, BTN_D, BTN_A, BTN_E, -1},           // 6 sharps
  {BTN_F, BTN_C, BTN_G, BTN_D, BTN_A, BTN_E, BTN_B}         // 7 sharps
};
const int8_t flat_notes[8][7] = { // Notes affected by flats in each key, in hardware order (B, E, A, D, G, C, F)
  {BTN_B, -1, -1, -1, -1, -1, -1},                          // 1 flat
  {BTN_B, BTN_E, -1, -1, -1, -1, -1},                       // 2 flats
  {BTN_B, BTN_E, BTN_A, -1, -1, -1, -1},                    // 3 flats
  {BTN_B, BTN_E, BTN_A, BTN_D, -1, -1, -1},                 // 4 flats
  {BTN_B, BTN_E, BTN_A, BTN_D, BTN_G, -1, -1},              // 5 flats
  {BTN_B, BTN_E, BTN_A, BTN_D, BTN_G, BTN_C, -1},           // 6 flats
  {BTN_B, BTN_E, BTN_A, BTN_D, BTN_G, BTN_C, BTN_F},        // 7 flats
  {BTN_B, BTN_E, BTN_A, BTN_D, BTN_G, BTN_C, BTN_F}         // 8: Fb, whose double flat is applied separately
};
// Buttons carrying a second sharp in G#, D#, A#, E#, B#
const int8_t double_sharp_notes[5][7] = {
  {BTN_F, -1, -1, -1, -1, -1, -1},                          // G#
  {BTN_F, BTN_C, -1, -1, -1, -1, -1},                       // D#
  {BTN_F, BTN_C, BTN_G, -1, -1, -1, -1},                    // A#
  {BTN_F, BTN_C, BTN_G, BTN_D, -1, -1, -1},                 // E#
  {BTN_F, BTN_C, BTN_G, BTN_D, BTN_A, -1, -1}               // B#
};
// Buttons carrying a second flat in Fb
const int8_t double_flat_notes[1][1] = {
  {BTN_B}                                                   // Fb
};

/* ---- keyboard temperaments -------------------------------------------------
 *
 * Equal temperament divides the octave into twelve identical steps, which makes
 * every key sound the same and no interval quite in tune. A major third comes
 * out 13.7 cents sharp of the 5:4 ratio it is approximating, which is what gives
 * a piano chord its faint beating.
 *
 * The alternatives here move each note a few cents, anchored on C:
 *
 *   Meantone    quarter-comma, the tuning most Renaissance and early Baroque
 *               music was written for. Major thirds are pure — 386.3 cents,
 *               zero error — paid for with fifths 5.4 cents flat. Keys far from
 *               C get progressively stranger, which is a feature: this is why
 *               that music stays near its home key.
 *   Just        five-limit just intonation for C major. Thirds and fifths pure
 *               in the home key, noticeably out in others.
 *   Pythagorean pure 3:2 fifths all the way round. Bright, wide thirds — 21.5
 *               cents sharp. Sounds right for medieval music and wrong for
 *               almost everything after it.
 *
 * The profiles themselves, twelve-note and otherwise, are in
 * temperament_profiles.h, generated by generator/temperaments.py from musical
 * definitions (fifth sizes or ratios). Offsets are whole cents from equal
 * temperament, so equal is all zeros and the default path is arithmetically
 * identical to before. To add a profile, add it at the end of PROFILES in that
 * script and run it; nothing in this file needs to change.
 */
void apply_temperament(uint8_t t);
uint8_t temperament_selection = 0;

// Temperament 13, your own: twelve offsets from equal temperament, one per
// pitch class from C, in cents (addresses 359-370, tenths of a cent from 1000).
// Set by hand, from a Scala file in the editor, or by a MIDI Tuning Standard
// scale/octave or bulk dump message (see mts_receive), which selects it.
float own_temperament_cents[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const uint8_t own_temperament = temperament_count;   // one past the generated profiles

// note number to frequency ratio. In the twelve-note tunings the note is nudged
// by a few cents; in 19 and 31 the octave is simply divided differently.
static inline float tempered_ratio(float note) {
  if (edo_index == 0) {
    int n = (int)floorf(note);
    int pc = ((n % 12) + 12) % 12;
    float cents = temperament_selection == own_temperament ? own_temperament_cents[pc]
                                                           : temperament_profiles[temperament_selection].cents[pc];
    return powf(2.0f, (note + cents / 100.0f) / 12.0f);
  }
  return powf(2.0f, note / (float)EDO);
}

// Purity (358): every note is drawn from its tempered pitch toward the just
// interval it makes with the chord's root, as far as purity says, so a held
// chord goes from beating to still under a knob or a hand: thirds to 5/4 and
// 6/5, fifths to 3/2, a dominant's seventh to 7/4, the neutral third of 31 to
// 11/9. The root stays where the temperament puts it, so the tuning never
// drifts from chord to chord, as tuning everything to the last chord would.
// The harp is drawn toward the same root, so it sits in the chord. A step
// that is no simple ratio within reach (25 cents, 40 for a dominant's 7/4)
// keeps its tempered pitch. rebuild_purity() makes purity_ratio[], one factor
// per step above the root, on a change of chord, purity or tuning.
uint8_t purity = 0;                // 0-100
int16_t purity_root_pc = 0;        // the chord root, transposed, in steps of the division
float purity_ratio[31] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
void rebuild_purity();
void purity_follow_chord(bool from_chord = false);

__attribute__((noinline)) float temper_ratio(float note) {   // called from many places: one copy keeps RAM1 code down
  float r = tempered_ratio(note);
  if (purity == 0) return r;
  int n = (int)floorf(note);
  return r * purity_ratio[(((n - purity_root_pc) % EDO) + EDO) % EDO];
}

// a minor third in the live temperament, for the relative-minor harp modes
static inline int8_t minor_third_steps() {
  return edo_index == 0 ? 3 : edo_index == 1 ? 5 : edo_index == 2 ? 8 : 6;
}

// MIDI note numbers are integers, so a 31st of an octave has nowhere to go.
// Rescale to the nearest semitone: the recording lands in the right register and
// is audibly an approximation, rather than silently absurd.
static inline uint8_t midi_out_note(int16_t note) {
  int32_t v = (edo_index == 0) ? note : (int32_t)lroundf(note * 12.0f / (float)EDO);
  if (v < 0) v = 0;
  if (v > 127) v = 127;
  return (uint8_t)v;
}

float c_frequency = 130.81;                      // for C3

//>>SCALAR HARP MODE<<
// 0 follows the chord as before. 1-7 are fixed scales rooted on the key. 8 and 9
// pick a scale to suit whichever chord is currently held, 9 being the pentatonic
// version of 8. 10 and 11 play the custom scale and 12 and 13 the generator
// scale, rooted on the key and on the chord respectively.
uint8_t scalar_harp_selection = 0;

// Tonic pitch class for each key signature, in the order of the KeySig enum
int8_t scale_root_offsets[21] = {
  0, 7, 2, 9, 4, 11,       // C, G, D, A, E, B
  5, 10, 3, 8, 1, 6,       // F, Bb, Eb, Ab, Db, Gb
  6, 1, 8, 3, 10, 5, 0,    // F#, C#, G#, D#, A#, E#, B#
  4, 11                    // Fb, Cb
};

// Fixed scales for modes 1-7, semitones from the root
uint8_t scale_intervals[7][8] = {
  {0, 2, 4, 5, 7, 9, 11, 0}, // 1: Major (Ionian)
  {0, 2, 4, 7, 9, 0, 0, 0},  // 2: Major Pentatonic
  {0, 2, 3, 7, 10, 0, 0, 0}, // 3: Minor Pentatonic
  {0, 2, 4, 5, 7, 8, 9, 11}, // 4: Diminished 6th
  {0, 2, 3, 5, 7, 8, 10, 0}, // 5: Relative Natural Minor
  {0, 2, 3, 5, 7, 8, 11, 0}, // 6: Relative Harmonic Minor
  {0, 2, 3, 7, 10, 0, 0, 0}  // 7: Relative Minor Pentatonic
};
const uint8_t scale_lengths[7] = {7, 5, 5, 8, 7, 7, 5};

// Scales chosen per chord type for modes 8 and 9
uint8_t chord_scale_intervals[19][8] = {
  {0, 2, 4, 7, 9, 0, 0, 0},  //  0: Major Pentatonic, major chord
  {0, 2, 4, 6, 9, 0, 0, 0},  //  1: Lydian Pentatonic, major seventh
  {0, 3, 5, 7, 10, 0, 0, 0}, //  2: Minor Pentatonic, minor
  {0, 2, 4, 7, 10, 0, 0, 0}, //  3: Mixolydian Pentatonic, dominant seventh
  {0, 3, 5, 7, 9, 0, 0, 0},  //  4: Dorian Pentatonic, minor seventh
  {0, 1, 3, 4, 6, 7, 9, 10}, //  5: Octatonic, diminished
  {0, 2, 4, 6, 8, 10, 0, 0}, //  6: Whole Tone, augmented
  {0, 2, 4, 5, 7, 8, 9, 11}, //  7: Diminished 6th, major sixth
  {0, 2, 3, 5, 7, 8, 9, 11}, //  8: Diminished 6th Minor, minor sixth
  {0, 2, 3, 4, 6, 7, 9, 11}, //  9: Offset Diminished 6th, full diminished
  {0, 2, 4, 5, 7, 9, 11, 0}, // 10: Ionian, major
  {0, 2, 3, 5, 7, 9, 10, 0}, // 11: Dorian, minor seventh
  {0, 2, 4, 6, 7, 9, 11, 0}, // 12: Lydian, major seventh
  {0, 2, 4, 5, 7, 9, 10, 0}, // 13: Mixolydian, dominant seventh
  {0, 2, 3, 5, 7, 8, 10, 0}, // 14: Aeolian, minor
  // For the alternate layout's chords. The suspended ones leave the third out
  // altogether rather than pick one, since that ambiguity is the whole point of
  // a sus chord and a harp landing on a third resolves it for you.
  {0, 2, 5, 7, 9, 0, 0, 0},  // 15: Suspended Pentatonic (1 2 4 5 6), sus2 and sus4
  {0, 2, 5, 7, 10, 0, 0, 0}, // 16: Suspended b7 Pentatonic (1 2 4 5 b7), 7sus4
  {0, 3, 5, 6, 10, 0, 0, 0}, // 17: Half-diminished Pentatonic (1 b3 4 b5 b7), m7b5
  {0, 1, 3, 5, 6, 8, 10, 0}  // 18: Locrian, m7b5
};
const uint8_t chord_scale_lengths[19] = {5, 5, 5, 5, 5, 8, 6, 8, 8, 8, 7, 7, 7, 7, 7, 5, 5, 5, 7};
// The letter degree each scale entry names (0 = root, 1 = second, 2 = third, ...
// 6 = seventh), so a chord tone can take the place of the scale's own third or
// seventh rather than whichever entry happens to lie nearest it. The degrees are
// the same in every division; only the steps are retuned. The octatonic, whole
// tone and diminished sixth rows name a degree twice or not at all, so they are
// left unlabelled (DEG_NONE) and fall back to the nearest entry -- none of the
// chords that are retuned maps to them.
#define DEG_NONE 255
const uint8_t chord_scale_degrees[19][8] = {
  {0, 1, 2, 4, 5}, {0, 1, 2, 3, 5}, {0, 2, 3, 4, 6}, {0, 1, 2, 4, 6}, {0, 2, 3, 4, 5},
  {DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE},
  {DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE},
  {DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE},
  {DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE},
  {DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE, DEG_NONE},
  {0, 1, 2, 3, 4, 5, 6}, {0, 1, 2, 3, 4, 5, 6}, {0, 1, 2, 3, 4, 5, 6}, {0, 1, 2, 3, 4, 5, 6}, {0, 1, 2, 3, 4, 5, 6},
  {0, 1, 3, 4, 5}, {0, 1, 3, 4, 6}, {0, 2, 3, 4, 6}, {0, 1, 2, 3, 4, 5, 6}
};

// A user-defined scale, entered as one toggle per chromatic degree over sysex.
// custom_scale_mask holds the toggles; rebuild_custom_scale() collapses them
// into the same ascending interval list the fixed scales above use.
// One bit per chromatic degree, bit 0 = root. 0b101010110101 is the major
// scale, which is the default. Twelve bits is 4095 at most, well inside what
// sysex can carry.
uint16_t custom_scale_mask = 0b101010110101;
// The step scale (addresses 344-346): one bit per step of the division, eleven
// to an address, so 344 holds steps 0-10 (bit 0 the root), 345 11-21 and 346
// 22-30. In 19, 24 and 31 any step ticked below the division's size makes it the
// scale of modes 10 and 11 in place of the custom scale; in twelve, and with
// none ticked, the custom scale plays. Steps past the division are ignored, so a
// scale written in 31 keeps its lower steps in 19 rather than wrapping them.
const uint8_t step_scale_bits = 11;
uint16_t step_scale_mask[3] = {0, 0, 0};
uint8_t custom_scale_intervals[31] = {0, 2, 4, 5, 7, 9, 11};
uint8_t custom_scale_length = 7;

// Melodic direction (351): a raga's aroha and avaroha, a maqam's way up and
// way down, melodic minor. With it on, the harp in modes 10, 11, 14 and 15
// plays a scale's descending form while the melody goes down: each string
// plucked lower than the one before (a strum down, a falling line) takes its
// note from the down form, each higher from the up form, and a string plucked
// again keeps the direction it had. On the ribbon the finger's slide says.
// Strings already ringing keep their pitch. The down form of the custom scale
// is custom scale down (352, by name) or step scale down (353-355, in steps of
// 19, 24 and 31), built as the up form is; with neither ticked the scale goes
// down as it went up. The maqams carry their own.
bool melodic_direction = false;
bool harp_descending = false;    // the melody's direction, for the forms
uint16_t custom_scale_down_mask = 0;
uint16_t step_scale_down_mask[3] = {0, 0, 0};
uint8_t custom_down_intervals[31];
uint8_t custom_down_length = 0;  // 0: the scale goes down as it went up

// The maqams (scalar harp modes 14 and 15, maqam 356): each a scale of seven,
// as it goes up, as it comes down, and with its other upper jins (357), the
// swap a player makes to move the maqam on, written in quarter-tones (steps of
// 24) from the tonic and rounded to the live division: 31 holds the neutral
// steps within a few cents (7 of 24 is 9 of 31), 12 and 19 approximate them.
// The common forms, as maqamworld.com gives them:
//   0 Rast        C D E‡ F G A B‡     down B♭, upper Hijaz on G: Suznak
//   1 Bayati      D E‡ F G A B♭ C     upper Bayati on A: Husseini
//   2 Saba        D E‡ F G♭ A B♭ C    lower Hijaz: Saba Zamzam
//   3 Hijaz       D E♭ F# G A B‡ C    down B♭, upper Hijaz on G: Hijazkar
//   4 Sikah       E‡ F G A B‡ C D     upper Hijaz on G: Huzam
//   5 Nahawand    C D E♭ F G A♭ B     down B♭, upper Kurd on G
//   6 Kurd        D E♭ F G A B♭ C
//   7 Ajam        C D E F G A B       upper Nahawand on G
const uint8_t maqam_count = 8;
const uint8_t maqam_quarter_tones[maqam_count][3][7] = {
  {{0, 4, 7, 10, 14, 18, 21}, {0, 4, 7, 10, 14, 18, 20}, {0, 4, 7, 10, 14, 16, 22}},
  {{0, 3, 6, 10, 14, 16, 20}, {0, 3, 6, 10, 14, 16, 20}, {0, 3, 6, 10, 14, 17, 20}},
  {{0, 3, 6, 8, 14, 16, 20},  {0, 3, 6, 8, 14, 16, 20},  {0, 2, 6, 8, 14, 16, 20}},
  {{0, 2, 8, 10, 14, 17, 20}, {0, 2, 8, 10, 14, 16, 20}, {0, 2, 8, 10, 14, 16, 22}},
  {{0, 3, 7, 11, 14, 17, 21}, {0, 3, 7, 11, 14, 17, 21}, {0, 3, 7, 9, 15, 17, 21}},
  {{0, 4, 6, 10, 14, 16, 22}, {0, 4, 6, 10, 14, 16, 20}, {0, 4, 6, 10, 14, 16, 20}},
  {{0, 2, 6, 10, 14, 16, 20}, {0, 2, 6, 10, 14, 16, 20}, {0, 2, 6, 10, 14, 16, 20}},
  {{0, 4, 8, 10, 14, 18, 22}, {0, 4, 8, 10, 14, 18, 22}, {0, 4, 8, 10, 14, 18, 20}}
};
uint8_t maqam_selection = 0;
bool maqam_upper_jins = false;
uint8_t maqam_up[7] = {0, 4, 7, 10, 14, 18, 21};   // in steps of the live division
uint8_t maqam_down[7] = {0, 4, 7, 10, 14, 18, 20};
void rebuild_maqam();
const uint8_t custom_scale_max_octave = 3; // how far the harp may climb, in octaves, in any scale mode

// A scale made of one interval, the generator, stacked so many times and folded
// into the octave (addresses 267-269). That is how a regular temperament makes
// its scales: in 31, seven steps nine times over is Orwell[9] (its generator is
// 19 steps of 84, a 7/6 of 271 cents, whence the name), eighteen seven times is
// the meantone diatonic and nine seven times Mohajira. The generator counts
// steps of the live division, so the same numbers make another scale in
// another: in 12, 7 is the fifth and seven of them the major scale. The mode
// says how many of the generators lie below the root, which picks the rotation:
// 0 builds the chain straight up from it. A chain that comes back on itself
// before it is done (4 in 12 closes after three) keeps the notes it has.
// rebuild_generator_scale() makes the interval list, on any change to these and
// to the division; modes 12 and 13 play it as 10 and 11 play the custom scale.
uint8_t generator_steps = 7;
uint8_t generator_size = 9;
uint8_t generator_mode = 0;
uint8_t generator_scale_intervals[31] = {0, 1, 2, 4, 6, 7, 8, 9, 11};   // 7 in 12, nine times
uint8_t generator_scale_length = 9;
uint8_t chord_octave_change=4;
uint8_t harp_octave_change=4;
uint8_t chord_frame_shift=0;
uint8_t transpose_semitones=0;                       // to use to transpose the instrument, number of semitones
uint8_t transpose_steps=0;                           // the same transposition in steps of the live division; equal to transpose_semitones in 12
uint8_t (*current_chord)[7] = &major;            // the array holding the current chord
uint8_t current_chord_notes[7];                  // the array for the note calculation within the chord, calculate 7 of them for the arpeggiator mode
uint8_t current_applied_chord_notes[7];          // the array for the note calculation within the chord
uint8_t current_harp_notes[12];                  // the array for the note calculation within the string

//>>SWITCHING LOGIC GLOBAL VARIABLES<<
int8_t current_line = -1;      // holds the current selected line of button, -1 if nothing is on
int8_t fundamental = 0;        // holds the value of the last selected line, hence the fundamental
uint8_t slash_value = 0;       // stores the "slash", ie when a different alternative note is selected
bool slash_chord = false;      // flag for when a slashed chord is currently activated
bool button_pushed = false;    // flag for when any button has been pushed during the main loop
bool trigger_chord = false;    // flag to trigger the enveloppe of the chord
bool sharp_active = false;     // flag for when the sharp is active
// The modifier and slash state the chord and harp notes were last worked out
// with. Both flags follow the buttons, so once a chord is released (or held by
// the hold button) they no longer describe what is sounding; these do.
bool chord_context_sharp = false;
bool chord_context_slashed = false;
bool flat_button_modifier= false; //flag to set the modifier to flat instead of sharp
// ms a change to the set of chord type buttons held on the sounding line must
// stand before it is believed, when the chord is already established. See
// handle_chord_type().
const uint16_t chord_type_grace = 60;
bool continuous_chord = false; // wether the chord is held continuously. Controlled by the "hold" button
bool rythm_mode = false;
bool barry_harris_mode = false;
IntervalTimer note_timer[4]; // timers for delayed chord enveloppe
// Whether each chord voice has been released since its last note on. A release
// has to reach a voice in whatever stage it is in, not only once it has
// settled into sustain, and remembering that it was sent lets the loop, which
// runs the check every pass, send it once.
volatile bool chord_voice_released[4] = {true, true, true, true};
bool inhibit_button=false;

//>>SWITCHING LOGIC PARAMETERS<<
uint8_t note_slash_level = 0;     // the level we are replacing in the chord when slashing (usually the fundamental)
// Slash voice: which of the four chord voices, counted from the bottom (1 bass,
// 2 tenor, 3 alto, 4 soprano), a slash replaces. 0 leaves slashes to slash
// level, exactly as before; see apply_slash_voice().
uint8_t slash_voice = 0;
// Slash re-voice: with a tenor, alto or soprano slash, the other three voices
// re-voice around it to keep the chord whole, instead of holding still.
uint8_t slash_revoice = 0;
// Cantus: which chord voice the harp sets, the way slash voice lets a slash set
// one. Counted from the bottom like slash voice (1 bass, 2 tenor, 3 alto,
// 4 soprano), plus 5 for whichever voice is nearest the note; 0 is off. See
// apply_cantus() and cantus_follow().
uint8_t cantus_voice = 0;
// The pitch class the harp last gave the cantus, in steps of the live division,
// or -1 when the harp is not steering a voice.
int16_t cantus_pc = -1;
// ms of quiet on the harp before a pluck counts as a new gesture. Only the first
// string of a strum moves the cantus; the rest of the strum is accompaniment.
const uint16_t cantus_strum_gap = 80;
// Brothers (390): the harp's note chooses the chord, its own or its brother,
// as a block with that note on top. 0 off, 1 close position. See
// apply_brothers().
uint8_t brothers_mode = 0;
// The harp note that last chose the block, in steps of the live division, or
// -1 when the harp is not choosing the chord.
int16_t brothers_note = -1;
// Brothers again (394): the same harp note struck again re-voices the block
// under it, the way the held chord pressed again (324) climbs the chord. 0 off,
// 1 climbs close, drop 2, drop 3, drop 2 and 4 and round again, 2 climbs and
// comes back down, 3 close and drop 2 in turn. See brothers_again_step().
uint8_t brothers_again = 1;
uint8_t brothers_drop = 0;      // 0 close, 1 drop 2, 2 drop 3, 3 drop 2 and 4
int8_t brothers_drop_dir = 1;   // which way pattern 2 is going
// The chord slot the harp and rhythm mode's extra voices replace while slashed.
// Slash level itself when slash voice is off; otherwise the slot of whichever
// tone the chosen voice gave up, so they follow the chord as it sounds.
uint8_t slash_slot_effective = 0;
bool retrigger_chord = true;      // wether or not to retrigger the enveloppe when the chord is switched within current line (including when selecting slash chord)
bool change_held_strings = false; // to control wether hold strings change with chord:
bool chromatic_harp_mode = false; // to switch the harp to chromatic mode
// Harp rank: which set of twelve steps the chromatic harp plays, 1 for steps
// 0-11 of the division, 2 for 12-23, 3 for 24-35, like the rows of strings on a
// triple harp. A division has as many ranks as it takes twelve strings to cover
// it: one in 12, two in 19 and 24, three in 31. A higher rank than the division has
// plays its last, so in 12 the setting changes nothing.
uint8_t harp_rank = 1;
// Palm mute: this many harp pads held at once is a hand laid across the strings
// rather than a strum, and stops everything ringing. 0 is off. Pads under the
// palm are silent until they are lifted, so the landing hand is a short chuck,
// not a chord; other pads still play while it is held. See handle_harp().
uint8_t palm_mute_pads = 0;
uint16_t palm_mute_release = 15;   // ms, how fast a palm stops the strings
uint16_t string_release = 1000;    // the strings' own release, put back after a palm mute
// Harp pluck on lift: a touch stops the string and the lift plucks it, as a
// harpist's finger does. 0 plucks on touch, as before.
bool harp_pluck_on_lift = false;
bool string_plucked[12] = {false, false, false, false, false, false, false, false, false, false, false, false};   // plucked on lift, not yet ringing out
// Koto press: plucking on lift, a finger on a ringing string presses it rather
// than stopping it, as a koto player presses a string beside the bridge, and
// the string bends up as far as it is pressed: this many semitones at a full
// press, 0 off. Easing off lets it back down. Lifting a finger that pressed
// lets the string ring on at its own pitch; one that only rested plucks it
// again. On the stock strip and the plates of twelve zones. See koto_follow().
uint8_t koto_press = 0;
// In 19, 24 and 31 a bend counted in semitones lands between the division's
// steps. koto press steps counts koto press in steps of the division instead
// (in twelve they are the same), and koto press snap holds the bend to each
// step it passes, that much of each step's width in tune, as ribbon snap holds
// the ribbon to its strings: meend through the shrutis rather than past them.
bool koto_press_steps = false;
uint8_t koto_press_snap = 0;    // 0-100
bool string_pressing[12] = {false, false, false, false, false, false, false, false, false, false, false, false};   // a finger is on this ringing string
bool string_pressed[12] = {false, false, false, false, false, false, false, false, false, false, false, false};    // ...and has bent it, so its lift plucks nothing
int16_t press_rest[12] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // the lightest it has read since settling, which a press is measured from
// Hammer-on: plucking on touch, a finger landing beside a string still held down takes that string
// over: it moves to the new pad's note without being plucked again, as a guitarist's finger
// hammering onto the next fret does, and on along the strip as the finger slides. Lifting the pad
// it sounds while another of its pads is held pulls it off to that one; lifting the last lets it
// ring out. On the stock strip and the plates of twelve zones. See hammer_land().
bool hammer_on = false;
uint16_t hammer_after_ms = 180;        // 376: how long a string rings before a finger beside it takes it over
int8_t pad_voice[12] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // the string each held pad has hold of, -1 none
uint32_t voice_plucked_ms[12] = {0};   // when each string was last plucked
// Pads arriving this soon after a palm mute are part of the palm. A slap's
// last finger or the heel of the hand can trail the rest by a hundred ms or
// more; plucking on touch the window is kept short so a strum can follow the
// mute at once, but plucking on lift a straggler would be plucked as the palm
// lifts, and a touch there sounds nothing anyway.
const uint8_t palm_landing_ms = 30;
const uint8_t palm_landing_lift_ms = 150;
const uint8_t palm_spread_ms = 40;   // plucking on lift, a palm's pads land within this of one another
bool string_palmed[12] = {false, false, false, false, false, false, false, false, false, false, false, false};
// Harp plate: which plate is fitted. The released plates come first: 0 is the
// stock strip and any plate of twelve separate zones, 1 the Keymaster, 2 the
// piano plate (the neighbour guard, below), 3 twin rows and 4 framed strings;
// all but the piano play as the stock strip, each with its own touch
// calibration (see harp_plate_strength). The stock strip and framed strings
// have the strip guard (417), and the stock strip's own thresholds are lower
// while it is on; the Keymaster plays at the stock thresholds and needs no guard
// (see apply_harp_thresholds()). The plates not out yet start at 8,
// read between their zones as well as on them. Zipper 24's neighbouring zones
// interlock so a finger between two touches both, with one more band above the
// top zone where it meets an island of the bottom zone: 24 positions. The arcade
// wheel plays its twelve zones as they are and sends where a finger is round its
// ring. The fretless wrap adds an island of the bottom zone above the top one:
// twelve strings as the stock strip, and a thirteenth place for the ribbon. The
// fretless plate is twelve zones in a line, so it is 0: it is made for the harp
// ribbon, below, and plays twelve strings without it. 11 belongs to a plate on
// another branch. Before firmware 93 zipper 24, the arcade wheel and the
// fretless wrap were 1 to 3, and that plate 4: apply_preset_version moves them.
const int16_t harp_plate_adress = 243;
const uint8_t plate_stock = 0, plate_keymaster = 1, plate_piano = 2, plate_twin_rows = 3, plate_framed_strings = 4;
const uint8_t plate_zipper_24 = 8, plate_arcade_wheel = 9, plate_fretless_wrap = 10;
uint8_t harp_plate = plate_stock;
// The neighbour guard (address 416), for the piano plate (harp plate 2), see neighbour_guard_pass()
uint8_t neighbour_guard = 5;          // tenths: a key landing under this share of a touching neighbour stays silent; 0 off
const uint8_t neighbour_guard_promote = 8;   // tenths: a silent key that grows to this share plays
const uint16_t neighbour_guard_attack_ms = 30;   // how long after landing a silent key's own attack can let it off
const int16_t neighbour_guard_attack = 8;        // counts it must grow by beyond its neighbours' growth together
// The strip guard (address 417): the same on the stock strip (plate_stock) and framed strings (plate_framed_strings), in tenths
// as 416, 9 by default; 0 plays every pad touched, as the stock strip always did, at the stock thresholds
uint8_t strip_guard = 9;
const uint8_t strip_guard_promote = 10;   // tenths: on the strip a silent string plays once level with its neighbour
uint16_t came_from[12];               // the pads already held as each pad last landed, as bits, for a strum's runs
bool guard_carried[12] = {false};     // a shadow a run let off this pass, to land with it
bool pad_shadow[12] = {false};        // a key held silent as the edge of a finger on its neighbour
uint32_t shadow_since[12];            // when it landed
int16_t shadow_from[12][12];          // every key's strength as it landed
uint8_t harp_touch_threshold = 0;     // 0 for the stock thresholds, see harp::set_thresholds()
uint8_t harp_release_threshold = 0;
// On a zipper plate each zone's core and each band between two zones is a
// position, a string of its own: 2i is zone i's core (in harp order, 0 the
// lowest) and 2i+1 the band above it. The scales carry on up through all of
// them. Position p sounds on voice p % 12, so a voice's envelope, its midi note
// and the palm mute work as they always have. Chord mode plays the chord's
// tones across twelve strings with nothing between them, so in that mode the
// plate is read as twelve zones, as the stock one is. See handle_harp_positions().
uint8_t harp_voice_position[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};   // the position each voice last sounded
bool harp_voice_held[12] = {false, false, false, false, false, false, false, false, false, false, false, false};
struct harp_touch {
  bool active;
  uint8_t position;
  float centre;      // where it read last, in positions, which it is followed by
  float peak;        // the strongest the touch has read lately, to tell a finger lifting from one moving
  uint32_t peak_at;  // ms, when the peak was last relaxed
  bool palmed;       // part of a palm mute: silent until lifted
  bool landing;      // still pressing down, where it is not yet settled
  uint32_t landed_at;
  float centre_sum;  // the centres read while landing, weighted by how firmly
  float weight_sum;
};
const uint8_t harp_touch_max = 6;
harp_touch harp_touches[harp_touch_max];
// Harp ribbon: the strip is one fretless string, a ribbon controller. A finger's
// place along it, read from the balance between the zones it covers, sets the
// pitch, gliding from one string's note to the next as it slides; each finger
// sounds a voice of its own. On the fretless plate, whose neighbouring zones
// taper into one another, the place moves evenly along the strip. On the stock
// strip and the other plates of separate zones a finger mostly reads one zone,
// so the pitch rests on each string's note and slides where the finger bridges
// two, and ribbon glide smooths the steps into a slide. See handle_harp_ribbon().
bool harp_ribbon = false;
uint8_t ribbon_span = 0;        // strings from the bottom of the strip to the top, 0 for the twelve
uint8_t ribbon_snap = 0;        // 0-100: how much of each string's width holds its note, 0 fretless
uint8_t ribbon_step_snap = 0;   // 0-100: the same between two strings, for each step of the tuning they span
uint8_t ribbon_glide_ms = 0;    // how slowly the pitch follows the finger, in ms; 0 for the stock 10
// Semitones each voice sits above the note it is tuned to: where a ribbon touch
// has slid to since it landed. 0 for every other way of playing.
float harp_voice_glide[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
struct ribbon_touch {
  bool active;
  bool palmed;
  bool landing;
  bool sounding;     // landed and given a voice, rather than silenced by a palm as it landed
  uint8_t voice;
  uint8_t string;    // the string whose note the voice's midi note is
  float centre;      // where it read last, in zones, which it is followed by
  float place;       // where it sounds, in zones: the centre, smoothed by ribbon glide
  float peak;
  uint32_t peak_at;
  uint32_t landed_at;
  uint32_t moved_at; // micros, when the place last followed the centre
  float centre_sum;
  float weight_sum;
};
ribbon_touch ribbon_touches[harp_touch_max];
//>>SYSEX PARAMETERS<<
// SYSEX midi message are used to control up to 512 synthesis parameters, in two pages of 256.
//
// Page 0 (0-255) is every setting there was before the array grew, and stays exactly as it was
// everywhere, so no existing preset changes: the preset files, the factory presets below, the
// dump minicontrol (and the Sound Lab, the Lab) read on command 0, and preset codes. Page 1
// (256-511) is new settings. A preset file holds all 512 now, but one saved before holds 256, and
// loading starts every slot at its default (parameter_default) before the file is read over it,
// so page 1 comes up at defaults; firmware from before the growth reads the first 256 of a longer
// file and keeps page 0. Page 1 goes to an editor only when asked (command 7), with a header, so
// no tool that knows only page 0 can take it for a dump. 382, 383, 510 and 511 stay unused: their
// low byte is a universal SysEx id (0x7E, 0x7F), as the first byte of a write. 290-301 hold the
// preset's name, two ASCII letters to each, which the firmware only keeps for an editor to show.
const uint16_t parameter_size = 512;
const uint16_t page_size = 256;
const uint8_t preset_number = 12;
static_assert(sizeof(parameter_control) == parameter_size, "parameter_lookup.h is for another array size: run the generator");
static_assert(sizeof(parameter_page1_defaults) / sizeof(parameter_page1_defaults[0]) == parameter_size - page_size, "parameter_lookup.h is for another array size: run the generator");
// The factory presets' page 0, tuned by hand; page 1's defaults are generated, the same for every
// bank (parameter_page1_defaults in parameter_lookup.h, from parameters.json)
int16_t default_bank_sysex_parameters[preset_number][page_size] = {
  {0,0,50,50,512,512,512,0,0,0,192,100,49,100,184,100,157,100,0,0,0,0,0,0,43,0,50,37,38,67,0,0,0,0,0,0,0,0,0,0,0,16,0,8,8,12,42,1171,1,423,20,70,3,35,83,59,2658,1,0,0,0,0,0,0,0,1,1,1,100,1,1,0,1,1,1,1,14,0,0,70,0,0,0,100,0,6,0,0,755,195,23,61,29,0,0,0,0,108,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,2,13,8,100,16,0,200,0,0,50,0,50,18,32,50,0,0,10,66,353,65,995,1,569,16,141,32,83,28,48,54,1,0,0,0,56,0,389,0,20,0,0,0,0,1,1,1,0,1,1,0,1,1,1,1,0,0,0,70,0,0,0,100,0,64,0,0,80,16,4,94,753,474,70,5,100,100,101,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,16,0,6,6,32,0,6,0,16,0,6,6,32,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,194,100,85,100,60,100,61,100,0,0,10,1,0,0,0,0,0,0,0,100,0,0,0,0,0,0,0,0,0,0,4,25,8,3,29,18,65,488,3,159,25,70,5,18,4,26,6,1,77,0,587,32,0,390,0,76,1,1,100,1,1,68,17,14,22,20,0,340,1682,70,48,0,0,100,18,54,0,0,800,70,57,100,100,0,0,0,0,86,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,4,7,0,100,0,0,100,6,8,200,2,50,36,75,50,28,0,3,1,1,80,1218,2,1659,38,114,19,8,32,80,1,1,0,30,0,0,0,0,0,0,0,24,0,3,1,1,1,100,1,1,100,1,1,1,1,31,0,0,70,0,0,0,100,0,33,2,1,162,16,4,100,100,1436,118,100,50,32,63,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,7,0,0,0,13,0,4,0,7,0,0,2,13,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,192,100,49,100,184,100,157,100,0,0,30,0,1,1,87,33,62,12,80,67,0,0,0,0,0,0,0,0,0,0,0,6,3,8,8,12,42,1855,1,42,20,217,3,35,83,59,2658,1,185,0,282,14,0,247,11,1,1,1,100,1,1,0,1,1,1,1,14,0,0,70,0,0,0,100,0,23,0,0,755,195,82,61,29,0,0,0,0,160,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,1,16,0,100,28,8,200,0,0,50,0,50,18,32,50,0,0,10,66,353,44,1452,1,569,16,141,32,83,28,48,54,1,0,698,82,56,0,579,0,28,0,0,0,0,1,1,1,0,1,1,100,1,1,1,1,0,0,0,70,0,0,0,100,0,100,0,0,80,16,4,94,753,474,70,5,100,100,185,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,16,0,6,6,32,0,6,0,16,0,6,6,32,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,196,76,92,100,184,100,85,100,0,0,60,0,1,0,14,0,0,0,42,46,0,0,0,0,0,0,0,0,0,0,3,10,8,11,42,30,61,2137,1,106,22,140,4,35,83,24,1956,1,139,0,282,0,0,247,10,1,1,1,100,1,1,0,1,1,1,1,0,0,0,70,0,0,0,100,0,57,0,1,858,70,66,100,100,0,0,0,0,156,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,0,17,8,200,11,0,133,10,0,100,7,50,59,15,50,0,15,10,10,45,61,1489,1,652,16,84,32,21,15,33,19,1,0,490,0,109,0,252,0,8,0,244,1,49,1,1,1,15,1,1,100,1,1,1,1,26,479,1931,70,0,50,0,100,40,78,21,0,162,16,4,100,1000,639,140,100,47,100,65,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,65,0,12,0,0,12,0,0,65,0,6,0,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,146,100,76,100,85,79,184,100,0,0,110,0,0,0,57,0,0,0,67,58,0,0,0,0,0,0,0,0,0,0,0,7,8,8,65,12,38,1855,5,57,24,152,3,35,83,59,2658,6,137,0,640,8,0,247,11,1,1,1,100,1,1,131,4,116,3,1,11,424,1360,70,46,0,0,100,60,85,0,0,996,125,82,61,71,0,0,0,0,185,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,0,20,0,100,23,0,200,15,8,100,0,50,0,18,50,0,0,2,293,17,44,1012,1,10,33,141,32,83,28,67,239,1,0,0,0,287,0,579,0,0,0,366,0,35,5,52,12,0,1,1,100,1,1,1,1,16,0,0,70,0,0,0,100,0,50,0,0,184,16,4,100,100,657,70,86,100,100,77,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,7,0,0,2,4,2,15,0,2,2,6,2,6,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,192,60,47,56,159,100,157,100,0,0,138,1,0,0,0,0,0,0,27,74,0,0,0,0,0,0,0,0,0,0,4,14,11,3,29,18,65,1102,3,302,11,70,5,18,4,26,6,1,38,0,516,16,0,0,0,76,1,1,100,1,1,68,17,14,22,20,0,340,1682,70,48,0,0,100,18,58,0,0,800,70,43,100,100,0,0,0,0,137,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,4,10,11,100,9,0,200,4,8,200,0,50,36,75,50,0,0,3,1,1,80,1855,2,769,15,114,19,8,32,80,1,1,0,30,0,11,0,467,0,35,0,24,0,3,1,1,1,100,1,1,100,1,1,1,1,0,341,2164,70,54,0,0,100,36,42,0,1,80,16,4,100,20,2101,70,100,6,22,75,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,1,2,4,8,17,0,12,0,1,2,4,8,16,6,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,192,100,61,100,130,100,83,100,0,0,175,0,0,0,0,0,0,0,0,35,0,0,0,0,0,0,0,0,0,0,0,12,9,11,1,1,70,2141,1,383,17,302,101,103,34,30,1,12,0,0,367,17,0,363,5,1,1,1,86,1,1,0,1,1,1,1,8,0,0,70,0,0,0,100,0,33,50,0,5000,70,100,0,0,0,0,0,0,35,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,5,9,9,200,13,6,99,6,0,50,27,50,50,50,50,0,0,345,1,1,80,800,32,2200,0,70,1,1,1,100,1,1,0,30,0,0,0,166,0,6,0,0,0,0,1,1,1,0,1,1,100,1,1,1,1,0,0,0,70,0,0,0,100,0,59,100,0,80,16,4,100,100,2366,70,100,40,23,45,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,1,0,4,4,16,16,12,0,1,6,8,6,0,6,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,146,100,76,100,85,79,184,100,0,0,220,0,0,0,0,0,0,86,55,63,0,0,0,0,0,0,0,0,0,0,0,6,8,8,65,12,38,1855,5,57,24,152,3,35,83,59,2658,6,137,0,640,8,0,247,11,1,1,1,100,1,1,131,4,116,3,1,11,424,1360,70,46,0,0,100,60,84,0,0,996,125,82,61,71,0,0,0,0,197,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,0,20,0,100,23,0,200,15,8,100,0,50,0,18,50,0,0,2,293,17,44,1012,1,10,33,141,32,83,28,67,239,1,0,0,0,287,0,579,0,0,0,366,0,35,5,52,12,0,1,1,100,1,1,1,1,16,0,0,70,0,0,0,100,0,50,0,0,184,16,4,100,100,657,70,86,100,100,80,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,7,0,0,2,4,2,15,0,2,2,6,2,6,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,196,100,47,56,159,100,157,100,0,0,253,1,0,0,0,0,0,0,0,59,0,0,0,0,0,0,0,0,0,0,4,15,11,3,29,18,65,1102,3,302,11,70,5,18,4,26,6,1,38,0,516,16,0,0,12,76,1,22,100,1,1,68,17,14,22,20,0,340,1682,70,48,0,0,100,18,58,0,0,800,70,43,100,100,0,0,0,0,139,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,4,10,0,100,9,0,200,4,8,200,0,50,36,75,50,0,0,3,1,1,80,1855,2,769,15,114,19,8,32,80,1,1,0,30,0,11,0,467,0,22,0,24,0,3,1,1,1,100,1,1,100,1,1,1,1,0,341,2164,70,54,0,0,100,36,42,0,1,80,16,4,100,20,532,70,100,6,82,145,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,1,2,4,8,17,0,12,0,1,2,4,8,16,6,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,149,69,76,100,85,79,184,100,0,0,266,0,0,0,0,0,0,0,65,70,0,0,0,0,0,0,0,0,0,0,0,5,0,8,65,12,38,1855,5,57,24,152,3,35,83,59,2658,6,137,0,640,8,0,247,11,1,1,1,100,1,1,131,4,116,3,1,0,424,1360,70,46,0,0,100,60,81,0,0,996,125,82,61,71,0,0,0,0,197,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,1,20,11,100,23,0,200,15,8,100,0,50,45,17,0,0,0,2,293,17,44,1012,1,10,33,141,32,83,28,67,239,1,0,0,0,287,0,579,0,0,0,366,0,35,5,52,12,0,1,1,100,1,1,1,1,16,0,0,70,0,0,0,100,0,50,0,0,184,16,4,100,100,857,70,42,100,100,81,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,7,0,0,2,4,2,15,0,2,2,6,2,6,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,192,55,61,100,184,100,85,100,0,0,310,0,0,0,86,0,56,87,37,60,0,0,0,0,0,0,0,0,0,0,4,18,11,36,42,30,59,1410,1,30,33,140,4,35,83,24,615,1,94,0,282,15,0,247,0,1,1,1,100,1,1,0,1,1,1,1,0,0,0,70,0,0,0,100,0,40,0,1,858,70,46,100,82,0,0,0,0,186,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,1,16,8,200,18,0,133,21,0,200,2,50,59,15,50,18,15,10,66,43,35,711,1,271,16,84,32,83,733,31,54,1,0,698,0,220,0,252,0,8,0,244,1,49,1,1,1,15,1,1,100,1,1,1,1,26,479,1931,70,0,50,0,100,34,75,0,0,162,16,4,100,1000,1889,116,49,100,42,141,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,65,0,12,0,0,12,0,0,65,0,6,0,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0},
  {0,0,50,50,512,512,512,0,0,0,194,100,85,100,60,100,61,100,0,0,340,1,0,0,0,0,0,0,0,62,0,0,0,0,0,0,0,0,0,0,4,25,3,3,29,18,65,488,3,159,25,70,5,18,4,26,40,1,77,0,587,32,0,715,11,76,1,1,100,1,1,68,17,14,22,20,17,340,1682,70,48,0,0,100,18,54,0,0,800,70,57,100,100,0,0,0,0,101,0,2,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,1,5,1,100,0,0,100,10,8,200,3,50,36,75,50,28,0,3,1,1,80,1218,2,706,38,114,19,8,32,80,1,1,0,30,0,0,0,0,0,0,0,24,0,3,1,1,1,100,1,1,100,1,1,1,1,31,0,0,70,0,0,0,100,0,33,2,1,162,16,4,100,100,678,118,100,50,32,177,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,7,0,0,0,13,0,4,0,7,0,0,2,13,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0}
}; 
int16_t current_sysex_parameters[parameter_size] = {0,0,50,50,512,512,512,1,0,0,192,100,49,100,184,100,157,100,0,0,0,0,0,0,0,0,0,0,0,67,0,0,0,0,0,0,0,0,0,0,0,16,0,8,8,12,42,1171,1,423,20,70,3,35,83,59,2658,1,0,0,0,0,0,0,0,1,1,1,100,1,1,0,1,1,1,1,14,0,0,70,0,0,0,100,0,6,0,0,755,195,23,61,29,0,0,0,0,162,0,0,0,0,0,0,0,0,0,0,0,4400,0,0,0,0,0,0,0,0,0,0,2,13,8,100,16,0,200,0,0,50,0,50,18,32,50,0,0,10,66,353,65,995,1,569,16,141,32,83,28,48,54,1,0,0,0,56,0,389,0,20,0,0,0,0,1,1,1,0,1,1,0,1,1,1,1,0,0,0,70,0,0,0,100,0,38,0,0,80,16,4,94,753,474,70,5,100,100,100,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,300,30,16,0,6,6,32,0,6,0,16,0,6,6,32,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0};

// Every chord the instrument can make, in one list, so a button combination can
// be pointed at any of them rather than at a fixed table.
uint8_t (*chord_catalogue[29])[7] = {
  &major, &minor, &seventh, &maj_seventh, &min_seventh, &dim, &aug,
  &maj_sixth, &min_sixth, &full_dim, &half_dim,
  &sus_fourth, &sus_second, &seventh_sus,
  &major_ninth, &minor_ninth, &added_ninth, &six_nine,
  // The entries past eighteen keep the order of their twelve tone counterparts:
  // triads first (supermajor as major, subminor as minor, neutral as the third
  // triad twelve does not have), then the sevenths tracking dominant, major and
  // minor in that order -- 11/6 sits nearer the major seventh than the minor,
  // so the neutral seventh takes the major seat -- then the
  // utonal tetrad where m7b5 sits, the harmonic ninth with the ninths, and the
  // hexad last.
  &supermajor, &subminor, &neutral, &harmonic_7th,
  &neutral_seventh, &subminor_seventh, &utonal_tetrad, &harmonic_ninth, &otonal_hexad,
  // Appended rather than inserted, so entries nineteen to twenty seven keep
  // their meaning: the just augmented fills the augmented seat honestly, and
  // the supermajor seventh matches the septimal set on 8b8.
  &just_augmented, &supermajor_seventh
};
const uint8_t chord_catalogue_size = 29;

// Which entry of the catalogue each button combination plays in the alternate
// layout. The defaults give the suspended and extended set; any slot can be
// pointed somewhere else, so the layout doubles the available chords or just
// moves them to where a particular player wants them.
const uint8_t alt_slot_adress[7] = {202, 203, 204, 205, 206, 207, 208};
// What each slot plays when its parameter is 0. That matters for compatibility:
// a preset saved before these addresses existed holds 0 in all of them, and
// should still give the suspended and extended set rather than seven majors.
const uint8_t alt_slot_default[7] = {11, 12, 13, 14, 15, 16, 17};

uint8_t (*alt_chord_for(uint8_t slot))[7] {
  int16_t index = current_sysex_parameters[alt_slot_adress[slot]];
  if (index <= 0 || index > chord_catalogue_size) index = alt_slot_default[slot];
  else index -= 1;   // 1 selects the first catalogue entry, so 0 stays free for the default
  return chord_catalogue[index];
}

const char *bank_name[preset_number] = {"a.txt", "b.txt", "c.txt", "d.txt", "e.txt", "f.txt", "g.txt", "h.txt", "i.txt", "j.txt", "k.txt", "l.txt"};
int8_t current_bank_number = 0;
float bank_led_hue = 0;
// Reserved SYSEX adresses
// =0 is for the control command
// 1 is the bank ID
// >2 and <10 are protected
int8_t harp_volume_sysex = 2;
int8_t chord_volume_sysex = 3;
int8_t chord_pot_alternate_storage = 4;
int8_t harp_pot_alternate_storage = 5;
int8_t mod_pot_alternate_storage = 6;

// >10 and <21 are limited access, for example the potentiometer settings (we don't want a pot to control another pot sysex adress or range)
int8_t chord_pot_alternate_control = 10;
int8_t chord_pot_alternate_range = 11;
int8_t harp_pot_alternate_control = 12;
int8_t harp_pot_alternate_range = 13;
int8_t mod_pot_main_control = 14;
int8_t mod_pot_main_range = 15;
int8_t mod_pot_alternate_control = 16;
int8_t mod_pot_alternate_range = 17;

// Knob layer (address 117). At 0 the potentiometers play their main functions, and holding the
// modifier selects their alternates, as it always has, sharpening any chord played meanwhile. At 1
// they play their alternates and the modifier only sharpens, so an alternate can be turned while
// chords are sounding. It is a preset parameter like any other, and the double tap can be assigned
// to it (double tap control 117, value 1) to switch layers while playing. While it is 1 the LED shows
// the preset's colour pale, so the layer is never a hidden state.
// Switching layers asks each knob for pickup: the function it moves to keeps its value until the
// knob reaches the position that function was last set from, so nothing jumps to where the knob
// happens to be. Holding the modifier doesn't ask, and behaves as it always has.
bool knob_layer = false;
static inline float bank_led_saturation() { return knob_layer ? 0.45 : 1.0; }
// 21-39 are global parameters (switching logic, global reverb etc.)
// 40-119 are harp parameters
// 120-219 are chord parameters
// 220-235 are rythm patterns
// 236-255 are the parameters added since, when 21-219 had filled up
// What the controls can be pointed at goes by each parameter's "controls" in parameters.json
// (parameter_control, in parameter_lookup.h), not by its address: the knobs and hover sweep, so
// they take only "all"; the double tap sets an exact value, so "tap" too.
static inline bool control_can_sweep(int16_t adress) {
  return adress >= 0 && adress < (int16_t)parameter_size && parameter_control[adress] == PARAMETER_CONTROL_ALL;
}
static inline bool control_can_tap(int16_t adress) {
  return adress >= 0 && adress < (int16_t)parameter_size && parameter_control[adress] >= PARAMETER_CONTROL_TAP;
}
bool sysex_controler_connected=false; //bool to remember if there is a controller that is connected to avoid saving any change

//>>AUDIO OBJECT ARRAYS<<
// for the strings
AudioSynthWavetableMulaw *harp_sample_array[12] = {&harp_sample_1, &harp_sample_2, &harp_sample_3, &harp_sample_4, &harp_sample_5, &harp_sample_6, &harp_sample_7, &harp_sample_8, &harp_sample_9, &harp_sample_10, &harp_sample_11, &harp_sample_12};
AudioMixer4Lean *harp_source_mix_array[12] = {&harp_source_mix_1, &harp_source_mix_2, &harp_source_mix_3, &harp_source_mix_4, &harp_source_mix_5, &harp_source_mix_6, &harp_source_mix_7, &harp_source_mix_8, &harp_source_mix_9, &harp_source_mix_10, &harp_source_mix_11, &harp_source_mix_12};
AudioSynthWavetableMulaw *chord_sample_array[4] = {&chord_sample_1, &chord_sample_2, &chord_sample_3, &chord_sample_4};
AudioMixer4Lean *chord_source_mix_array[4] = {&chord_source_mix_1, &chord_source_mix_2, &chord_source_mix_3, &chord_source_mix_4};
AudioSynthPluckedString *string_pluck_array[12] = {&pluck_string_1, &pluck_string_2, &pluck_string_3, &pluck_string_4, &pluck_string_5, &pluck_string_6, &pluck_string_7, &pluck_string_8, &pluck_string_9, &pluck_string_10, &pluck_string_11, &pluck_string_12};
AudioSynthWaveformModulated *string_waveform_array[12] = {&waveform_string_1, &waveform_string_2, &waveform_string_3, &waveform_string_4, &waveform_string_5, &waveform_string_6, &waveform_string_7, &waveform_string_8, &waveform_string_9, &waveform_string_10, &waveform_string_11, &waveform_string_12};
AudioEffectEnvelopeRetrigger *string_enveloppe_array[12] = {&envelope_string_1, &envelope_string_2, &envelope_string_3, &envelope_string_4, &envelope_string_5, &envelope_string_6, &envelope_string_7, &envelope_string_8, &envelope_string_9, &envelope_string_10, &envelope_string_11, &envelope_string_12};
AudioEffectEnvelopeRetrigger *string_enveloppe_filter_array[12] = {&envelope_filter_1, &envelope_filter_2, &envelope_filter_3, &envelope_filter_4, &envelope_filter_5, &envelope_filter_6, &envelope_filter_7, &envelope_filter_8, &envelope_filter_9, &envelope_filter_10, &envelope_filter_11, &envelope_filter_12};
AudioMixer4Lean *string_mixer_array[3] = {&string_mix_1, &string_mix_2, &string_mix_3};
AudioMixer4Lean *string_mixer_r_array[3] = {&string_mix_1_r, &string_mix_2_r, &string_mix_3_r};   // the right side (see STEREO STRINGS)
AudioFilterStateVariableLean *string_filter_array[12] = {&filter_string_1, &filter_string_2, &filter_string_3, &filter_string_4, &filter_string_5, &filter_string_6, &filter_string_7, &filter_string_8, &filter_string_9, &filter_string_10, &filter_string_11, &filter_string_12};
AudioSynthWaveform *string_transient_waveform_array[12] = {&waveform_transient_1, &waveform_transient_2, &waveform_transient_3, &waveform_transient_4, &waveform_transient_5, &waveform_transient_6, &waveform_transient_7, &waveform_transient_8, &waveform_transient_9, &waveform_transient_10, &waveform_transient_11, &waveform_transient_12};
AudioEffectEnvelopeRetrigger *string_transient_envelope_array[12] = {&envelope_transient_1, &envelope_transient_2, &envelope_transient_3, &envelope_transient_4, &envelope_transient_5, &envelope_transient_6, &envelope_transient_7, &envelope_transient_8, &envelope_transient_9, &envelope_transient_10, &envelope_transient_11, &envelope_transient_12};
AudioMixer4Lean *transient_mixer_array[3] = {&transient_mix_1, &transient_mix_2, &transient_mix_3};
// for the chord
AudioEffectEnvelopeRetrigger *chord_vibrato_envelope_array[4] = {&voice1_vibrato_envelope, &voice2_vibrato_envelope, &voice3_vibrato_envelope, &voice4_vibrato_envelope};
AudioEffectEnvelopeRetrigger *chord_vibrato_dc_envelope_array[4] = {&voice1_vibrato_dc_envelope, &voice2_vibrato_dc_envelope, &voice3_vibrato_dc_envelope, &voice4_vibrato_dc_envelope};
AudioMixer4Lean *chord_vibrato_mixer_array[4] = {&voice1_vibrato_mixer, &voice2_vibrato_mixer, &voice3_vibrato_mixer, &voice4_vibrato_mixer};
AudioSynthWaveformModulated *chord_osc_1_array[4] = {&voice1_osc1, &voice2_osc1, &voice3_osc1, &voice4_osc1};
AudioSynthWaveformModulated *chord_osc_2_array[4] = {&voice1_osc2, &voice2_osc2, &voice3_osc2, &voice4_osc2};
AudioSynthWaveformModulated *chord_osc_3_array[4] = {&voice1_osc3, &voice2_osc3, &voice3_osc3, &voice4_osc3};
AudioSynthWaveformDc *chord_freq_dc_array[4]= {&voice1_frequency_dc, &voice2_frequency_dc, &voice3_frequency_dc, &voice4_frequency_dc};
AudioSynthNoiseWhite *chord_noise_array[4] = {&voice1_noise, &voice2_noise, &voice3_noise, &voice4_noise};
AudioMixer4Lean *chord_voice_mixer_array[4] = {&voice1_mixer, &voice2_mixer, &voice3_mixer, &voice4_mixer};
AudioFilterStateVariableLean *chord_voice_filter_array[4] = {&voice1_filter, &voice2_filter, &voice3_filter, &voice4_filter};
AudioEffectEnvelopeRetrigger *chord_envelope_filter_array[4] = {&voice1_envelope_filter, &voice2_envelope_filter, &voice3_envelope_filter, &voice4_envelope_filter};
AudioEffectMultiply *chord_tremolo_mult_array[4] = {&voice1_tremolo_mult, &voice2_tremolo_mult, &voice3_tremolo_mult, &voice4_tremolo_mult};
AudioEffectEnvelopeRetrigger *chord_envelope_array[4] = {&voice1_envelope, &voice2_envelope, &voice3_envelope, &voice4_envelope};

//>>SYNTHESIS VARIABLE<<
// waveshaper shape
float wave_shape[257] = {};
float ws_sin_param = 1;
// waveform array 
int8_t waveform_array[12] = {
    0, //WAVEFORM_SINE
    1, //WAVEFORM_SAWTOOTH
    2, //WAVEFORM_SQUARE
    3, //WAVEFORM_TRIANGLE
    12, //WAVEFORM_BANDLIMIT_PULSE
    5, //WAVEFORM_PULSE
    6, //WAVEFORM_SAWTOOTH_REVERSE
    7, //WAVEFORM_SAMPLE_HOLD 
    8, //WAVEFORM_TRIANGLE_VARIABLE
    9, //WAVEFORM_BANDLIMIT_SAWTOOTH
    10,//WAVEFORM_BANDLIMIT_SAWTOOTH_REVERSE
    11, //WAVEFORM_BANDLIMIT_SQUARE
}; 
// shuffling arrays and index for the harp
int8_t harp_shuffling_array[7][12] = {
    //each number indicates the note for the string 0-6 are taken within the chord pattern. 
    //the /10 number indicates the octave
    {0, 1, 2, 10, 11, 12, 20, 21, 22, 30, 31, 32},
    {4, 1, 0, 2, 14, 11, 10, 12, 24, 21, 20, 22}, //add the seconds
    {5, 2, 0, 1, 15, 12, 10, 11, 25, 22, 20, 21}, //add the fourth
    {6, 2, 0, 1, 16, 12, 10, 11, 26, 22, 20, 21}, //add the sixth
    {0, 1, 2, 3, 10, 11, 12, 13, 20, 21, 22, 23}, //replaced octave by barry_harris shuffling array 
    {0, 4, 1, 5, 2, 6, 10, 14, 11, 15, 12, 16}, //chromatic
    {0, 10, 20, 1, 11, 21, 2, 12, 22, 3, 13, 23}}; //special array for keymaster/barry_harris combo
int8_t harp_shuffling_selection = 0;
int8_t transient_note_level=0; //level of the note of the transient in the scale;
int8_t chord_shuffling_array[6][7] = {
    //each number indicates the note for the voice 0-6 are taken within the chord pattern. In normal mode, only 0-3 is used, and 4-6 is available in rythm mode 
    //the /10 number indicates the octave
    {0, 1, 2, 3, 4, 5, 6}, //normal 
    {10, 11, 12, 13, 14, 15, 16},//one octave up with chromatics
    {10, 11, 12, 13, 0, 2, 3},//one octave up with low chord notes
    {10, 11, 12, 13, 2, 5, 6},//one octave up with low fifth and low chromatics
    {10, 11, 12, 13, 2, 15, 16},//one octave up with low fifth and high chromatics
    {20, 21, 22, 23, 24, 25, 26}};//two octave up
int8_t chord_shuffling_selection = 0;
uint8_t chord_inversion = 0; // 0 = root position, 1-3 = successive inversions
// Pressing the held chord again in hold mode (324): 0 strikes it again, as before; 1 climbs it an
// inversion each press, to root position an octave up and then back to where it began; 2 climbs that
// far and comes back down; 3 an octave up, and back on the next. The climb is a live step on top of
// chord inversion, never kept, and starts over with any other chord. See inversion_repress().
uint8_t repress_mode = 0;
uint8_t inversion_step = 0;
int8_t inversion_direction = 1;
bool inversion_stepped = false;  // this build is a step: the voicing as climbed, not led
bool chord_repressed = false;    // a chord button pressed, for update_chord_notes to see
// Automatic voice leading. When on, the four chord voices are placed to move as
// little as possible from the chord already sounding, instead of being built
// upward from the root every time. The range is how far, in semitones, the
// voicing may sit outside where root position would have put it; it is
// converted to steps of the live division where it is used.
uint8_t voice_leading = 0;
uint8_t voice_leading_range = 12;
#define VOICE_POOL_MAX 40
#define UNREACHABLE 32767
// what the four voices were last given, so the next chord can be placed near them
int16_t previous_voicing[4] = {0, 0, 0, 0};
uint8_t chord_spacing = 0;   // 0 = close, 1 = drop 2, 2 = drop 3, 3 = drop 2+4, 4 = spread
int16_t chord_note_floor = 12;    // one octave above the base, in steps of the live division; below this the chord voices turn to mud
int16_t chord_note_ceiling = 96;  // eight octaves above the base; both move with the division in apply_temperament
// retrigger release for chord delayed note
int chord_retrigger_release=0;
int glide_length=0;
// Fingered glide (320): the chords glide only into a chord the fingers move to while still holding
// the last, as a monosynth's fingered portamento does; a chord struck afresh starts on its pitch.
// Legato is any change of the chord buttons or the modifier made with a chord button held through
// it: rolling onto the next chord, sharpening it, adding or letting go of a slash's bass, a column's
// kind changed by a second button. Not in rhythm mode, whose notes are its own (glide as set).
bool fingered_glide = false;
bool chord_legato = true;   // the latest change was made with a chord button held through it
// strings filter parameters
float string_filter_keytrack = 0;
int string_filter_base_freq = 0;
// lfo for chord parameters
float chord_vibrato_base_freq = 0;
float chord_vibrato_keytrack = 0;
float chord_tremolo_base_freq = 0;
float chord_tremolo_keytrack = 0;
// Speed ramp (380): the tremolo's and the vibrato's rates, harp and chords, move to a new rate over
// this many tenths of a second instead of jumping to it, as a rotating speaker's rotors spin up when
// switched to fast and coast down when switched to slow, taking half again as long to slow (they
// have only friction to stop them). The rate closes in on where it's going as a rotor's speed does,
// quickly at first, and is 95% of the way there at the time set. A knob, the double tap, hover or a
// new chord's keytrack all move the rates; loading a preset sets them at once. 0 jumps, as before.
enum { RAMP_HARP_TREMOLO, RAMP_HARP_VIBRATO, RAMP_CHORD_TREMOLO, RAMP_CHORD_VIBRATO };
uint8_t speed_ramp = 0;
bool parameters_loading = false;   // load_config is applying a preset
struct lfo_rate_t { AudioSynthWaveform *lfo; float now, to; };
lfo_rate_t lfo_rates[4] = {{&string_tremolo_lfo, 0, 0}, {&string_vibrato_lfo, 0, 0}, {&chords_tremolo_lfo, 0, 0}, {&chords_vibrato_lfo, 0, 0}};
// LFO which heads for hz; from the loop, or a chord timer's interrupt (the keytrack)
FLASHMEM void lfo_rate(uint8_t which, float hz) {
  lfo_rate_t &r = lfo_rates[which];
  r.to = hz;
  if (speed_ramp == 0 || parameters_loading) {
    r.now = hz;
    r.lfo->frequency(hz);
  }
}
// From loop(): each rate on its way
FLASHMEM void lfo_rates_update() {
  static uint32_t last = 0;
  uint32_t now = micros();
  float dt = (now - last) * 1e-6f;
  if (dt < 0.002f) return;
  last = now;
  if (dt > 0.1f) dt = 0.1f;
  for (uint8_t i = 0; i < 4; i++) {
    noInterrupts();
    lfo_rate_t &r = lfo_rates[i];
    if (r.now != r.to) {
      float tau = speed_ramp / 30.0f * (r.to < r.now ? 1.5f : 1.0f);   // tenths of a second, to 95% (three time constants)
      r.now = tau > 0 ? r.now + (r.to - r.now) * (1.0f - expf(-dt / tau)) : r.to;
      if (fabsf(r.to - r.now) < 0.002f) r.now = r.to;
      r.lfo->frequency(r.now);
    }
    interrupts();
  }
}
// The chords' rates: the setting's, and the chord's lowest note's keytrack on it
FLASHMEM void chord_lfo_rates() {
  lfo_rate(RAMP_CHORD_VIBRATO, chord_vibrato_base_freq + chord_vibrato_keytrack * current_chord_notes[0]);
  lfo_rate(RAMP_CHORD_TREMOLO, chord_tremolo_base_freq + chord_tremolo_keytrack * current_chord_notes[0]);
}
float chord_filter_base_freq = 0;
float chord_filter_keytrack = 0;
// frequency mutlipliers for chord
float osc_1_freq_multiplier = 1;
float osc_2_freq_multiplier = 1;
float osc_3_freq_multiplier = 1;
// string delay_parameter
u_int32_t inter_string_delay = 30000;
u_int32_t random_delay = 10000;
// pan for audio output 
float pan=1;
float reverb_dry_proportion=0.6; //to avoid drop in volume in full reverb, keep some part of the dry signal in
float string_dry = 1, chord_dry = 1;   // the dry share each side keeps under its reverb (85, 184), as last applied
void apply_harp_bus();
void apply_chord_bus();

//>>AUTO RYTHM<<
u_int8_t rythm_pattern[16] = {};
float rythm_bpm = 80;
u_int8_t rythm_current_step = 0;
u_int16_t note_pushed_duration = 30;
float shuffle = 1;
bool rythm_timer_running = false;
volatile bool rythm_clock_synced = false;   // the steps following a MIDI clock (see the clock in processMIDI)
IntervalTimer rythm_timer;       // the rhythm's clock, every millisecond (see ACCOMPANIMENT)
volatile float clock_bend_ratio = 1;   // the rhythm's clock bent (clock bend, 436): its tempo times this (see BENDS)
// The accompaniment's settings (274-280), see ACCOMPANIMENT
uint8_t acc_style = 0;          // 0 the sixteen steps of rythm pattern, else a written part (accompaniment_styles.h)
uint8_t acc_bass_part = 0;      // voice 0: 0 plays the style's part, 1 holds the chord's note, 2 rests
uint8_t acc_chord_part = 0;     // voices 1-3, the same
uint8_t acc_follow = 0;         // 0 plays on, 1 only while a chord is held, 2 that and the bar starts on the press
uint8_t acc_change = 0;         // a new chord comes in: 0 on the next note, 1 on the beat, 2 on the bar
uint8_t acc_meter = 0;          // time signature (393): 0 the style's own, else the bar every part wraps to (see METER)
// Variations, fills, breaks, intro and ending (see SECTIONS)
uint8_t acc_style_b = 0;        // rhythm style B (314): the style a fill switches to and back from, 0 none
bool rhythm_fill_combo = false; // modifier held with a tap of hold plays a fill (316)
bool rhythm_intro = false;      // rhythm mode starts with a bar of the style's fill (317)
bool rhythm_ending = false;     // the long press on hold ends on a fill and a last chord (318)
bool rhythm_palm_break = false; // a palm mute rests the accompaniment to the bar line (319)
const int16_t rhythm_fill_adress = 315;   // the action: 1 a fill, 2 a break, 3 the ending; the double tap's target
bool modifier_combo_used = false;   // the modifier, held for a combo, isn't a tap of the double tap when let go
// Tap and hold (321): a tap of the modifier and then a press held flips the double tap's settings
// while it is held, and back as it lets go; let go as quickly as a tap, it latches as a double tap
// does. The second press acts as it lands. While held so, the modifier neither sharpens a chord
// pressed meanwhile nor switches the knobs to their alternates: it is busy holding.
bool double_tap_hold = false;
// Revert and panic (see PANIC AND REVERT)
bool modifier_preset_combo = false;   // the modifier held, down reverts the preset and up is the panic (322)
const int16_t preset_action_adress = 323;   // the action: 1 revert, 2 panic; the double tap's target
const int16_t phone_deck_adress = 332;      // the action: 1 the tape stop and back, 2 a grab; the double tap's target
void preset_action(int16_t action);
bool modifier_holding = false;      // the modifier is down as a tap and hold's second press
void acc_fill_request(bool ending);
void acc_break_request();
// Drums (334-343, see DRUMS)
bool drums_on = false;            // 334: rhythm mode plays drums
uint8_t drum_groove = 0;          // 335: 0 the rhythm style's own drum part, else a groove (drum_patterns.h, from 1)
uint8_t drum_groove_b = 0;        // 336: with style B: 0 as style A has it (the style's own, or the groove), else a groove
uint8_t drum_kit = 0;             // 337: 0 the groove's own kit, else a kit (from 1)
uint8_t drum_level = 70;          // 338
uint8_t drum_humanize = 0;        // 339: up to 15 ms late and 20% softer at 100
uint8_t drum_midi = 0;            // 340: each hit out on channel 10: 0 off, 1 port 1, 2 port 2
bool count_in = false;            // 341: a bar of sticks before rhythm mode begins
bool drums_only = false;          // 342: the accompaniment rests, the drums play
bool harp_drums = false;          // 343: the harp's strings play the kit
uint8_t tabla_laya = 0;           // 377: a theka's bols 0 as written, 1 one a beat, 2 two, 3 four
volatile double drum_landed = -1; // a stretched theka's sam: the bar line its last fill landed on (-1 none)
void drum_pad(uint8_t string, int16_t strength);
void drum_strike(uint8_t v, uint8_t velocity, bool written = true, bool loose = true);
float touch_firmness(float strength);
void drums_set();
void rhythm_fill_action(int16_t action);
uint8_t acc_articulation = 100; // % of each written note's length
uint8_t acc_accents = 100;      // % of the written difference between loud and soft notes
uint8_t tap_counts = 0;         // tap tempo counts (281): 0 a tap a beat, 1 a tap an eighth; see acc_tap
uint8_t midi_chords = 0;        // MIDI chords (282): in rhythm mode, notes in on the chords' port and channel set its chord (see MIDI CHORDS)
volatile bool midi_chord_held = false;   // a note (or the pedal) holds a chord sent in, as a button would
volatile double acc_phase_error = 0;   // 24ths of a beat the rhythm is to catch up (or fall back) to a tap
// MIDI clock out (266): 0 off, 1 on port 1, 2 on port 2, 3 on both; see the clock in acc_tick
uint8_t midi_clock_out = 0;
volatile uint8_t midi_clock_started = 0;      // the ports a Start has gone to and no Stop since (bit 0 port 1, bit 1 port 2)
volatile uint32_t midi_clock_heard_ms = 0;    // when a MIDI clock last came in, rhythm mode or not
volatile bool midi_clock_heard = false;
// The rhythm LED's flash: lit on the step or the beat (acc_step, acc_beat), let go by the loop 200 ms on
// (rythm_led_settle), not by a timer of its own, so the rhythm holds one of the four timer channels
// and not two, and nothing starts a timer from inside an interrupt
volatile bool rythm_led_lit = false;
volatile uint32_t rythm_led_lit_at = 0;

//>>KEY CHANGE MODE<<
// Holding both preset buttons together enters key change mode, where the chord
// buttons select a key signature directly: seven rows of F C G D A E B, three
// columns of sharp, natural, flat.
bool key_change_mode = false;
bool preset_inhibit = false;   // suppresses preset changes around the combo
// Three independent clocks. Sharing one caused the mode timeout to be restarted
// by unrelated events, which made the combo behave differently run to run.
elapsedMillis preset_inhibit_timer; // time since inhibition was raised
elapsedMillis key_change_log_timer; // throttle for debug output only
const uint32_t KEY_CHANGE_BLINK_US = 150000; // LED blink half-period while waiting
const uint32_t SIMULTANEOUS_WINDOW = 400;  // both buttons must arrive within this
const uint32_t PRESET_INHIBIT_DELAY = 400; // presets stay inhibited this long after
int8_t key_change_reported = -1;   // last key signature reported to the host
uint32_t key_button_was_down = 0;  // raw state of each chord button on the previous pass, for key selection edges
// Chord buttons that belong to the key change gesture: every button pressed
// while the mode was on, and every one still down when it ended. None of them
// may start a chord until it has been let go. See handle_chords_button().
uint32_t key_selection_buttons = 0;
elapsedMicros last_midi_clock_in;

uint8_t rythm_limit_change_to_every = 2; // when we allow the chord change
elapsedMillis since_last_button_push;
elapsedMillis last_key_change;

uint8_t rythm_freeze_current_chord_notes[7]; // this array is needed because we need to handle the situation when a
uint8_t rythm_loop_length = 16;
u_int8_t current_selected_voice=0; //increment at each voice steal to rotate amongst voices;

//-->>MIDI PARAMETERS
uint8_t chord_port=0;
uint8_t chord_channel=1;
uint8_t chord_attack_velocity=127;
uint8_t chord_release_velocity=20;
uint8_t chord_started_notes[4]={0,0,0,0};                   
uint8_t harp_port=1;
uint8_t harp_channel=1;
uint8_t harp_attack_velocity=127; 
// MIDI in plays (address 8): notes sent to the minichord play its voices (see MIDI IN)
bool midi_in_plays = false;
struct midi_in_voice {
  bool owned;       // sounding a note that came in, which the minichord's own playing leaves alone
  bool key_down;
  bool sustained;   // let go of with the pedal down: it rings until the pedal lifts
  uint8_t note;
  uint32_t since;
  bool looped;      // the looper's, not an incoming note's (see LOOPER)
  float freq;       // the frequency it sounds
};
midi_in_voice midi_in_harp[12] = {};
midi_in_voice midi_in_chord[4] = {};
void midi_in_release(midi_in_voice *voices, uint8_t i, bool harp);
float chord_voice_level[4] = {1, 1, 1, 1};   // each chord voice's loudness, from an incoming note's velocity
// Chords follow the harp's touch (395): the chords take their dynamic from the harp hand. Each new
// harp gesture sets it from its first string's softness (touch velocity and strum velocity's say,
// as the string itself plays); a chord struck then, by the buttons or by brothers, plays at it, and
// a chord already sounding glides to it. See CHORD TOUCH.
uint8_t chord_touch = 0;
float chord_touch_gain = 1;       // the chord voices' level at the harp's dynamic
float chord_touch_vscale = 1;     // and their MIDI velocity's share of chord velocity
float chord_touch_firmness = 1;   // and the firmness the looper keeps
bool chord_touch_unwind = false;  // turned off: the voices still glide back to full
float chord_noise_level = 0;                // the chord voices' noise (130), before that
void apply_chord_voice_level(uint8_t i);
void midi_in_message(uint8_t type, uint8_t cable, uint8_t channel, uint8_t data1, uint8_t data2);
void midi_in_set(bool on);
void midi_in_retune();
void looper_action(int16_t action);
void looper_voice_taken(bool harp, uint8_t v);
void looper_update();
void looper_capture(uint8_t kind, uint8_t voice, float freq, float firmness, uint8_t midi_note, uint8_t midi_velocity);
enum { LOOP_START, LOOP_RELEASE, LOOP_RETUNE, LOOP_DRUM };
enum { LOOPER_EMPTY, LOOPER_RECORDING, LOOPER_PLAYING, LOOPER_STOPPED, LOOPER_COUNTIN };
volatile uint8_t looper_state = LOOPER_EMPTY;   // see LOOPER
volatile bool looper_overdub = false;
bool looper_double_tap_owned = false;   // the double tap took the looper's latest step, so a running loop keeps it (see LOOPER)
volatile uint32_t rythm_beat_ms = 0;   // when the rhythm's latest beat began (its even steps), for the looper
volatile uint32_t rythm_beat_count = 0;   // beats the rhythm has begun, and whether the latest began a bar (the looper's click)
volatile bool rythm_beat_bar = false;
// The looper in time (283-286, 288), see LOOPER
uint8_t looper_quantize = 0;     // 0 off, 1 quarter notes, 2 eighths, 3 sixteenths, 4 eighth triplets
uint8_t looper_count_in = 0;     // bars of count-in before a recording: 0, 1 or 2
uint8_t looper_click = 0;        // 0 off, 1 the count-in, 2 and the recording, 3 and the playing
uint8_t looper_click_level = 50;
uint8_t looper_length = 0;       // bars a recording runs before it closes by itself; 0 as long as it is played (288)
uint32_t looper_click_flash_ms = 0;   // the latest tick, for the count-in's LED
uint8_t loop_phrase_bars = 0;    // the recording's phrase in bars, from looper length; an early step shortens it
// Chord memory (287), see CHORD MEMORY
enum { CHORD_MEMORY_EMPTY, CHORD_MEMORY_RECORDING, CHORD_MEMORY_PLAYING, CHORD_MEMORY_STOPPED };
volatile uint8_t chord_memory_state = CHORD_MEMORY_EMPTY;
const int16_t chord_memory_adress = 287;
void chord_memory_action(int16_t action);
void chord_memory_step();
void chord_memory_record_chord();
uint32_t acc_bar_ms_after(uint32_t at_least);
float acc_beats_per_bar();
uint8_t acc_bar_eighths();
uint32_t acc_bar_beats();
uint32_t modifier_pressed_ms = 0;      // when the modifier was last pressed: a double tap's second tap lands then
bool string_held(uint8_t i);

// String spread and chord ensemble (addresses 257-259), see STEREO
uint8_t string_spread = 0;          // 0-100: how wide the strings fan out, 0 all in the middle as before
uint8_t string_spread_pattern = 0;  // 0 by pitch, low to the left; 1 neighbouring strings alternate sides
void apply_string_spread(uint8_t i);
void apply_string_spreads();
void set_chord_ensemble(uint8_t depth);
short chord_ensemble_line_l[2048];  // the ensemble's delay lines (8 kB)
short chord_ensemble_line_r[2048];
// The vocoder (addresses 260-262), see VOCODER
uint8_t vocoder_amount = 0;      // 0-100: the vocoded sound in place of the dry, 0 off
uint8_t vocoder_carrier = 0;     // what the incoming sound shapes: 0 the chords, 1 the harp, 2 both
uint8_t vocoder_consonants = 30; // 0-100: the incoming sound's hiss blended in, for words
void vocoder_set();
void vocoder_setup();
void phone_setup();
void phone_dj_setup();
void drums_update();
void phone_update();
// Strum velocity (address 263), see strum_softness
uint8_t strum_velocity = 0;      // 0-100: how much softer a slow strum plays than a fast one, 0 off
float pluck_strum = -1;          // strum velocity's say on the plucks made now, -1 none (as pluck_strength)

// Sampled voices (addresses 264, 265, and 270-273 for each chord note), see SAMPLES
uint8_t harp_voice_source = 0;    // 0 the harp's own synth, 1 piano, 2 pizzicato, 3 choir, 4 strings, 5 the song, 6 vibraphone, 7 bass, 8 sitar, 9 electric piano, 10 reed piano, 11 kalimba, 12 marimba, 13 organ, 14 upright bass, 15 accordion
float string_blend = 0;           // the string model (217), 0-1; resting while a sample plays the harp
void apply_string_blend();
uint8_t chord_voice_source = 0;   // the same, for the chords as a whole (chord voice, 265)
// How hard the reed piano (voice source 10) is struck at full strength, 0-1, which sets its bark: the
// harp's (386) scaled by how hard each string is plucked, the chords' (387) as it is
float harp_reed_bark = 0.6f;
float chord_reed_bark = 0.6f;
uint8_t chord_note_source[4] = {0, 0, 0, 0};   // each note's own (270-273): 0 as the chord voice, else 1 + a source
uint8_t chord_source_of[4] = {0, 0, 0, 0};     // what each chord voice sounds now, as chord_voice_source counts
void set_harp_source(uint8_t source);
void set_chord_source(uint8_t source);
void set_chord_note_source(uint8_t i, uint8_t value);
void harp_sample_start(uint8_t i);
void chord_sample_start(uint8_t i);
void chord_sample_glide(uint8_t i, float hz, uint16_t ms);
const float harp_sample_level = 3.0f;    // a sample's loudness (0.2 RMS) against the strings' (string_level, 0.15 of a saw)
const float chord_sample_level = 1.0f;   // and the chords'

// Touch velocity and pressure (addresses 252, 253): how firmly a string is plucked, and how hard
// it is pressed while held, read from its pad's strength (see touch_firmness)
uint8_t touch_velocity = 0;      // 0-100: how much softer and darker a light touch plays than a firm one
uint8_t touch_pressure = 0;      // a held string follows how hard it is pressed (1) or only swells (2), and sends it as MIDI pressure
float string_level = 0.15;       // the strings' amplitude (41), before each string's own firmness
float transient_level = 0.1;     // the transient's amplitude (101), likewise
float string_pluck_firmness[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};   // 0-1, how firmly each string was plucked
float string_firmness[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};         // that, or firmer while pressed harder
uint8_t string_pressure_sent[12] = {0};
// A held string's pressure, kept as the firmest of each of the last four 5 ms stretches, so it
// follows a finger up at once and down 20 ms late. Easing off, the lag doesn't show; lifting, the
// reading falls away for a few milliseconds before the pad lets go, and the string would follow it
// down and then release from there, which sounds like the note cut short.
const uint8_t press_slots = 4;
const uint8_t press_slot_ms = 5;
float press_recent[12][press_slots] = {{0}};
uint8_t press_slot = 0;
float string_note_freq[12] = {0};   // the frequency each string sounds, for its filter
float pluck_strength = -1;          // the strength the plucks made now are made with; below 0, full
void apply_string_firmness(uint8_t i);
float touch_softness(float firmness);
// How much softer than full string i plays: an incoming note by its velocity always, a plucked one as
// far as touch velocity says
// A plucked string's: touch velocity's say (from how firmly it is held, if touch velocity is on) and
// strum velocity's (from the strum it was part of, if it is on and the strum had a speed), the less
// soft of the two; full if neither has a say.
float string_strum_softness[12] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // -1: no say
static inline float string_softness(uint8_t i) {
  float f = string_firmness[i];
  if (midi_in_harp[i].owned) return (1 - f) * (1 - f);
  float s = touch_velocity ? touch_softness(f) : -1;
  if (string_strum_softness[i] >= 0) s = s < 0 ? string_strum_softness[i] : fminf(s, string_strum_softness[i]);
  return s < 0 ? 0 : s;
}

//>>CHORD TOUCH<<
// The chord buttons are switches: a chord has no touch of its own. With chords follow the harp's
// touch (395) on it takes the harp hand's: every new harp gesture sets the instrument's dynamic
// from its first string, by the harp's own laws (down to 24 dB, MIDI velocity down to a tenth), so
// the chords and the strings play as one hand plays them. Three ways it reaches them:
//   strike  a chord brothers strikes plays at the touch that struck it
//   follow  a chord already sounding glides there, in about 30 ms, so a held chord swells and
//           fades under the harp as the hand digs in or lightens
//   carry   a chord struck from the buttons starts at the harp's last dynamic, not at full
// With touch velocity and strum velocity both off every gesture is full, as before. Not in rhythm
// mode, whose notes carry their own accents, nor on a voice an incoming note holds.
void apply_chord_voice_level(uint8_t i);
FLASHMEM void chord_touch_take(uint8_t string) {
  const float s = string_softness(string);
  chord_touch_gain = powf(10.0f, -1.2f * s);
  chord_touch_vscale = 1 - 0.9f * s;
  chord_touch_firmness = 1 - sqrtf(s);
}

// From the loop: the sounding voices a step nearer the dynamic every 3 ms, a quarter of the way,
// small enough steps that the gain never clicks
FLASHMEM void chord_touch_glide() {
  static elapsedMillis since;
  if ((!chord_touch && !chord_touch_unwind) || rythm_mode || since < 3) return;
  since = 0;
  const float target = chord_touch ? chord_touch_gain : 1;
  bool settled = true;
  for (uint8_t i = 0; i < 4; i++) {
    if (midi_in_chord[i].owned || !chord_envelope_array[i]->isActive()) continue;
    float l = chord_voice_level[i];
    if (l == target) continue;
    l += (target - l) * 0.25f;
    if (fabsf(target - l) < 0.002f) l = target;
    else settled = false;
    chord_voice_level[i] = l;
    apply_chord_voice_level(i);
  }
  if (settled) chord_touch_unwind = false;
}
// How bright a plucked string model's pluck is: a soft one is muffled, never dull
static inline float pluck_brightness(uint8_t i) {
  return 0.35f + 0.65f * (1 - string_softness(i));
}
uint8_t harp_release_velocity=20;
uint8_t harp_started_notes[12]={0,0,0,0,0,0,0,0,0,0,0,0};
// Where each harp note went, so its note-off follows it there even if the
// channel or port changes while it rings.
uint8_t harp_started_channel[12]={0,0,0,0,0,0,0,0,0,0,0,0};
uint8_t harp_started_port[12]={0,0,0,0,0,0,0,0,0,0,0,0};
// A string's midi note lasts as long as the string sounds, so a strum arrives
// at the far end as ringing strings, not a row of finger-length blips. 1 ends
// it when the finger lifts, as before.
bool harp_note_off_on_lift = false;
uint16_t harp_voice_current_note[12] = {0};   // last note each harp string was tuned to, so a tuning change can re-apply it
uint8_t midi_base_note=48; // for C3
uint8_t midi_base_note_transposed=midi_base_note; //to handle note transposition
uint midi_buffer_delay=300; //in microseconds, helps compatibility with some hardware devices

//-->>MIDI OUTPUT QUEUE
// usbMIDI is not reentrant; calling it from both loop() and a PIT ISR corrupts its
// transmit state. Rule: only drain_midi_queue(), called at the end of loop(), touches
// usbMIDI. Everything else enqueues via queue_midi(), safe from ISR context.
#define MIDI_QUEUE_SIZE 256 // power of two, max 256 for uint8_t indices
#define MIDI_DRAIN_MAX_PER_LOOP 16 // caps how long a drain can hold up loop()
#define MIDI_EVT_NOTE_ON 0
#define MIDI_EVT_NOTE_OFF 1
#define MIDI_EVT_BEND 2
#define MIDI_EVT_CC 3
#define MIDI_EVT_PRESSURE 4   // channel pressure, in velocity
#define MIDI_EVT_POLY 5       // polyphonic aftertouch: note and pressure
#define MIDI_EVT_REALTIME 6   // a system real-time message (clock, start, stop): its type in note
#define MIDI_EVT_TUNING 7     // an MTS single note tuning change: the key in note, its semitone in velocity, the fraction in bend
struct midi_event_t {
  uint8_t note;      // note number, or controller number for a CC
  uint8_t velocity;  // velocity, or controller value for a CC
  uint8_t channel;
  uint8_t cable;
  uint8_t kind;
  int16_t bend;      // MIDI_EVT_BEND only, -8192..8191
};
volatile midi_event_t midi_queue[MIDI_QUEUE_SIZE];
volatile uint8_t midi_queue_head = 0; // written by producers
volatile uint8_t midi_queue_tail = 0; // written by loop() only
volatile uint32_t midi_queue_dropped = 0; // diagnostic: events lost to a full queue

// Safe to call from any context, including an ISR. Drops the event if the queue is
// full rather than blocking -- blocking is what caused the original fault.
void queue_midi_event(uint8_t kind, uint8_t a, uint8_t b, int16_t bend, uint8_t channel, uint8_t cable) {
  uint32_t primask;
  __asm__ volatile("mrs %0, primask" : "=r"(primask));
  __disable_irq();
  uint8_t next = (midi_queue_head + 1) & (MIDI_QUEUE_SIZE - 1);
  if (next != midi_queue_tail) {
    midi_queue[midi_queue_head].note = a;
    midi_queue[midi_queue_head].velocity = b;
    midi_queue[midi_queue_head].channel = channel;
    midi_queue[midi_queue_head].cable = cable;
    midi_queue[midi_queue_head].kind = kind;
    midi_queue[midi_queue_head].bend = bend;
    midi_queue_head = next;
  } else {
    midi_queue_dropped++;
  }
  if (!primask) __enable_irq();
}

void queue_midi(bool note_on, uint8_t note, uint8_t velocity, uint8_t channel, uint8_t cable) {
  queue_midi_event(note_on ? MIDI_EVT_NOTE_ON : MIDI_EVT_NOTE_OFF, note, velocity, 0, channel, cable);
}

// Pitch bend and control change, for the MPE output below. Same queue, so they
// keep their order with the notes they belong to -- a bend that has to arrive
// before its note-on actually does.
void queue_midi_bend(int16_t bend, uint8_t channel, uint8_t cable) {
  queue_midi_event(MIDI_EVT_BEND, 0, 0, bend, channel, cable);
}

void queue_midi_cc(uint8_t controller, uint8_t value, uint8_t channel, uint8_t cable) {
  queue_midi_event(MIDI_EVT_CC, controller, value, 0, channel, cable);
}

// The knobs as MIDI controllers. With "knobs send MIDI" on, each knob's position goes out as a
// control change as it turns: CC 20 the chord knob, 21 the harp knob, 22 the modulation knob, on
// the chord channel and port, 0 to 127 across the knob's travel, so they can be MIDI-learnt in a DAW
// or read by a program on the computer (the
// Minichord Lab's games use the knobs as paddles and dials). The knobs keep doing their usual jobs;
// this only reports where they are. Read on its own, smoothed, and sent only once the reading has
// moved a whole value from the last one sent, so a knob resting between two values doesn't chatter:
// with less (0.7 of a value, and lighter smoothing) a resting mod knob flickered between 71 and 72.
bool knob_midi = false;        // the knobs' positions sent out as control changes (address 238)
bool knob_midi_resend = false; // send every knob's position again, e.g. just after it's switched on
bool upside_down = false;      // a left-handed player's minichord, turned round (address 384, see upside_down_apply)
void send_knob_ccs() {
  const uint8_t pins[3] = {upside_down ? POT_MOD_PIN : POT_CHORD_PIN, POT_HARP_PIN, upside_down ? POT_CHORD_PIN : POT_MOD_PIN};
  static float smoothed[3] = {-1, -1, -1};
  static int16_t sent[3] = {-1, -1, -1};
  static elapsedMillis since;
  if (since < 8) return;
  since = 0;
  for (uint8_t k = 0; k < 3; k++) {
    float reading = 1024 - analogRead(pins[k]);
    smoothed[k] = smoothed[k] < 0 ? reading : smoothed[k] * 0.875f + reading * 0.125f;
    float scaled = constrain((smoothed[k] - 12.0f) * 127.0f / 1000.0f, 0.0f, 127.0f);
    bool moved = sent[k] < 0 || fabsf(scaled - sent[k]) >= 1.0f;
    if (moved || knob_midi_resend) {
      int16_t value = (int16_t)(scaled + 0.5f);
      if (value != sent[k] || knob_midi_resend) queue_midi_cc(20 + k, value, chord_channel, chord_port);
      sent[k] = value;
    }
  }
  knob_midi_resend = false;
}

// Hover: the harp plate as a fourth knob, played by holding a hand over it (see handle_hover)
int16_t hover_target = 0;      // the setting hover moves (address 249), 0 for none
int16_t hover_value = 0;       // what that setting reaches with a hand 2 cm above the plate (250)
uint8_t hover_reach = 7;       // how high above the plate hover begins, roughly in centimetres (251)
bool hover_reapply = true;     // apply again even if the value hasn't changed
// Alternate hover (325, 326): what hover moves on the knobs' alternate layer, the modifier held or
// knob layer 1, as each knob has its alternate. 0 keeps hover on its own setting on both layers.
int16_t hover_alt_target = 0;
int16_t hover_alt_value = 0;
void hover_set_target(int16_t adress);

// The phone follower: what plays into the minichord over USB moves settings, keeps rhythm mode on
// its beat, and plays on its kicks (see PHONE)
struct phone_follow_slot {
  int16_t target;        // the setting it moves (303, 307), 0 for none
  int16_t value;         // what that setting reaches with the music at its loudest (304, 308)
  uint8_t band;          // what it listens to: 0 the whole sound, 1 the bass (the kick), 2 the treble (305, 309)
  uint16_t release_ms;   // how slowly it falls back as the music drops (306, 310)
  bool reapply;
};
phone_follow_slot phone_follow[2] = {{0, 0, 0, 200, true}, {0, 0, 0, 200, true}};
bool phone_beat = false;       // rhythm mode follows the music's beat (311)
bool phone_kick_stab = false;  // each kick strikes the held chord again (312)
uint8_t phone_kick_note = 0;   // each kick sends this midi note, 0 none (313)
void phone_set_target(uint8_t slot, int16_t adress);
void phone_set();
// The DJ box and the harp that plays the song (see PHONE DJ)
uint8_t phone_dj_filter_value = 100;   // 327: below 100 a low pass closing, above a high pass opening, 100 neither
uint8_t phone_dj_resonance = 30;       // 328
uint8_t phone_echo_send = 0;           // 329
uint8_t phone_echo_time = 1;           // 330: 0 an eighth, 1 a dotted eighth, 2 a quarter, 3 a sixteenth, 4 an eighth triplet
uint8_t phone_echo_feedback = 45;      // 331
uint8_t phone_strum = 0;               // 333: 0 off, 1 down on every beat, 2 down and up in eighths
void phone_dj_set();
void phone_deck_action(int16_t action);

// Called from loop() only. The single point at which this firmware talks to usbMIDI.
void drain_midi_queue() {
  bool sent = false;
  uint8_t budget = MIDI_DRAIN_MAX_PER_LOOP;

  while (midi_queue_tail != midi_queue_head && budget > 0) {
    budget--;
    midi_event_t e;
    e.note = midi_queue[midi_queue_tail].note;
    e.velocity = midi_queue[midi_queue_tail].velocity;
    e.channel = midi_queue[midi_queue_tail].channel;
    e.cable = midi_queue[midi_queue_tail].cable;
    e.kind = midi_queue[midi_queue_tail].kind;
    e.bend = midi_queue[midi_queue_tail].bend;
    midi_queue_tail = (midi_queue_tail + 1) & (MIDI_QUEUE_SIZE - 1);
    // The pacing exists for slower hardware synths reading notes; bends and the
    // zone setup are a USB-side concern and a glide would stall behind it.
    if (sent && e.kind <= MIDI_EVT_NOTE_OFF) delayMicroseconds(midi_buffer_delay);
    switch (e.kind) {
      case MIDI_EVT_NOTE_ON:  usbMIDI.sendNoteOn(e.note, e.velocity, e.channel, e.cable); break;
      case MIDI_EVT_NOTE_OFF: usbMIDI.sendNoteOff(e.note, e.velocity, e.channel, e.cable); break;
      case MIDI_EVT_BEND:     usbMIDI.sendPitchBend(e.bend, e.channel, e.cable); break;
      case MIDI_EVT_CC:       usbMIDI.sendControlChange(e.note, e.velocity, e.channel, e.cable); break;
      case MIDI_EVT_PRESSURE: usbMIDI.sendAfterTouch(e.velocity, e.channel, e.cable); break;
      case MIDI_EVT_POLY:     usbMIDI.sendAfterTouchPoly(e.note, e.velocity, e.channel, e.cable); break;
      case MIDI_EVT_REALTIME: usbMIDI.sendRealTime(e.note, e.cable); break;
      case MIDI_EVT_TUNING: {
        uint8_t m[12] = {0xF0, 0x7F, 0x7F, 0x08, 0x02, 0x00, 0x01, e.note, e.velocity,
                         (uint8_t)((e.bend >> 7) & 0x7F), (uint8_t)(e.bend & 0x7F), 0xF7};
        usbMIDI.sendSysEx(sizeof(m), m, true, e.cable);
        break;
      }
    }
    sent = true;
  }
  // No send_now() here. The packet a message leaves half full goes out on the next USB start-of-frame
  // (125 us at 480 Mbit, 1 ms at 12), from the SOF interrupt. Flushing it from here as well froze the
  // USB controller under load: the core's usb_midi_flush_output() has no lock, the interrupt's flush
  // could land inside this one, and the descriptor the hardware had just taken was prepared again and
  // linked to itself. See scripts/usb_audio_choice.py for the core side.
}

//-->>MPE OUTPUT
// One channel carries one pitch bend and one note identity, which is not enough
// for an instrument whose voices move independently. Glide slides each chord
// voice along its own contour and the note stream steps. Two voices landing on
// the same note number arrive as one note, so the first note off releases both.
// And once the octave is divided into other than twelve, midi_out_note() has to
// round every step to the nearest semitone, which collapses distinct chords into
// each other: in 31 steps a subminor, a minor and a neutral third all leave as
// three semitones. MPE is the standard answer and is still MIDI 1.0: give every
// voice its own channel, and a bend belongs to one note.
//
// Opt-in. With the mode off nothing here sends anything and the output is what
// it has always been, byte for byte.
//
// Channel layout: the chord takes the first four member channels of its cable,
// the harp the first twelve of its own. With single port mode on they share a
// cable, and fifteen member channels have to hold sixteen voices: the chord
// takes 2 to 5 and the strings 6 to 16, the twelfth string sharing the first
// string's channel.
#define MPE_BEND_RANGE_SEMITONES 48  // the MPE default, and wide enough for any glide
#define MPE_GLIDE_UPDATE_MS 10       // how often a moving voice re-sends its bend
#define MPE_BEND_EPSILON 8           // 4.7 cents at that range: smaller moves wait while a glide runs, and are sent once it settles

uint8_t mpe_mode = 0;          // 0 single channel (unchanged), 1 MPE
bool mpe_zone_declared = false;

// Where each chord voice is, in semitones, between the note it left and the note
// it is heading for. The firmware cannot read a glide back out of the audio
// library -- amplitude(target, milliseconds) ramps the DC inside the object --
// so the ramp is modelled here. The DC reaches the voice through
// frequencyModulation(2), so a linear ramp is linear in octaves, and a straight
// interpolation between the two pitches is the real curve rather than an
// approximation of it. Both ends are taken from the frequencies the voice is
// actually given, so the model holds in any temperament or division.
float mpe_chord_from[4] = {0, 0, 0, 0};
float mpe_chord_to[4] = {0, 0, 0, 0};
uint32_t mpe_chord_ramp_start[4] = {0, 0, 0, 0};
uint16_t mpe_chord_ramp_ms[4] = {0, 0, 0, 0};
float mpe_chord_static[4] = {0, 0, 0, 0};   // see mpe_offset_semitones()
int16_t mpe_chord_bend_sent[4] = {0, 0, 0, 0};
uint32_t mpe_chord_bend_time[4] = {0, 0, 0, 0};
int16_t mpe_harp_bend_sent[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint32_t mpe_harp_bend_time[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static inline uint8_t mpe_chord_channel_for(uint8_t i, uint8_t mode) {
  return mode ? (uint8_t)(2 + i) : chord_channel;
}

static inline uint8_t mpe_harp_channel_for(uint8_t i, uint8_t mode) {
  if (!mode) return harp_channel;
  if (harp_port != chord_port) return 2 + i;
  return 6 + (i % 11);  // shared cable: string twelve doubles up on string one
}

static inline uint8_t mpe_chord_channel(uint8_t i) { return mpe_chord_channel_for(i, mpe_mode); }
static inline uint8_t mpe_harp_channel(uint8_t i) { return mpe_harp_channel_for(i, mpe_mode); }

// How far a note really sits from the note number it is sent as. In twelve equal
// this is zero. In a historical temperament it is that pitch class's offset. In
// 19 or 31 it is everything midi_out_note() had to round away, so the five
// thirds that arrive as two intervals arrive as five again.
static inline float mpe_offset_semitones(int note) {
  return 12.0f * (float)log2((double)temper_ratio(note + transpose_steps))
         - (float)(transpose_semitones + midi_out_note(note));
}

// MTS output (371): outside MPE, nothing in a note on says where between the
// semitones the note is, so before each one the key is retuned with a MIDI
// Tuning Standard single note tuning change (real time, F0 7F 7F 08 02), which
// many synths read (Surge, Pianoteq, Vital, hardware with MTS): the divided
// octaves, the temperaments, purity, a string's bend and master tuning arrive
// as they sound. Two notes of one key share its tuning, the last one's: in 31,
// C# and Db leave as the same key. A key keeps its tuning till it moves, so
// one already in tune is not sent again.
bool mts_output = false;
DMAMEM uint32_t mts_sent[128];   // each key's tuning as last sent, semitone << 14 | fraction, or none

FLASHMEM void mts_reset() {
  for (uint8_t k = 0; k < 128; k++) mts_sent[k] = 0xFFFFFFFF;
}

// Key `key`, about to start, sounds `offset` semitones from its equal tempered pitch.
void mts_tune(uint8_t key, float offset, uint8_t cable) {
  if (!mts_output || mpe_mode || key > 127) return;
  float exact = key + offset + 12.0f * log2f(c_frequency / 130.8128f);
  exact = constrain(exact, 0.0f, 127.99f);
  int16_t semitone = (int16_t)floorf(exact);
  int16_t fraction = (int16_t)lroundf((exact - semitone) * 16384.0f);
  if (fraction >= 16384) { semitone++; fraction = 0; }
  if (semitone > 127) { semitone = 127; fraction = 16383; }
  uint32_t packed = ((uint32_t)semitone << 14) | (uint32_t)fraction;
  if (mts_sent[key] == packed) return;
  mts_sent[key] = packed;
  queue_midi_event(MIDI_EVT_TUNING, key, (uint8_t)semitone, fraction, 0, cable);
}

float mpe_chord_position(uint8_t i) {
  if (mpe_chord_ramp_ms[i] == 0) return mpe_chord_to[i];
  uint32_t elapsed = millis() - mpe_chord_ramp_start[i];
  if (elapsed >= mpe_chord_ramp_ms[i]) return mpe_chord_to[i];
  return mpe_chord_from[i] + (mpe_chord_to[i] - mpe_chord_from[i]) * ((float)elapsed / (float)mpe_chord_ramp_ms[i]);
}

static inline int16_t mpe_bend_value(float semitones) {
  float v = semitones * (8192.0f / MPE_BEND_RANGE_SEMITONES);
  if (v > 8191.0f) v = 8191.0f;
  if (v < -8192.0f) v = -8192.0f;
  return (int16_t)lroundf(v);
}

static inline float mpe_chord_bend_semitones(uint8_t i) {
  return (mpe_chord_position(i) - mpe_chord_to[i]) + mpe_chord_static[i];
}

// Called wherever a chord voice is given a new note, so the model starts from
// where the voice actually is: a note arriving mid-glide ramps from the current
// position, exactly as the DC object does. target is the pitch in semitones,
// taken from the frequency the voice was just set to.
void mpe_chord_note_change(uint8_t i, float target, uint16_t ramp_ms, int note) {
  mpe_chord_from[i] = ramp_ms ? mpe_chord_position(i) : target;
  mpe_chord_to[i] = target;
  mpe_chord_ramp_start[i] = millis();
  mpe_chord_ramp_ms[i] = ramp_ms;
  mpe_chord_static[i] = mpe_offset_semitones(note);
}

// The bend has to reach the receiver before the note does, or the note speaks at
// the equal tempered pitch and jumps to the right one.
void mpe_prepare_chord(uint8_t i) {
  if (!mpe_mode) return;
  int16_t b = mpe_bend_value(mpe_chord_bend_semitones(i));
  queue_midi_bend(b, mpe_chord_channel(i), chord_port);
  mpe_chord_bend_sent[i] = b;
  mpe_chord_bend_time[i] = millis();
}

void mpe_prepare_harp(uint8_t i) {
  if (!mpe_mode) return;
  int16_t b = mpe_bend_value(mpe_offset_semitones(current_harp_notes[i]) + harp_voice_glide[i]);
  if (b == mpe_harp_bend_sent[i]) return;
  queue_midi_bend(b, mpe_harp_channel(i), harp_port);
  mpe_harp_bend_sent[i] = b;
  mpe_harp_bend_time[i] = millis();
}

// Called from loop(). A gliding voice re-sends its bend as it moves and settles
// when the ramp is over. Strings are checked too, so a temperament or a division
// changed under a held or ringing string follows it out instead of waiting for
// the next strike. A string follows the note it is sounding, not the one its
// pad would play now: a ringing string keeps its pitch through a chord change.
void mpe_update_glide() {
  if (!mpe_mode) return;
  uint32_t now = millis();
  for (uint8_t i = 0; i < 4; i++) {
    if (chord_started_notes[i] == 0) continue;
    if (now - mpe_chord_bend_time[i] < MPE_GLIDE_UPDATE_MS) continue;
    int16_t b = mpe_bend_value(mpe_chord_bend_semitones(i));
    // While a glide runs, small steps wait for a bigger one; once the voice has arrived (or was retuned
    // in place: purity, a temperament, master tuning) it gets its exact bend, however small the move.
    bool gliding = mpe_chord_ramp_ms[i] && now - mpe_chord_ramp_start[i] < mpe_chord_ramp_ms[i];
    if (b == mpe_chord_bend_sent[i] || (gliding && abs(b - mpe_chord_bend_sent[i]) < MPE_BEND_EPSILON)) continue;
    queue_midi_bend(b, mpe_chord_channel(i), chord_port);
    mpe_chord_bend_sent[i] = b;
    mpe_chord_bend_time[i] = now;
  }
  for (uint8_t i = 0; i < 12; i++) {
    if (harp_started_notes[i] == 0) continue;
    if (now - mpe_harp_bend_time[i] < MPE_GLIDE_UPDATE_MS) continue;
    int16_t b = mpe_bend_value(mpe_offset_semitones(harp_voice_current_note[i]) + harp_voice_glide[i]);
    if (b == mpe_harp_bend_sent[i]) continue;
    queue_midi_bend(b, harp_started_channel[i], harp_started_port[i]);
    mpe_harp_bend_sent[i] = b;
    mpe_harp_bend_time[i] = now;
  }
}

// RPN 6 on a master channel declares a zone and how many member channels it has;
// RPN 0 on any member sets the bend range for the whole zone. A host that reads
// these configures itself, so the player does not set channels on both sides.
void mpe_send_rpn(uint8_t rpn, uint8_t value, uint8_t channel, uint8_t cable) {
  queue_midi_cc(101, 0, channel, cable);
  queue_midi_cc(100, rpn, channel, cable);
  queue_midi_cc(6, value, channel, cable);
}

void mpe_configure() {
  if (!mpe_mode) {
    if (!mpe_zone_declared) return;  // nothing was declared, so nothing to release
    mpe_send_rpn(6, 0, 1, chord_port);
    if (harp_port != chord_port) mpe_send_rpn(6, 0, 1, harp_port);
    mpe_zone_declared = false;
    return;
  }
  if (harp_port != chord_port) {
    mpe_send_rpn(6, 4, 1, chord_port);
    mpe_send_rpn(0, MPE_BEND_RANGE_SEMITONES, 2, chord_port);
    mpe_send_rpn(6, 12, 1, harp_port);
    mpe_send_rpn(0, MPE_BEND_RANGE_SEMITONES, 2, harp_port);
  } else {
    mpe_send_rpn(6, 15, 1, chord_port);
    mpe_send_rpn(0, MPE_BEND_RANGE_SEMITONES, 2, chord_port);
  }
  mpe_zone_declared = true;
}

void harp_midi_off(uint8_t i) {
  if (harp_started_notes[i] == 0) return;
  queue_midi(false, harp_started_notes[i], harp_release_velocity, harp_started_channel[i], harp_started_port[i]);
  harp_started_notes[i] = 0;
}

// A string still sounding the note another is about to start, on the same
// channel, is ended first. Its own note-off would otherwise arrive later and cut
// the new note short at the far end. Only outside MPE, where strings share a
// channel, and only when two strings play the same note.
// A plucked string's midi velocity: the harp's, softer for a light touch, down to a tenth
uint8_t harp_note_velocity(uint8_t i) {
  float softer = 0.9f * string_softness(i);   // touch and strum velocity's say (string_firmness is the pluck's as it sounds)
  return max(1, (int)lroundf(harp_attack_velocity * (1 - softer)));
}

void harp_midi_on(uint8_t i) {
  harp_midi_off(i);
  uint8_t note = midi_base_note_transposed + midi_out_note(current_harp_notes[i]);
  uint8_t channel = mpe_harp_channel(i);
  for (uint8_t j = 0; j < 12; j++) {
    if (j != i && harp_started_notes[j] == note && harp_started_channel[j] == channel && harp_started_port[j] == harp_port) {
      harp_midi_off(j);
    }
  }
  mpe_prepare_harp(i);
  mts_tune(note, mpe_offset_semitones(current_harp_notes[i]) + harp_voice_glide[i], harp_port);
  queue_midi(true, note, harp_note_velocity(i), channel, harp_port);
  harp_started_notes[i] = note;
  harp_started_channel[i] = channel;
  harp_started_port[i] = harp_port;
}

// Switching mode moves every voice to a different channel, so anything sounding
// is released on the channel it was started on before the change takes effect.
void mpe_set_mode(uint8_t value) {
  uint8_t requested = value ? 1 : 0;
  if (requested != mpe_mode) {
    for (uint8_t i = 0; i < 4; i++) {
      if (chord_started_notes[i] != 0) {
        queue_midi(false, chord_started_notes[i], chord_release_velocity, mpe_chord_channel_for(i, mpe_mode), chord_port);
        chord_started_notes[i] = 0;
      }
    }
    for (uint8_t i = 0; i < 12; i++) {
      harp_midi_off(i);
      mpe_harp_bend_sent[i] = 0;
    }
    mpe_mode = requested;
  }
  mpe_configure();
}

//-->>FUNCTION THAT NEED ANNOUNCING
void save_config(int bank_number, bool default_save);
void load_config(int bank_number);
uint8_t calculate_note_harp(uint8_t string, bool slashed, bool sharp);
uint8_t harp_string_note(uint8_t i, bool slashed, bool sharp);
uint8_t calculate_note_chord(uint8_t voice, bool slashed, bool sharp);
bool apply_voice_leading(bool sharp, uint8_t *out);
bool apply_slash_voice_leading(uint8_t *out);
void apply_slash_voice(bool sharp);
void apply_cantus();
bool apply_brothers();
bool brothers_scale(uint8_t (*chord)[7], uint8_t *scale);
void build_chord_notes();
void cantus_follow(uint8_t string);
void refresh_chord_voicing();
void set_chord_voice_frequency(uint8_t i, int16_t current_note);
void retune_active_voices();
void refresh_chord_filter();
// the note frequency each chord voice is currently sounding, kept so the filter
// corner can be recomputed for a voice without touching anything else about it
float chord_voice_note_freq[4] = {0, 0, 0, 0};
int16_t chord_voice_current_note[4] = {0, 0, 0, 0};   // last note each chord voice was tuned to, so a tuning change can re-apply it (below 0 for a bass line under the chord, see ACCOMPANIMENT)
void set_harp_voice_frequency(uint8_t i, uint16_t current_note);
void rebuild_custom_scale();
void rebuild_generator_scale();
void calculate_ws_array();
void acc_begin();
void acc_end();
void acc_midi_start();
void acc_midi_stop();
void acc_midi_clock();
void acc_snapshot();
void acc_settings_changed();
void acc_meter_changed();
void acc_clock_out_changed();
void acc_tap(bool button = true);
void acc_tap_reset();
void set_rythm_mode(bool on);
bool midi_chord_message(uint8_t type, uint8_t cable, uint8_t channel, uint8_t data1, uint8_t data2);
void midi_chords_clear();
uint8_t acc_held_mask();
void stop_chord_voices(uint8_t mask);

//-->>LED HSV CALCULATION
// function to calculate led RGB value, thank you SO
void set_led_color(float h, float s, float v) {
  double hh, p, q, t, ff;
  long i;
  double r, g, b;
  if (s <= 0.0) {
    r = v;
    g = v;
    b = v;
    analogWrite(R_LED_PIN, 0);
    analogWrite(G_LED_PIN, 0);
    analogWrite(B_LED_PIN, 0);
    return;
  }
  hh = h;
  if (hh >= 360.0)
    hh = 0.0;
  hh /= 60.0;
  i = (long)hh;
  ff = hh - i;
  p = v * (1.0 - s);
  q = v * (1.0 - (s * ff));
  t = v * (1.0 - (s * (1.0 - ff)));
  switch (i) {
  case 0:
    r = v;
    g = t;
    b = p;
    break;
  case 1:
    r = q;
    g = v;
    b = p;
    break;
  case 2:
    r = p;
    g = v;
    b = t;
    break;
  case 3:
    r = p;
    g = q;
    b = v;
    break;
  case 4:
    r = t;
    g = p;
    b = v;
    break;
  case 5:
  default:
    r = v;
    g = p;
    b = q;
    break;
  }
  analogWrite(R_LED_PIN, r * 200);
  analogWrite(G_LED_PIN, g * 115);
  analogWrite(B_LED_PIN, b * 70);
  return;
}

//-->>UTILITIES FOR SYSEX HANDLING
// Push and pop: one saved copy of the live parameters, so a remote can change whatever it likes and
// have everything put back exactly, including anything it forgot or couldn't reach. It is the live
// parameters only: nothing is written to a preset, so a saved bank is never touched. A second push
// while one is held is ignored, so a remote that pushes twice can't lose the original.
// ---------- USB audio (address 244), a setting of the instrument, not of a preset ----------
// The minichord sends its sound to the phone or computer it's plugged into (the recording tap,
// audio_definition.h USB_out). The Teensy's USB type that gives it that also offers the host a USB
// speaker, which nothing ever played: a phone sends all its sound there and goes quiet. Now:
//   0  as it always was: the speaker offered, what's sent to it not played
//   1  play along: what's sent to it played through the minichord, beside the instrument
//   2  no speaker offered, so the host keeps its own sound
// The host reads what a device offers once, when it connects, so the choice is kept in the Teensy's
// EEPROM (not in the presets, whose every address a preset load replaces) and read before USB starts
// (startup_middle_hook); changing to or from 2 saves it and restarts the minichord, once the
// settings have stopped arriving for a moment, so a whole backup sent from an editor isn't cut off.
// The descriptors for both arrangements are in the core's usb_desc.c, as scripts/usb_audio_choice.py
// builds it. Without the speaker the minichord connects at full speed (a Pixel won't record 44.1 kHz
// from a high-speed device) and its recording also offers 48 kHz beside 44.1, which the host picks (a
// Pixel's audio runs at 48 and played 44.1 choppy); it's sent from a ring of its own, the packet sizes
// following the minichord's clock (include/usb_audio_rate.h).
const int usb_audio_adress = 244;
const int usb_audio_eeprom = 0;                  // EEPROM byte; erased (0xFF) reads as 0, stock
const uint32_t usb_audio_restart_ms = 1500;      // quiet before the restart
uint8_t usb_audio_mode = 0;                      // as it works now
uint8_t usb_audio_booted = 0;                    // as the host was told when it connected
bool usb_audio_restart_pending = false;
elapsedMillis usb_audio_since;
extern "C" uint8_t eeprom_read_byte(const uint8_t *addr);
extern "C" void eeprom_write_byte(uint8_t *addr, uint8_t value);
extern "C" void eepromemu_flash_write(void *addr, const void *data, uint32_t len);   // the core's raw flash routines (eeprom.c), for the preset store
extern "C" void eepromemu_flash_erase_sector(void *addr);
void preset_store_wipe();
extern "C" void usb_desc_pick(int speaker);      // the core's usb_desc.c, as usb_audio_choice.py builds it
static uint8_t usb_audio_stored() { uint8_t v = eeprom_read_byte((const uint8_t *)usb_audio_eeprom); return v <= 2 ? v : 0; }
static bool usb_audio_speaker(uint8_t mode) { return mode != 2; }
// before USB starts: the host is told of a speaker or not, as last chosen
extern "C" void startup_middle_hook(void) {
  usb_audio_booted = usb_audio_mode = usb_audio_stored();
  usb_desc_pick(usb_audio_speaker(usb_audio_booted));
}
// the host's sound through the minichord only when playing along, at the host's volume where the host
// sets the minichord's (most computers, some phones), and as it comes where it never does
void usb_audio_gain() {
#ifdef AUDIO_INTERFACE
  static float last = -1;
  float v = usb_audio_mode != 1 ? 0.0f : (AudioInputUSB::features.change ? USB_in.volume() : 1.0f);
  if (v == last) return;
  last = v;
  DAC_l_mixer.gain(1, v);
  DAC_r_mixer.gain(1, v);
#endif
}
// a value written to 244 (by an editor or a page): kept, and a restart scheduled if the host must be told
// FLASHMEM on the settings path (this and the other functions so marked: the dump, control commands,
// save, temperament, ensemble, vocoder and sample setup): run from flash, as apply_audio_parameter
// already is, so they don't take ITCM. The code had crossed a 32 KB ITCM block by a few hundred bytes,
// and the rest of that block was padding: 33 KB of RAM1 the stack couldn't have.
FLASHMEM void usb_audio_request(int value) {
  uint8_t mode = constrain(value, 0, 2);
  current_sysex_parameters[usb_audio_adress] = mode;
  if (mode == usb_audio_mode && !usb_audio_restart_pending) return;
  usb_audio_mode = mode;
  if (usb_audio_stored() != mode) eeprom_write_byte((uint8_t *)usb_audio_eeprom, mode);
  usb_audio_restart_pending = usb_audio_speaker(mode) != usb_audio_speaker(usb_audio_booted);
  usb_audio_since = 0;
  usb_audio_gain();
}
// in the loop: the restart, once nothing's been sent for a moment
void usb_audio_update() {
  usb_audio_gain();
  if (!usb_audio_restart_pending || usb_audio_since < usb_audio_restart_ms) return;
  Serial.println("USB audio changed: restarting so the host sees it");
  digitalWrite(_MUTE_PIN, LOW);                  // no click on the way down
  delay(20);
  SCB_AIRCR = 0x05FA0004;                        // a system reset: the program starts again, USB with it
}

// ---------- Upside down (address 384), for a left-handed player: a setting of the instrument ----------
// Turned round, the harp is under the left hand and the chord buttons under the right. Every control
// is read as if it had turned with the player, so the instrument plays as it does the right way up:
//   the chord buttons: the one at the top left as the player holds it is F major (button i is 22 - i)
//   the harp: the zone at the top plays what the top zone always has (zone i is 11 - i)
//   the knobs: the top one is the chord knob and the bottom one the mod knob, the harp knob between
//   the preset buttons: the upper one goes up (its arrow, turned round, points up too)
// The rhythm button and the sharp button stay what they are. Everything past the reading (chords,
// combinations, the harp's plates, the looper, MIDI) sees the instrument the right way up, so nothing
// else changes. One thing doesn't turn: zipper 24 and the fretless wrap (harp plates 8 and 10) have an
// island of the bottom zone above the top one, which turned round is below the bottom zone and still
// plays the place above the top; a plate made for the left hand would have it at the other end.
// The arcade wheel (plate 9) is read turned round, its bearing from the top as the player holds it.
// Kept in the Teensy's EEPROM beside USB audio, not in the presets, so it stays as chosen
// whichever preset is loaded; 0 the right way up, 1 upside down.
const int upside_down_adress = 384;
const int upside_down_eeprom = 1;                // EEPROM byte; erased (0xFF) reads as 0, the right way up
void upside_down_apply() {
  harp_sensor.set_upside_down(upside_down);
  chord_matrix.upside_down = upside_down;
  chord_pot.set_pin(upside_down ? POT_MOD_PIN : POT_CHORD_PIN);
  mod_pot.set_pin(upside_down ? POT_CHORD_PIN : POT_MOD_PIN);
  current_sysex_parameters[upside_down_adress] = upside_down;
}
FLASHMEM void upside_down_request(int value) {
  bool flipped = value > 0;
  if (eeprom_read_byte((const uint8_t *)upside_down_eeprom) != (uint8_t)flipped) eeprom_write_byte((uint8_t *)upside_down_eeprom, flipped);
  if (flipped != upside_down) Serial.println(flipped ? "Upside down, for a left-handed player" : "The right way up");
  upside_down = flipped;
  upside_down_apply();
}
static inline uint8_t up_button_pin() { return upside_down ? DOWN_PGM_PIN : UP_PGM_PIN; }
static inline uint8_t down_button_pin() { return upside_down ? UP_PGM_PIN : DOWN_PGM_PIN; }

#include <parameter_introduction.h>
// A setting's factory default in a bank: page 0's from the bank's factory preset, page 1's generated
static inline int16_t parameter_default(int bank_number, uint16_t i) {
  return i < page_size ? default_bank_sysex_parameters[bank_number][i] : parameter_page1_defaults[i - page_size];
}

// ---------- LOCKS: settings the instrument keeps as its own, whatever preset is loaded ----------
// A preset holds every setting, as it always has, and a preset file round-trips unchanged. But some
// settings describe the instrument or the room rather than the sound: which harp plate is fitted,
// how firm a touch it takes, the orchestra's A. A lock keeps the instrument's own value for an
// address: a preset load skips it, as it already skips USB audio (244) and upside down (384), and a
// change made to it live (an editor, the key change combo, MIDI tuning messages) is kept in the
// Teensy's EEPROM a moment later rather than waiting for a save. A save writes the active values,
// so a preset saved here describes this instrument, and the stock firmware loading that file finds
// the right plate.
// Which addresses may be locked goes by each parameter's "lock" in parameters.json (parameter_lock
// in parameter_lookup.h): "never" for actions, the preset's name and the controls' own storage;
// "default" for the ones locked until a player says otherwise (the plate, the touch and release
// thresholds, the master tuning); "fixed" for 244 and 384, the instrument's by their own code,
// which are only reported here. Writing lock (385) with an address locks it, with 1024 + the
// address unlocks it; control command 9 reports the locked addresses (7D 6C, a count, then two
// bytes an address, the fixed ones first); command 10 unlocks them all (0) or locks the default
// ones again (1). The first boot with no locks in EEPROM locks the default ones, each at the value
// the first preset loaded holds, so nothing changes until a player changes it. A default lock a
// newer firmware brings is locked when that firmware first starts on an instrument that already
// has its locks (lock_upgrade): the table keeps the version whose default locks it holds.
const int16_t lock_adress = 385;
const uint8_t lock_max = 64;
const int lock_eeprom = 16;                      // EEPROM, after USB audio and upside down: 'L', the layout, the count, the version, then four bytes an entry
const uint8_t lock_eeprom_layout = 1;
// The version byte was a spare written 0 before firmware 97, and the locks came in with 76, so a 0
// reads as 76: a table seeded later than that can't be told apart, and a setting brought in after 76
// that a player unlocked before 97 is locked once more, the one cost. Settings from 76 and before a
// player unlocked stay unlocked. version_ID has to stay under 256 for the byte, as for parameter_introduction.
const uint8_t lock_first_version = 76;
struct lock_entry { uint16_t adress; int16_t value; };
lock_entry lock_table[lock_max];
uint8_t lock_count = 0;
uint8_t lock_mask[parameter_size / 8];           // a bit an address, for the test in the loop
bool locks_unseeded = false;                     // no locks in EEPROM yet: the defaults take the first preset's values
bool locks_dirty = false;                        // a locked value changed live, to write once the changes stop
elapsedMillis locks_since_change;
const uint32_t lock_write_delay_ms = 2000;
static inline bool lockable(int16_t a) { return a > 0 && a < (int16_t)parameter_size && parameter_lock[a] >= PARAMETER_LOCK_ALLOWED && parameter_lock[a] != PARAMETER_LOCK_FIXED; }
static inline bool locked(int16_t a) { return a >= 0 && a < (int16_t)parameter_size && (lock_mask[a >> 3] & (1 << (a & 7))); }
static int8_t lock_find(uint16_t a) {
  for (uint8_t k = 0; k < lock_count; k++) if (lock_table[k].adress == a) return k;
  return -1;
}
FLASHMEM static void lock_mask_rebuild() {
  memset(lock_mask, 0, sizeof lock_mask);
  for (uint8_t k = 0; k < lock_count; k++) lock_mask[lock_table[k].adress >> 3] |= 1 << (lock_table[k].adress & 7);
}
FLASHMEM static void lock_save() {
  eeprom_write_byte((uint8_t *)(lock_eeprom + 0), 'L');
  eeprom_write_byte((uint8_t *)(lock_eeprom + 1), lock_eeprom_layout);
  eeprom_write_byte((uint8_t *)(lock_eeprom + 2), lock_count);
  eeprom_write_byte((uint8_t *)(lock_eeprom + 3), version_ID);   // the default locks this firmware knows are in, or were unlocked here
  for (uint8_t k = 0; k < lock_count; k++) {
    int at = lock_eeprom + 4 + 4 * k;
    eeprom_write_byte((uint8_t *)(at + 0), lock_table[k].adress & 0xFF);
    eeprom_write_byte((uint8_t *)(at + 1), lock_table[k].adress >> 8);
    eeprom_write_byte((uint8_t *)(at + 2), (uint16_t)lock_table[k].value & 0xFF);
    eeprom_write_byte((uint8_t *)(at + 3), (uint16_t)lock_table[k].value >> 8);
  }
  locks_dirty = false;
}
// The default locks a newer firmware brings, on an instrument whose table an older one wrote: each
// one introduced after the table's version and not in it is locked, at its factory default rather
// than at the live value. Up to 96 these settings rode with each preset, and an editor's bank sets
// from firmware 69 to 89 wrote a 0 into every setting they didn't know (neighbour guard and strip
// guard off), so an upgraded instrument's presets often hold a 0 nobody chose; and this runs before
// any preset is in. A player who did set one on purpose, preset by preset, finds the default once
// and sets it again, and from then on it is the instrument's, whatever preset loads. A page 0
// setting takes the factory value of the bank about to load.
FLASHMEM static void lock_upgrade(uint8_t from) {
  for (int16_t a = 1; a < (int16_t)parameter_size; a++) {
    if (parameter_lock[a] != PARAMETER_LOCK_DEFAULT || parameter_introduction[a] <= from || lock_find(a) >= 0) continue;
    if (lock_count >= lock_max) { Serial.printf("No room to lock %d\n", a); continue; }
    lock_table[lock_count].adress = a;
    lock_table[lock_count].value = parameter_default(current_bank_number, a);
    Serial.printf("Locked %d at %d, new since firmware %d\n", a, lock_table[lock_count].value, from);
    lock_count++;
  }
  lock_save();   // with this version, so it is done once
}
// at power on, before the first preset loads
FLASHMEM void lock_load() {
  lock_count = 0;
  locks_unseeded = eeprom_read_byte((const uint8_t *)(lock_eeprom + 0)) != 'L' || eeprom_read_byte((const uint8_t *)(lock_eeprom + 1)) != lock_eeprom_layout;
  if (!locks_unseeded) {
    uint8_t n = eeprom_read_byte((const uint8_t *)(lock_eeprom + 2));
    if (n > lock_max) n = lock_max;
    for (uint8_t k = 0; k < n; k++) {
      int at = lock_eeprom + 4 + 4 * k;
      uint16_t a = eeprom_read_byte((const uint8_t *)(at + 0)) | (eeprom_read_byte((const uint8_t *)(at + 1)) << 8);
      int16_t v = (int16_t)(eeprom_read_byte((const uint8_t *)(at + 2)) | (eeprom_read_byte((const uint8_t *)(at + 3)) << 8));
      if (!lockable(a) || lock_find(a) >= 0) continue;   // a setting that can't be locked any more (another firmware's table): dropped
      lock_table[lock_count].adress = a;
      lock_table[lock_count].value = v;
      lock_count++;
    }
    uint8_t version = eeprom_read_byte((const uint8_t *)(lock_eeprom + 3));
    if (version == 0) version = lock_first_version;
    if (version < version_ID) lock_upgrade(version);
  }
  lock_mask_rebuild();
  Serial.printf("Locks: %d%s\n", lock_count, locks_unseeded ? " (none kept yet: the defaults will be locked at the first load)" : "");
}
// the default locks, each at the value the live settings hold now
FLASHMEM static void lock_defaults() {
  for (int16_t a = 1; a < (int16_t)parameter_size; a++) {
    if (parameter_lock[a] != PARAMETER_LOCK_DEFAULT || lock_find(a) >= 0 || lock_count >= lock_max) continue;
    lock_table[lock_count].adress = a;
    lock_table[lock_count].value = current_sysex_parameters[a];
    lock_count++;
  }
}
// in load_config, once the preset's values are in: the locked ones give way to the instrument's
FLASHMEM void lock_apply() {
  if (locks_unseeded) {
    locks_unseeded = false;
    lock_defaults();
    lock_mask_rebuild();
    lock_save();
    Serial.printf("Locked the %d default settings as this preset has them\n", lock_count);
  }
  for (uint8_t k = 0; k < lock_count; k++) current_sysex_parameters[lock_table[k].adress] = lock_table[k].value;
}
FLASHMEM static void lock_set(uint16_t a, bool on) {
  if (a < parameter_size && parameter_lock[a] == PARAMETER_LOCK_FIXED) { Serial.printf("Setting %d is the instrument's already\n", a); return; }
  if (!lockable(a)) { Serial.printf("Setting %d can't be locked\n", a); return; }
  int8_t k = lock_find(a);
  if (on && k < 0) {
    if (lock_count >= lock_max) { Serial.println("No room for another lock"); return; }
    lock_table[lock_count].adress = a;
    lock_table[lock_count].value = current_sysex_parameters[a];
    lock_count++;
    Serial.printf("Locked %d at %d\n", a, current_sysex_parameters[a]);
  } else if (!on && k >= 0) {
    for (uint8_t j = k; j + 1 < lock_count; j++) lock_table[j] = lock_table[j + 1];
    lock_count--;
    Serial.printf("Unlocked %d\n", a);
  } else return;
  lock_mask_rebuild();
  lock_save();
}
FLASHMEM void lock_reset(bool defaults) {
  lock_count = 0;
  if (defaults) lock_defaults();
  lock_mask_rebuild();
  lock_save();
  Serial.printf(defaults ? "The default settings locked, %d\n" : "All settings unlocked\n", lock_count);
}
// lock (385): an address locks it, 1024 + an address unlocks it; an action, never a value to keep
FLASHMEM void lock_action(int value) {
  current_sysex_parameters[lock_adress] = 0;
  if (parameters_loading || value <= 0) return;   // a preset holds 0 here; one that doesn't is no reason to lock anything
  if (value < 1024) lock_set(value, true);
  else if (value - 1024 < (int)parameter_size) lock_set(value - 1024, false);
}
// control command 9: the locked addresses to an editor, 7D 6C, a count, then two bytes an address
FLASHMEM void lock_report() {
  uint8_t data[3 + 2 * (lock_max + 2)];
  uint8_t n = 0;
  data[0] = 0x7D;
  data[1] = 0x6C;
  const uint16_t fixed[2] = {usb_audio_adress, upside_down_adress};
  for (uint8_t k = 0; k < 2; k++) { data[3 + 2 * n] = fixed[k] % 128; data[4 + 2 * n] = fixed[k] / 128; n++; }
  for (uint8_t k = 0; k < lock_count; k++) { data[3 + 2 * n] = lock_table[k].adress % 128; data[4 + 2 * n] = lock_table[k].adress / 128; n++; }
  data[2] = n;
  usbMIDI.sendSysEx(3 + 2 * n, data, 0);
}
// in the loop: a locked setting changed live (an editor, the key change combo, a pop, MIDI tuning)
// becomes the instrument's new value, written once the changes have stopped for a moment. The knobs
// and hover apply their sweep without writing the setting, so they move a locked setting for as
// long as they hold it, as they do any other, and leave the lock as it was.
void lock_update() {
  static elapsedMillis since_check;
  if (since_check < 50 || parameters_loading) return;
  since_check = 0;
  for (uint8_t k = 0; k < lock_count; k++) {
    int16_t now = current_sysex_parameters[lock_table[k].adress];
    if (now == lock_table[k].value) continue;
    if (double_tap_engaged) {   // a double tap's toggle is held, not chosen: it is let go of without a trace
      bool held = false;
      for (uint8_t j = 0; j < double_tap_pairs; j++) if (double_tap_engaged_adress[j] == (int16_t)lock_table[k].adress) held = true;
      if (held) continue;
    }
    lock_table[k].value = now;
    locks_dirty = true;
    locks_since_change = 0;
  }
  if (locks_dirty && locks_since_change > lock_write_delay_ms) lock_save();
}

// Push and pop (control commands 5 and 6). A second push keeps the first unless it asks to replace
// it: a remote that has lost track (a page reloaded) can't know one is held. What was pushed
// belongs to its bank, so loading any other bank drops it.
int16_t pushed_sysex_parameters[parameter_size];
bool sysex_pushed = false;
int8_t pushed_bank = -1;
void apply_audio_parameter(int adress, int value);      // defined in the generated sysex_handler.h, included below
void pop_sysex_parameters() {
  if (!sysex_pushed) return;
  sysex_pushed = false;
  for (int i = 0; i < parameter_size; i++) {
    if (i == 7 || i == usb_audio_adress || i == upside_down_adress) continue;   // the firmware version isn't a parameter, and USB audio and upside down are the instrument's
    if (current_sysex_parameters[i] == pushed_sysex_parameters[i]) continue;
    current_sysex_parameters[i] = pushed_sysex_parameters[i];
    apply_audio_parameter(i, pushed_sysex_parameters[i]);
  }
}
// A page of settings to an editor. Page 0 is the dump every editor has always read, recognised by
// its length alone, so it stays exactly that; the others carry a header (7D, the id for
// non-commercial use, then 6D and the page) and are a few bytes longer, which an editor that
// knows only page 0 drops.
FLASHMEM void send_parameter_page(uint8_t page) {
  if (page >= parameter_size / page_size) return;
  const uint8_t header = page == 0 ? 0 : 3;
  uint8_t midi_data_array[3 + page_size * 2];
  midi_data_array[0] = 0x7D;
  midi_data_array[1] = 0x6D;
  midi_data_array[2] = page;
  for (uint16_t i = 0; i < page_size; i++) {
    int16_t parameter_value = constrain(current_sysex_parameters[page * page_size + i], 0, 16383); // a sysex byte only carries 7 bits
    midi_data_array[header + 2 * i] = parameter_value % 128;
    midi_data_array[header + 2 * i + 1] = parameter_value / 128;
  }
  usbMIDI.sendSysEx(header + page_size * 2, midi_data_array, 0);
}

FLASHMEM void control_command(uint8_t command, uint8_t parameter) {
  switch (command) {
  case 0: // SIGNAL TO SEND BACK ALL DATA: page 0, as it always was, two bytes a setting and no header
    Serial.println("Reporting all data");
    send_parameter_page(0);
    break;
  case 7: // page 1 and on, for an editor that knows them: 7D 6D <page>, then two bytes a setting
    send_parameter_page(parameter);
    break;
  case 1: // SIGNAL TO WIPE MEMORY
    Serial.println("Wiping memory");
    digitalWrite(_MUTE_PIN, LOW); // muting the DAC
    preset_store_wipe();
    current_bank_number = 0;
    load_config(current_bank_number);
    digitalWrite(_MUTE_PIN, HIGH); // unmuting the DAC
    break;
  case 2: // saving bank
    Serial.print("Saving to bank: ");
    Serial.println(parameter);
    save_config(parameter, false);
    break;
  case 3: // setting bank to default
    Serial.print("Saving to bank: ");
    Serial.println(parameter);
    current_bank_number = parameter;
    save_config(parameter, true);
    break;
  case 5: // push: remember the live parameters as they are now; parameter 1 replaces one already held
    if (!sysex_pushed || parameter == 1) {
      for (int i = 0; i < parameter_size; i++) pushed_sysex_parameters[i] = current_sysex_parameters[i];
      sysex_pushed = true;
      pushed_bank = current_bank_number;
      Serial.println("Parameters pushed");
    } else Serial.println("Parameters already pushed: keeping the first");
    break;
  case 6: // pop: put the pushed parameters back, and report
    if (sysex_pushed) {
      pop_sysex_parameters();
      Serial.println("Parameters popped");
      control_command(0, 0);
    } else Serial.println("Nothing pushed");
    break;
  case 8: // rhythm mode: parameter 0 off, 1 on, 2 the other way, as a long press on hold
    set_rythm_mode(parameter == 2 ? !rythm_mode : parameter == 1);
    break;
  case 9: // the locked settings, for an editor (see LOCKS)
    lock_report();
    break;
  case 10: // the locks: 0 none, 1 the default ones (see LOCKS)
    lock_reset(parameter == 1);
    lock_report();
    break;
  case 4: // loading a bank, so a remote can read every preset in turn
    if (parameter < preset_number) {
      Serial.print("Loading bank: ");
      Serial.println(parameter);
      current_bank_number = parameter;
      sysex_pushed = false;                                // a new preset: what was pushed belonged to the last one
      load_config(current_bank_number);
      set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
    }
    break;

  default:
    break;
  }
}
// the autogenerated code (see ./generator for the script)
// ---- Formants --------------------------------------------------------------
// A vowel is a set of fixed resonances, the vocal tract's formants, shaping
// whatever the voice feeds it. They don't move with pitch, so one stage on the
// chord voices' mix gives every chord the same vowel, as a choir singing "ah"
// on different notes does: three band-passes at the first three formants,
// blended with the dry sound. Vowel (118) morphs through a, e, i, o and u;
// formant amount (119) is the blend, 0 leaving the sound as it always was;
// voice size (239) moves every formant together, a child's small tract high,
// a bass's large one low; resonance (240) is how sharp they ring.
// The table is the classic adult averages (Peterson and Barney), in Hz.
const float formant_table[5][3] = {
  {730, 1090, 2440},  // a, as in father
  {530, 1840, 2480},  // e, as in bed
  {270, 2290, 3010},  // i, as in heed
  {570, 840, 2410},   // o, as in law
  {300, 870, 2240},   // u, as in boot
};
const float formant_level[3] = {1.0, 0.7, 0.45};   // the higher formants quieter, as in a voice
const float formant_makeup = 1.6;                  // the band-passes pass less than the dry sound
float formant_vowel = 0, formant_amount = 0, formant_size = 50, formant_resonance = 40;
void update_formants() {
  float pos = constrain(formant_vowel, 0, 100) / 100.0 * 4;   // 0 to 4 across the five vowels
  int i = min(3, (int)pos);
  float t = pos - i;
  float scale = powf(2.0, (50.0 - formant_size) / 100.0);   // half an octave either way
  float q = 2.0 + formant_resonance * 0.1;                     // 2 to 12
  AudioFilterBiquad *f[3] = {&formant_1, &formant_2, &formant_3};
  for (int k = 0; k < 3; k++) {
    // between two vowels, each formant glides on a log scale, as pitch is heard
    float hz = expf(logf(formant_table[i][k]) * (1 - t) + logf(formant_table[i + 1][k]) * t) * scale;
    f[k]->setBandpass(0, min(hz, 10000.0f), q);
  }
  float a = constrain(formant_amount, 0, 100) / 100.0;
  formant_mix.gain(0, 1 - a);
  for (int k = 0; k < 3; k++) formant_mix.gain(k + 1, a * formant_level[k] * formant_makeup);
}

// ---- Each plate's touch calibration -----------------------------------------
// The harp's touch strengths (touch velocity and pressure, koto press, harp drums, chords following the
// harp) were set by ear on the stock strip, and a plate with more copper under the finger reads more
// strongly. So each plate carries its own calibration, how strongly it reads against the stock strip in
// percent, and choosing the plate (243) is all a plate swap asks. Measured 2026-10-14 with plate_cal.py:
// twin rows' pads, three times a stock pad's, read 2-3 times as strongly (a normal tap about 400 against
// 150) and play as the stock strip does at 240; the piano plate and framed strings' strings read as the
// stock strip does, and so does the Keymaster's round pads (taps 306 against the strip's 232, light taps
// 132 against 126). Harp touch scale (418) trims on top, in percent: for a player's touch, or framed
// strings played mostly on its pads, which read about 1.4 times its strings but share their zones, so the
// firmware can't tell them apart. Choosing another plate puts the trim back to 100, so a trim made for one
// plate never carries to the next; a preset load leaves both as it brings them (locked, usually: see LOCKS).
const int16_t harp_touch_scale_adress = 418;
const uint8_t harp_plate_count = 12;
const uint16_t harp_plate_strength[harp_plate_count] = {100, 100, 100, 240, 100, 100, 100, 100, 100, 100, 100, 100};
uint16_t harp_touch_scale = 100;   // 0, a preset saved before the setting, is taken as 100
FLASHMEM void apply_harp_touch_scale() {
  static uint32_t applied = 0;
  uint32_t plate = harp_plate_strength[harp_plate < harp_plate_count ? harp_plate : 0];
  uint32_t trim = harp_touch_scale ? harp_touch_scale : 100;
  if (plate * trim / 100 == applied) return;
  applied = plate * trim / 100;
  harp_sensor.set_strength_scale(applied);
  Serial.printf("Harp touch scale %lu: plate %d reads at %lu, trimmed to %lu%%\n", applied, harp_plate, plate, trim);
}
//>>PLATE ID<<
// Which plate is fitted, from how its zones read at rest. The touch chip charges every zone with the same
// current for the same time, and reads 1024 I T / (C VDD): the more copper a zone has, the lower it reads.
// Most of each zone's capacitance is the minichord's own wiring, about 11 pF, and a plate's copper adds a
// pF or so on top, but the pattern over the twelve zones is the plate's. Measured 2026-10-14 on one
// minichord (~/minichord-bench results/platecal_*, rest and rest_after), with the part common to every zone
// taken out (the supply, the temperature): the five plates then fitted, the stock strip, the Keymaster,
// the piano, twin rows and framed strings, were 0.08 to 0.25 pF apart (rms over the zones), and each plate
// read within 0.012 pF of itself over four hours. So a plate is known by its print, the twelve readings.
// Another minichord's wiring is its own, so the prints are learned on each instrument, not shipped: choosing
// a plate live (243) with it fitted learns it, once nothing has touched it for plate_id_learn_ms. Then at
// power on, and every plate_id_check_ms while the harp rests (a plate swapped with the power on), the
// nearest print learned is chosen, if it is close (plate_id_near) and clearly nearer than the next. Whatever
// is not clear changes nothing: a hand over the plate, a plate never learned. A plate's print follows it
// slowly each time it is known, for days of drift no one has measured yet. Plates of the same copper share a
// print, and a plate number.
// Plate detect (422): 0 off, 1 on, 2 forgets every print learned and is on, 3 learns the plate fitted again as
// the one harp plate has (an editor's teach button: choosing the plate already chosen changes nothing) and
// is on. 2 and 3 are things done, not kept: the setting goes back to 1, and a preset loaded never does them.
// What it does is shown on the LED (plate_led_show), as a player has no serial monitor.
const int16_t plate_detect_adress = 422;
uint8_t plate_detect = 1;
const int plate_id_eeprom = 320;                 // after the locks (16 to 291): 'P', the layout, then a print a plate
const uint8_t plate_id_layout = 1;
const uint8_t plate_id_bytes = 14;               // the mean reading (two bytes), each zone's difference from it (one each)
const float plate_id_k = 1024 * 63 * 0.5f / 3.3f;   // pF x counts at the touch charge (63 uA, 0.5 us), for C = k / reading
const float plate_id_near = 0.035f;              // pF (rms over the zones): a print this close is the plate
const uint16_t plate_id_learn_ms = 2000;
const uint16_t plate_id_check_ms = 3000;
const uint8_t plate_id_samples = 32;
struct plate_print { bool known; float pf[12]; };   // each zone's capacitance less the plate's mean, pF
plate_print plate_prints[harp_plate_count];
bool plate_id_learn_wanted = false;              // a plate was chosen live: learn it once it rests
bool plate_id_check_wanted = false;              // a plate was just swapped in: look at once (PLATE SWAP)
uint8_t plate_swap_state = 0;                    // SWAP_NONE while no swap is being waited out (PLATE SWAP)
bool plate_swap_bare = false;                    // the last swap settled with no plate on
void choose_harp_plate(int value);

// Plate detect said on the LED for a second or so, over whatever it showed, which takes it back after
// (step_led_animation): white three times, a plate learned; white twice, a plate found fitted and chosen; red
// once, long, a plate not learned, as the harp reads as another plate already learned (it was chosen without
// being fitted, or the two plates' copper is the same). Nothing stock lights it: only plate detect's doings.
enum { PLATE_LED_NONE, PLATE_LED_LEARNED, PLATE_LED_FOUND, PLATE_LED_REFUSED };
uint8_t plate_led_pattern = PLATE_LED_NONE;
int8_t plate_led_shown = -1;                     // the step lit last: 1 lit, 0 dark
elapsedMillis plate_led_timer;

FLASHMEM static void plate_led_show(uint8_t pattern) {
  plate_led_pattern = pattern;
  plate_led_shown = -1;
  plate_led_timer = 0;
}

// From step_led_animation while a pattern shows; the LED is written only when the step changes
FLASHMEM void plate_led_step() {
  uint32_t t = plate_led_timer;
  int8_t step;   // 1 lit, 0 dark, -1 done
  if (plate_led_pattern == PLATE_LED_REFUSED) step = t < 900 ? 1 : -1;
  else step = t >= (plate_led_pattern == PLATE_LED_LEARNED ? 3u : 2u) * 300 ? -1 : t % 300 < 180;
  if (step == plate_led_shown) return;
  plate_led_shown = step;
  float level = 1 - led_attenuation;
  if (step < 0) {
    plate_led_pattern = PLATE_LED_NONE;
    set_led_color(bank_led_hue, bank_led_saturation(), level);   // an animation running takes it on its next step
  } else if (!step) set_led_color(0, 1.0, 0);
  else if (plate_led_pattern == PLATE_LED_REFUSED) set_led_color(0, 1.0, level);
  else set_led_color(0, 0.01, level);   // white: saturation 0 turns the LED off
}

FLASHMEM static void plate_print_of(const uint16_t (&rest)[12], uint16_t &mean, float (&pf)[12]) {
  float sum = 0, c[12];
  uint32_t total = 0;
  for (uint8_t z = 0; z < 12; z++) { c[z] = plate_id_k / max(rest[z], (uint16_t)1); sum += c[z]; total += rest[z]; }
  mean = total / 12;
  for (uint8_t z = 0; z < 12; z++) pf[z] = c[z] - sum / 12;
}

FLASHMEM static void plate_id_store(uint8_t plate, const uint16_t (&rest)[12]) {
  int at = plate_id_eeprom + 2 + plate * plate_id_bytes;
  uint32_t total = 0;
  for (uint8_t z = 0; z < 12; z++) total += rest[z];
  uint16_t mean = total / 12;
  uint8_t bytes[plate_id_bytes] = {(uint8_t)(mean & 0xFF), (uint8_t)(mean >> 8)};
  for (uint8_t z = 0; z < 12; z++) bytes[2 + z] = (uint8_t)(int8_t)constrain((int)rest[z] - mean, -127, 127);
  if (eeprom_read_byte((const uint8_t *)plate_id_eeprom) != 'P' || eeprom_read_byte((const uint8_t *)(plate_id_eeprom + 1)) != plate_id_layout) {
    for (uint8_t p = 0; p < harp_plate_count; p++) {   // every slot unlearned: its mean, both bytes, 0
      eeprom_write_byte((uint8_t *)(plate_id_eeprom + 2 + p * plate_id_bytes), 0);
      eeprom_write_byte((uint8_t *)(plate_id_eeprom + 3 + p * plate_id_bytes), 0);
    }
    eeprom_write_byte((uint8_t *)plate_id_eeprom, 'P');
    eeprom_write_byte((uint8_t *)(plate_id_eeprom + 1), plate_id_layout);
  }
  for (uint8_t k = 0; k < plate_id_bytes; k++) {
    if (eeprom_read_byte((const uint8_t *)(at + k)) != bytes[k]) eeprom_write_byte((uint8_t *)(at + k), bytes[k]);
  }
  uint16_t back[12];
  for (uint8_t z = 0; z < 12; z++) back[z] = mean + (int8_t)bytes[2 + z];
  uint16_t m;
  plate_print_of(back, m, plate_prints[plate].pf);
  plate_prints[plate].known = true;
}

FLASHMEM void plate_id_load() {
  for (uint8_t p = 0; p < harp_plate_count; p++) plate_prints[p].known = false;
  if (eeprom_read_byte((const uint8_t *)plate_id_eeprom) != 'P' || eeprom_read_byte((const uint8_t *)(plate_id_eeprom + 1)) != plate_id_layout) return;
  for (uint8_t p = 0; p < harp_plate_count; p++) {
    int at = plate_id_eeprom + 2 + p * plate_id_bytes;
    uint16_t mean = eeprom_read_byte((const uint8_t *)at) | (eeprom_read_byte((const uint8_t *)(at + 1)) << 8);
    if (mean == 0 || mean > 1023) continue;   // unlearned (or, before this was fixed, half wiped: 0xFF00)
    uint16_t rest[12];
    for (uint8_t z = 0; z < 12; z++) rest[z] = mean + (int8_t)eeprom_read_byte((const uint8_t *)(at + 2 + z));
    uint16_t m;
    plate_print_of(rest, m, plate_prints[p].pf);
    plate_prints[p].known = true;
  }
}

FLASHMEM void plate_detect_set(int value) {
  if (value >= 2) {
    // done once, when sent: kept as 2 it forgot every plate again at each preset load and power on
    if (!parameters_loading && value == 2) {   // forget every plate
      eeprom_write_byte((uint8_t *)plate_id_eeprom, 0);
      for (uint8_t p = 0; p < harp_plate_count; p++) plate_prints[p].known = false;
      Serial.println("Plate detect: every plate forgotten");
    }
    if (!parameters_loading && value == 3) {   // teach: learn the plate fitted as the one chosen, once it rests
      plate_id_learn_wanted = true;
      Serial.printf("Plate detect: learning plate %d, hands off the harp\n", harp_plate);
    }
    value = 1;
    current_sysex_parameters[plate_detect_adress] = 1;
    if (sysex_controler_connected && !parameters_loading) send_parameter_page(plate_detect_adress / page_size);   // so an editor's control follows
  }
  plate_detect = value ? 1 : 0;
}

// The harp at rest, averaged over plate_id_samples readings: false while any zone is touched or the
// readings wander (a hand coming near), which ends the average and starts it again.
FLASHMEM static bool plate_id_rest(uint16_t (&rest)[12]) {
  static uint32_t sum[12];
  static uint16_t lo[12], hi[12];
  static uint8_t count = 0;
  static elapsedMillis since;
  if (since < 2) return false;
  since = 0;
  uint16_t now[12];
  bool touched;
  harp_sensor.read_rest(now, touched);
  // touched as the harp has it: right after a swap a zone can sit a little under its rest, short of a touch,
  // which read_rest's own test (past the release threshold) took for a finger, and nothing was ever looked at
  touched = false;
  for (uint8_t z = 0; z < 12; z++) touched |= harp_array[z].read_value();
  if (touched) { count = 0; return false; }
  for (uint8_t z = 0; z < 12; z++) {
    if (count == 0) { sum[z] = 0; lo[z] = hi[z] = now[z]; }
    sum[z] += now[z];
    lo[z] = min(lo[z], now[z]);
    hi[z] = max(hi[z], now[z]);
    if (hi[z] - lo[z] > 4) { count = 0; return false; }
  }
  if (++count < plate_id_samples) return false;
  for (uint8_t z = 0; z < 12; z++) rest[z] = (sum[z] + plate_id_samples / 2) / plate_id_samples;
  count = 0;
  return true;
}

// From the loop: learn a plate just chosen, or find the plate fitted
FLASHMEM void plate_id_update() {
  static elapsedMillis since_check;
  static bool booted = false;
  if (!plate_detect && !plate_id_learn_wanted) return;
  // never while a swap is waited out, nor with no plate on: on 2026-10-17 a learn still waiting for a quiet harp
  // took its readings as the plate came off and learned the bare pins as the Keymaster, then found the bare pins
  // to be the Keymaster twice
  if (plate_swap_state != 0 || plate_swap_bare) return;
  bool due = plate_id_learn_wanted ? since_check >= plate_id_learn_ms : !booted ? millis() > 1500 :
             plate_id_check_wanted || since_check >= plate_id_check_ms;
  if (!due) return;
  uint16_t rest[12];
  if (!plate_id_rest(rest)) return;
  since_check = 0;
  uint16_t mean;
  float pf[12];
  plate_print_of(rest, mean, pf);
  if (plate_id_learn_wanted) {
    plate_id_learn_wanted = false;
    // a plate chosen without being fitted (an editor setting up presets) reads as the plate that is: not
    // learned, if another plate's print is that close
    for (uint8_t p = 0; p < harp_plate_count; p++) {
      if (p == harp_plate || !plate_prints[p].known) continue;
      float d = 0;
      for (uint8_t z = 0; z < 12; z++) d += (pf[z] - plate_prints[p].pf[z]) * (pf[z] - plate_prints[p].pf[z]);
      if (sqrtf(d / 12) <= plate_id_near) {
        Serial.printf("Plate detect: plate %d not learned, it reads as plate %d\n", harp_plate, p);
        plate_led_show(PLATE_LED_REFUSED);
        return;
      }
    }
    plate_id_store(harp_plate, rest);
    Serial.printf("Plate detect: learned plate %d (mean %d)\n", harp_plate, mean);
    plate_led_show(PLATE_LED_LEARNED);
    return;
  }
  booted = true;
  plate_id_check_wanted = false;
  int8_t best = -1;
  float best_d = 1e9, next_d = 1e9;
  for (uint8_t p = 0; p < harp_plate_count; p++) {
    if (!plate_prints[p].known) continue;
    float d = 0;
    for (uint8_t z = 0; z < 12; z++) d += (pf[z] - plate_prints[p].pf[z]) * (pf[z] - plate_prints[p].pf[z]);
    d = sqrtf(d / 12);
    if (d < best_d) { next_d = best_d; best_d = d; best = p; }
    else if (d < next_d) next_d = d;
  }
  if (best < 0 || best_d > plate_id_near || best_d * 2 > next_d) return;   // not clear: leave it be
  // the print follows the plate slowly: a quarter of the way, written only when that moves a count
  uint16_t was[12], blended[12];
  int at = plate_id_eeprom + 2 + best * plate_id_bytes;
  uint16_t m0 = eeprom_read_byte((const uint8_t *)at) | (eeprom_read_byte((const uint8_t *)(at + 1)) << 8);
  bool moved = false;
  for (uint8_t z = 0; z < 12; z++) {
    was[z] = m0 + (int8_t)eeprom_read_byte((const uint8_t *)(at + 2 + z));
    blended[z] = (was[z] * 3 + rest[z] + 2) / 4;
    moved |= blended[z] != was[z];
  }
  if (moved) plate_id_store(best, blended);
  if (best == harp_plate) return;
  Serial.printf("Plate detect: plate %d fitted (%.3f pF from its print, the next %.3f)\n", best, best_d, next_d);
  current_sysex_parameters[harp_plate_adress] = best;
  choose_harp_plate(best);
  plate_id_learn_wanted = false;   // found, not chosen
  harp_sensor.recalibrate();   // the baselines the old plate left
  if (sysex_controler_connected) send_parameter_page(harp_plate_adress / page_size);
  plate_led_show(PLATE_LED_FOUND);
}

// harp plate (243): a plate chosen live, rather than loaded with a preset, brings its calibration untrimmed
FLASHMEM void choose_harp_plate(int value) {
  bool swapped = value != harp_plate && !parameters_loading;
  if (value != harp_plate && !parameters_loading && plate_detect) plate_id_learn_wanted = true;
  harp_plate = value;
  if (swapped && current_sysex_parameters[harp_touch_scale_adress] != 100) {
    current_sysex_parameters[harp_touch_scale_adress] = 100;
    harp_touch_scale = 100;
    if (sysex_controler_connected) send_parameter_page(harp_touch_scale_adress / page_size);   // so an editor's control follows
  }
  apply_harp_touch_scale();
  apply_harp_thresholds();
}

//>>BENDS<<
// What a circuit bender wires into an Omnichord's body-contact points: pitch drop (430-432), voltage
// starve (433), bitcrush (434-435), clock bend (436-437) and stutter (438-439), each with a depth from
// 0 to 100. Four are bent_bus (lib/bent_bus) on the whole mix; clock bend multiplies the rhythm's
// clock (acc_tick). A depth comes from its setting, which a preset keeps and the knobs, hover and the
// double tap can move, or live from control change 112-116 (any channel, 0 to 127), which is never
// kept; the deeper counts. At 0 the instrument is as it was: nothing stock changes.
uint8_t bend_setting[5] = {0, 0, 0, 0, 0};   // pitch drop, voltage starve, bitcrush, clock bend, stutter
uint8_t bend_cc[5] = {0, 0, 0, 0, 0};        // the same from control change 112-116
uint8_t pitch_drop_range = 12;               // semitones at 100 (431)
uint16_t pitch_drop_sag = 150;               // ms (432)
uint8_t bitcrush_mode = 0;                   // 0 bits and rate, 1 bits, 2 rate (435)
bool clock_bend_faster = false;              // clock bend speeds the rhythm up rather than slowing it (437)
bool stutter_sync = false;                   // stutter's slices a beat's fraction while the rhythm runs (439)

FLASHMEM void bend_apply() {
  float d[5];
  for (uint8_t k = 0; k < 5; k++) d[k] = max(bend_setting[k], bend_cc[k]) / 100.0f;
  clock_bend_ratio = powf(2.0f, (clock_bend_faster ? 2.0f : -2.0f) * d[3]);   // a quarter or four times the tempo at 100
  bool in_time = stutter_sync && rythm_mode && rythm_timer_running && !rythm_clock_synced;
  uint32_t beat = in_time ? (uint32_t)(AUDIO_SAMPLE_RATE_EXACT * 60.0f / (rythm_bpm * clock_bend_ratio)) : 0;
  bent_bus.pitch_drop(d[0], pitch_drop_range, pitch_drop_sag);
  bent_bus.starve(d[1]);
  bent_bus.crush(d[2], bitcrush_mode);
  bent_bus.stutter(d[4], beat);
}

// Stutter (438) from its setting: never from a preset, which would load the instrument frozen in a
// slice (a knob or hover left on it when the preset was saved), so a load sets it to 0
FLASHMEM void stutter_setting(int value) {
  if (parameters_loading) {
    value = 0;
    current_sysex_parameters[438] = 0;
  }
  bend_setting[4] = value;
  bend_apply();
}

// From the loop: stutter in sync follows the tempo as it moves, and the rhythm starting and stopping
FLASHMEM void bend_watch() {
  static elapsedMillis since;
  if (!stutter_sync || since < 50) return;
  since = 0;
  bend_apply();
}

// Control change 112-116: a bend's depth, live. 121 (reset all controllers) lets them all go; it
// carries on to whatever else listens for it.
FLASHMEM bool bend_cc_message(uint8_t controller, uint8_t value) {
  if (controller == 121) {
    for (uint8_t k = 0; k < 5; k++) bend_cc[k] = 0;
    bend_apply();
    return false;
  }
  if (controller < 112 || controller > 116) return false;
  bend_cc[controller - 112] = (value * 100 + 63) / 127;
  bend_apply();
  return true;
}

#include <sysex_handler.h>
// A parameter write is F0 <adress lo> <adress hi> <value lo> <value hi> F7. Universal system
// exclusive messages are six bytes long too, F0 <7E or 7F> <device id> <sub id 1> <sub id 2> F7,
// and hosts and DAWs send them unprompted, so they must not be read as a parameter write. Their
// first byte can't tell them apart: it is also the low byte of adresses 126, 127, 254 and 255.
// A device id of 2 or more already makes the adress 382 or more, past the parameter array, so
// only messages sent to device 0 or 1 need telling apart; most go to 0x7F, "all call". Those
// are recognised by their sub ids: identity request (7E 06 01), GM system on/off (7E 09 01-03)
// and MIDI machine control commands (7F 06 01-7F). The price is that writes of the same shape
// are dropped: 134 and 137 to adress 126 or 254 (frequency multiplier 2 of 1.34 or 1.37, a
// neighbouring slider step still lands), 265 and 393 to them, and 6+128*n (n>0) to 127 or 255,
// which amplitude 3 (0 to 100) never sends. Rarer six-byte universal messages sent to device 0
// or 1, such as the sample dump handshakes (7E 7B-7F), still read as a write to those adresses.
// A MIDI Tuning Standard message from a computer (Scala, Leimma, an MTS-ESP
// master's export) tunes the minichord: an octave's twelve offsets become your
// own temperament (13), which it selects, as if they had been set by hand, so a
// preset saved now keeps them. Read: the scale/octave tunings, 1-byte (08 08,
// -64 to +63 cents) and 2-byte (08 09, +-100 cents), real time or not, and the
// bulk dump (08 01), whose octave from middle C (keys 60-71) is taken as the
// pattern of every octave. Twelve-note tunings only: the divided octaves stay
// with the temperament setting. Whether the message was one.
FLASHMEM bool mts_receive(const byte *data, uint32_t length) {
  if (length < 6 || data[0] != 0xF0 || (data[1] != 0x7E && data[1] != 0x7F) || data[3] != 0x08) return false;
  float cents[12];
  if (data[4] == 0x08 && length >= 21) {
    for (uint8_t k = 0; k < 12; k++) cents[k] = (float)data[8 + k] - 64;
  } else if (data[4] == 0x09 && length >= 33) {
    for (uint8_t k = 0; k < 12; k++) {
      int16_t v = (data[8 + 2 * k] << 7) | data[9 + 2 * k];
      cents[k] = (v - 8192) * 100.0f / 8192.0f;
    }
  } else if (data[4] == 0x01 && data[1] == 0x7E && length >= 406) {
    for (uint8_t k = 0; k < 12; k++) {
      const byte *t = data + 22 + 3 * (60 + k);
      if (t[0] == 0x7F && t[1] == 0x7F && t[2] == 0x7F) { cents[k] = 0; continue; }   // no change
      float pitch = t[0] + ((t[1] << 7) | t[2]) / 16384.0f;
      float off = 100.0f * (pitch - (60 + k));
      cents[k] = constrain(off, -100.0f, 100.0f);
    }
  } else {
    return false;
  }
  for (uint8_t k = 0; k < 12; k++) {
    int16_t value = constrain((int16_t)lroundf(cents[k] * 10) + 1000, 0, 2000);
    current_sysex_parameters[359 + k] = value;
    apply_audio_parameter(359 + k, value);
  }
  current_sysex_parameters[237] = own_temperament;
  apply_audio_parameter(237, own_temperament);
  return true;
}

bool is_universal_sysex(const byte *data) {
  if (data[1] == 0x7E) {
    return (data[3] == 0x06 && data[4] == 0x01) || (data[3] == 0x09 && data[4] >= 0x01 && data[4] <= 0x03);
  }
  if (data[1] == 0x7F) {
    return data[3] == 0x06 && data[4] >= 0x01;
  }
  return false;
}

void processMIDI(void) {
  byte type;
  type = usbMIDI.getType();
  // The bends' depths, live (see BENDS)
  if (type == usbMIDI.ControlChange && bend_cc_message(usbMIDI.getData1(), usbMIDI.getData2())) return;
  // The looper, from a foot controller or a computer: control change 85 record, 86 play, 87 stop,
  // 88 clear, 89 overdub, 90 the next step, on any channel, acting as the value rises past 63
  if (type == usbMIDI.ControlChange && usbMIDI.getData1() >= 85 && usbMIDI.getData1() <= 90) {
    if (usbMIDI.getData2() >= 64) looper_action(usbMIDI.getData1() - 84);
    return;
  }
  // Chord memory, the same way: control change 104 record, 105 play, 106 stop, 107 clear, 108 the next step
  if (type == usbMIDI.ControlChange && usbMIDI.getData1() >= 104 && usbMIDI.getData1() <= 108) {
    if (usbMIDI.getData2() >= 64) chord_memory_action(usbMIDI.getData1() - 103);
    return;
  }
  // Fills from a foot controller or a computer: control change 109 a fill, 110 a break, 111 the
  // ending; on any channel, as the value rises past 63
  if (type == usbMIDI.ControlChange && usbMIDI.getData1() >= 109 && usbMIDI.getData1() <= 111) {
    static bool high[3] = {false, false, false};
    uint8_t k = usbMIDI.getData1() - 109;
    bool now = usbMIDI.getData2() >= 64;
    if (now && !high[k]) rhythm_fill_action(k + 1);
    high[k] = now;
    return;
  }
  // Rhythm mode from a foot controller or a computer, as the hold button: control change 102 turns
  // it on or off (a long press), 103 is a tap of the tempo; on any channel, as the value rises past 63
  if (type == usbMIDI.ControlChange && (usbMIDI.getData1() == 102 || usbMIDI.getData1() == 103)) {
    static bool high[2] = {false, false};
    uint8_t k = usbMIDI.getData1() - 102;
    bool now = usbMIDI.getData2() >= 64;
    if (now && !high[k]) {
      if (k == 0) set_rythm_mode(!rythm_mode);
      else acc_tap(false);   // in rhythm mode, or for the looper's tempo
    }
    high[k] = now;
    return;
  }
  if ((type == usbMIDI.NoteOn || type == usbMIDI.NoteOff || type == usbMIDI.ControlChange)
      && midi_chord_message(type, usbMIDI.getCable(), usbMIDI.getChannel(), usbMIDI.getData1(), usbMIDI.getData2())) {
    return;
  }
  // All Sound Off and All Notes Off (120, 123), as a computer sends them when its transport stops:
  // the notes it sent in end; the minichord's own playing goes on
  if (midi_in_plays && type == usbMIDI.ControlChange && (usbMIDI.getData1() == 120 || usbMIDI.getData1() == 123)) {
    for (uint8_t i = 0; i < 12; i++) if (midi_in_harp[i].owned && !midi_in_harp[i].looped) midi_in_release(midi_in_harp, i, true);
    for (uint8_t i = 0; i < 4; i++) if (midi_in_chord[i].owned && !midi_in_chord[i].looped) midi_in_release(midi_in_chord, i, false);
    return;
  }
  if (midi_in_plays && (type == usbMIDI.NoteOn || type == usbMIDI.NoteOff || type == usbMIDI.ControlChange)) {
    midi_in_message(type, usbMIDI.getCable(), usbMIDI.getChannel(), usbMIDI.getData1(), usbMIDI.getData2());
    return;
  }
  if (type == usbMIDI.SystemExclusive && mts_receive(usbMIDI.getSysExArray(), usbMIDI.getSysExArrayLength())) return;
  if (type == usbMIDI.SystemExclusive && usbMIDI.getSysExArrayLength() == 6) {
    const byte *data = usbMIDI.getSysExArray();
    if (!is_universal_sysex(data)) {
      int adress = data[1] + 128 * data[2];
      if (adress < parameter_size) { // an adress can reach 16383, the parameter array holds 256
        sysex_controler_connected=true; //the message was meant for us, so a controller is connected
        if (adress == 0) { // it is a control command
          control_command(data[3], data[4]);
        } else {
          Serial.print("Received instruction on adress:");
          Serial.print(adress);
          int value = data[3] + 128 * data[4];
          Serial.print(" with value:");
          Serial.println(value);
          current_sysex_parameters[adress] = value;
          apply_audio_parameter(adress, value);
        }
        usb_audio_since = 0;                           // a restart waits till the settings stop coming
      }
    }
  }
  // Following a MIDI clock (24 a beat), see ACCOMPANIMENT: Start begins the pattern on the next
  // clock, as MIDI has it; Stop, or the clock going quiet for half a second (rythm_clock_watch),
  // hands the rhythm back to the minichord's own tempo.
  if(type==usbMIDI.Start && rythm_mode){
    acc_midi_start();
    last_midi_clock_in = 0;
    Serial.println("Start received");
  }
  if(type==usbMIDI.Stop && rythm_mode){
    acc_midi_stop();
  }
  if(type==usbMIDI.Clock){   // the minichord keeps its own clock to itself while another is about
    midi_clock_heard = true;
    midi_clock_heard_ms = millis();
  }
  if(type==usbMIDI.Clock && rythm_mode){
    uint32_t us = last_midi_clock_in;
    last_midi_clock_in = 0;
    // the tempo from the clock's spacing, in float: it was floored to a whole BPM. Only a real
    // interval counts (25 to 1250 BPM), not the first clock after a gap
    if (us > 2000 && us < 100000) {
      rythm_bpm = constrain((rythm_bpm * 10 + 60e6f / (24.0f * us)) / 11.0f, 30.0f, 300.0f);
    }
    acc_midi_clock();
  }
}

//-->>TIMER FUNCTIONS
// function to handle the delayed chord activation
// The chords start a note on voice i, taking it back from an incoming note if it had it.
void chords_take_voice(int i) {
  if (!midi_in_chord[i].owned) return;
  looper_voice_taken(false, i);
  midi_in_chord[i].owned = false;
  chord_voice_level[i] = 1;
  apply_chord_voice_level(i);
}

// A chord voice's oscillators, noise, mixers and filter all come before its envelope, which is
// applied by a multiply after them, so they ran on every block whether the voice sounded or not,
// for the multiply to throw the result away. A voice whose envelope is done now rests: its
// oscillators and noise go to amplitude 0 (keeping their phase, sending nothing), and what follows
// them goes idle with them. Where a note starts it (the three places a chord envelope's note on is
// called), it wakes first, so its sound starts in the same update as its envelope.
bool chord_voice_resting[4] = {false, false, false, false};

// the voice's oscillators and noise as they should be: their own settings while it sounds the
// synth, silent while it rests or a sample sounds in their place
void chord_voice_sources(uint8_t i) {
  bool synth = !chord_voice_resting[i] && !chord_source_of[i];
  chord_osc_1_array[i]->amplitude(synth ? current_sysex_parameters[121] / 100.0 : 0);
  chord_osc_2_array[i]->amplitude(synth ? current_sysex_parameters[124] / 100.0 : 0);
  chord_osc_3_array[i]->amplitude(synth ? current_sysex_parameters[127] / 100.0 : 0);
  chord_noise_array[i]->amplitude(synth ? 0.5 : 0);
}

// from the note on's own context, a timer's interrupt as often as not
void wake_chord_voice(uint8_t i) {
  if (!chord_voice_resting[i]) return;
  chord_voice_resting[i] = false;
  chord_voice_sources(i);
}

// from the loop: the check and the rest together, so a timer's note on can't fall between them
void rest_chord_voices() {
  for (uint8_t i = 0; i < 4; i++) {
    noInterrupts();
    if (!chord_voice_resting[i] && !chord_envelope_array[i]->isActive()) {
      chord_voice_resting[i] = true;
      chord_voice_sources(i);
    }
    interrupts();
  }
}

// The MIDI note a chord voice's note goes out as. A bass line under the chord can go below note 0
// (see ACCOMPANIMENT); anything else is worked out exactly as it always was.
uint8_t chord_midi_note(int16_t note) {
  if (note >= 0) return midi_base_note_transposed + midi_out_note(note);
  int16_t below = (edo_index == 0) ? note : (int16_t)lroundf(note * 12.0f / (float)EDO);
  return (uint8_t)constrain(midi_base_note_transposed + below, 0, 127);
}

// How loud a note of the given firmness (0-1) plays: down to 24 dB, as a string or an incoming note
float firmness_gain(float firmness) {
  return powf(10.0f, -1.2f * (1 - firmness) * (1 - firmness));
}

void play_single_note(int i, IntervalTimer *timer) {
  timer->end();
  chords_take_voice(i);
  wake_chord_voice(i);
  // Full, or the harp's dynamic with chords follow the harp's touch on (also a voice the rhythm
  // played softer, now holding the chord)
  const float level = chord_touch ? chord_touch_gain : 1;
  if (chord_voice_level[i] != level) {
    chord_voice_level[i] = level;
    apply_chord_voice_level(i);
  }
  const uint8_t velocity = chord_touch ? max(1, (int)lroundf(chord_attack_velocity * chord_touch_vscale)) : chord_attack_velocity;
  set_chord_voice_frequency(i, current_applied_chord_notes[i]);
  chord_vibrato_envelope_array[i]->noteOn();
  chord_vibrato_dc_envelope_array[i]->noteOn();
  chord_envelope_array[i]->noteOn();
  chord_envelope_filter_array[i]->noteOn();
  chord_voice_released[i] = false;
  chord_sample_start(i);
  // ISR context: queue only, never touch usbMIDI here.
  if(chord_started_notes[i]!=0){
    queue_midi(false, chord_started_notes[i],chord_release_velocity,mpe_chord_channel(i), chord_port);
    chord_started_notes[i]=0;}
  mpe_prepare_chord(i);
  mts_tune(midi_base_note_transposed + midi_out_note(current_applied_chord_notes[i]), mpe_offset_semitones(current_applied_chord_notes[i]), chord_port);
  queue_midi(true, midi_base_note_transposed+ midi_out_note(current_applied_chord_notes[i]),velocity,mpe_chord_channel(i), chord_port);
  chord_started_notes[i]=midi_base_note_transposed+ midi_out_note(current_applied_chord_notes[i]);
  looper_capture(LOOP_START, 12 + i, chord_voice_note_freq[i], chord_touch ? chord_touch_firmness : 1, chord_started_notes[i], velocity);
}

// A note of the rhythm on chord voice i, from its timer's interrupt: firmness 0-1 sets how loud it
// plays here and goes out as its MIDI velocity (on chord velocity's scale)
void acc_play(uint8_t i, int16_t note, float firmness) {
  chords_take_voice(i);
  wake_chord_voice(i);
  float gain = firmness_gain(firmness);
  if (chord_voice_level[i] != gain) {
    chord_voice_level[i] = gain;
    apply_chord_voice_level(i);
  }
  // ISR context: queue only, never touch usbMIDI here. The old note goes first, so moving the
  // voice to its new one doesn't also send the move (set_chord_voice_frequency) just before the
  // strike sends it again.
  if(chord_started_notes[i]!=0){
    queue_midi(false, chord_started_notes[i],chord_release_velocity,mpe_chord_channel(i), chord_port);
    chord_started_notes[i]=0;}
  set_chord_voice_frequency(i, note);
  chord_vibrato_envelope_array[i]->noteOn();
  chord_vibrato_dc_envelope_array[i]->noteOn();
  chord_envelope_array[i]->noteOn();
  chord_envelope_filter_array[i]->noteOn();
  chord_voice_released[i] = false;
  chord_sample_start(i);
  uint8_t velocity = constrain((int)lroundf(chord_attack_velocity * firmness), 1, 127);
  mpe_prepare_chord(i);
  chord_started_notes[i] = chord_midi_note(note);
  mts_tune(chord_started_notes[i], mpe_offset_semitones(note), chord_port);
  queue_midi(true, chord_started_notes[i], velocity, mpe_chord_channel(i), chord_port);
  looper_capture(LOOP_START, 12 + i, chord_voice_note_freq[i], firmness, chord_started_notes[i], velocity);
}

// A note of the rhythm let go, from its timer's interrupt or the loop. As in stop_chord_notes(): the
// check and the flag are one step, so a note starting in between isn't marked released without
// ever being.
void acc_release(uint8_t i) {
  if (midi_in_chord[i].owned) return;   // an incoming note's, not the rhythm's
  noInterrupts();
  if (chord_envelope_array[i]->isSustain() || (chord_envelope_array[i]->isActive() && !chord_voice_released[i])) {
    chord_vibrato_envelope_array[i]->noteOff();
    chord_vibrato_dc_envelope_array[i]->noteOff();
    chord_envelope_array[i]->noteOff();
    chord_envelope_filter_array[i]->noteOff();
    chord_voice_released[i] = true;
    looper_capture(LOOP_RELEASE, 12 + i, 0, 0, 0, 0);
  }
  interrupts();
  // See stop_chord_notes().
  if (chord_started_notes[i] != 0) {
    queue_midi(false, chord_started_notes[i], chord_release_velocity, mpe_chord_channel(i), chord_port);
    chord_started_notes[i] = 0;
  }
}


// The LED animations are stepped from the main loop rather than from an
// IntervalTimer. Teensy 4 has four timer channels and this firmware declares
// thirteen IntervalTimers; a blink left running holds one of those channels, and
// begin() fails silently when they are gone. That cost the fourth chord voice
// whenever a blink was active, which is not a trade an LED should be making.
bool key_change_led_on = false;
// LED hue per key signature, so the stepper can hold the chosen one.
const float key_change_hues[21] = {
    0.0, 17.14, 34.29, 51.43, 68.57, 85.71, 102.86, 120.0, 137.14, 154.29, 171.43,
    188.57, 205.71, 222.86, 240.0, 257.14, 274.29, 291.43, 308.57, 325.71, 342.86
  };
int8_t key_change_shown = -1;   // key whose colour is being held, or -1 for none
uint8_t double_tap_led_step = 0;
elapsedMillis led_anim_timer;

// Steps whichever LED animation is active. Key change blinks about three times
// a second; a latched double tap breathes at about 0.8Hz, slow enough that the
// two are not mistaken for each other.
void step_led_animation() {
  if (key_change_mode) {
    if (key_change_shown >= 0) {
      // a key is being held: show its colour steadily, not the waiting blink
      set_led_color(key_change_hues[key_change_shown], 1.0, 1.0);
      key_change_led_on = true;
      led_anim_timer = 0;
      return;
    }
    if (led_anim_timer < 150) return;
    led_anim_timer = 0;
    key_change_led_on = !key_change_led_on;
    set_led_color(bank_led_hue, bank_led_saturation(), key_change_led_on ? 1.0 : 0.12);
  } else if (plate_led_pattern) {
    plate_led_step();   // plate detect's flashes, a second or so (PLATE ID)
  } else if (looper_state == LOOPER_COUNTIN) {
    // counting in: red, bright on each beat
    if (led_anim_timer < 20) return;
    led_anim_timer = 0;
    set_led_color(0, 1.0, (millis() - looper_click_flash_ms < 120 ? 1.0 : 0.12) * (1 - led_attenuation));
  } else if (looper_state == LOOPER_RECORDING || looper_state == LOOPER_PLAYING) {
    // the looper: red pulsing while recording, amber pulsing while overdubbing, green while playing
    if (led_anim_timer < 60) return;
    led_anim_timer = 0;
    static uint8_t looper_led_step = 0;
    looper_led_step = (looper_led_step + 1) % 16;
    float pulse = looper_led_step < 8 ? 1.0 : 0.3;
    if (looper_state == LOOPER_RECORDING) set_led_color(0, 1.0, pulse * (1 - led_attenuation));
    else if (looper_overdub) set_led_color(40, 1.0, pulse * (1 - led_attenuation));
    else set_led_color(120, 1.0, 1 - led_attenuation);
  } else if (chord_memory_state == CHORD_MEMORY_RECORDING || chord_memory_state == CHORD_MEMORY_PLAYING) {
    // chord memory: violet pulsing while recording, steady while playing
    if (led_anim_timer < 60) return;
    led_anim_timer = 0;
    static uint8_t memory_led_step = 0;
    memory_led_step = (memory_led_step + 1) % 16;
    float pulse = chord_memory_state == CHORD_MEMORY_PLAYING || memory_led_step < 8 ? 1.0 : 0.3;
    set_led_color(285, 1.0, pulse * (1 - led_attenuation));
  } else if (double_tap_engaged) {
    if (led_anim_timer < 60) return;
    led_anim_timer = 0;
    double_tap_led_step = (double_tap_led_step + 1) % 20;
    set_led_color(bank_led_hue, bank_led_saturation(), (double_tap_led_step < 10 ? 1.0 : 0.45) * (1 - led_attenuation));
  }
}

void rythm_led_settle() {
  noInterrupts();   // so a step lighting it again can't fall between the check and the off
  if (rythm_led_lit && millis() - rythm_led_lit_at >= 200) {
    rythm_led_lit = false;
    analogWrite(RYTHM_LED_PIN, 0);
  }
  interrupts();
}

//-->>AUDIO HELPER FUNCTIONS
// calculationg the ws array
FLASHMEM void calculate_ws_array() {
  for (int i = 0; i < 257; i++) {
    float current_x = (i / 256.0 - 0.5) * 2.0 * PI;
    wave_shape[i] = sin(current_x);
    for (int j = 0; j < ws_sin_param; j++) {
      wave_shape[i] = sin(wave_shape[i] * PI);
    }
  }
}
// setting the pad_frequency
/* Recompute only the filter corner for voices that are sounding.
 *
 * Deliberately NOT set_chord_voice_frequency: that also retunes three
 * oscillators, restarts the glide ramp, and sends a MIDI note off and on when
 * the voice's note has moved. Called on every step of a knob sweep, that would
 * restart the portamento continuously and spray note messages down the wire.
 * This touches the filter and nothing else.
 */
void refresh_chord_filter() {
  AudioNoInterrupts();
  for (int i = 0; i < 4; i++) {
    if (chord_envelope_array[i]->isActive()) {
      chord_voice_filter_array[i]->frequency(chord_voice_note_freq[i] * chord_filter_keytrack + chord_filter_base_freq);
    }
  }
  AudioInterrupts();
}

void set_chord_voice_frequency(uint8_t i, int16_t current_note) {
  if (midi_in_chord[i].owned) return;   // sounding an incoming note, until the chords start one on it
  chord_voice_current_note[i] = current_note;
  float note_freq = pow(2,chord_octave_change)*c_frequency/8 * temper_ratio(current_note+transpose_steps); //down one octave to let more possibilities with the shuffling array
  if(glide_length>0){
        //ok so first we need to set the "middle note". Keep in mind that the signal will be +/-1 and will go +/- 2 octaves (frequencyModulation(2), hence the /24.0 below)
    //let's do a trick to select a middle note: get the level (relative to the C) and the note and do a modulo 
    int note_level=EDO*chord_octave_change-3*EDO+current_note+transpose_steps;
    int base_octave =chord_octave_change-2+(chord_shuffling_array[chord_shuffling_selection][i])/10;
    int middle_note=base_octave*EDO+transpose_steps; 
    int note_delta=note_level-middle_note;
    // the offset reaches two octaves either way; a voice that inversion, spacing or voice leading has
    // taken further moves the centre instead, so a note within reach glides as it always did
    while (note_delta > 2*EDO) { middle_note += EDO; note_delta -= EDO; }
    while (note_delta < -2*EDO) { middle_note -= EDO; note_delta += EDO; }
    float middle_freq=c_frequency*temper_ratio(middle_note);

    AudioNoInterrupts();
    chord_voice_note_freq[i] = note_freq;
    chord_lfo_rates();
    // hord_vibrato_lfo_array[i]->frequency(chord_vibrato_base_freq);
    // chord_tremolo_lfo_array[i]->frequency(chord_tremolo_base_freq);
    chord_voice_filter_array[i]->frequency(note_freq * chord_filter_keytrack + chord_filter_base_freq);
    chord_osc_1_array[i]->frequency(osc_1_freq_multiplier * middle_freq);
    chord_osc_2_array[i]->frequency(osc_2_freq_multiplier * middle_freq);
    chord_osc_3_array[i]->frequency(osc_3_freq_multiplier * middle_freq);
    // The oscillators sit on the middle note and the DC offset reaches the
    // voice's own note through frequencyModulation(2), two octaves per unit.
    // note_delta/24 is that distance only in equal temperament with twelve
    // steps; anywhere else the offset has to come from the real ratio, or every
    // glide voice lands on an equal-tempered pitch (and in 19 or 31 steps, on
    // an unrelated one, since a step is read as a semitone).
    float glide_offset = (temperament_selection == 0 && purity == 0)
      ? note_delta/24.0
      : log2f(note_freq / middle_freq) / 2.0f;
    uint16_t glide = (fingered_glide && !rythm_mode && !chord_legato) ? 0 : glide_length;   // struck afresh: there at once
    chord_freq_dc_array[i]->amplitude(glide_offset,glide);
    chord_sample_glide(i, note_freq, glide);
    mpe_chord_note_change(i, 12.0f * log2f(note_freq), glide, current_note);
    // chord_voice_filter_array[i]->frequency(1*freq);
    AudioInterrupts();
  }else{
    float note_freq = pow(2,chord_octave_change)*c_frequency/8 * temper_ratio(current_note+transpose_steps); //down one octave to let more possibilities with the shuffling array
    AudioNoInterrupts();
    chord_voice_note_freq[i] = note_freq;
    chord_lfo_rates();
    // hord_vibrato_lfo_array[i]->frequency(chord_vibrato_base_freq);
    // chord_tremolo_lfo_array[i]->frequency(chord_tremolo_base_freq);
    chord_voice_filter_array[i]->frequency(note_freq * chord_filter_keytrack + chord_filter_base_freq);
    chord_osc_1_array[i]->frequency(osc_1_freq_multiplier * note_freq);
    chord_osc_2_array[i]->frequency(osc_2_freq_multiplier * note_freq);
    chord_osc_3_array[i]->frequency(osc_3_freq_multiplier * note_freq);
    chord_freq_dc_array[i]->amplitude(0,0);
    chord_sample_glide(i, note_freq, 0);
    mpe_chord_note_change(i, 12.0f * log2f(note_freq), 0, current_note);
    // chord_voice_filter_array[i]->frequency(1*freq);
    AudioInterrupts();
  }

  // Reached from BOTH the main loop (update_chord_notes) and PIT ISR context
  // (play_single_note, acc_tick), so it must queue rather than send.
  if(chord_started_notes[i]!=0 && chord_started_notes[i]!=chord_midi_note(current_note)){
    //we need to change the note without triggering the change, ie a pitch bend
    queue_midi(false, chord_started_notes[i],chord_release_velocity,mpe_chord_channel(i), chord_port);
    chord_started_notes[i]=0;
    mpe_prepare_chord(i);
    mts_tune(chord_midi_note(current_note), mpe_offset_semitones(current_note), chord_port);
    queue_midi(true, chord_midi_note(current_note),chord_attack_velocity,mpe_chord_channel(i), chord_port);
    chord_started_notes[i]=chord_midi_note(current_note);
  }
  if (chord_envelope_array[i]->isActive() && !chord_voice_released[i]) {   // a chord change under a held chord
    looper_capture(LOOP_RETUNE, 12 + i, chord_voice_note_freq[i], 1, chord_started_notes[i], chord_attack_velocity);
  }
}
// setting the harp
// A light touch plays darker, by up to two octaves of the string's filter at full touch velocity
float string_filter_freq(uint8_t i) {
  float darker = powf(2.0f, -2.0f * string_softness(i));
  return (string_filter_base_freq + string_note_freq[i] * string_filter_keytrack) * darker;
}

// and quieter, by up to 24 dB
// A string whose envelopes are done rests: its oscillator and transient oscillator go to amplitude 0
// (keeping their phase, sending nothing), so the chain after them goes idle; they ran on every
// block for the envelopes to mute. A pluck wakes it (pluck_string, owned_harp_on), through here.
bool string_resting[12] = {false, false, false, false, false, false, false, false, false, false, false, false};
void apply_string_firmness(uint8_t i) {
  float gain = powf(10.0f, -1.2f * string_softness(i));
  AudioNoInterrupts();
  string_waveform_array[i]->amplitude(harp_voice_source || string_resting[i] ? 0 : string_level * gain);   // silent resting, or with a sample in its place (set_harp_source)
  string_pluck_array[i]->amplitude(string_level * gain);
  harp_sample_array[i]->amplitude(fminf(1, string_level * gain * harp_sample_level));
  string_transient_waveform_array[i]->amplitude(string_resting[i] ? 0 : transient_level * gain);
  string_filter_array[i]->frequency(string_filter_freq(i));
  AudioInterrupts();
}

void set_harp_voice_frequency(uint8_t i, uint16_t current_note) {
  if (midi_in_harp[i].owned) return;    // sounding an incoming note, until the harp plucks it
  harp_voice_current_note[i] = current_note;
  float note_freq =  pow(2,harp_octave_change)*c_frequency/4 * temper_ratio(current_note+transpose_steps);
  if (harp_voice_glide[i] != 0) note_freq *= powf(2.0f, harp_voice_glide[i] / 12.0f);   // a ribbon touch's slide
  float transient_freq =  64.0*c_frequency/4 *temper_ratio((current_note+transpose_steps)%EDO+transient_note_level);
  string_note_freq[i] = note_freq;
  AudioNoInterrupts();
  string_waveform_array[i]->frequency(note_freq);
  string_pluck_array[i]->frequency(note_freq);
  if (harp_voice_source && harp_sample_array[i]->isPlaying()) harp_sample_array[i]->setFrequency(note_freq);
  string_transient_waveform_array[i]->frequency(transient_freq);
  string_filter_array[i]->frequency(string_filter_freq(i));
  // string_vibrato_1.offset(0);
  AudioInterrupts();
}
/* Re-apply every sounding voice's own note after c_frequency changes, so master
 * tuning is audible on a held chord while the control moves. Each voice is
 * retuned to the note it last played, so the note number does not move and no
 * MIDI is sent. Interrupts are held per voice because the rhythm and delayed
 * chord timers can retune a voice from ISR context; without the lock, a timer
 * could move the voice to a new note between reading the stored note and
 * re-applying it, and this would put the old note back. */
void retune_active_voices() {
  for (uint8_t i = 0; i < 4; i++) {
    noInterrupts();
    if (chord_envelope_array[i]->isActive()) {
      set_chord_voice_frequency(i, chord_voice_current_note[i]);
    }
    interrupts();
  }
  for (uint8_t i = 0; i < 12; i++) {
    noInterrupts();
    if (string_enveloppe_array[i]->isActive()) {
      set_harp_voice_frequency(i, harp_voice_current_note[i]);
    }
    interrupts();
  }
  midi_in_retune();
}
//>>MIDI IN<<
// With MIDI in plays on, notes sent to the minichord play its voices, as a sound module: on the
// harp's port and channel the harp strings (twelve voices), on the chords' port and channel the
// chord voices (four), the same ones it sends its own notes on (by default the chords on port 1 and
// the harp on port 2, both on channel 1). A note sounds at its true pitch, middle C as middle C
// whatever the preset's octaves and transpose, in the minichord's master tuning and temperament; in
// 19, 24 and 31 steps on the nearest step. Velocity sets its loudness, and a string's brightness as
// touch velocity does; the sustain pedal (64) holds notes let go of. Nothing goes back out.
//
// An incoming note takes a voice nothing is playing, else one ringing out, else the oldest
// incoming one; a harp string under a finger is never taken. The voice is then the note's: the
// minichord's own playing leaves it alone (set_chord_voice_frequency, set_harp_voice_frequency,
// stop_chord_notes) until it starts a note on it itself, which takes it back (pluck_string,
// chords_take_voice).

// the frequency an incoming note sounds at
float midi_in_frequency(uint8_t note) {
  float semitones = (float)note - midi_base_note;   // from C3
  if (edo_index == 0) return c_frequency * temper_ratio(semitones);
  return c_frequency * powf(2.0f, lroundf(semitones * EDO / 12.0f) / (float)EDO);
}

void apply_chord_voice_level(uint8_t i) {
  float g = chord_voice_level[i];
  chord_voice_mixer_array[i]->gain(0, g);
  chord_voice_mixer_array[i]->gain(1, g);
  chord_voice_mixer_array[i]->gain(2, g);
  chord_voice_mixer_array[i]->gain(3, chord_noise_level * g);
  chord_sample_array[i]->amplitude(fminf(1, g * chord_sample_level));
}

//>>SAMPLES<<
// A sampled instrument's voices silence their oscillators (and the chords' noise) while it is
// selected, rather than leaving them running into a mixer input at 0: an oscillator at amplitude
// 0 keeps its phase but makes no waveform and sends nothing.
//
// A section's voices can sound a sampled instrument in place of their oscillators (harp voice,
// chord voice): piano, pizzicato strings, choir, a string quartet, and after the song (5) a
// vibraphone, a fingered bass, a sitar or an electric piano (lib/minichord_samples, made by
// generator/samples.py); 10 is the reed piano, a model rather than samples (lib/reed_piano), played
// by the same player, struck as hard as the harp is plucked; then a kalimba and a marimba (11, 12), and
// the tonewheel organ (13), a model too (lib/tonewheel_organ), its drawbars and percussion 388-389; an
// upright bass and an accordion (14, 15). The sample runs through the voice's own envelope, filter and level, so
// the preset shapes it as it does the synth, and the voice's pitch (the ribbon, a retune, a chord
// change) moves it. Its own envelope only passes it on; once the voice's envelope is done it stops,
// so a voice not sounding costs nothing.
// The song: a second of whatever plays into the minichord, grabbed from the phone deck (phone_grab)
// and played as an instrument, voice source 5. Kept at the deck's rate, a third of the audio rate, one
// shot, and pitched so a string at middle C plays it as it was. The struct never changes: only the
// sound in its buffer does.
#define SONG_SAMPLE_LENGTH 14706   // a second at 14706 Hz
DMAMEM int16_t phone_ring[SONG_SAMPLE_LENGTH] __attribute__((aligned(4)));
DMAMEM int16_t song_pcm[SONG_SAMPLE_LENGTH] __attribute__((aligned(4)));
static const AudioSynthWavetableMulaw::sample_data song_samples[1] = {{
  song_pcm, nullptr, false, 14, (float)(1 << 18) * (1.0f / AudioPhoneDeck::DECIMATE) / 261.6256f,
  (uint32_t)(SONG_SAMPLE_LENGTH - 1) << 18, 0, 0, 65535,
  0, 1, 0, 1, 110, 0,                       // its envelope passes it on, as the instruments' do
  0, 0, 0.0f, 0.0f, 0, 0, 0.0f, 0.0f, 0, 0}};
static const uint8_t song_ranges[] = {127};
const AudioSynthWavetableMulaw::instrument_data song_instrument = {1, song_ranges, song_samples};
bool song_grabbed = false;

static const AudioSynthWavetableMulaw::instrument_data *sampled_instrument(uint8_t source) {
  switch (source) {
    case 1: return &piano_instrument;
    case 2: return &pizzicato_instrument;
    case 3: return &choir_instrument;
    case 4: return &strings_instrument;
    case 5: return &song_instrument;
    case 6: return &vibraphone_instrument;
    case 7: return &bass_instrument;
    case 8: return &sitar_instrument;
    case 9: return &epiano_instrument;
    case 10: return &AudioSynthWavetableMulaw::reed_instrument;   // no samples: the reed piano's model (lib/reed_piano)
    case 11: return &kalimba_instrument;
    case 12: return &marimba_instrument;
    case 13: return &AudioSynthWavetableMulaw::organ_instrument;   // no samples: the tonewheel organ's model (lib/tonewheel_organ)
    case 14: return &upright_instrument;
    case 15: return &accordion_instrument;
  }
  return nullptr;
}

FLASHMEM void apply_string_blend() {
  for (uint8_t s = 0; s < 12; s++) string_pluck_array[s]->blend(harp_voice_source ? 0 : string_blend);
}

FLASHMEM void set_harp_source(uint8_t source) {
  const AudioSynthWavetableMulaw::instrument_data *instrument = sampled_instrument(source);
  harp_voice_source = instrument ? source : 0;
  for (uint8_t i = 0; i < 12; i++) {
    harp_sample_array[i]->stop();
    if (instrument) harp_sample_array[i]->setInstrument(*instrument);
    harp_source_mix_array[i]->gain(0, instrument ? 0 : 1);
    harp_source_mix_array[i]->gain(1, instrument ? 1 : 0);
    apply_string_firmness(i);   // the oscillator silent while a sample sounds in its place
  }
  apply_string_blend();   // and the string model, which would fade the sample out (it passes its input at 1 - blend)
}

// Each chord voice has its own sample player, so each note can sound its own instrument: the chord
// voice (265) for all four, or a note's own (270-273, counted from the first note as the note levels
// 131-134 are) in its place, say pizzicato in the bass under a choir. Nothing more runs for it: a
// voice plays one source either way.
FLASHMEM static void apply_chord_source(uint8_t i) {
  uint8_t source = chord_note_source[i] ? chord_note_source[i] - 1 : chord_voice_source;
  const AudioSynthWavetableMulaw::instrument_data *instrument = sampled_instrument(source);
  chord_source_of[i] = instrument ? source : 0;
  chord_sample_array[i]->stop();
  if (instrument) chord_sample_array[i]->setInstrument(*instrument);
  chord_source_mix_array[i]->gain(0, instrument ? 0 : 1);
  chord_source_mix_array[i]->gain(1, instrument ? 1 : 0);
  chord_voice_sources(i);   // the oscillators and noise silent while a sample sounds in their place
}

FLASHMEM void set_chord_source(uint8_t source) {
  chord_voice_source = sampled_instrument(source) ? source : 0;
  for (uint8_t i = 0; i < 4; i++) apply_chord_source(i);
}

// value as stored: 0 the chord voice, 1 the synth, 2 piano, 3 pizzicato, 4 choir, 5 string quartet,
// 6 the song, 7 vibraphone, 8 bass, 9 sitar, 10 electric piano, 11 reed piano, 12 kalimba, 13 marimba,
// 14 organ, 15 upright bass, 16 accordion (1 + the voice source)
FLASHMEM void set_chord_note_source(uint8_t i, uint8_t value) {
  if (i > 3) return;
  chord_note_source[i] = value <= 16 ? value : 0;
  apply_chord_source(i);
}

// Starts harp voice i's sample at the frequency it sounds; its level follows (apply_string_firmness).
// The reed piano is struck as hard as the string was plucked, times the harp's bark (386), which sets
// its tone: a sample has one strength, and the level does the rest. The strength is the note's amplitude
// (apply_string_firmness's gain, -24 dB at the softest), as a reed's swing is: so a soft pluck stays on the
// pickup's flank and only the hard ones bark.
void harp_sample_start(uint8_t i) {
  if (!harp_voice_source) return;
  int strength = harp_voice_source == 10 ? (int)lroundf(127 * harp_reed_bark * powf(10.0f, -1.2f * string_softness(i))) : 127;
  harp_sample_array[i]->playFrequency(string_note_freq[i], strength);
}

// Glide on a sampled chord voice. The oscillators glide by their DC offset's ramp
// (set_chord_voice_frequency); a sample has no input for it, so its pitch is moved from the loop
// instead (chord_samples_glide), along the same straight line in octaves over the same glide time,
// from wherever the last note left it, as the oscillators do: a barbershop swipe on the choir.
struct chord_sample_glide_t { float from, to; uint32_t at; uint16_t ms; bool known; };   // in octaves (log2 Hz)
chord_sample_glide_t chord_sample_glides[4] = {};

static float chord_sample_octaves(uint8_t i) {
  const chord_sample_glide_t &g = chord_sample_glides[i];
  uint32_t gone = millis() - g.at;
  if (g.ms == 0 || gone >= g.ms) return g.to;
  return g.from + (g.to - g.from) * gone / g.ms;
}

// Voice i heads for hz over ms (0: there at once); from the loop or a chord timer's interrupt
void chord_sample_glide(uint8_t i, float hz, uint16_t ms) {
  if (i > 3 || hz <= 0) return;
  chord_sample_glide_t &g = chord_sample_glides[i];
  float to = log2f(hz);
  g.from = g.known && ms ? chord_sample_octaves(i) : to;
  g.to = to;
  g.at = millis();
  g.ms = g.known ? ms : 0;
  g.known = true;
  if (chord_source_of[i] && chord_sample_array[i]->isPlaying()) chord_sample_array[i]->setFrequency(exp2f(g.ms ? g.from : to));
}

// From loop(): a gliding sample on its way
FLASHMEM void chord_samples_glide() {
  for (uint8_t i = 0; i < 4; i++) {
    noInterrupts();
    chord_sample_glide_t &g = chord_sample_glides[i];
    if (g.ms) {
      float octaves = chord_sample_octaves(i);
      if (millis() - g.at >= g.ms) g.ms = 0;
      if (chord_source_of[i] && chord_sample_array[i]->isPlaying()) chord_sample_array[i]->setFrequency(exp2f(octaves));
    }
    interrupts();
  }
}

void chord_sample_start(uint8_t i) {
  if (!chord_source_of[i]) return;
  float hz = chord_sample_glides[i].known ? exp2f(chord_sample_octaves(i)) : chord_voice_note_freq[i];   // mid-glide: from where it is
  chord_sample_array[i]->playFrequency(hz, chord_source_of[i] == 10 ? (int)lroundf(127 * chord_reed_bark) : 127);   // the reed piano struck as hard as 387 says
  chord_sample_array[i]->amplitude(fminf(1, chord_voice_level[i] * chord_sample_level));
}

// Called from loop(): a sample whose voice's envelope is done stops
void samples_update() {
  if (harp_voice_source) {
    for (uint8_t i = 0; i < 12; i++) if (harp_sample_array[i]->isPlaying() && !string_enveloppe_array[i]->isActive()) harp_sample_array[i]->stop();
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (chord_source_of[i] && chord_sample_array[i]->isPlaying() && !chord_envelope_array[i]->isActive()) chord_sample_array[i]->stop();
  }
}

void midi_in_tune_harp(uint8_t i) {
  float f = midi_in_harp[i].freq;
  int16_t step = (int16_t)lroundf(log2f(f / c_frequency) * EDO);   // the transient follows the step it's nearest
  float transient_freq = 64.0 * c_frequency / 4 * temper_ratio((step % EDO + EDO) % EDO + transient_note_level);
  string_note_freq[i] = f;
  AudioNoInterrupts();
  string_waveform_array[i]->frequency(f);
  string_pluck_array[i]->frequency(f);
  if (harp_voice_source && harp_sample_array[i]->isPlaying()) harp_sample_array[i]->setFrequency(f);
  string_transient_waveform_array[i]->frequency(transient_freq);
  string_filter_array[i]->frequency(string_filter_freq(i));
  AudioInterrupts();
}

void midi_in_tune_chord(uint8_t i) {
  float f = midi_in_chord[i].freq;
  AudioNoInterrupts();
  chord_voice_note_freq[i] = f;
  chord_voice_filter_array[i]->frequency(f * chord_filter_keytrack + chord_filter_base_freq);
  chord_osc_1_array[i]->frequency(osc_1_freq_multiplier * f);
  chord_osc_2_array[i]->frequency(osc_2_freq_multiplier * f);
  chord_osc_3_array[i]->frequency(osc_3_freq_multiplier * f);
  chord_freq_dc_array[i]->amplitude(0, 0);
  chord_sample_glide(i, f, 0);
  AudioInterrupts();
}

// A tuning change reaches the incoming notes too; a looped note keeps the pitch it was played at
void midi_in_retune() {
  for (uint8_t i = 0; i < 12; i++) if (midi_in_harp[i].owned) {
    if (!midi_in_harp[i].looped) midi_in_harp[i].freq = midi_in_frequency(midi_in_harp[i].note);
    midi_in_tune_harp(i);
  }
  for (uint8_t i = 0; i < 4; i++) if (midi_in_chord[i].owned) {
    if (!midi_in_chord[i].looped) midi_in_chord[i].freq = midi_in_frequency(midi_in_chord[i].note);
    midi_in_tune_chord(i);
  }
}

// The voice for a note among count: the one already sounding it, else the best free one; -1 if all
// are under fingers.
int8_t midi_in_pick(midi_in_voice *voices, uint8_t count, uint8_t note, bool harp) {
  for (uint8_t i = 0; i < count; i++) if (voices[i].owned && voices[i].note == note) return i;
  int8_t best = -1;
  int8_t best_rank = -1;
  uint32_t best_since = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (harp && string_held(i)) continue;
    bool active = harp ? string_enveloppe_array[i]->isActive() : chord_envelope_array[i]->isActive();
    int8_t rank = !active ? 3 : (voices[i].owned && !voices[i].key_down && !voices[i].sustained) ? 2 : !voices[i].owned ? 1 : 0;
    uint32_t since = voices[i].owned ? voices[i].since : 0;
    if (rank > best_rank || (rank == best_rank && since < best_since)) { best = i; best_rank = rank; best_since = since; }
  }
  return best;
}

// Sounds harp voice i, which voice record midi_in_harp[i] now owns, at its frequency and firmness
void owned_harp_on(uint8_t i, float firmness) {
  harp_voice_glide[i] = 0;
  string_plucked[i] = false;
  string_pluck_firmness[i] = string_firmness[i] = firmness;
  midi_in_tune_harp(i);
  harp_sample_start(i);
  string_resting[i] = false;
  apply_string_firmness(i);
  apply_string_spread(i);
  AudioNoInterrupts();
  string_pluck_array[i]->pluck(pluck_brightness(i));
  envelope_string_vibrato_lfo.noteOn();
  envelope_string_vibrato_dc.noteOn();
  string_enveloppe_filter_array[i]->noteOn();
  string_enveloppe_array[i]->noteOn();
  string_transient_envelope_array[i]->noteOn();
  AudioInterrupts();
}

void midi_in_harp_on(uint8_t note, uint8_t velocity) {
  int8_t i = midi_in_pick(midi_in_harp, 12, note, true);
  if (i < 0) return;
  harp_midi_off(i);   // the harp's own midi note on it, if it had one, ends with its sound
  looper_voice_taken(true, i);
  midi_in_harp[i] = midi_in_voice{true, true, false, note, millis(), false, midi_in_frequency(note)};
  owned_harp_on(i, velocity / 127.0f);
}

// Takes chord voice i from the chords for a note of their own: a chord note waiting to start on it
// is called off, and the chords' midi note on it ends with its sound
void chords_give_voice(uint8_t i) {
  note_timer[i].end();
  if (chord_started_notes[i] != 0) {
    queue_midi(false, chord_started_notes[i], chord_release_velocity, mpe_chord_channel(i), chord_port);
    chord_started_notes[i] = 0;
  }
}

// Sounds chord voice i, which voice record midi_in_chord[i] now owns, at its frequency and firmness
void owned_chord_on(uint8_t i, float firmness) {
  chord_voice_level[i] = powf(10.0f, -1.2f * (1 - firmness) * (1 - firmness));   // down to 24 dB, as a string
  apply_chord_voice_level(i);
  midi_in_tune_chord(i);
  chord_sample_start(i);
  noInterrupts();
  wake_chord_voice(i);
  chord_vibrato_envelope_array[i]->noteOn();
  chord_vibrato_dc_envelope_array[i]->noteOn();
  chord_envelope_array[i]->noteOn();
  chord_envelope_filter_array[i]->noteOn();
  chord_voice_released[i] = false;
  interrupts();
}

void midi_in_chord_on(uint8_t note, uint8_t velocity) {
  int8_t i = midi_in_pick(midi_in_chord, 4, note, false);
  if (i < 0) return;
  chords_give_voice(i);
  looper_voice_taken(false, i);
  midi_in_chord[i] = midi_in_voice{true, true, false, note, millis(), false, midi_in_frequency(note)};
  owned_chord_on(i, velocity / 127.0f);
}

void midi_in_release(midi_in_voice *voices, uint8_t i, bool harp) {
  voices[i].key_down = false;
  voices[i].sustained = false;
  noInterrupts();
  if (harp) {
    string_enveloppe_array[i]->noteOff();
    string_transient_envelope_array[i]->noteOff();
    string_enveloppe_filter_array[i]->noteOff();
  } else {
    chord_vibrato_envelope_array[i]->noteOff();
    chord_vibrato_dc_envelope_array[i]->noteOff();
    chord_envelope_array[i]->noteOff();
    chord_envelope_filter_array[i]->noteOff();
    chord_voice_released[i] = true;
  }
  interrupts();
}

void midi_in_message(uint8_t type, uint8_t cable, uint8_t channel, uint8_t data1, uint8_t data2) {
  static bool pedal_harp = false, pedal_chord = false;
  bool harp = cable == harp_port && channel == harp_channel;
  bool chord = !harp && cable == chord_port && channel == chord_channel;
  if (!harp && !chord) return;
  midi_in_voice *voices = harp ? midi_in_harp : midi_in_chord;
  uint8_t count = harp ? 12 : 4;
  bool &pedal = harp ? pedal_harp : pedal_chord;
  if (type == usbMIDI.NoteOn && data2 > 0) {
    if (harp) midi_in_harp_on(data1, data2);
    else midi_in_chord_on(data1, data2);
  } else if (type == usbMIDI.NoteOn || type == usbMIDI.NoteOff) {
    for (uint8_t i = 0; i < count; i++) {
      if (!voices[i].owned || voices[i].looped || !voices[i].key_down || voices[i].note != data1) continue;
      if (pedal) { voices[i].key_down = false; voices[i].sustained = true; }
      else midi_in_release(voices, i, harp);
    }
  } else if (type == usbMIDI.ControlChange && data1 == 64) {
    pedal = data2 >= 64;
    if (!pedal) for (uint8_t i = 0; i < count; i++) if (voices[i].owned && !voices[i].looped && voices[i].sustained) midi_in_release(voices, i, harp);
  }
}

// The harp's vibrato and bend envelopes are the twelve strings' together, started by each pluck,
// and nothing ended them: held at sustain after the first pluck, they kept every harp oscillator
// taking frequency modulation, which costs it twice the work (twelve times 0.14% of the processor,
// idle). Once no string is sounding they are let go, and the next pluck starts them afresh.
void harp_vibrato_settle() {
  if (!envelope_string_vibrato_lfo.isSustain() && !envelope_string_vibrato_dc.isSustain()) return;
  for (uint8_t i = 0; i < 12; i++) if (string_enveloppe_array[i]->isActive()) return;
  envelope_string_vibrato_lfo.noteOff();
  envelope_string_vibrato_dc.noteOff();
}

// A string's model rings on for its string decay (3 s, up to 20) after the string's envelope has
// finished and gone silent, and it was all work for nothing then. Once the envelope is done, so is
// the string; the next pluck fills it afresh. (A pluck and its envelope's note on come together,
// under AudioNoInterrupts, so this never falls between them.)
void harp_strings_settle() {
  for (uint8_t i = 0; i < 12; i++) {
    if (string_pluck_array[i]->is_ringing() && !string_enveloppe_array[i]->isActive()) string_pluck_array[i]->stop();
    if (!string_resting[i] && !string_enveloppe_array[i]->isActive() && !string_transient_envelope_array[i]->isActive()) {
      string_resting[i] = true;   // its oscillators silent till the next pluck (apply_string_firmness)
      string_waveform_array[i]->amplitude(0);
      string_transient_waveform_array[i]->amplitude(0);
    }
  }
}

// Switched off, whatever incoming notes are sounding ring out
void midi_in_set(bool on) {
  if (!on) {
    for (uint8_t i = 0; i < 12; i++) if (midi_in_harp[i].owned && !midi_in_harp[i].looped && (midi_in_harp[i].key_down || midi_in_harp[i].sustained)) midi_in_release(midi_in_harp, i, true);
    for (uint8_t i = 0; i < 4; i++) if (midi_in_chord[i].owned && !midi_in_chord[i].looped && (midi_in_chord[i].key_down || midi_in_chord[i].sustained)) midi_in_release(midi_in_chord, i, false);
  }
  midi_in_plays = on;
}

// A note off that lands in an envelope's retrigger ramp (a note on to a voice still sounding starts
// one) is ignored by the Teensy envelope, and the voice would then hold at its sustain for good: a
// note repeated quickly, or sent on and off at once, stuck that way. So a voice that has been let go
// of and has settled at sustain is released again, as the chords' and the harp's own are.
void midi_in_settle() {
  for (uint8_t i = 0; i < 12; i++) {
    if (midi_in_harp[i].owned && !midi_in_harp[i].key_down && !midi_in_harp[i].sustained && string_enveloppe_array[i]->isSustain()) midi_in_release(midi_in_harp, i, true);
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (midi_in_chord[i].owned && !midi_in_chord[i].key_down && !midi_in_chord[i].sustained && chord_envelope_array[i]->isSustain()) midi_in_release(midi_in_chord, i, false);
  }
}

//>>LOOPER<<
// Records what the minichord plays, chords and harp, as it plays it, and loops it. What it keeps
// is the notes, not the sound: each start, let-go and chord retune, with the voice, its frequency,
// how firmly it was played and the midi note that went out (looper_capture, from pluck_string,
// release_string, damp_string, the chords' note-ons and note-offs and set_chord_voice_frequency).
// It plays them back through the voices MIDI in uses (midi_in_harp, midi_in_chord, marked looped),
// so the minichord's own playing takes a voice back as it does from an incoming note, and the
// looped notes go out over midi as they did when played. Harp drums' hits are kept too (LOOP_DRUM,
// from drum_pad: the kit's voice and how hard), struck again on the kit as they come round, so a
// beat played on the pads loops; quantize puts each on the grid.
//
// It is driven through setting 256, which is never saved as anything but 0: writing it does one
// thing (looper_action: 1 record, 2 play, 3 stop, 4 clear, 5 overdub on or off, 6 the next step,
// 7 the next step sent to a computer's looper instead), so minicontrol's looper buttons write it,
// and control changes 85-90 do the same on any channel.
// Assigned to the double tap, each double tap takes the next step (looper_step): record, play,
// stop, then record a new loop. A loop closes where the second tap landed, not where the gesture
// was recognised, a moment later. In rhythm mode, or with midi clock coming in, a loop starts on
// the nearest beat and its length rounds to whole beats; otherwise it is as long as it was played.
// A loop plays on through a preset change, in the new preset's sounds, and one the double tap
// started keeps the double tap while it counts in, records or plays: in a preset without the
// looper on its double tap, the double tap takes the looper's next step and does nothing else, so
// the loop can always be stopped where it is. Once it stops, the double tap is the preset's again.
// A loop last worked from minicontrol or midi leaves the double tap to the preset.
// Overdub (minicontrol or midi only) adds to a playing loop, and a note captured on a pass isn't
// played back over itself on that pass.
//
// In time (283-286). The looper keeps a beat whenever rhythm mode runs, or any of these is on: the
// rhythm's, or without it its own at the rhythm's tempo (default bpm, or control change 103's taps),
// counted from the loop's start. Then a loop starts on a beat and its length rounds to whole beats.
// Looper count-in plays one or two bars first: the recording starts on the bar line after them
// (in rhythm mode the first bar line at least that far off), and a note played up to 100 ms before
// it, reaching for the downbeat, starts the loop. Looper click ticks the beats, higher on a bar's
// first, on the speaker and headphones only (after the USB recording tap): through the count-in,
// and the recording, and the playing, as it says. Looper length (288) sets the phrase in bars: the
// recording closes by itself that many bars from its start and plays on without a gap. A step
// before then closes it on the nearest bar line, recording on till it if it is still to come
// (stop closes it at once). Looper quantize moves each note's start to the
// nearest quarter, eighth (where the shuffle puts the off one), sixteenth or eighth triplet of the
// loop, and its let-go and retunes with it, so it keeps its length: the first pass's notes as the
// loop closes, when its length is known (one reaching for the downbeat at the end starts the
// loop), an overdub's as they come. A pass plays as it was played; the next one plays it in time. Voices still sounding when a loop closes are let go at its
// end, so none hangs across the join, and a chord ringing when recording starts begins the loop.
//
// A looper on the computer (the Lab's looper page) is worked the same way from here: double tap
// value 7 (with double tap control 256) sends each double tap out as control change 90, the next
// step, on the chords' port and channel, and leaves the minichord's own looper alone. The double
// tap is only known once the second tap lifts, so the value carries how long ago it landed, 4 ms a
// step down from 127 (127: just now; never under 64, so it still reads as a press anywhere a press is
// 64 or more): the computer places the step where the tap was, not where it was recognised.
const int16_t looper_adress = 256;
struct loop_event {
  uint32_t t;            // ms from the loop's start
  uint16_t cycle;        // the pass it was captured on
  uint8_t kind;          // LOOP_START, LOOP_RELEASE, LOOP_RETUNE or LOOP_DRUM
  uint8_t voice;         // as recorded: 0-11 a harp string, 12-15 a chord voice; a LOOP_DRUM's the kit's voice
  uint8_t midi_note;
  uint8_t midi_velocity; // a LOOP_DRUM's: how hard it was struck, 1-127
  float freq;
  float firmness;        // as heard: (1 - firmness) squared is how much softer than full it played
};
const uint16_t loop_event_max = 2048;
loop_event loop_events[loop_event_max];          // 32 kB
volatile uint16_t loop_event_count = 0;
volatile uint32_t loop_start_ms = 0;    // when pass 0 began: the recording
volatile uint32_t loop_length_ms = 0;
uint16_t loop_cycle = 0;                // the pass being played
int32_t loop_played_to = -1;            // played up to here in it, in the loop's ms (-1: from its start)
volatile bool loop_open[16];            // recorded voices started and not yet let go
int32_t loop_quantize_shift[16];        // an overdub's notes: how far each recorded voice's latest start moved
int8_t loop_voice[16];                  // recorded voice -> the voice playing it back, -1 none
uint8_t loop_sent_note[16];             // per playback voice (0-11 harp, 12-15 chord): the midi note sent, 0 none
uint8_t loop_sent_channel[16];

// Puts an event in its place, in time order; from the main loop or a chord timer's interrupt
static void loop_insert(const loop_event &e) {
  uint32_t primask;
  __asm__ volatile("mrs %0, primask" : "=r"(primask));
  __disable_irq();
  if (loop_event_count < loop_event_max) {
    uint16_t j = loop_event_count;
    while (j > 0 && loop_events[j - 1].t > e.t) { loop_events[j] = loop_events[j - 1]; j--; }
    loop_events[j] = e;
    loop_event_count = loop_event_count + 1;
  }
  if (!primask) __enable_irq();
}

static bool looper_on_beat() {
  return rythm_mode && rythm_timer_running && rythm_beat_ms != 0;
}
// whether the looper keeps a beat: the rhythm's, or its own from the loop's start
static bool looper_timed() {
  return looper_on_beat() || looper_quantize || looper_count_in || looper_click || looper_length || loop_phrase_bars;
}
static float looper_beat_ms() { return 60000.0f / rythm_bpm; }
static float looper_bar_ms() { return looper_beat_ms() * acc_beats_per_bar(); }

// the nearest point of looper quantize's grid to t, ms from the loop's start (on a beat)
static int32_t looper_grid_snap(int32_t t) {
  float beat = looper_beat_ms();
  if (looper_quantize == 2) {   // eighths: the off one where the shuffle puts it
    float b = floorf(t / beat) * beat;
    float options[3] = {b, b + beat / 2 * shuffle, b + beat};
    float best = options[0];
    for (uint8_t i = 1; i < 3; i++) if (fabsf(t - options[i]) < fabsf(t - best)) best = options[i];
    return lroundf(best);
  }
  float unit = looper_quantize == 1 ? beat : looper_quantize == 3 ? beat / 4 : beat / 3;
  return lroundf(lroundf(t / unit) * unit);
}

void looper_capture(uint8_t kind, uint8_t voice, float freq, float firmness, uint8_t midi_note, uint8_t midi_velocity) {
  uint8_t state = looper_state;
  // the last 100 ms of a count-in: a note reaching for the downbeat starts the loop
  if (state == LOOPER_COUNTIN && (int32_t)(millis() - loop_start_ms) > -100) state = LOOPER_RECORDING;
  if (state != LOOPER_RECORDING && !(state == LOOPER_PLAYING && looper_overdub)) return;
  if (kind == LOOP_DRUM) {}                           // a hit: nothing held, nothing to let go
  else if (kind == LOOP_START) loop_open[voice] = true;
  else if (!loop_open[voice]) return;                  // let go of or retuned, but started before the recording
  else if (kind == LOOP_RELEASE) loop_open[voice] = false;
  uint32_t elapsed = millis() - loop_start_ms;
  if ((int32_t)elapsed < 0) elapsed = 0;               // a recording starting on a beat still to come
  loop_event e;
  if (state == LOOPER_RECORDING) { e.t = elapsed; e.cycle = 0; }
  else {
    e.t = elapsed % loop_length_ms; e.cycle = elapsed / loop_length_ms;
    if (looper_quantize && looper_timed()) {   // an overdub's note in time as it comes; its let-go with it
      int32_t t = e.t;
      if (kind == LOOP_DRUM) {
        t = looper_grid_snap(t);                       // a hit on its own: to the grid, no length to keep
      } else {
        if (kind == LOOP_START) {
          int32_t snapped = looper_grid_snap(t);
          if (snapped >= (int32_t)loop_length_ms) snapped -= loop_length_ms;   // the next pass's downbeat
          loop_quantize_shift[voice] = snapped - t;
        }
        t += loop_quantize_shift[voice];
      }
      while (t < 0) t += loop_length_ms;
      while (t >= (int32_t)loop_length_ms) t -= loop_length_ms;
      e.t = t;
    }
  }
  e.kind = kind; e.voice = voice; e.midi_note = midi_note; e.midi_velocity = midi_velocity; e.freq = freq; e.firmness = firmness;
  loop_insert(e);
}

// The first pass's notes in time, as the loop closes at length: each start to the nearest point of
// the grid (one reaching for the end goes to the downbeat), its let-go and retunes with it
FLASHMEM static void loop_quantize_pass(int32_t length) {
  int32_t shift[16] = {0};
  noInterrupts();
  for (uint16_t i = 0; i < loop_event_count; i++) {
    loop_event &e = loop_events[i];
    int32_t t = e.t;
    if (e.kind == LOOP_DRUM) {                         // a hit: to the grid, the drum's voice no string's
      t = looper_grid_snap(t);
      if (t >= length) t -= length;
      e.t = t;
      continue;
    }
    if (e.kind == LOOP_START) {
      int32_t snapped = looper_grid_snap(t);
      if (snapped >= length) snapped -= length;
      shift[e.voice] = snapped - t;
    }
    t += shift[e.voice];
    if (t < 0) t = 0;
    if (t >= length) t = length - 1;
    e.t = t;
  }
  for (uint16_t i = 1; i < loop_event_count; i++) {   // back in time order: nearly so already
    loop_event e = loop_events[i];
    uint16_t j = i;
    while (j > 0 && loop_events[j - 1].t > e.t) { loop_events[j] = loop_events[j - 1]; j--; }
    loop_events[j] = e;
  }
  interrupts();
}

// The nearest beat to ms, in rhythm mode; ms itself otherwise
static uint32_t looper_snap(uint32_t ms) {
  if (!looper_on_beat()) return ms;
  float beat = looper_beat_ms();
  int32_t from_beat = (int32_t)(ms - rythm_beat_ms);
  return rythm_beat_ms + (int32_t)(lroundf(from_beat / beat) * beat);
}

static void loop_midi_off(uint8_t slot, uint8_t port) {
  if (loop_sent_note[slot] == 0) return;
  queue_midi(false, loop_sent_note[slot], slot < 12 ? harp_release_velocity : chord_release_velocity, loop_sent_channel[slot], port);
  loop_sent_note[slot] = 0;
}

// Lets go of playback voice v (a harp string, or chord voice v)
// The minichord's own playing, or an incoming note, takes voice v: the loop's midi note on it ends
void looper_voice_taken(bool harp, uint8_t v) {
  midi_in_voice &voice = harp ? midi_in_harp[v] : midi_in_chord[v];
  if (!voice.owned || !voice.looped) return;
  loop_midi_off(harp ? v : 12 + v, harp ? harp_port : chord_port);
  for (uint8_t r = 0; r < 16; r++) if (loop_voice[r] == v && (r < 12) == harp) loop_voice[r] = -1;
}

static void loop_voice_release(bool harp, uint8_t v) {
  midi_in_release(harp ? midi_in_harp : midi_in_chord, v, harp);
  loop_midi_off(harp ? v : 12 + v, harp ? harp_port : chord_port);
}

static void loop_play_event(const loop_event &e) {
  if (e.kind == LOOP_DRUM) {                           // the kit, as hard as it was struck, where it was (no humanize again)
    drum_strike(e.voice, e.midi_velocity, false, false);
    return;
  }
  bool harp = e.voice < 12;
  midi_in_voice *voices = harp ? midi_in_harp : midi_in_chord;
  int8_t v = loop_voice[e.voice];
  bool ours = v >= 0 && voices[v].owned && voices[v].looped;   // the live playing may have taken it back
  uint8_t slot = harp ? v : 12 + v;
  uint8_t port = harp ? harp_port : chord_port;
  if (e.kind == LOOP_START) {
    if (ours) loop_voice_release(harp, v);
    v = midi_in_pick(voices, harp ? 12 : 4, e.midi_note, harp);
    loop_voice[e.voice] = v;
    if (v < 0) return;                                 // every string under a finger
    for (uint8_t r = 0; r < 16; r++) {                 // a recorded voice that had it lets it go to this one
      if (r != e.voice && loop_voice[r] == v && (r < 12) == harp) loop_voice[r] = -1;
    }
    slot = harp ? v : 12 + v;
    if (harp) harp_midi_off(v);
    else chords_give_voice(v);
    loop_midi_off(slot, port);
    voices[v] = midi_in_voice{true, true, false, e.midi_note, millis(), true, e.freq};
    if (harp) owned_harp_on(v, e.firmness);
    else owned_chord_on(v, e.firmness);
    if (e.midi_note) {
      loop_sent_channel[slot] = harp ? mpe_harp_channel(v) : mpe_chord_channel(v);
      queue_midi(true, e.midi_note, e.midi_velocity, loop_sent_channel[slot], port);
      loop_sent_note[slot] = e.midi_note;
    }
  } else if (!ours) {
    loop_voice[e.voice] = -1;
  } else if (e.kind == LOOP_RELEASE) {
    loop_voice_release(harp, v);
    loop_voice[e.voice] = -1;
  } else {                                             // a chord retuned under a held chord
    voices[v].freq = e.freq;
    if (harp) midi_in_tune_harp(v);
    else midi_in_tune_chord(v);
    if (e.midi_note && e.midi_note != loop_sent_note[slot]) {
      loop_midi_off(slot, port);
      loop_sent_channel[slot] = harp ? mpe_harp_channel(v) : mpe_chord_channel(v);
      queue_midi(true, e.midi_note, e.midi_velocity, loop_sent_channel[slot], port);
      loop_sent_note[slot] = e.midi_note;
    }
  }
}

// Plays pass's events after from and up to to (loop ms). The ones that fall due are copied out
// with interrupts off, since a chord timer can insert one meanwhile, and played with them on.
static void loop_play_range(int32_t from, int32_t to, uint16_t pass) {
  loop_event due[48];
  uint8_t n = 0;
  noInterrupts();
  for (uint16_t i = 0; i < loop_event_count && n < 48; i++) {
    const loop_event &e = loop_events[i];
    if ((int32_t)e.t <= from) continue;
    if ((int32_t)e.t > to) break;
    if (e.cycle == pass && pass != 0) continue;       // captured on this pass: it played live
    due[n++] = e;
  }
  interrupts();
  for (uint8_t k = 0; k < n; k++) loop_play_event(due[k]);
}

// Lets go of everything the looper is playing, and of whatever an overdub left open
static void loop_release_all() {
  for (uint8_t i = 0; i < 12; i++) if (midi_in_harp[i].owned && midi_in_harp[i].looped && midi_in_harp[i].key_down) loop_voice_release(true, i);
  for (uint8_t i = 0; i < 4; i++) if (midi_in_chord[i].owned && midi_in_chord[i].looped && midi_in_chord[i].key_down) loop_voice_release(false, i);
  for (uint8_t v = 0; v < 16; v++) loop_voice[v] = -1;
}

// Voices still open are let go at t, so none hangs across the join
static void loop_close_open(uint32_t t, uint16_t cycle) {
  for (uint8_t v = 0; v < 16; v++) {
    if (!loop_open[v]) continue;
    loop_open[v] = false;
    loop_event e = {t, cycle, LOOP_RELEASE, v, 0, 0, 0, 0};
    loop_insert(e);
  }
}

static void looper_show() {
  if (looper_state != LOOPER_RECORDING && looper_state != LOOPER_PLAYING) set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
}

FLASHMEM void looper_stop() {
  if (looper_state == LOOPER_RECORDING) return;        // looper_close first
  if (looper_state == LOOPER_COUNTIN) {                // called off before it began: nothing recorded
    looper_state = LOOPER_EMPTY;
    looper_show();
    return;
  }
  if (looper_state == LOOPER_PLAYING) {
    if (looper_overdub) loop_close_open(loop_length_ms - 1, 0);
    looper_overdub = false;
    loop_release_all();
    looper_state = LOOPER_STOPPED;
  }
  looper_show();
}

FLASHMEM void looper_clear() {
  if (looper_state == LOOPER_RECORDING || looper_state == LOOPER_COUNTIN) looper_state = LOOPER_STOPPED;
  looper_stop();
  loop_event_count = 0;
  loop_length_ms = 0;
  loop_phrase_bars = 0;
  looper_state = LOOPER_EMPTY;
  looper_show();
}

// a chord ringing as the recording starts begins the loop
FLASHMEM static void looper_begin_recording() {
  looper_state = LOOPER_RECORDING;
  for (uint8_t i = 0; i < 4; i++) {
    if (chord_envelope_array[i]->isActive() && !chord_voice_released[i] && !midi_in_chord[i].owned && !loop_open[12 + i]) {
      looper_capture(LOOP_START, 12 + i, chord_voice_note_freq[i], 1, chord_started_notes[i], chord_attack_velocity);
    }
  }
}

FLASHMEM void looper_record(uint32_t at) {
  looper_clear();
  for (uint8_t v = 0; v < 16; v++) { loop_open[v] = false; loop_voice[v] = -1; loop_quantize_shift[v] = 0; }
  loop_phrase_bars = looper_length;
  if (looper_count_in && looper_timed()) {   // the count-in's bars first: the recording starts on the bar line after
    uint32_t bars = (uint32_t)lroundf(looper_count_in * acc_beats_per_bar() * looper_beat_ms());
    loop_start_ms = looper_on_beat() ? acc_bar_ms_after(bars - (uint32_t)(looper_beat_ms() / 2)) : at + bars;
    looper_state = LOOPER_COUNTIN;
    return;
  }
  loop_start_ms = looper_snap(at);
  looper_begin_recording();
}

// Recording to playing: the loop closes at at (with looper length, on a bar line; now, there even
// if it is still to come, for stop)
FLASHMEM void looper_close(uint32_t at, bool now = false) {
  if (looper_state != LOOPER_RECORDING) return;
  int32_t length = (int32_t)(at - loop_start_ms);
  if (loop_phrase_bars) {                              // a phrase in bars ends on a bar line, the nearest
    float bar = looper_bar_ms();
    uint8_t bars = min(max(1L, lroundf(length / bar)), (long)loop_phrase_bars);
    length = (int32_t)(bars * bar);
    if (!now && (int32_t)(millis() - loop_start_ms) < length) {   // still to come: recording on till it
      loop_phrase_bars = bars;
      return;
    }
  } else if (looper_timed()) {
    float beat = looper_beat_ms() / (acc_bar_eighths() % 2 ? 2 : 1);   // a 7/8's bar is three and a half
    length = (int32_t)(max(1L, lroundf(length / beat)) * beat);
  }
  if (length < 50) {                                   // a slip of the finger, not a loop
    looper_state = LOOPER_STOPPED;
    looper_clear();
    return;
  }
  loop_length_ms = length;
  noInterrupts();
  while (loop_event_count > 0 && loop_events[loop_event_count - 1].t >= (uint32_t)length) loop_event_count = loop_event_count - 1;   // past a beat-rounded end
  interrupts();
  loop_close_open(length - 1, 0);
  if (looper_quantize) loop_quantize_pass(length);
  uint32_t elapsed = millis() - loop_start_ms;
  loop_cycle = elapsed / length;
  loop_played_to = loop_cycle == 0 ? (int32_t)(elapsed % length) : -1;   // pass 0 played live; later passes from the top
  looper_state = LOOPER_PLAYING;
}

// From stopped: from the top, every note in it now an old one
FLASHMEM void looper_play() {
  if (looper_state == LOOPER_RECORDING) { looper_close(millis()); return; }
  if (looper_state != LOOPER_STOPPED || loop_length_ms == 0) return;
  noInterrupts();
  for (uint16_t i = 0; i < loop_event_count; i++) loop_events[i].cycle = 0;
  interrupts();
  uint32_t now = millis();
  loop_start_ms = looper_snap(now) - loop_length_ms;   // pass 1, at its start
  loop_cycle = 1;
  loop_played_to = -1;
  looper_state = LOOPER_PLAYING;
}

FLASHMEM void looper_overdub_toggle() {
  if (looper_state != LOOPER_PLAYING) return;
  if (looper_overdub) {
    uint32_t elapsed = millis() - loop_start_ms;
    loop_close_open(elapsed % loop_length_ms, elapsed / loop_length_ms);
    looper_overdub = false;
  } else {
    for (uint8_t v = 0; v < 16; v++) loop_open[v] = false;
    looper_overdub = true;
  }
}

// The double tap's step: record, play, stop, record a new loop
FLASHMEM void looper_step(uint32_t at) {
  switch (looper_state) {
    case LOOPER_EMPTY: looper_record(at); break;
    case LOOPER_COUNTIN: looper_clear(); break;          // called off before it began
    case LOOPER_RECORDING: looper_close(at); break;
    case LOOPER_PLAYING: looper_stop(); break;
    default: looper_record(at); break;
  }
}

// The next step, for a looper on the computer: control change 90, its value 127 less the tap's
// delay in 4 ms steps (a tap is at most modifier_tap_max, 250 ms, so it stays 64 or more)
void looper_send_step(uint32_t delay_ms) {
  uint8_t late = min((delay_ms + 2) / 4, 63UL);
  queue_midi_cc(90, 127 - late, chord_channel, chord_port);
}

FLASHMEM void looper_action(int16_t action) {
  current_sysex_parameters[looper_adress] = 0;         // an action, never a value to keep
  looper_double_tap_owned = false;                     // worked from minicontrol or midi: the double tap stays the preset's
  switch (action) {
    case 1: if (looper_state == LOOPER_RECORDING) looper_close(millis()); else if (looper_state == LOOPER_COUNTIN) looper_clear(); else looper_record(millis()); break;
    case 2: looper_play(); break;
    case 3: if (looper_state == LOOPER_RECORDING) looper_close(millis(), true); looper_stop(); break;
    case 4: looper_clear(); break;
    case 5: looper_overdub_toggle(); break;
    case 6: looper_step(millis()); break;
    case 7: looper_send_step(0); break;
  }
}

// The click (looper click, 285): each beat, higher on a bar's first, while the looper is in a state
// it ticks for. The rhythm's beats when it runs, the looper's own from the loop's start otherwise.
FLASHMEM static void looper_click_update() {
  static uint32_t heard = 0;    // the rhythm's beats ticked, or the looper's own
  static int32_t own = INT32_MIN;
  uint8_t state = looper_state;
  bool ticking = looper_click >= 1 && (state == LOOPER_COUNTIN
                 || (looper_click >= 2 && state == LOOPER_RECORDING)
                 || (looper_click >= 3 && state == LOOPER_PLAYING));
  bool bar = false, beat = false;
  if (looper_on_beat()) {
    uint32_t count = rythm_beat_count;
    if (count != heard) { heard = count; beat = true; bar = rythm_beat_bar; }
    own = INT32_MIN;
  } else if (state == LOOPER_COUNTIN || state == LOOPER_RECORDING || state == LOOPER_PLAYING) {
    float eighth_ms = looper_beat_ms() / 2;   // counted in eighths, for a time signature's groups of three
    int32_t from_start = (int32_t)(millis() - loop_start_ms);
    int32_t n = (int32_t)floorf(from_start / eighth_ms);
    if (own == INT32_MIN || n < own - 1) own = n - 1;   // came in partway: from the next
    if (n != own) {
      own = n;
      int32_t per_bar = acc_bar_eighths();
      int32_t e = ((n % per_bar) + per_bar) % per_bar;
      beat = acc_bar_beats() >> e & 1;
      bar = e == 0;
    }
  } else {
    own = INT32_MIN;
  }
  if (beat && ticking) {
    looper_click_tick.tick(bar ? 2093.0f : 1568.0f, looper_click_level / 100.0f);   // C7 on a bar's first, G6 the rest
    looper_click_flash_ms = millis();
  }
}

// Called from loop(): plays what has fallen due
void looper_update() {
  looper_click_update();
  if (looper_state == LOOPER_COUNTIN && (int32_t)(millis() - loop_start_ms) >= 0) looper_begin_recording();
  if (looper_state == LOOPER_RECORDING && loop_phrase_bars) {   // the phrase's bars are up: it closes by itself
    int32_t phrase = (int32_t)(loop_phrase_bars * looper_bar_ms());
    if ((int32_t)(millis() - loop_start_ms) >= phrase) looper_close(loop_start_ms + phrase);
  }
  if (looper_state != LOOPER_PLAYING || loop_length_ms == 0) return;
  uint32_t now = millis();
  if ((int32_t)(now - loop_start_ms) < 0) return;
  uint32_t elapsed = now - loop_start_ms;
  uint16_t cycle = elapsed / loop_length_ms;
  int32_t position = elapsed % loop_length_ms;
  if (cycle != loop_cycle) {                            // came round: the rest of the last pass first
    loop_play_range(loop_played_to, loop_length_ms, loop_cycle);
    loop_cycle = cycle;
    loop_played_to = -1;
  }
  if (position > loop_played_to) {
    loop_play_range(loop_played_to, position, loop_cycle);
    loop_played_to = position;
  }
}

// Function to compute MIDI note offset dynamically with circular frame shift
int8_t get_root_button(uint8_t key, uint8_t shift, uint8_t button) {
  int8_t note = base_notes[button];
  int8_t num_accidentals = key_signatures[key];

  if (key <= KEY_SIG_B || key == KEY_SIG_Fs || key == KEY_SIG_Cs) {
    // Ordinary sharp keys, plus F# and C# which need no double sharps
    for (int i = 0; i < num_accidentals && sharp_notes[num_accidentals - 1][i] != -1; i++) {
      if (button == sharp_notes[num_accidentals - 1][i]) note += sharp_step;
    }
  } else if (key >= KEY_SIG_Gs && key <= KEY_SIG_Bs) {
    // Enharmonic sharp keys: seven sharps, then a second sharp on some buttons
    for (int i = 0; i < 7 && sharp_notes[6][i] != -1; i++) {
      if (button == sharp_notes[6][i]) note += sharp_step;
    }
    int double_sharp_idx = key - KEY_SIG_Gs;
    for (int i = 0; i < 7 && double_sharp_notes[double_sharp_idx][i] != -1; i++) {
      if (button == double_sharp_notes[double_sharp_idx][i]) note += sharp_step;
    }
  } else if (key == KEY_SIG_F || (key >= KEY_SIG_Bb && key <= KEY_SIG_Cb)) {
    for (int i = 0; i < 7 && i < num_accidentals && flat_notes[num_accidentals - 1][i] != -1; i++) {
      if (key == KEY_SIG_Fb && button == BTN_B) continue; // Bbb, applied below
      if (button == flat_notes[num_accidentals - 1][i]) note -= sharp_step;
    }
    if (key == KEY_SIG_Fb) {
      for (int i = 0; i < 1 && double_flat_notes[0][i] != -1; i++) {
        if (button == double_flat_notes[0][i]) note -= 2 * sharp_step;
      }
    }
  }

  // Map button to musical index for the frame shift (C=0, D=1, E=2, F=3, G=4, A=5, B=6)
  int8_t musical_index;
  switch (button) {
    case BTN_B: musical_index = 6; break;
    case BTN_E: musical_index = 2; break;
    case BTN_A: musical_index = 5; break;
    case BTN_D: musical_index = 1; break;
    case BTN_G: musical_index = 4; break;
    case BTN_C: musical_index = 0; break;
    case BTN_F: musical_index = 3; break;
    default:    musical_index = 0; break;
  }

  // Double accidentals can push a note below zero, so normalise before shifting
  int8_t note_class = ((note % EDO) + EDO) % EDO;
  int8_t octave = note / EDO;
  note = note_class + octave * EDO;

  if (musical_index < shift) note += EDO;

  // With no user frame shift, C stays the lowest pitched button
  if (shift == 0) {
    int8_t c_note = base_notes[BTN_C];
    int8_t c_note_class = ((c_note % EDO) + EDO) % EDO;
    int8_t c_octave = c_note / EDO;
    c_note = c_note_class + c_octave * EDO;
    if (button != BTN_C && note <= c_note) note += EDO;
  }

  return note;
}
// function to calculate the frequency of individual chord notes
// Collects the distinct tones of the current chord, reduced into a single octave
// and sorted low to high. The first four entries of a chord table are the chord
// proper, so a triad whose fourth entry is the octave yields three tones while a
// seventh or sixth chord yields four. Returns how many were found.
uint8_t collect_chord_tones(uint8_t (*chord)[7], uint8_t *tones) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t t = (*chord)[i] % EDO;
    bool duplicate = false;
    for (uint8_t j = 0; j < n; j++) {
      if (tones[j] == t) duplicate = true;
    }
    if (!duplicate) tones[n++] = t;
  }
  for (uint8_t i = 1; i < n; i++) { // insertion sort, n is at most 4
    uint8_t key = tones[i];
    int8_t j = i - 1;
    while (j >= 0 && tones[j] > key) { tones[j + 1] = tones[j]; j--; }
    tones[j + 1] = key;
  }
  return n;
}

// Semitone offset of a voice for the current inversion. Voices stack upward
// through the repeating chord tones, so inversion N starts that stack N steps
// higher. Working from pitch rather than from the chord table's index order
// matters: seventh chords list the seventh before the fifth, so rotating
// indices would not produce an inversion.
int16_t inverted_voice_offset(uint8_t (*chord)[7], uint8_t voice, uint8_t inversion) {
  uint8_t tones[4];
  uint8_t n = collect_chord_tones(chord, tones);
  if (n == 0) return 0;
  uint8_t k = voice + inversion;
  return tones[k % n] + EDO * (k / n);
}

// Offset of a chord tone for this voice. The four chord voices follow the
// inversion; the extra voices used in rythm mode keep the shuffling array's
// own choice of added tones.
// How far a voice moves for the current spacing. Drop voicings take a voice
// down an octave to open the chord out; the numbering counts from the top, so
// "drop 2" is the second voice down. Once the inversion step has run the voices
// are in pitch order, which is what makes this expressible per voice.
int8_t chord_spacing_shift(uint8_t voice) {
  switch (chord_spacing) {
    case 1: return (voice == 2) ? -EDO : 0;                         // drop 2
    case 2: return (voice == 1) ? -EDO : 0;                         // drop 3
    case 3: return (voice == 2 || voice == 0) ? -EDO : 0;           // drop 2 and 4
    case 4: return (voice == 0) ? -EDO : ((voice == 3) ? EDO : 0);  // spread the outer voices
    default: return 0;
  }
}

// Moves a voice for the current spacing, but only when there is room. A drop
// that would take the chord below the usable range is simply not made, so the
// voicing narrows at the extremes rather than wrapping into noise.
uint8_t apply_chord_spacing(uint8_t note, uint8_t voice, uint8_t level, bool slashed, bool sharp) {
  if (chord_spacing == 0 || voice >= 4 || level % 10 >= 4) return note;
  int8_t shift = chord_spacing_shift(voice);
  if (shift == 0) return note;
  if (shift < 0 && (int16_t)note + shift < chord_note_floor) return note;
  if (shift > 0 && (int16_t)note + shift > chord_note_ceiling) return note;

  // A slash chord names its own bass, so a dropped voice must not end up
  // underneath it. Another octave of the slash root is fine, and thickens it;
  // any other tone below would turn a C/G into something closer to a C/E.
  if (slashed && shift < 0) {
    int8_t slash_offset = sharp ? (flat_button_modifier ? -modifier_step : modifier_step) : 0;
    int16_t slash_note = EDO * (level / 10)
      + get_root_button(key_signature_selection, chord_frame_shift, slash_value)
      + slash_offset;
    int16_t moved = (int16_t)note + shift;
    if (moved < slash_note && (moved % EDO) != (slash_note % EDO)) return note;
  }
  return note + shift;
}

int16_t chord_tone_offset(uint8_t level, uint8_t voice) {
  // Spacing needs the voices in pitch order, so it uses the same sorted path as
  // inversion. At inversion 0 that reorders which oscillator plays which note
  // without changing the notes themselves, so nothing sounds different.
  uint8_t inversion = chord_inversion + inversion_step;
  if ((inversion > 0 || chord_spacing > 0) && voice < 4 && level % 10 < 4) {
    return inverted_voice_offset(current_chord, voice, inversion);
  }
  return (*current_chord)[level % 10];
}

uint8_t calculate_note_chord(uint8_t voice, bool slashed, bool sharp) {
  uint8_t note = 0;
  uint8_t level = chord_shuffling_array[chord_shuffling_selection][voice];
  if (slashed && level % 10 == slash_slot_effective) {
    if (!flat_button_modifier) {
      note = (EDO * int(level / 10) + get_root_button(key_signature_selection, chord_frame_shift, slash_value) + sharp * modifier_step);
    } else {
      note = (EDO * int(level / 10) + get_root_button(key_signature_selection, chord_frame_shift, slash_value) - sharp * modifier_step);
    }
  } else {
    if (!flat_button_modifier) {
      note = (EDO * int(level / 10) + get_root_button(key_signature_selection, chord_frame_shift, fundamental) + sharp * modifier_step + chord_tone_offset(level, voice));
      note = apply_chord_spacing(note, voice, level, slashed, sharp);
    } else {
      note = (EDO * int(level / 10) + get_root_button(key_signature_selection, chord_frame_shift, fundamental) - sharp * modifier_step + chord_tone_offset(level, voice));
      note = apply_chord_spacing(note, voice, level, slashed, sharp);
    }
  }
  return note;
}
/* ---- automatic voice leading ------------------------------------------------
 *
 * A chord is a set of pitch classes; which octave each voice takes it in is
 * free, and the ear notices the choice more than the chord. Built upward from
 * the root every time, C major to A minor moves every voice and leaps an
 * octave, though the two chords share two notes. Placed by nearness instead,
 * two voices are simply held and one steps up to the A.
 *
 * So: collect the chord's tones in every octave that keeps the voicing within
 * voice_leading_range of where root position would have put it, then choose
 * four of them, in ascending order, that move the least from what is already
 * sounding. Voices may not cross -- a choir does not -- which is what keeps the
 * search small: a shortest path over at most forty candidates, four deep.
 *
 * The search also carries which of the chord's tones it has used, and a voicing
 * is complete only when every one appears. Without that, movement alone is the
 * wrong objective: asked for a seventh while a triad is sounding, the cheapest
 * answer doubles a note it already has rather than reaching for the seventh,
 * and a whole column of chord types collapses into the same sound.
 *
 * Everything here counts in steps of the live division, so it holds in 19 and
 * 31 as it does in 12: the tones come from collect_chord_tones(), which already
 * works modulo EDO, and the octave is EDO steps wide.
 *
 * With nothing sounding the previous voicing is taken to be root position, so
 * the search returns root position exactly and a chord played into silence is
 * always voiced the same way.
 */
// The search behind voice leading: choose `voices` notes from `pool` (ascending,
// each tagged with which of tone_count chord tones it is), in ascending order so
// no two voices cross, that include every tone and move the least in total from
// `from` (also ascending). A shortest path, `voices` deep, over the pool, carrying
// the set of tones used so far. Returns the total movement, or UNREACHABLE when
// no such choice fits the pool.
int16_t choose_voicing(const int16_t *pool, const uint8_t *pool_tone, uint8_t count, uint8_t tone_count,
                       const int16_t *from, uint8_t voices, int16_t *out) {
  if (voices == 0 || voices > 4 || count < voices || tone_count == 0 || tone_count > 4) return UNREACHABLE;
  const uint8_t full_mask = (uint8_t)((1 << tone_count) - 1);
  const uint8_t mask_count = (uint8_t)(1 << tone_count);
  static int16_t cost[4][VOICE_POOL_MAX][16];
  static uint8_t came_index[4][VOICE_POOL_MAX][16];
  static uint8_t came_mask[4][VOICE_POOL_MAX][16];

  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t mask = 0; mask < mask_count; mask++) cost[0][i][mask] = UNREACHABLE;
    uint8_t mask = (uint8_t)(1 << pool_tone[i]);
    cost[0][i][mask] = (int16_t)abs(pool[i] - from[0]);
  }

  for (uint8_t voice = 1; voice < voices; voice++) {
    int16_t best[16];
    uint8_t best_index[16];
    for (uint8_t mask = 0; mask < mask_count; mask++) { best[mask] = UNREACHABLE; best_index[mask] = 0; }
    for (uint8_t i = 0; i < count; i++) {
      if (i > 0) {
        for (uint8_t mask = 0; mask < mask_count; mask++) {
          if (cost[voice - 1][i - 1][mask] < best[mask]) {
            best[mask] = cost[voice - 1][i - 1][mask];
            best_index[mask] = i - 1;
          }
        }
      }
      for (uint8_t mask = 0; mask < mask_count; mask++) cost[voice][i][mask] = UNREACHABLE;
      uint8_t bit = (uint8_t)(1 << pool_tone[i]);
      int16_t step = (int16_t)abs(pool[i] - from[voice]);
      for (uint8_t mask = 0; mask < mask_count; mask++) {
        if (best[mask] == UNREACHABLE) continue;
        uint8_t reached = (uint8_t)(mask | bit);
        int16_t total = (int16_t)(best[mask] + step);
        if (total < cost[voice][i][reached]) {
          cost[voice][i][reached] = total;
          came_index[voice][i][reached] = best_index[mask];
          came_mask[voice][i][reached] = mask;
        }
      }
    }
  }

  const uint8_t last = voices - 1;
  int16_t best_total = UNREACHABLE;
  uint8_t index = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (cost[last][i][full_mask] < best_total) { best_total = cost[last][i][full_mask]; index = i; }
  }
  if (best_total == UNREACHABLE) return UNREACHABLE;

  uint8_t mask = full_mask;
  for (int8_t voice = last; voice >= 1; voice--) {
    out[voice] = pool[index];
    uint8_t next_index = came_index[voice][index][mask];
    uint8_t next_mask = came_mask[voice][index][mask];
    index = next_index;
    mask = next_mask;
  }
  out[0] = pool[index];
  return best_total;
}

static void sort4(int16_t *v, uint8_t n) {
  for (uint8_t i = 1; i < n; i++) {
    int16_t x = v[i];
    int8_t j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
}


/* Strict voice leading (voice leading 2). Minimising movement alone happily
 * moves two voices a perfect fifth or octave apart to the next fifth or octave
 * in the same direction -- parallel fifths and octaves, which make the four
 * voices sound like fewer -- and walks the outer voices into a fifth or octave
 * by leap. Strict adds a cost for those, so the search will take a little more
 * movement to avoid them, but will not jump across the range to do it; when
 * nothing nearby avoids one, it accepts it rather than fail.
 */
static int16_t parallel_cost(const int16_t *prev, const int16_t *next) {
  const int16_t fifth = (int16_t)((7 * EDO + 6) / 12);
  const int16_t parallel = (int16_t)((4 * EDO + 6) / 12);    // a parallel costs as much as 4 semitones of movement
  const int16_t direct = (int16_t)((2 * EDO + 6) / 12);      // a direct fifth or octave in the outer voices, 2
  const int16_t whole_step = (int16_t)((2 * EDO + 6) / 12);
  int16_t cost = 0;
  for (uint8_t i = 0; i < 4; i++) {
    for (uint8_t j = i + 1; j < 4; j++) {
      int16_t mi = next[i] - prev[i], mj = next[j] - prev[j];
      if (mi == 0 || mj == 0 || (mi > 0) != (mj > 0)) continue;   // only voices moving the same way
      int16_t before = (int16_t)(((prev[j] - prev[i]) % EDO + EDO) % EDO);
      int16_t after = (int16_t)(((next[j] - next[i]) % EDO + EDO) % EDO);
      bool perfect = (after == fifth || after == 0);
      if (perfect && before == after) cost += parallel;
      else if (perfect && i == 0 && j == 3 && abs(mj) > whole_step) cost += direct;   // top voice leaps into it
    }
  }
  return cost;
}

// The strict search: four notes, ascending so no voice crosses another, where
// voice v is fixed[v] when that is not -1 and otherwise a pool note within an
// octave of from[v] (the least movement never takes a voice further). The free
// voices' pool bits must cover `need`. The cost is the movement from `from`,
// plus parallel_cost() against `prev`, the voicing that is sounding. Every
// combination is tried: a few thousand at most. Returns the cost, or
// UNREACHABLE.
static int16_t strict_search(const int16_t *pool, const uint8_t *pool_bits, uint8_t count, uint8_t need,
                             const int16_t *fixed, const int16_t *from, const int16_t *prev, int16_t *out) {
  int16_t cand[4][VOICE_POOL_MAX];
  uint8_t cand_bits[4][VOICE_POOL_MAX];
  uint8_t n[4];
  for (uint8_t v = 0; v < 4; v++) {
    n[v] = 0;
    if (fixed[v] >= 0) { cand[v][0] = fixed[v]; cand_bits[v][0] = 0; n[v] = 1; continue; }
    for (uint8_t i = 0; i < count; i++) {
      if (abs(pool[i] - from[v]) <= EDO) { cand[v][n[v]] = pool[i]; cand_bits[v][n[v]] = pool_bits[i]; n[v]++; }
    }
    if (n[v] == 0) return UNREACHABLE;
  }
  int16_t best_total = UNREACHABLE;
  int16_t v4[4];
  for (uint8_t a = 0; a < n[0]; a++) {
    v4[0] = cand[0][a];
    for (uint8_t b = 0; b < n[1]; b++) {
      v4[1] = cand[1][b];
      if (v4[1] <= v4[0]) continue;
      for (uint8_t c = 0; c < n[2]; c++) {
        v4[2] = cand[2][c];
        if (v4[2] <= v4[1]) continue;
        for (uint8_t d = 0; d < n[3]; d++) {
          v4[3] = cand[3][d];
          if (v4[3] <= v4[2]) continue;
          uint8_t bits = (uint8_t)(cand_bits[0][a] | cand_bits[1][b] | cand_bits[2][c] | cand_bits[3][d]);
          if ((uint8_t)(bits & need) != need) continue;
          int16_t total = 0;
          for (uint8_t v = 0; v < 4; v++) total += (int16_t)abs(v4[v] - from[v]);
          if (prev) total += parallel_cost(prev, v4);
          if (total < best_total) {
            best_total = total;
            for (uint8_t v = 0; v < 4; v++) out[v] = v4[v];
          }
        }
      }
    }
  }
  return best_total;
}

// The voicing sounding now, bottom to top, or false when nothing is sounding.
static bool sounding_voicing(int16_t *prev) {
  bool sounding = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (chord_envelope_array[i]->isActive()) sounding = true;
  }
  if (!sounding) return false;
  for (uint8_t v = 0; v < 4; v++) prev[v] = previous_voicing[v];
  sort4(prev, 4);
  return true;
}

bool apply_voice_leading(bool sharp, uint8_t *out) {
  uint8_t tones[4];
  uint8_t tone_count = collect_chord_tones(current_chord, tones);
  if (tone_count == 0) return false;

  int8_t sharp_offset = sharp ? (flat_button_modifier ? -modifier_step : modifier_step) : 0;
  int16_t root = get_root_button(key_signature_selection, chord_frame_shift, fundamental) + sharp_offset;

  // Root position is the anchor: the range is measured from where this chord
  // would have sat with no voice leading at all, so the frame shift, the
  // shuffling octaves, the key and the division all carry through without being
  // consulted.
  int16_t anchor[4];
  int16_t lowest = 32767, highest = -32768;
  for (uint8_t voice = 0; voice < 4; voice++) {
    uint8_t level = chord_shuffling_array[chord_shuffling_selection][voice];
    anchor[voice] = EDO * (level / 10) + root + inverted_voice_offset(current_chord, voice, 0);
    if (anchor[voice] < lowest) lowest = anchor[voice];
    if (anchor[voice] > highest) highest = anchor[voice];
  }

  // Not clamped to chord_note_floor: at the default shuffling the voices already
  // sit below it, so clamping there would put the pool above the chord's own
  // root position and the search could not even return the chord it was given.
  // The range is the control that keeps the voicing out of the mud.
  // The range is set in semitones, so it means the same interval in every
  // division -- the same conversion transposition uses.
  int16_t range_steps = (int16_t)((voice_leading_range * EDO + 6) / 12);
  int16_t low_limit = lowest - range_steps;
  int16_t high_limit = highest + range_steps;
  if (low_limit < 0) low_limit = 0;
  if (high_limit > chord_note_ceiling) high_limit = chord_note_ceiling;

  int16_t pool[VOICE_POOL_MAX];
  uint8_t pool_tone[VOICE_POOL_MAX];   // which of the chord's tones each candidate is
  uint8_t count = 0;
  for (int16_t note = low_limit; note <= high_limit && count < VOICE_POOL_MAX; note++) {
    int16_t pitch_class = ((note - root) % EDO + EDO) % EDO;
    for (uint8_t i = 0; i < tone_count; i++) {
      if (pitch_class == tones[i]) { pool[count] = note; pool_tone[count] = i; count++; break; }
    }
  }
  if (count < 4) return false;  // nothing to choose from, leave the chord alone

  bool sounding = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (chord_envelope_array[i]->isActive()) sounding = true;
  }
  int16_t from[4];
  for (uint8_t voice = 0; voice < 4; voice++) {
    from[voice] = sounding ? previous_voicing[voice] : anchor[voice];
  }
  // Movement is measured voice by voice from the bottom up, so what it is
  // measured from has to be a voicing in that order too. A chord that was not
  // itself led -- a slash from before slashes were led, an inversion sounding
  // when voice leading was switched on -- can have its voices out of order,
  // and matching the new lowest voice to an old voice that was not the lowest
  // pulls the whole chord toward it.
  sort4(from, 4);

  int16_t chosen[4];
  if (voice_leading == 2 && sounding) {
    uint8_t bits[VOICE_POOL_MAX];
    for (uint8_t i = 0; i < count; i++) bits[i] = (uint8_t)(1 << pool_tone[i]);
    const int16_t free4[4] = {-1, -1, -1, -1};
    if (strict_search(pool, bits, count, (uint8_t)((1 << tone_count) - 1), free4, from, from, chosen) != UNREACHABLE) {
      for (uint8_t voice = 0; voice < 4; voice++) out[voice] = (uint8_t)chosen[voice];
      return true;
    }
    // nothing within an octave of each voice: fall back to the plain search
  }
  if (choose_voicing(pool, pool_tone, count, tone_count, from, 4, chosen) == UNREACHABLE) {
    return false;  // the chord will not fit in the range
  }
  for (uint8_t voice = 0; voice < 4; voice++) out[voice] = (uint8_t)chosen[voice];
  return true;
}

/* A slash chord names its bass, so voice leading must not move the bass to a
 * different note. It used to leave slash chords alone altogether: they were
 * built from root position every time, so a slash in the middle of a led
 * progression jumped to wherever root position put it -- up to most of an
 * octave above the led chords around it on the higher lines -- and the chords
 * after it were then led from up there.
 *
 * Now the bass keeps its note but takes the octave closest to the lowest voice
 * already sounding, and the other three voices are led from the three above it,
 * exactly as in apply_voice_leading(): every tone the slash chord was built
 * with, no crossing, least movement, strictly above the bass. The range applies
 * the same way, around the slash chord as built.
 *
 * With nothing sounding, the slash chord is left as built, as it always was.
 */
bool apply_slash_voice_leading(uint8_t *out) {
  bool sounding = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (chord_envelope_array[i]->isActive()) sounding = true;
  }
  if (!sounding) return false;

  // Which voice carries the slash bass, as calculate_note_chord() built it.
  int8_t bass_voice = -1;
  for (uint8_t voice = 0; voice < 4; voice++) {
    if (chord_shuffling_array[chord_shuffling_selection][voice] % 10 == note_slash_level) {
      if (bass_voice != -1) return false;   // more than one: leave it as built
      bass_voice = voice;
    }
  }
  if (bass_voice == -1) return false;       // the slash replaced no chord voice

  int16_t built[4];
  int16_t lowest = 32767, highest = -32768;
  for (uint8_t voice = 0; voice < 4; voice++) {
    built[voice] = current_chord_notes[voice];
    if (built[voice] < lowest) lowest = built[voice];
    if (built[voice] > highest) highest = built[voice];
  }
  // A slash level that replaces an upper voice makes it not a bass at all;
  // only a bass that is the chord's lowest note is kept at the bottom.
  if (built[bass_voice] != lowest) return false;
  const int16_t bass_pc = (int16_t)(built[bass_voice] % EDO);

  // The tones the other three voices carry, by pitch class.
  int16_t upper_pc[3];
  uint8_t upper_count = 0;
  for (uint8_t voice = 0; voice < 4; voice++) {
    if (voice == bass_voice) continue;
    int16_t pc = (int16_t)(built[voice] % EDO);
    bool seen = false;
    for (uint8_t k = 0; k < upper_count; k++) if (upper_pc[k] == pc) seen = true;
    if (!seen) upper_pc[upper_count++] = pc;
  }

  int16_t range_steps = (int16_t)((voice_leading_range * EDO + 6) / 12);
  int16_t low_limit = lowest - range_steps;
  int16_t high_limit = highest + range_steps;
  if (low_limit < 0) low_limit = 0;
  if (high_limit > chord_note_ceiling) high_limit = chord_note_ceiling;

  int16_t from[4];
  for (uint8_t voice = 0; voice < 4; voice++) from[voice] = previous_voicing[voice];
  sort4(from, 4);

  int16_t best_total = UNREACHABLE;
  int16_t best[4] = {0, 0, 0, 0};
  for (int16_t bass = low_limit; bass <= high_limit; bass++) {
    if (bass % EDO != bass_pc) continue;
    int16_t pool[VOICE_POOL_MAX];
    uint8_t pool_tone[VOICE_POOL_MAX];
    uint8_t count = 0;
    for (int16_t note = bass + 1; note <= high_limit && count < VOICE_POOL_MAX; note++) {
      int16_t pc = (int16_t)(note % EDO);
      for (uint8_t k = 0; k < upper_count; k++) {
        if (pc == upper_pc[k]) { pool[count] = note; pool_tone[count] = k; count++; break; }
      }
    }
    int16_t upper[3];
    int16_t total;
    if (voice_leading == 2) {
      uint8_t bits[VOICE_POOL_MAX];
      for (uint8_t i = 0; i < count; i++) bits[i] = (uint8_t)(1 << pool_tone[i]);
      const int16_t pinned[4] = {bass, -1, -1, -1};
      int16_t four[4];
      total = strict_search(pool, bits, count, (uint8_t)((1 << upper_count) - 1), pinned, from, from, four);
      if (total == UNREACHABLE) continue;
      upper[0] = four[1]; upper[1] = four[2]; upper[2] = four[3];
    } else {
      int16_t moved = choose_voicing(pool, pool_tone, count, upper_count, from + 1, 3, upper);
      if (moved == UNREACHABLE) continue;
      total = (int16_t)(moved + abs(bass - from[0]));
    }
    if (total < best_total) {
      best_total = total;
      best[0] = bass;
      best[1] = upper[0];
      best[2] = upper[1];
      best[3] = upper[2];
    }
  }
  if (best_total == UNREACHABLE) return false;   // will not fit in the range: leave it as built
  for (uint8_t voice = 0; voice < 4; voice++) out[voice] = (uint8_t)best[voice];
  return true;
}

/* Slash voice: the slash replaces one of the four chord voices, chosen by its
 * place in the chord counted from the bottom, whatever chord tone it happens to
 * be holding in this voicing. Slash level instead replaces a slot of the
 * chord's recipe (root, third, and then the fifth or the seventh depending on
 * the chord type), which lands in a different voice with every inversion,
 * spacing and voice leading choice.
 *
 * - Bass: a slash chord in the usual sense. The bass takes the slash note, in
 *   the octave nearest where the bass was, and the three voices above it hold
 *   the whole chord, moving as little as they can from where they were: a
 *   triad's three tones, or a four-note chord without its fifth (or, lacking
 *   a fifth, without its last tone).
 * - Tenor, alto, soprano: that voice alone takes the slash note, in the octave
 *   nearest where it was, kept between the voices either side of it; the other
 *   three stay exactly where they were. A melody note over the chord in the
 *   soprano, a suspension or passing tone in the inner voices.
 *
 * Rhythm mode's extra voices and the harp then replace the slot of the tone
 * the chosen voice gave up (slash_slot_effective), so they follow the chord as
 * it sounds. For the bass that is the root slot, as at slash level 0.
 */
// The chord tones three voices can hold: all of a triad's, or a four-note
// chord's without its fifth (the tone that adds least), or without its last
// tone if it has no fifth. Leaves out `skip` first if it is a chord tone,
// since the pinned voice already sounds it. Returns how many.
static uint8_t tones_for_three(int16_t skip, uint8_t *tones) {
  uint8_t n = collect_chord_tones(current_chord, tones);
  for (uint8_t t = 0; t < n; t++) {
    if (tones[t] == skip) { for (uint8_t u = t; u + 1 < n; u++) tones[u] = tones[u + 1]; n--; break; }
  }
  if (n == 4) {
    int16_t fifth = (int16_t)((7 * EDO + 6) / 12);
    int8_t drop = 3, best_d = 127;
    for (uint8_t t = 1; t < 4; t++) {
      int16_t d = (int16_t)abs((int16_t)tones[t] - fifth);
      if (d <= (EDO + 6) / 12 && d < best_d) { best_d = d; drop = t; }
    }
    for (uint8_t t = drop; t < 3; t++) tones[t] = tones[t + 1];
    n = 3;
  }
  return n;
}

static int16_t nearest_with_pc(int16_t near, int16_t pc, int16_t lo, int16_t hi) {
  // The note with pitch class pc nearest `near`, inside [lo, hi] if possible.
  int16_t best = -1, best_d = 32767;
  for (int16_t n = (lo < 0 ? 0 : lo); n <= hi && n <= chord_note_ceiling; n++) {
    if (n % EDO != pc) continue;
    int16_t d = (int16_t)abs(n - near);
    if (d < best_d) { best_d = d; best = n; }
  }
  if (best >= 0) return best;
  // Nothing in range: the nearest anywhere.
  int16_t base = near - ((near % EDO - pc + EDO) % EDO);
  int16_t up = base + EDO;
  best = (abs(near - base) <= abs(up - near)) ? base : up;
  if (best < 0) best += EDO;
  return best;
}

// The chord voices from the bottom up, as indices into current_chord_notes.
// Voice leading already delivers them in order; without it, inversion and
// spacing can leave them out of order.
static void voices_bottom_up(uint8_t *order) {
  for (uint8_t i = 0; i < 4; i++) order[i] = i;
  for (uint8_t i = 1; i < 4; i++) {
    uint8_t v = order[i];
    int8_t j = i - 1;
    while (j >= 0 && current_chord_notes[order[j]] > current_chord_notes[v]) { order[j + 1] = order[j]; j--; }
    order[j + 1] = v;
  }
}

// The three ways one chord voice can be given a pitch class, shared by the
// slash (apply_slash_voice) and the harp (apply_cantus):
//   PIN_BASS    the bass takes it and the three voices above hold the chord
//   PIN_REVOICE the voice takes it and the two voices that are neither it nor
//               the bass re-voice to keep the chord whole; the bass holds
//   PIN_ALONE   the voice takes it, between its neighbours, and nothing else moves
enum PinMode : uint8_t { PIN_ALONE, PIN_REVOICE, PIN_BASS };

// Gives voice k (0 bass .. 3 soprano) the pitch class pin_pc. root is the
// chord's root with any sharp applied. Sets slash_slot_effective to the slot
// the harp and rhythm mode's extra voices should replace; a caller that is not
// a slash puts it back.
static void pin_voice(int16_t root, int16_t pin_pc, uint8_t k, uint8_t mode) {
  const int16_t slash_pc = pin_pc;   // the paths below were written for the slash
  uint8_t order[4];
  voices_bottom_up(order);
  if (k > 3) return;
  uint8_t target = order[k];
  int16_t was = current_chord_notes[target];

  if (mode == PIN_BASS) {
    // The tones the three voices above have to hold.
    uint8_t tones[4];
    uint8_t tone_count = tones_for_three(-1, tones);   // the whole chord above the bass, even its root
    int16_t from[3];
    for (uint8_t i = 0; i < 3; i++) from[i] = current_chord_notes[order[i + 1]];
    int16_t hi = from[2] + EDO;
    if (hi > chord_note_ceiling) hi = chord_note_ceiling;

    // The slash note just below the bass and just above it, whichever lets the
    // whole chord move least: going down usually leaves the voices above
    // exactly where they are.
    int16_t best_total = UNREACHABLE;
    int16_t best[4] = {0, 0, 0, 0};
    int16_t down = was - ((was % EDO - slash_pc + EDO) % EDO);
    int16_t candidates[2] = {down, (int16_t)(down + EDO)};
    for (uint8_t c = 0; c < 2; c++) {
      int16_t bass = candidates[c];
      if (bass < 0 || bass > chord_note_ceiling) continue;
      int16_t pool[VOICE_POOL_MAX];
      uint8_t pool_tone[VOICE_POOL_MAX];
      uint8_t count = 0;
      for (int16_t n = bass + 1; n <= hi && count < VOICE_POOL_MAX; n++) {
        int16_t pc = ((n - root) % EDO + EDO) % EDO;
        for (uint8_t t = 0; t < tone_count; t++) {
          if (pc == tones[t]) { pool[count] = n; pool_tone[count] = t; count++; break; }
        }
      }
      int16_t upper[3];
      int16_t moved = choose_voicing(pool, pool_tone, count, tone_count, from, 3, upper);
      if (moved == UNREACHABLE) continue;
      int16_t total = (int16_t)(moved + abs(bass - was));
      if (total < best_total) {
        best_total = total;
        best[0] = bass; best[1] = upper[0]; best[2] = upper[1]; best[3] = upper[2];
      }
    }
    // Strict voice leading: the same choice, with the cost of parallels
    // against the chord sounding now.
    int16_t sounding_now[4];
    if (voice_leading == 2 && sounding_voicing(sounding_now)) {
      int16_t plain4[4];
      for (uint8_t i = 0; i < 4; i++) plain4[i] = current_chord_notes[order[i]];
      int16_t strict_total = UNREACHABLE, strict_best[4] = {0, 0, 0, 0};
      for (uint8_t c = 0; c < 2; c++) {
        int16_t bass = candidates[c];
        if (bass < 0 || bass > chord_note_ceiling) continue;
        int16_t pool[VOICE_POOL_MAX];
        uint8_t bits[VOICE_POOL_MAX];
        uint8_t count = 0;
        for (int16_t n = bass + 1; n <= hi && count < VOICE_POOL_MAX; n++) {
          int16_t pc = ((n - root) % EDO + EDO) % EDO;
          for (uint8_t t = 0; t < tone_count; t++) {
            if (pc == tones[t]) { pool[count] = n; bits[count] = (uint8_t)(1 << t); count++; break; }
          }
        }
        const int16_t pinned[4] = {bass, -1, -1, -1};
        int16_t four[4];
        int16_t total = strict_search(pool, bits, count, (uint8_t)((1 << tone_count) - 1), pinned, plain4, sounding_now, four);
        if (total < strict_total) {
          strict_total = total;
          for (uint8_t i = 0; i < 4; i++) strict_best[i] = four[i];
        }
      }
      if (strict_total != UNREACHABLE) {
        best_total = strict_total;
        for (uint8_t i = 0; i < 4; i++) best[i] = strict_best[i];
      }
    }
    if (best_total != UNREACHABLE) {
      for (uint8_t i = 0; i < 4; i++) current_chord_notes[i] = (uint8_t)best[i];
    } else {
      current_chord_notes[target] = (uint8_t)nearest_with_pc(was, slash_pc, was - EDO, was + EDO);   // no room above: just the bass
    }
    slash_slot_effective = 0;   // the root slot, as slash level 0 always did
    return;
  }

  if (mode == PIN_REVOICE && k > 0) {
    // The chosen voice takes the slash note and the two voices that are
    // neither it nor the bass re-voice around it, keeping the chord whole:
    // between them and the bass, every tone three voices can hold (see
    // tones_for_three), moving as little as they can. The bass holds, since
    // moving it would change what the chord stands on. The slash note's octave
    // is chosen with them: just below or just above where the voice was,
    // whichever moves the chord least, and never below the bass.
    int16_t bass = current_chord_notes[order[0]];
    int16_t bass_rel = ((bass - root) % EDO + EDO) % EDO;
    int16_t slash_rel = ((slash_pc - root) % EDO + EDO) % EDO;
    uint8_t tones[4];
    uint8_t tone_count = tones_for_three(slash_rel, tones);   // the slash note, if a chord tone, is already sounding
    uint8_t need = 0;                                          // the tones the two free voices must add
    for (uint8_t t = 0; t < tone_count; t++) if (tones[t] != bass_rel) need |= (uint8_t)(1 << t);

    // Every chord tone may be used, required or not: doubling one that is
    // already sounding is often the smallest move.
    uint8_t all[4];
    uint8_t all_count = collect_chord_tones(current_chord, all);
    int16_t free_from[2];
    uint8_t f = 0;
    for (uint8_t i = 1; i < 4; i++) if (i != k) free_from[f++] = current_chord_notes[order[i]];
    int16_t hi = free_from[1] + EDO;
    if (hi > chord_note_ceiling) hi = chord_note_ceiling;
    int16_t pool[VOICE_POOL_MAX];
    uint8_t pool_bit[VOICE_POOL_MAX];   // which of `tones` it is, as a bit, or 0 for an unrequired tone
    uint8_t count = 0;
    for (int16_t n = bass + 1; n <= hi && count < VOICE_POOL_MAX; n++) {
      int16_t pc = ((n - root) % EDO + EDO) % EDO;
      bool chord_tone = false;
      for (uint8_t t = 0; t < all_count; t++) if (pc == all[t]) chord_tone = true;
      if (!chord_tone) continue;
      uint8_t bit = 0;
      for (uint8_t t = 0; t < tone_count; t++) if (pc == tones[t]) bit = (uint8_t)(1 << t);
      pool[count] = n; pool_bit[count] = bit; count++;
    }
    int16_t down = was - ((was % EDO - slash_pc + EDO) % EDO);
    int16_t pins[2] = {down, (int16_t)(down + EDO)};
    int16_t best_total = UNREACHABLE;
    int16_t best_widest = UNREACHABLE;   // on a tie, the choice whose biggest single move is smallest
    int16_t best[4] = {0, 0, 0, 0};
    for (uint8_t c = 0; c < 2; c++) {
      int16_t pin = pins[c];
      if (pin <= bass || pin > chord_note_ceiling) continue;
      // The two free voices, ascending, with k - 1 of them below the pin and
      // the rest above, adding every tone still needed. The pool is a few
      // dozen notes at most, so every pair is simply tried.
      for (uint8_t a = 0; a < count; a++)
        for (uint8_t b = a + 1; b < count; b++) {
          if (pool[a] == pin || pool[b] == pin) continue;
          uint8_t below_pin = (pool[a] < pin) + (pool[b] < pin);
          if (below_pin != k - 1) continue;
          if ((uint8_t)((pool_bit[a] | pool_bit[b]) & need) != need) continue;
          int16_t m0 = (int16_t)abs(pin - was), m1 = (int16_t)abs(pool[a] - free_from[0]), m2 = (int16_t)abs(pool[b] - free_from[1]);
          int16_t total = (int16_t)(m0 + m1 + m2);
          int16_t widest = m0 > m1 ? (m0 > m2 ? m0 : m2) : (m1 > m2 ? m1 : m2);
          if (total < best_total || (total == best_total && widest < best_widest)) {
            best_total = total;
            best_widest = widest;
            int16_t fr[2] = {pool[a], pool[b]};
            uint8_t o = 0;
            best[0] = bass;
            for (uint8_t i = 1; i < 4; i++) best[i] = (i == k) ? pin : fr[o++];
          }
        }
    }
    // Strict voice leading: the same choice, with the cost of parallels
    // against the chord sounding now.
    int16_t sounding_now[4];
    if (voice_leading == 2 && sounding_voicing(sounding_now)) {
      int16_t plain4[4];
      for (uint8_t i = 0; i < 4; i++) plain4[i] = current_chord_notes[order[i]];
      int16_t strict_total = UNREACHABLE, strict_best[4] = {0, 0, 0, 0};
      for (uint8_t c = 0; c < 2; c++) {
        int16_t pin = pins[c];
        if (pin <= bass || pin > chord_note_ceiling) continue;
        int16_t pinned[4] = {bass, -1, -1, -1};
        pinned[k] = pin;
        int16_t four[4];
        int16_t total = strict_search(pool, pool_bit, count, need, pinned, plain4, sounding_now, four);
        if (total < strict_total) {
          strict_total = total;
          for (uint8_t i = 0; i < 4; i++) strict_best[i] = four[i];
        }
      }
      if (strict_total != UNREACHABLE) {
        best_total = strict_total;
        for (uint8_t i = 0; i < 4; i++) best[i] = strict_best[i];
      }
    }
    if (best_total != UNREACHABLE) {
      for (uint8_t i = 0; i < 4; i++) current_chord_notes[i] = (uint8_t)best[i];
      // The chord is whole, so the harp and rhythm mode's extra voices play it
      // as it is, unless a four-note chord had to give up a tone to make room:
      // then the slash note takes that tone's slot there too.
      slash_slot_effective = 255;   // no slot: nothing replaced
      for (uint8_t t = 0; t < all_count; t++) {
        bool kept = (all[t] == slash_rel);
        for (uint8_t u = 0; u < tone_count; u++) if (tones[u] == all[t]) kept = true;
        if (kept) continue;
        for (uint8_t slot = 0; slot < 7; slot++) {
          if ((*current_chord)[slot] % EDO == all[t]) { slash_slot_effective = slot; break; }
        }
        break;
      }
      return;
    }
    // no way to keep it whole in this range: fall through and change the one voice
  }

  // One voice alone: between the neighbours, nearest where it was. The bass
  // alone (a harp note under a slash held above it) stays within an octave of
  // where it was, as the bass fallback above does.
  int16_t below = (k > 0) ? (int16_t)current_chord_notes[order[k - 1]] : (int16_t)(was - EDO - 1);
  int16_t above = (k < 3) ? current_chord_notes[order[k + 1]] : (int16_t)chord_note_ceiling;
  current_chord_notes[target] = (uint8_t)nearest_with_pc(was, slash_pc, below + 1, above - 1);

  // The slot of the tone it gave up, the first slot holding that pitch class.
  int16_t gave_up = ((was - root) % EDO + EDO) % EDO;
  slash_slot_effective = note_slash_level;
  for (uint8_t slot = 0; slot < 7; slot++) {
    if ((*current_chord)[slot] % EDO == gave_up) { slash_slot_effective = slot; break; }
  }
}

void apply_slash_voice(bool sharp) {
  if (slash_voice < 1 || slash_voice > 4) return;
  int8_t sharp_offset = sharp ? (flat_button_modifier ? -modifier_step : modifier_step) : 0;
  int16_t root = get_root_button(key_signature_selection, chord_frame_shift, fundamental) + sharp_offset;
  int16_t slash_pc = ((get_root_button(key_signature_selection, chord_frame_shift, slash_value) + sharp_offset) % EDO + EDO) % EDO;
  uint8_t k = slash_voice - 1;           // 0 bass .. 3 soprano
  pin_voice(root, slash_pc, k, k == 0 ? PIN_BASS : (slash_revoice ? PIN_REVOICE : PIN_ALONE));
}

// The chord's root as it sounds, with any sharp applied.
static int16_t sounding_root() {
  int8_t sharp_offset = chord_context_sharp ? (flat_button_modifier ? -modifier_step : modifier_step) : 0;
  return get_root_button(key_signature_selection, chord_frame_shift, fundamental) + sharp_offset;
}

// Whether the cantus note is one of the current chord's own tones.
static bool cantus_is_chord_tone() {
  if (cantus_pc < 0) return false;
  int16_t rel = ((cantus_pc - sounding_root()) % EDO + EDO) % EDO;
  uint8_t tones[4];
  uint8_t n = collect_chord_tones(current_chord, tones);
  for (uint8_t t = 0; t < n; t++) if (tones[t] == rel) return true;
  return false;
}

// Brothers' place for a note, defined with the chord scales below.
static uint8_t brothers_role(int16_t pc, uint8_t *degree = nullptr);
static void brothers_again_step();

/* Cantus: the harp sets one chord voice, the way a slash does from the chord
 * buttons. The cantus voice (address 115) names which: 1 bass, 2 tenor, 3 alto,
 * 4 soprano counted from the bottom, or 5 for whichever voice can reach the
 * note by the smallest move (a tie goes to the upper voice).
 *
 * A chord tone keeps the chord whole: the other voices re-voice around it as
 * little as they can, and with the bass it is a slash chord in the usual sense.
 * Any other note is a passing or neighbour tone: that voice takes it alone and
 * the rest hold, until the next pluck or chord.
 *
 * A slash wins any voice it holds. With slash voice on, that is its voice; with
 * slash level, whichever voices sound the slash note. The cantus does not take
 * those, and while one of the upper voices is held it moves only its own voice,
 * so re-voicing never disturbs the slash. A slash bass is simply held, as the
 * bass always is when re-voicing.
 *
 * The harp and rhythm mode's extra voices are left as the chord and slash made
 * them: the harp is the source here, and a pluck must never retune the strings. */
void apply_cantus() {
  if (cantus_voice == 0 || cantus_voice > 5 || cantus_pc < 0) return;
  uint8_t order[4];
  voices_bottom_up(order);

  uint8_t held = 0;   // positions from the bottom a slash holds, one bit each
  if (chord_context_slashed) {
    if (slash_voice >= 1 && slash_voice <= 4) {
      held = (uint8_t)(1 << (slash_voice - 1));
    } else {
      int8_t sharp_offset = chord_context_sharp ? (flat_button_modifier ? -modifier_step : modifier_step) : 0;
      int16_t slash_pc = ((get_root_button(key_signature_selection, chord_frame_shift, slash_value) + sharp_offset) % EDO + EDO) % EDO;
      for (uint8_t p = 0; p < 4; p++) {
        if (current_chord_notes[order[p]] % EDO == slash_pc) held |= (uint8_t)(1 << p);
      }
    }
  }

  uint8_t k = 255;
  if (cantus_voice <= 4) {
    k = cantus_voice - 1;
    if (held & (1 << k)) return;
  } else {
    int16_t best_d = UNREACHABLE;
    for (int8_t p = 3; p >= 0; p--) {
      if (held & (1 << p)) continue;
      int16_t up = ((cantus_pc - current_chord_notes[order[p]]) % EDO + EDO) % EDO;
      int16_t d = (up < EDO - up) ? up : (int16_t)(EDO - up);
      if (d < best_d) { best_d = d; k = (uint8_t)p; }
    }
    if (k == 255) return;
  }

  const bool upper_held = (held & 0x0E) != 0;
  uint8_t mode;
  if (k == 0) mode = upper_held ? PIN_ALONE : PIN_BASS;
  else mode = (cantus_is_chord_tone() && !upper_held) ? PIN_REVOICE : PIN_ALONE;

  uint8_t slot = slash_slot_effective;
  pin_voice(sounding_root(), cantus_pc, k, mode);
  slash_slot_effective = slot;
}

// A harp pluck. The first string of a new gesture gives the cantus its note and
// the chord moves at once, retuning only the voices that changed, the way a
// legato chord change does: no retrigger, glide if the preset glides, and on
// MIDI a new note on each moved voice's own channel.
FLASHMEM void cantus_follow(uint8_t string) {
  static elapsedMillis since_pluck = cantus_strum_gap;   // the first pluck after power-up is a new gesture too
  const bool new_gesture = since_pluck >= cantus_strum_gap;
  since_pluck = 0;
  if (new_gesture && chord_touch && !midi_in_harp[string].owned) chord_touch_take(string);   // the harp's dynamic (CHORD TOUCH)
  if ((cantus_voice == 0 && brothers_mode == 0) || !new_gesture) return;
  if (current_line < 0 && !continuous_chord) return;   // no chord to steer
  const int16_t pc = current_harp_notes[string] % EDO;
  // Brothers takes the notes of the sixth diminished scale, the cantus the rest.
  const bool block_was = brothers_note >= 0;
  bool rebuild = false, by_brothers = false;
  if (brothers_mode && brothers_role(pc)) {
    by_brothers = true;
    if (current_harp_notes[string] == brothers_note) brothers_again_step();
    brothers_note = current_harp_notes[string];
    cantus_pc = -1;
  } else if (cantus_voice) {
    brothers_note = -1;
    cantus_pc = pc;
    rebuild = block_was;   // the cantus steers the chord, not the block that was
  } else {
    return;   // outside the scale with no cantus: the block holds
  }
  uint8_t before[4];
  for (uint8_t i = 0; i < 4; i++) before[i] = current_chord_notes[i];
  if (rebuild) build_chord_notes();   // applies the cantus too
  else if (!apply_brothers()) apply_cantus();
  // With retrigger chords (21) on, a gesture brothers takes strikes the block,
  // the way a chord change on the buttons does, so a sound that decays starts
  // again with each one; off, the voices slide there. The cantus always slides:
  // it moves one voice of a chord that goes on.
  const bool strike = by_brothers && retrigger_chord && !rythm_mode;
  for (uint8_t i = 0; i < 4; i++) {
    previous_voicing[i] = current_chord_notes[i];
    if (strike) { current_applied_chord_notes[i] = current_chord_notes[i]; continue; }
    if (current_chord_notes[i] == before[i]) continue;
    current_applied_chord_notes[i] = current_chord_notes[i];
    if (chord_envelope_array[i]->isActive()) set_chord_voice_frequency(i, current_chord_notes[i]);
  }
  if (strike) trigger_chord = true;   // trigger_chord_notes() strikes it, strum and all
}

// function to calculate the level of individual harp touch
// Collapse the degree toggles into an ascending interval list. An empty scale
// would leave the harp silent, so the root is kept in that case.
static inline bool step_scale_has(uint8_t step) {
  return step_scale_mask[step / step_scale_bits] & (1 << (step % step_scale_bits));
}

FLASHMEM void rebuild_custom_scale() {
  custom_scale_length = 0;
  bool steps = false;
  if (edo_index != 0) {
    for (uint8_t i = 0; i < EDO; i++) if (step_scale_has(i)) steps = true;
  }
  if (steps) {
    for (uint8_t i = 0; i < EDO; i++) {
      if (step_scale_has(i)) custom_scale_intervals[custom_scale_length++] = i;
    }
  } else {
    for (uint8_t i = 0; i < 12; i++) {
      if (custom_scale_mask & (1 << i)) {
        custom_scale_intervals[custom_scale_length++] = edo_chromatic_degrees[edo_index][i];
      }
    }
  }
  if (custom_scale_length == 0) {
    custom_scale_intervals[0] = 0;
    custom_scale_length = 1;
  }
  // The down form, from the step scale down in the divided octaves, or the
  // custom scale down by name; with neither, none (the up form serves).
  custom_down_length = 0;
  bool down_steps = false;
  if (edo_index != 0) {
    for (uint8_t i = 0; i < EDO; i++) {
      if (step_scale_down_mask[i / step_scale_bits] & (1 << (i % step_scale_bits))) down_steps = true;
    }
  }
  if (down_steps) {
    for (uint8_t i = 0; i < EDO; i++) {
      if (step_scale_down_mask[i / step_scale_bits] & (1 << (i % step_scale_bits))) custom_down_intervals[custom_down_length++] = i;
    }
  } else {
    for (uint8_t i = 0; i < 12; i++) {
      if (custom_scale_down_mask & (1 << i)) custom_down_intervals[custom_down_length++] = edo_chromatic_degrees[edo_index][i];
    }
  }
}

FLASHMEM void rebuild_maqam() {
  uint8_t m = maqam_selection < maqam_count ? maqam_selection : 0;
  for (uint8_t d = 0; d < 7; d++) {
    uint8_t up = maqam_quarter_tones[m][maqam_upper_jins ? 2 : 0][d];
    uint8_t down = maqam_quarter_tones[m][maqam_upper_jins ? 2 : 1][d];
    maqam_up[d] = (up * EDO + 12) / 24;
    maqam_down[d] = (down * EDO + 12) / 24;
  }
}

// The scale modes 10-15 play, in the melody's direction.
void harp_scale_list(const uint8_t *&intervals, uint8_t &length) {
  bool down = melodic_direction && harp_descending;
  uint8_t m = scalar_harp_selection;
  if (m == 12 || m == 13) {
    intervals = generator_scale_intervals;
    length = generator_scale_length;
  } else if (m == 14 || m == 15) {
    intervals = down ? maqam_down : maqam_up;
    length = 7;
  } else if (down && custom_down_length > 0) {
    intervals = custom_down_intervals;
    length = custom_down_length;
  } else {
    intervals = custom_scale_intervals;
    length = custom_scale_length;
  }
}

// Whether the harp's mode has a down form for melodic direction to follow.
bool harp_has_forms() {
  uint8_t m = scalar_harp_selection;
  return melodic_direction && !chromatic_harp_mode && (m == 10 || m == 11 || m == 14 || m == 15);
}

// A string sounding at position p: the melody goes up if it is above the last
// one, down if below.
void harp_motion(uint8_t p) {
  static int16_t last = -1;
  if (last >= 0 && p > last) harp_descending = false;
  if (last >= 0 && p < last) harp_descending = true;
  last = p;
}

FLASHMEM void rebuild_generator_scale() {
  bool present[31] = {false};
  uint8_t generator = generator_steps % EDO;
  uint8_t size = generator_size < 1 ? 1 : generator_size;
  uint8_t below = generator_mode < size ? generator_mode : size - 1;
  for (uint8_t k = 0; k < size; k++) {
    int16_t step = ((int16_t)k - below) * generator;   // the root is k = below
    present[((step % EDO) + EDO) % EDO] = true;
  }
  generator_scale_length = 0;
  for (uint8_t i = 0; i < EDO; i++) {
    if (present[i]) generator_scale_intervals[generator_scale_length++] = i;
  }
}

// Modes 10 and 11 both run the user scale, 12 and 13 the generator scale; each
// pair differs only in what it roots the scale on, the key signature or the
// chord being held.
uint8_t calculate_list_scale_note(uint8_t string, uint8_t root_note, int8_t sharp_offset,
                                  const uint8_t *intervals, uint8_t length) {
  uint8_t octave = string / length;
  uint8_t degree = string % length;
  // A very short scale would otherwise climb an octave per string: with a
  // single note the top string lands eleven octaves up, far past Nyquist, and
  // aliases into noise. Cap the climb and let the upper strings repeat.
  if (octave > custom_scale_max_octave) octave = custom_scale_max_octave;
  return root_note + sharp_offset + intervals[degree] + (octave * EDO);
}

enum ChordType {
  CHORD_MAJOR, CHORD_MINOR, CHORD_SEVENTH, CHORD_MAJ_SEVENTH, CHORD_MIN_SEVENTH,
  CHORD_DIM, CHORD_AUG, CHORD_MAJ_SIXTH, CHORD_MIN_SIXTH, CHORD_FULL_DIM,
  CHORD_HALF_DIM, CHORD_SUS_FOURTH, CHORD_SUS_SECOND, CHORD_SEVENTH_SUS,
  CHORD_MAJ_NINTH, CHORD_MIN_NINTH, CHORD_ADD_NINTH, CHORD_SIX_NINE,
  CHORD_NEUTRAL, CHORD_HARMONIC_7TH, CHORD_SUBMINOR, CHORD_SUPERMAJOR,
  CHORD_SUBMINOR_SEVENTH, CHORD_UTONAL_TETRAD, CHORD_HARMONIC_NINTH,
  CHORD_NEUTRAL_SEVENTH, CHORD_OTONAL_HEXAD,
  CHORD_JUST_AUGMENTED, CHORD_SUPERMAJOR_SEVENTH,
  CHORD_UNKNOWN
};

ChordType get_chord_type(uint8_t (*chord)[7]);

// The just intervals purity draws toward, simplest first: the first within
// reach of a tempered step is the one it takes.
const float purity_just[][2] = {
  {1, 1}, {3, 2}, {4, 3}, {5, 4}, {6, 5}, {5, 3}, {8, 5}, {9, 8}, {15, 8}, {9, 5}, {16, 15},
  {7, 4}, {7, 6}, {9, 7}, {7, 5}, {10, 7}, {11, 9}, {11, 8}, {12, 7}, {16, 9}, {18, 11},
  {11, 6}, {12, 11}, {10, 9}, {45, 32}
};
const float purity_reach = 25;          // cents from a tempered step to a just interval it may take
const float purity_seventh_reach = 40;  // ...and for a dominant's seventh, which takes 7/4 first

FLASHMEM void rebuild_purity() {
  ChordType type = get_chord_type(current_chord);
  bool dominant = type == CHORD_SEVENTH || type == CHORD_HARMONIC_7TH || type == CHORD_SEVENTH_SUS
                  || type == CHORD_HARMONIC_NINTH || type == CHORD_OTONAL_HEXAD;
  float root = log2f(tempered_ratio(purity_root_pc));
  for (uint8_t k = 0; k < EDO; k++) {
    float cents = 1200.0f * (log2f(tempered_ratio(purity_root_pc + k)) - root);
    cents -= 1200.0f * floorf(cents / 1200.0f);
    float correction = 0;
    float seventh = 1200.0f * log2f(7.0f / 4.0f);
    if (dominant && fabsf(cents - seventh) <= purity_seventh_reach) {
      correction = seventh - cents;
    } else {
      for (const auto &j : purity_just) {
        float just = 1200.0f * log2f(j[0] / j[1]);
        float d = just - cents;
        if (fabsf(d) <= purity_reach) { correction = d; break; }
        if (fabsf(d + 1200) <= purity_reach) { correction = d + 1200; break; }   // just under the octave above
      }
    }
    purity_ratio[k] = exp2f(purity / 100.0f * correction / 1200.0f);
  }
}

// The chord being played gives the root; called as a chord changes. Until the first chord since power
// on there is none, and the key's root stands in (it had been the first button's, B).
bool purity_have_chord = false;
FLASHMEM void purity_follow_chord(bool from_chord) {
  if (from_chord) purity_have_chord = true;
  int16_t root = scale_root_offsets[key_signature_selection];
  if (purity_have_chord) {
    root = get_root_button(key_signature_selection, chord_frame_shift, fundamental);
    if (chord_context_sharp) root += flat_button_modifier ? -modifier_step : modifier_step;
  }
  purity_root_pc = (((root + transpose_steps) % EDO) + EDO) % EDO;
  if (purity > 0) rebuild_purity();
}

ChordType get_chord_type(uint8_t (*chord)[7]) {
  if (chord == &major)       return CHORD_MAJOR;
  if (chord == &minor)       return CHORD_MINOR;
  if (chord == &seventh)     return CHORD_SEVENTH;
  if (chord == &maj_seventh) return CHORD_MAJ_SEVENTH;
  if (chord == &min_seventh) return CHORD_MIN_SEVENTH;
  if (chord == &dim)         return CHORD_DIM;
  if (chord == &aug)         return CHORD_AUG;
  if (chord == &maj_sixth)   return CHORD_MAJ_SIXTH;
  if (chord == &min_sixth)   return CHORD_MIN_SIXTH;
  if (chord == &full_dim)    return CHORD_FULL_DIM;
  if (chord == &half_dim)    return CHORD_HALF_DIM;
  if (chord == &sus_fourth)  return CHORD_SUS_FOURTH;
  if (chord == &sus_second)  return CHORD_SUS_SECOND;
  if (chord == &seventh_sus) return CHORD_SEVENTH_SUS;
  if (chord == &major_ninth) return CHORD_MAJ_NINTH;
  if (chord == &minor_ninth) return CHORD_MIN_NINTH;
  if (chord == &added_ninth) return CHORD_ADD_NINTH;
  if (chord == &six_nine)    return CHORD_SIX_NINE;
  if (chord == &neutral)      return CHORD_NEUTRAL;
  if (chord == &harmonic_7th) return CHORD_HARMONIC_7TH;
  if (chord == &subminor)     return CHORD_SUBMINOR;
  if (chord == &supermajor)   return CHORD_SUPERMAJOR;
  if (chord == &subminor_seventh) return CHORD_SUBMINOR_SEVENTH;
  if (chord == &utonal_tetrad)    return CHORD_UTONAL_TETRAD;
  if (chord == &harmonic_ninth)   return CHORD_HARMONIC_NINTH;
  if (chord == &neutral_seventh)  return CHORD_NEUTRAL_SEVENTH;
  if (chord == &otonal_hexad)     return CHORD_OTONAL_HEXAD;
  if (chord == &just_augmented)   return CHORD_JUST_AUGMENTED;
  if (chord == &supermajor_seventh) return CHORD_SUPERMAJOR_SEVENTH;
  return CHORD_UNKNOWN;
}

// Which scale suits the chord currently held. Pentatonic variants are used in
// mode 9; the diminished sixth scales suit the sixth and diminished chords in
// both modes.
//
// This does not need to test barry_harris_mode. handle_chord_type() already
// substitutes maj_sixth, min_sixth and full_dim for major, minor and dim when
// that mode is on, so the chord arriving here has the Barry Harris harmonisation
// baked in and maps to the diminished sixth scales by type alone.
uint8_t get_chord_scale_index(ChordType chord_type, bool use_pentatonic) {
  switch (chord_type) {
    case CHORD_MAJOR:        return use_pentatonic ? 0 : 10;
    case CHORD_MAJ_SEVENTH:  return use_pentatonic ? 1 : 12;
    case CHORD_MINOR:        return use_pentatonic ? 2 : 14;
    case CHORD_SEVENTH:      return use_pentatonic ? 3 : 13;
    case CHORD_MIN_SEVENTH:  return use_pentatonic ? 4 : 11;
    case CHORD_DIM:          return 5;
    case CHORD_AUG:          return 6;
    case CHORD_MAJ_SIXTH:    return 7;
    case CHORD_MIN_SIXTH:    return 8;
    case CHORD_FULL_DIM:     return 9;
    // The alternate layout's chords. A ninth chord takes the same scale as the
    // seventh it is built on, since the ninth is already in the scale; what
    // matters is that the third stays minor when the chord's is.
    case CHORD_MAJ_NINTH:    return use_pentatonic ? 1 : 12;  // as major seventh: lydian
    case CHORD_MIN_NINTH:    return use_pentatonic ? 2 : 11;  // as minor seventh: dorian
    case CHORD_ADD_NINTH:    return use_pentatonic ? 0 : 10;  // a major triad with a 9th
    case CHORD_SIX_NINE:     return use_pentatonic ? 0 : 12;  // major, and lydian suits the 6/9 colour
    // The ratio-built chords, each on the scale of its twelve-tone counterpart,
    // which substitute_chord_tones() then retunes to the chord's own tones. Each
    // row is chosen so it has a seat for every tone the chord defines, so the
    // retuning replaces like with like. The neutral triad takes the major rows
    // and its neutral third takes the third's seat: a neutral third is still the
    // chord's third, and the harp plays the chord's tones. (It used to take the
    // suspended pentatonic to leave the third out, but the retuning put one back
    // in the seat of whichever neighbour lay nearer, which was the second in 12
    // and the fourth in 31.)
    case CHORD_NEUTRAL:      return use_pentatonic ? 0 : 10;
    case CHORD_HARMONIC_7TH: return use_pentatonic ? 3 : 13;  // as a dominant: mixolydian
    case CHORD_SUBMINOR:     return use_pentatonic ? 2 : 14;  // leans minor
    case CHORD_SUPERMAJOR:   return use_pentatonic ? 0 : 10;  // leans major
    // The larger just chords. The subminor seventh takes the minor pentatonic
    // rather than m7's dorian one, which voices a sixth in place of the seventh
    // that is the whole point of this chord. The neutral seventh sits in the
    // major seventh's seat but has no major third or seventh to keep, so it takes
    // the major rows and both its neutral tones move in; the pentatonic has no
    // seventh, so 11/6 takes the sixth's place, its nearest.
    case CHORD_SUBMINOR_SEVENTH: return use_pentatonic ? 2 : 11;   // as m7: dorian
    case CHORD_UTONAL_TETRAD:    return use_pentatonic ? 17 : 18;  // as m7b5: locrian
    case CHORD_HARMONIC_NINTH:   return use_pentatonic ? 3 : 13;   // as a dominant ninth
    case CHORD_NEUTRAL_SEVENTH:  return use_pentatonic ? 0 : 10;
    case CHORD_OTONAL_HEXAD:     return use_pentatonic ? 3 : 13;   // 4:5:6:7 is a dominant
    // The just augmented keeps the augmented chord's whole tone scale, which
    // has no letter degrees; its thirds already fall on whole tone steps in
    // every division, so nothing moves. The supermajor seventh takes the major
    // seventh's lydian rows; the pentatonic has no seventh, so 27/14 takes the
    // sixth's place, as the neutral seventh's 11/6 does.
    case CHORD_JUST_AUGMENTED:     return 6;
    case CHORD_SUPERMAJOR_SEVENTH: return use_pentatonic ? 1 : 12;
    case CHORD_HALF_DIM:     return use_pentatonic ? 17 : 18; // half-diminished, locrian
    // Suspended chords deliberately withhold the third, so the harp does too.
    case CHORD_SUS_FOURTH:   return use_pentatonic ? 15 : 13; // no third; mixolydian full
    case CHORD_SUS_SECOND:   return use_pentatonic ? 15 : 10;
    case CHORD_SEVENTH_SUS:  return use_pentatonic ? 16 : 13;
    default:                 return use_pentatonic ? 0 : 10;
  }
}

/* Brothers: the harp's note chooses the chord, its own or its brother, the
 * way Barry Harris harmonised a line. A sixth chord and its diminished
 * brother alternate up the sixth diminished scale: C6 is C E G A, and the
 * scale's other four, D F G# B, are B°7. A chord tone gives the chord and a
 * tone between gives the brother, each as a close block of four with the harp's
 * note on top, so a walk up the scale walks the whole block with it, every
 * voice a step: C6, B°7 over D, C6 over E, and so on.
 *
 * Every chord on the standard buttons has its scale, as Barry taught them:
 *   major, major 7, major 6   C6 and B°7 (a major chord is played as its sixth;
 *                             a major 7's seventh has no room for a brother
 *                             between it and the root, so it sounds in B°7)
 *   minor, minor 6            Cm6 and B°7
 *   diminished, dim 7         C°7 and G6 (D E G B, the notes between its own)
 *   7th, minor 7              the chord itself and the diminished whose notes
 *                             fall one in each gap between its own: G7 and F#°7
 *                             (the leading tone's), Cm7 and B°7 (Cm7 is Eb6)
 * The sevenths need the diminished to divide the octave evenly, so only in 12
 * and 24; the sixths' scales are tabled for every division. The augmented and
 * the alternate layout's chords have none.
 *
 * It is the cantus's opposite. The cantus moves one voice and keeps the chord,
 * so a note outside it is a passing tone sung against the rest; here there are
 * no passing tones, since every note of the scale has a chord of its own.
 *
 * Unslashed only, and only for notes of the scale: harp modes 8 and 9 play it
 * while brothers is on (brothers_scale() in calculate_note_harp()). Anything
 * else is left to the cantus if it is on, and otherwise the block holds. The
 * block's top is the harp's note itself, moved by whole octaves so the harp's
 * middle string lands around the top of the chord as the buttons alone would
 * voice it: a walk up the harp climbs the block with it, with no drop back at
 * the octave, and the same string always gives the same block, wherever the
 * hand came from. */

// The eight-note scale Brothers walks under a chord, from its root: the
// chord's own tones on the even degrees, its brother's on the odd. False when
// the chord has none.
FLASHMEM bool brothers_scale(uint8_t (*chord)[7], uint8_t *scale) {
  ChordType type = get_chord_type(chord);
  int8_t table = -1;
  switch (type) {
    case CHORD_MAJOR: case CHORD_MAJ_SEVENTH: case CHORD_MAJ_SIXTH: table = 7; break;
    case CHORD_MINOR: case CHORD_MIN_SIXTH:                          table = 8; break;
    case CHORD_DIM: case CHORD_FULL_DIM:                             table = 9; break;
    case CHORD_SEVENTH: case CHORD_MIN_SEVENTH: break;
    default: return false;
  }
  if (table >= 0) {
    if (chord_scale_lengths[table] != 8) return false;
    memcpy(scale, chord_scale_intervals[table], 8);
    return true;
  }
  if (EDO % 4) return false;
  uint8_t tones[4];
  if (collect_chord_tones(chord, tones) != 4) return false;
  const uint8_t third = EDO / 4;   // a minor third: the diminished's step
  // Semitone offsets only: in 24 a diminished a quarter tone off would fit the
  // gaps too, and isn't the chord.
  for (uint8_t o = EDO / 12; o < third; o += EDO / 12) {
    // one of the diminished's notes strictly inside each gap, the last gap
    // running up to the root an octave higher
    bool fits = true;
    for (uint8_t g = 0; g < 4 && fits; g++) {
      uint8_t lo = tones[g], hi = g < 3 ? tones[g + 1] : EDO;
      uint8_t inside = 0;
      for (uint8_t k = 0; k < 4; k++) {
        uint8_t d = o + k * third;
        if (d > lo && d < hi) inside++;
      }
      fits = inside == 1;
    }
    if (!fits) continue;
    for (uint8_t g = 0; g < 4; g++) {
      scale[2 * g] = tones[g];
      for (uint8_t k = 0; k < 4; k++) {
        uint8_t d = o + k * third, hi = g < 3 ? tones[g + 1] : EDO;
        if (d > tones[g] && d < hi) scale[2 * g + 1] = d;
      }
    }
    return true;
  }
  return false;
}

// Where the note sits in the held chord's Brothers scale: 1 a tone of the
// chord, 2 a tone of its brother, 0 neither, or no scale.
FLASHMEM static uint8_t brothers_role(int16_t pc, uint8_t *degree) {
  if (pc < 0 || chord_context_slashed) return 0;
  uint8_t scale[8];
  if (!brothers_scale(current_chord, scale)) return 0;
  int16_t rel = ((pc - sounding_root()) % EDO + EDO) % EDO;
  for (uint8_t d = 0; d < 8; d++) {
    if (scale[d] == rel) {
      if (degree) *degree = d;
      return (d & 1) ? 2 : 1;
    }
  }
  return 0;
}

// The same note again: the next voicing in brothers again's pattern. The
// voicing then holds for every note after it, so a line can be walked in
// drop 2, until the chord ends or the same note is struck again.
FLASHMEM static void brothers_again_step() {
  switch (brothers_again) {
    case 1: brothers_drop = (brothers_drop + 1) % 4; break;
    case 2:
      if (brothers_drop >= 3) brothers_drop_dir = -1;
      else if (brothers_drop == 0) brothers_drop_dir = 1;
      brothers_drop += brothers_drop_dir;
      break;
    case 3: brothers_drop = brothers_drop ? 0 : 1; break;
  }
}

// Voices the block for brothers_note, if Brothers has one: true when it did, and
// the four chord voices hold it.
FLASHMEM bool apply_brothers() {
  if (!brothers_mode) return false;
  uint8_t degree = 0;
  if (brothers_note < 0 || brothers_role(brothers_note % EDO, &degree) == 0) return false;
  uint8_t scale[8];
  brothers_scale(current_chord, scale);   // brothers_role() found it
  const int16_t root = sounding_root();
  int16_t ref = 0;   // the top of the plain chord
  for (uint8_t i = 0; i < 4; i++) {
    int16_t n = calculate_note_chord(i, false, chord_context_sharp);
    if (n > ref) ref = n;
  }
  int16_t block[4];
  const int16_t lo = ref - EDO / 2 + 1;
  const int16_t mid = current_harp_notes[6];
  block[3] = brothers_note + (lo + ((mid - lo) % EDO + EDO) % EDO) - mid;   // whole octaves from the harp
  // Down the scale in thirds of it, every other degree: the rest of the block.
  for (uint8_t k = 1; k < 4; k++) {
    int16_t pc = root + scale[(degree + 8 - 2 * k) % 8];
    int16_t above = block[4 - k];
    block[3 - k] = above - (((above - pc) % EDO + EDO) % EDO);
  }
  // The drops keep the harp's note on top and open the block beneath it: a
  // voice below it taken down an octave, as a pianist or a big band voices a line
  int16_t b0 = block[0], b1 = block[1], b2 = block[2];
  switch (brothers_drop) {
    case 1: block[0] = b2 - EDO; block[1] = b0; block[2] = b1; break;              // drop 2
    case 2: block[0] = b1 - EDO; block[1] = b0; block[2] = b2; break;              // drop 3
    case 3: block[0] = b0 - EDO; block[1] = b2 - EDO; block[2] = b1; break;        // drop 2 and 4
  }
  while (block[0] < 0) for (uint8_t i = 0; i < 4; i++) block[i] += EDO;
  while (block[3] > chord_note_ceiling) for (uint8_t i = 0; i < 4; i++) block[i] -= EDO;
  // Each voice keeps its place from the bottom, so a step of the block is a
  // step of every voice, not voices trading notes.
  uint8_t order[4];
  voices_bottom_up(order);
  for (uint8_t i = 0; i < 4; i++) current_chord_notes[order[i]] = (uint8_t)block[i];
  return true;
}


// Modes 1-7: a fixed scale rooted on the key signature, ignoring the chord.
uint8_t calculate_static_scale_note(uint8_t string, uint8_t mode, uint8_t key) {
  uint8_t scale_index = mode - 1;
  uint8_t scale_length = scale_lengths[scale_index];
  uint8_t octave = string / scale_length;
  uint8_t scale_degree = string % scale_length;
  // Twelve strings never get this far; a zipper plate's 24 positions through
  // a pentatonic scale would, and the upper ones repeat the top octave instead.
  if (octave > custom_scale_max_octave) octave = custom_scale_max_octave;
  uint8_t scale_root = scale_root_offsets[key];
  if (mode >= 5 && mode <= 7) {
    scale_root = (scale_root + EDO - minor_third_steps()) % EDO; // relative minor, a minor third down
  }
  return scale_root + scale_intervals[scale_index][scale_degree] + (octave * EDO) + EDO;
}

// Modes 8 and 9: a scale chosen to suit the chord being held, rooted on it.
//
// The ratio-built chords then retune that scale to their own tones: each of the
// chord's four voices replaces the scale entry naming the same degree -- its
// third the scale's third, its seventh the scale's seventh -- so the harp's
// third or seventh is the chord's own instead of the diatonic stand-in one step
// away, and passing notes stay diatonic. A tone with no seat of its degree in
// the scale (a seventh over a pentatonic that has none) takes the nearest
// entry, the upper on a tie.
//
// This used to be nearest-entry for every tone, which breaks wherever a chord
// tone falls midway between two scale entries: in 19 the supermajor third sits
// one step above the major third and one below the fourth, and took the
// fourth's place, leaving the harp both thirds and no fourth.
//
// The classical chords never reach this: their scales were designed around
// their tones, including the pentatonic rows that deliberately voice a sixth in
// place of a seventh.
//
// Degrees of each chord's four voices, in table order; a voice that repeats a
// pitch class already placed (the octave doubling a triad's root) is skipped.
bool is_ratio_chord(ChordType chord_type) {
  return chord_type >= CHORD_NEUTRAL && chord_type <= CHORD_SUPERMAJOR_SEVENTH;
}

const uint8_t *ratio_chord_degrees(ChordType chord_type) {
  static const uint8_t triad[4]    = {0, 2, 4, 0};   // root, third, fifth, octave
  static const uint8_t dominant[4] = {0, 2, 6, 4};   // harmonic 7th lists its seventh before its fifth
  static const uint8_t seventh[4]  = {0, 2, 4, 6};
  static const uint8_t ninth[4]    = {0, 1, 2, 6};   // harmonic ninth: root, ninth, third, seventh
  switch (chord_type) {
    case CHORD_HARMONIC_7TH:   return dominant;
    case CHORD_HARMONIC_NINTH: return ninth;
    case CHORD_SUBMINOR_SEVENTH: case CHORD_UTONAL_TETRAD:
    case CHORD_NEUTRAL_SEVENTH:  case CHORD_OTONAL_HEXAD:
    case CHORD_SUPERMAJOR_SEVENTH:
      return seventh;
    default: return triad;
  }
}

void substitute_chord_tones(uint8_t *scale, uint8_t scale_length, uint8_t scale_index,
                            uint8_t (*chord)[7], ChordType chord_type) {
  const uint8_t *degrees = ratio_chord_degrees(chord_type);
  uint8_t placed[4];
  uint8_t placed_count = 0;
  for (uint8_t voice = 0; voice < 4; voice++) {
    uint8_t tone = (*chord)[voice] % EDO;
    bool seen = false;
    for (uint8_t i = 0; i < placed_count; i++) {
      if (placed[i] == tone) seen = true;
    }
    if (seen) continue;
    placed[placed_count++] = tone;

    uint8_t seat = 255;
    for (uint8_t j = 0; j < scale_length; j++) {
      if (chord_scale_degrees[scale_index][j] == degrees[voice]) { seat = j; break; }
    }
    if (seat == 255) {
      uint8_t best_distance = 255;
      for (uint8_t j = 0; j < scale_length; j++) {
        uint8_t distance = (scale[j] > tone) ? scale[j] - tone : tone - scale[j];
        if (distance < best_distance || (distance == best_distance && scale[j] > tone)) {
          best_distance = distance;
          seat = j;
        }
      }
    }
    scale[seat] = tone;
  }
}

uint8_t calculate_chord_specific_note(uint8_t string, uint8_t root_note, int8_t sharp_offset,
                                      uint8_t (*chord)[7], bool use_pentatonic) {
  ChordType chord_type = get_chord_type(chord);
  uint8_t scale_index = get_chord_scale_index(chord_type, use_pentatonic);
  uint8_t scale_length = chord_scale_lengths[scale_index];
  uint8_t octave = string / scale_length;
  uint8_t scale_degree = string % scale_length;
  if (octave > custom_scale_max_octave) octave = custom_scale_max_octave;   // as in the static scales
  if (is_ratio_chord(chord_type)) {
    uint8_t scale[8];
    memcpy(scale, chord_scale_intervals[scale_index], 8); // work on a copy, never the live table
    substitute_chord_tones(scale, scale_length, scale_index, chord, chord_type);
    return root_note + sharp_offset + scale[scale_degree] + (octave * EDO);
  }
  return root_note + sharp_offset + chord_scale_intervals[scale_index][scale_degree] + (octave * EDO);
}

uint8_t calculate_note_harp(uint8_t string, bool slashed, bool sharp) {
  if (chromatic_harp_mode) {
    // Chromatic mode: from C two octaves up, whatever the division, one step a
    // string, from the start of the chosen rank. Steps past the end of the
    // division carry on into the next octave.
    uint8_t ranks = (EDO + 11) / 12;
    uint8_t rank = harp_rank < 1 ? 1 : (harp_rank > ranks ? ranks : harp_rank);
    return 2 * EDO + 12 * (rank - 1) + string;
  }

  // Modes 1-7 ignore the chord entirely and run a fixed scale from the key
  if (scalar_harp_selection >= 1 && scalar_harp_selection <= 7) {
    return calculate_static_scale_note(string, scalar_harp_selection, key_signature_selection);
  }

  // Modes 10, 12 and 14 run the user scale, the generator scale and the maqam
  // from the key signature, like modes 1-7
  if (scalar_harp_selection == 10 || scalar_harp_selection == 12 || scalar_harp_selection == 14) {
    const uint8_t *intervals;
    uint8_t length;
    harp_scale_list(intervals, length);
    return calculate_list_scale_note(string, scale_root_offsets[key_signature_selection] + EDO, 0, intervals, length);
  }

  // Modes 11, 13 and 15 run them from the chord's root, like modes 8 and 9
  if (scalar_harp_selection == 11 || scalar_harp_selection == 13 || scalar_harp_selection == 15) {
    const uint8_t *intervals;
    uint8_t length;
    harp_scale_list(intervals, length);
    uint8_t root_note = slashed
      ? get_root_button(key_signature_selection, chord_frame_shift, slash_value)
      : get_root_button(key_signature_selection, chord_frame_shift, fundamental);
    int8_t sharp_offset = sharp ? (flat_button_modifier ? -modifier_step : modifier_step) : 0;
    return calculate_list_scale_note(string, root_note, sharp_offset, intervals, length);
  }

  // Modes 8 and 9 keep the chord's root but choose the scale to suit its type
  if (scalar_harp_selection == 8 || scalar_harp_selection == 9) {
    uint8_t root_note = slashed
      ? get_root_button(key_signature_selection, chord_frame_shift, slash_value)
      : get_root_button(key_signature_selection, chord_frame_shift, fundamental);
    int8_t sharp_offset = sharp ? (flat_button_modifier ? -modifier_step : modifier_step) : 0;
    // With brothers on, the scale that alternates the chord with its brother
    uint8_t scale[8];
    if (brothers_mode && !slashed && brothers_scale(current_chord, scale)) {
      return calculate_list_scale_note(string, root_note, sharp_offset, scale, 8);
    }
    return calculate_chord_specific_note(string, root_note, sharp_offset, current_chord,
                                         scalar_harp_selection == 9);
  }

  // Mode 0, the existing chord-following behaviour, unchanged
  uint8_t note = 0;
  uint8_t level = harp_shuffling_array[harp_shuffling_selection][string];
  if (slashed && level % 10 == slash_slot_effective) {
    if (!flat_button_modifier) {
      note = (EDO * int(level / 10) + get_root_button(key_signature_selection, chord_frame_shift, slash_value) + sharp * modifier_step);
    } else {
      note = (EDO * int(level / 10) + get_root_button(key_signature_selection, chord_frame_shift, slash_value) - sharp * modifier_step);
    }
  } else {
    if (!flat_button_modifier) {
      note = (EDO * int(level / 10) + get_root_button(key_signature_selection, chord_frame_shift, fundamental) + sharp * modifier_step + (*current_chord)[level % 10]);
    } else {
      note = (EDO * int(level / 10) + get_root_button(key_signature_selection, chord_frame_shift, fundamental) - sharp * modifier_step + (*current_chord)[level % 10]);
    }
  }
  return note;
}

// The note voice i plays: its own string's, or on a zipper plate the position
// it last sounded.
uint8_t harp_string_note(uint8_t i, bool slashed, bool sharp) {
  return calculate_note_harp(harp_voice_position[i], slashed, sharp);
}
//-->>RYTHM MODE UTILITIES
/* ---- ACCOMPANIMENT -----------------------------------------------------------
 *
 * Rhythm mode plays the chord voices in time. It used to be one thing: sixteen
 * steps, each a set of the chord's seven notes, all the same length and
 * loudness. It still is, as rhythm style 0, and it plays as it always did. The
 * other styles are written parts (accompaniment_styles.h): an Alberti bass, a
 * waltz, a walking bass, a strummed guitar, and so on, each a few bars of notes
 * that know what they are -- the root, the fifth below it, the top of the
 * chord, a step into the next chord -- so they follow whatever chord is held,
 * in any key, temperament and division, with its own lengths, accents and
 * strums.
 *
 * One clock runs it all: the rhythm timer, every millisecond, moving a position
 * counted in 24ths of a beat (MIDI clock's unit) at the tempo, and playing each
 * note whose time it passes. The old timer fired once a step and worked out the
 * next step's length; this one has a time for everything, so a tempo change or
 * a tap moves on smoothly, a shuffle is just where the off eighth (or sixteenth)
 * falls, and following a MIDI clock is keeping the position from running past
 * the clock's latest tick. The timer only plays notes and lets them go; MIDI
 * is queued, as everywhere (see MIDI OUTPUT QUEUE).
 *
 * The chord: acc_snapshot() records the chord as the player has just made it
 * (its voicing, root, bass and tones) whenever it is built; the rhythm plays
 * from its own copy, taken on the next note, the next beat or the next bar
 * (rhythm chord change). A chord pressed a moment after notes fell due -- the
 * player meant it for the beat and was late -- moves those notes to it
 * (acc_catch_late), as it would on an instrument a player strikes.
 *
 * Voice 0 is the bass part and voices 1-3 the chord part; either can play the
 * style, hold the chord as rhythm mode never could (a bass line walking under a
 * sustained choir), or rest. With rhythm follows hands, the parts sound only
 * while a chord is held, the clock running on underneath so they come back in
 * time, or with the bar starting from the press.
 */
#include "accompaniment_styles.h"
#include "drum_patterns.h"

#define ACC_NO_NOTE -32768

// The chord as the rhythm sees it, in steps of the live division
struct acc_chord_t {
  bool valid;
  int16_t v[4];        // the four chord voices' notes, lowest first
  int16_t root, bass;  // pitch classes
  int16_t third, fifth, seventh, second, fourth, sixth;   // above the root
};
acc_chord_t acc_latest = {};    // as the player last made it (from the loop, acc_snapshot)
acc_chord_t acc_now = {};       // as the rhythm plays it (taken from acc_latest as rhythm chord change says)
volatile uint16_t acc_latest_serial = 0;
uint16_t acc_serial_from_memory = 0xFFFF;   // the snapshot chord memory made, which the late catch leaves alone

double acc_pos = 0;                     // where the rhythm has got to, in 24ths of a beat since it began: every note before it has played
volatile double acc_pos_dropped = 0;    // what acc_pos has gone back to 0 from (a press restarting the bar, a MIDI Start) since chord memory last looked
volatile double acc_clock_pos = 0;      // under a MIDI clock, where its latest tick fell
volatile bool acc_clock_waiting = false;   // under a MIDI clock, waiting for the tick it starts on
volatile bool acc_clock_restart = false;   // ... which begins the pattern (Start), rather than the next beat
volatile bool acc_restart = false;      // the next millisecond begins the pattern over (rhythm follows hands 2)
volatile bool acc_hands = false;        // a chord is held (from the loop)
volatile uint32_t acc_latch_ms = 0;     // when the rhythm last took a new chord on the beat or the bar

// each voice's latest note of the part
struct acc_voice_t {
  bool off_pending;   // to let go at off_at
  uint32_t off_at;
  bool due;           // a note of the part fell due, at `at`: struck, or not while the hands were off
  bool struck;
  uint32_t at;
  uint8_t role, length, velocity;
  int8_t octave;
  int16_t note, before;   // the note it played and the one before, for approaches
};
acc_voice_t acc_voice[4];

// notes of a strum waiting their string's turn
struct acc_rolled_t {
  bool used;
  uint32_t at;
  uint8_t voice, role, length, velocity;
  int8_t octave;
};
#define ACC_ROLL_MAX 8
acc_rolled_t acc_rolled[ACC_ROLL_MAX];

// SECTIONS: what the rhythm plays, bar by bar. The main part is style A (rhythm style, 274) or B
// (314), from the bar line it came in on (acc_origin), so a two-bar pattern starts at its first bar.
// A fill plays the style's fill bar from where it was asked for up to a bar line (the next one, or
// the one after if less than a beat is left), then switches to the other style if there is a B; the
// intro is the fill as the first bar, and the ending a fill and then the chord struck on the bar
// line, ringing, before rhythm mode turns itself off. A break rests every part up to a bar line. B
// takes its turn only in A's metre, so bar lines stay where they were for everything that counts
// them (the clock out, chord memory, the looper).
volatile bool acc_main_b = false;       // style B is the main part
volatile double acc_origin = 0;         // the bar line the main part began on
volatile bool acc_fill_on = false;
volatile double acc_fill_from = 0, acc_fill_to = 0;
volatile bool acc_fill_switch = false;  // ...and the other style after it
volatile bool acc_intro = false;        // the fill playing is the intro
bool acc_pressed = false;               // a hand has been down since rhythm mode came on (rhythm follows hands)
volatile bool acc_ending = false;       // ...is the ending's
volatile bool acc_ending_done = false;  // the last chord is struck: the loop turns rhythm mode off
volatile double acc_rest_to = 0;        // a break: nothing plays before this
volatile double acc_countin_to = 0;     // the count-in's bar: sticks, and nothing else, before this

static inline uint8_t acc_style_a() { return acc_style < acc_style_count ? acc_style : 0; }
// B must count A's bar, unless a time signature sets the bar for both (METER)
static inline bool acc_b_valid() {
  return acc_style_b > 0 && acc_style_b < acc_style_count && (acc_meter || acc_styles[acc_style_b].beats_per_bar == acc_styles[acc_style_a()].beats_per_bar);
}
static inline uint8_t acc_style_index() { return acc_main_b && acc_b_valid() ? acc_style_b : acc_style_a(); }

//>>METER<<
// The time signature (393). Every style and groove is written in its own metre, and at 0 the style's
// sets the bar, as it always has. Set, it is the bar for everything that counts one: the parts, the
// drums, fills, breaks, the intro and ending, the count-in, the clock out's Start, chord memory and the
// looper. 1 to 12 are that many beats, the tempo's: 2 is 2/4 (or 6/8, with a triplet groove), 3 3/4
// (9/8), 4 4/4 (12/8). The rest are counted in eighths, half a beat each, grouped in twos and threes,
// a beat on each group: 13 5/8 as 2+3, 14 as 3+2, 15 7/8 as 2+2+3, 16 as 3+2+2, 17 9/8 as 2+2+2+3, 18
// 11/8 as 2+2+3+2+2, 19 8/8 as 3+3+2. A part written in another metre wraps to the bar: each bar starts
// its bar again (the next of them, in a part of several), cut off where the bar is shorter and from
// its own start again where it is longer; a fill is lined up to end on the bar line, its last part in
// a shorter bar, the groove up to it in a longer one. A tabla theka stretched by laya keeps its cycle.
struct acc_meter_t { uint8_t eighths; uint32_t beats; };   // beats: bit k, a beat begins on eighth k
static const acc_meter_t acc_odd_meters[] = {
  {5, 0b101}, {5, 0b1001}, {7, 0b10101}, {7, 0b101001}, {9, 0b1010101}, {11, 0b1010010101}, {8, 0b1001001},
};
#define ACC_METER_MAX (12 + sizeof(acc_odd_meters) / sizeof(acc_meter_t))
uint8_t acc_bar_eighths() {
  if (acc_meter > 12 && acc_meter <= ACC_METER_MAX) return acc_odd_meters[acc_meter - 13].eighths;
  return 2 * (acc_meter ? acc_meter : acc_styles[acc_style_index()].beats_per_bar);
}
uint32_t acc_bar_beats() {
  if (acc_meter > 12 && acc_meter <= ACC_METER_MAX) return acc_odd_meters[acc_meter - 13].beats;
  return 0x55555555;   // every other eighth
}
static inline double acc_bar_ticks() { return ACC_BEAT / 2 * acc_bar_eighths(); }
// the bar line a fill or a break runs to: the next, or the one after if less than a beat is left
static inline double acc_section_end(double pos) {
  double bar = acc_bar_ticks();
  double to = ceil(pos / bar) * bar;
  if (to - pos < ACC_BEAT) to += bar;
  return to;
}
static inline int16_t acc_pc(int16_t n) { return ((n % EDO) + EDO) % EDO; }
// the highest note of pitch class pc at or below ceiling
static inline int16_t acc_at_or_below(int16_t pc, int16_t ceiling) { return ceiling - acc_pc(ceiling - pc); }
static inline float acc_ms_per_tick() { return 60000.0f / (rythm_bpm * ACC_BEAT); }

// The voices the rhythm leaves to hold the chord as it is played outside rhythm mode
FLASHMEM uint8_t acc_held_mask() {
  if (acc_style_index() == 0) return 0;
  return (acc_bass_part == 1 ? 0x1 : 0) | (acc_chord_part == 1 ? 0xE : 0);
}

// The chord the player has just made, from the buttons or sent in (MIDI CHORDS)
FLASHMEM void acc_set_latest(const acc_chord_t &c) {
  noInterrupts();
  acc_latest = c;
  acc_latest_serial++;
  interrupts();
}

// From the loop, whenever a chord is built (build_chord_notes) or the division changes
FLASHMEM void acc_snapshot() {
  acc_chord_t c;
  c.valid = true;
  for (uint8_t i = 0; i < 4; i++) c.v[i] = current_chord_notes[i];
  for (uint8_t i = 1; i < 4; i++) {   // lowest first: inversion and spacing can reorder the voices
    for (uint8_t j = i; j > 0 && c.v[j] < c.v[j - 1]; j--) {
      int16_t t = c.v[j]; c.v[j] = c.v[j - 1]; c.v[j - 1] = t;
    }
  }
  // as calculate_note_chord() builds it
  int16_t sharp = chord_context_sharp ? (flat_button_modifier ? -modifier_step : modifier_step) : 0;
  const uint8_t *tones = *current_chord;
  c.root = acc_pc(get_root_button(key_signature_selection, chord_frame_shift, fundamental) + sharp + tones[0]);
  c.bass = chord_context_slashed ? acc_pc(get_root_button(key_signature_selection, chord_frame_shift, slash_value) + sharp) : c.root;
  // The chord's tones in its first four slots are not in order (a seventh chord keeps its fifth in
  // the fourth), so the fifth is whichever is nearest a perfect fifth and the seventh whichever is
  // above a major sixth; the second, fourth and sixth are the slots after them.
  const int16_t fifth = (7 * EDO + 6) / 12, sixth_and_more = (9 * EDO + 6) / 12;
  c.third = acc_pc(tones[1] - tones[0]);
  c.fifth = fifth;
  c.seventh = EDO;   // none: the octave
  int16_t nearest = 32767;
  for (uint8_t s = 1; s < 4; s++) {
    int16_t a = acc_pc(tones[s] - tones[0]);
    if (abs(a - fifth) < nearest) { nearest = abs(a - fifth); c.fifth = a; }
    if (a >= sixth_and_more) c.seventh = a;
  }
  c.second = acc_pc(tones[4] - tones[0]);
  c.fourth = acc_pc(tones[5] - tones[0]);
  c.sixth = acc_pc(tones[6] - tones[0]);
  acc_set_latest(c);
}

static inline void acc_take_chord() {
  if (acc_latest.valid) acc_now = acc_latest;
}

// The note a role plays in the chord the rhythm is playing; previous is the voice's last note
int16_t acc_note(uint8_t role, int8_t octave, int16_t previous) {
  const acc_chord_t &c = acc_now;
  int16_t root = acc_at_or_below(c.root, c.v[0]);
  int16_t n;
  switch (role) {
    case ACC_V1: case ACC_V2: case ACC_V3: case ACC_V4: n = c.v[role - ACC_V1]; break;
    case ACC_BASS: n = acc_at_or_below(c.bass, c.v[0]); break;
    case ACC_THIRD: n = root + c.third; break;
    case ACC_FIFTH: n = root + c.fifth; break;
    case ACC_SIXTH: n = root + c.sixth; break;
    case ACC_SEVENTH: n = root + c.seventh; break;
    case ACC_FLAT7: n = root + (10 * EDO + 6) / 12; break;
    case ACC_SECOND: n = root + c.second; break;
    case ACC_FOURTH: n = root + c.fourth; break;
    case ACC_APPROACH: {
      // a step from the next chord's bass (the one held, or waiting to come in) where that chord
      // will put it, from the side the voice is coming from: G to D flat to C, E to B to C
      const acc_chord_t &next = acc_latest.valid ? acc_latest : c;
      int16_t target = acc_at_or_below(next.bass, next.v[0]) + octave * EDO;
      int16_t step = max(1, (EDO + 6) / 12);
      return previous != ACC_NO_NOTE && previous > target ? target + step : target - step;
    }
    default: n = root; break;   // ACC_ROOT
  }
  return n + octave * EDO;
}

// A note of a style's part on voice v, now
void acc_strike(uint8_t v, uint8_t role, int8_t octave, uint8_t length, uint8_t velocity) {
  acc_voice_t &a = acc_voice[v];
  int16_t note = acc_note(role, octave, a.note);
  float firmness = (127 - (127 - velocity) * acc_accents / 100.0f) / 127.0f;
  acc_play(v, note, firmness);
  a.before = a.note;
  a.note = note;
  a.struck = true;
  a.off_pending = length != 0;   // 0 rings until the voice plays again
  if (a.off_pending) a.off_at = millis() + (uint32_t)(length * acc_articulation / 100.0f * acc_ms_per_tick());
}

// A note of a style falls due
void acc_event(const acc_event_t &ev) {
  uint8_t v = ev.voice;
  if ((v == 0 ? acc_bass_part : acc_chord_part) != 0) return;   // holding the chord, or resting
  if (acc_change == 0) acc_take_chord();
  if (!acc_now.valid) return;   // no chord played yet
  acc_voice_t &a = acc_voice[v];
  a.due = true;
  a.struck = false;
  a.at = millis();
  a.role = ev.role; a.octave = ev.octave; a.length = ev.length; a.velocity = ev.velocity;
  if (acc_follow && !acc_hands) return;
  if (ev.roll == 0) {
    acc_strike(v, ev.role, ev.octave, ev.length, ev.velocity);
    return;
  }
  // a later string of a strum: inter-string delay a string, the whole strum within a quarter beat.
  // It counts as played (acc_catch_late moves it, it doesn't play it again).
  a.struck = true;
  float string_ms = fminf(inter_string_delay / 1000.0f, 15000.0f / rythm_bpm / 3);
  for (uint8_t r = 0; r < ACC_ROLL_MAX; r++) {
    if (acc_rolled[r].used) continue;
    acc_rolled[r] = acc_rolled_t{true, millis() + (uint32_t)lroundf(ev.roll * string_ms), v, ev.role, ev.length, ev.velocity, ev.octave};
    return;
  }
  acc_strike(v, ev.role, ev.octave, ev.length, ev.velocity);   // no room to wait: with the rest
}

// A step of rhythm style 0, the sixteen steps as they always played: every chord note the step's
// bits name, the fourth to seventh on voices 1 to 3, for note pushed duration
void acc_step(uint8_t step) {
  rythm_current_step = step;
  if (step % rythm_limit_change_to_every == 0) {
    for (int i = 0; i < 7; i++) {
      rythm_freeze_current_chord_notes[i] = current_applied_chord_notes[i];
    }
  }
  // handling the led pattern
  uint8_t active_modulus = 1;
  uint8_t possible_pattern[2] = {3, 2};
  for (uint8_t i = 0; i < sizeof(possible_pattern) / sizeof(uint8_t); i++) {
    if (rythm_loop_length % possible_pattern[i] == 0) {
      active_modulus = possible_pattern[i];
      break;
    }
  }
  analogWrite(RYTHM_LED_PIN, (220 * (step % rythm_limit_change_to_every == 0) + 15) * (step % active_modulus == 0));
  rythm_led_lit_at = millis();   // off 200 ms on (rythm_led_settle)
  rythm_led_lit = true;
  if (acc_follow && !acc_hands) return;
  u_int8_t result = rythm_pattern[step];
  for (int i = 6; i >= 0; i--) {
    if (result & (1 << i)) {
      uint8_t v = i < 4 ? i : i - 3;
      acc_play(v, rythm_freeze_current_chord_notes[i], 1);
      acc_voice[v].off_pending = true;
      acc_voice[v].off_at = millis() + note_pushed_duration;
    }
  }
}

// A beat begins, the bar's first or not
void acc_beat(bool bar) {
  rythm_beat_ms = millis();   // for the looper
  const acc_style_t &st = acc_styles[acc_style_index()];
  rythm_beat_bar = bar;
  rythm_beat_count = rythm_beat_count + 1;
  if (st.count == 0) return;   // the sixteen steps light the LED themselves
  if (acc_change == 2 ? bar : acc_change == 1) {
    acc_take_chord();
    acc_latch_ms = millis();
  }
  analogWrite(RYTHM_LED_PIN, bar ? 235 : 15);
  rythm_led_lit_at = millis();
  rythm_led_lit = true;
}

// Where a note written at t (24ths of a beat from the start) plays: the second of each pair of
// swing units moves by the shuffle, later above 1 and earlier below, as the old steps did
static inline double acc_swung(int64_t t, uint8_t unit) {
  if (!unit) return t;   // a triplet groove: nothing to swing
  if (t % unit == 0 && (t / unit) % 2 == 1) return t + (shuffle - 1) * unit;
  return t;
}

// A part written in bars of own ticks, length long, as it falls from t0 to t1 in the time signature's
// bars from origin (METER): each bar starts the part's bar again, the next of them in a part of
// several, cut off where the bar is shorter and from the part's own start again where it is longer.
// The events are stride bytes apart, each beginning with its at; play plays one.
typedef void (*acc_play_t)(const void *ev);
FLASHMEM static void acc_wrapped_window(const void *events, size_t stride, uint16_t count, uint16_t length, uint16_t own, uint8_t swing_unit, double origin, double t0, double t1, acc_play_t play) {
  int64_t bar = (int64_t)acc_bar_ticks();
  uint16_t piece = length > own && length % own == 0 ? own : length;   // one of its bars, or the whole of a shorter part
  if (!piece || !bar) return;
  uint16_t pieces = length / piece;
  int64_t first = (int64_t)floor((t0 - origin) / bar) - 1, last = (int64_t)floor((t1 - origin) / bar);
  if (first < 0) first = 0;
  for (int64_t i = first; i <= last; i++) {
    double start = origin + i * bar;
    uint16_t from = (i % pieces) * piece;
    for (uint16_t e = 0; e < count; e++) {
      const void *ev = (const uint8_t *)events + e * stride;
      uint16_t at = *(const uint16_t *)ev;
      if (at < from || at >= from + piece) continue;
      for (int64_t p = at - from; p < bar; p += piece) {
        double t = start + acc_swung(p, swing_unit);
        if (t >= t0 && t < t1) play(ev);
      }
    }
  }
}

// A fill written in a bar of own ticks, lined up to end on the fill's bar line: a shorter bar has its
// last part (and a fill asked for late in the bar before gets the whole of it)
FLASHMEM static void acc_wrapped_fill(const void *events, size_t stride, uint16_t count, uint16_t own, uint8_t swing_unit, double t0, double t1, acc_play_t play) {
  double from = acc_fill_to - own;
  for (uint16_t e = 0; e < count; e++) {
    const void *ev = (const uint8_t *)events + e * stride;
    double t = from + acc_swung(*(const uint16_t *)ev, swing_unit);
    if (t >= t0 && t < t1 && t >= acc_fill_from) play(ev);
  }
}

static void acc_play_event(const void *ev) { acc_event(*(const acc_event_t *)ev); }

//>>DRUMS<<
// Rhythm mode's drums: a synthesized kit (include/drum_machine.h) playing a groove (drum_patterns.h, from
// generator/drums.py) on the rhythm's own clock, so everything that moves the rhythm moves them: tap
// tempo, a MIDI clock, the phone's beat, the shuffle. The groove is the rhythm style's own drum part, or
// one chosen (335), and style B's or another one (336) after a fill; the kit is the groove's, or one
// chosen (337). They ride the sections: the groove's own fill bar in a fill, the intro and the ending's
// lead-in, silence in a break, a crash on the bar line a fill lands on and the ending's last hit. Each hit
// takes the rhythm's accents, a humanize (late by up to 15 ms, softer by up to 20%) and, on a kit whose
// kick follows, the chord's root as its pitch (an 808 bass); and goes out on channel 10 if asked
// (General MIDI's drum map). The harp's strings can play the kit too (drum_pad).
struct drum_off_t { uint8_t note, port; uint32_t at; };
static drum_off_t drum_offs[32];
static volatile uint8_t drum_off_head = 0, drum_off_tail = 0;
static uint32_t drum_random = 0x9E3779B9;
static inline uint32_t drum_rand() { drum_random ^= drum_random << 13; drum_random ^= drum_random >> 17; drum_random ^= drum_random << 5; return drum_random; }

static const drum_groove_t *drum_groove_playing() {
  bool b = acc_main_b && acc_b_valid();
  uint8_t g;
  if (b && drum_groove_b) g = drum_groove_b - 1;
  else if (drum_groove) g = drum_groove - 1;
  else g = style_drum_groove[b ? acc_style_b : acc_style_a()];
  return &drum_grooves[min(g, (uint8_t)(drum_groove_count - 1))];
}

static const drum_kit_t *drum_kit_wanted() {
  uint8_t k = drum_kit ? drum_kit - 1 : drum_groove_playing()->kit;
  return &drum_kits[min(k, (uint8_t)(drum_kit_count - 1))];
}

// A tabla's dayan is tuned to Sa, the key: the chord key signature's root and the transpose, the Sa
// the drone and the harp's scales take, so dayan and tanpura agree in any key. A dayan only
// stretches a couple of semitones from the pitch its size and skin give it, so where Sa is further than
// that from the one recorded, it's tuned to Pa, the fifth, or Ma, the fourth, as players do: in twelve
// notes that keeps every key within two semitones of the recording, where the strokes still sound
// themselves. own_hz is the pitch the stroke was recorded at; the answer is in whichever octave is
// nearest it.
static float dayan_tuning(float own_hz) {
  if (own_hz <= 0) return 0;
  const int16_t fifth = (int16_t)lroundf(EDO * 0.5849625f);   // log2(3/2) of an octave, in steps
  const int16_t sa = scale_root_offsets[key_signature_selection] + transpose_steps;
  const int16_t choices[3] = {sa, (int16_t)(sa + fifth), (int16_t)(sa + EDO - fifth)};
  float best = 0, best_cents = 1e9f;
  for (uint8_t i = 0; i < 3; i++) {
    float hz = c_frequency * 2 * tempered_ratio(choices[i]);   // from C4, as the drone's (purity has no chord to pull it to)
    while (hz > own_hz * 1.4142f) hz *= 0.5f;
    while (hz < own_hz * 0.7071f) hz *= 2.0f;
    float cents = fabsf(1200.0f * log2f(hz / own_hz));
    if (cents <= 250) return hz;   // Sa first, then Pa, then Ma
    if (cents < best_cents) { best = hz; best_cents = cents; }
  }
  return best;
}

// ---- DRONE ----
// A tanpura or a shruti box on the key's Sa, under everything, rhythm mode or not (372-375, and the
// jawari, 381; the sound is include/tanpura.h, rendered with the drums). Sa is the root the harp's scales take from
// the key, the chord key signature's, and the transpose, at its tempered pitch (purity leaves it
// alone, it has no chord), folded into the register chosen; the strings above and below it are
// pure, as a tanpura is tuned. The dayan takes the same Sa (dayan_tuning).
uint8_t drone_mode = 0;       // 0 off, 1-4 a tanpura with Pa, Ma, Ni or Sa first, 5 a shruti box
uint8_t drone_register = 1;   // Sa from 0 E2, 1 A2, 2 D3, the octave up from there
float drone_sa_hz() {
  static const float lowest[3] = {82.41f, 110.0f, 146.83f};
  float lo = lowest[drone_register < 3 ? drone_register : 1];
  float hz = c_frequency * tempered_ratio(scale_root_offsets[key_signature_selection] + transpose_steps);
  while (hz >= 2 * lo) hz *= 0.5f;
  while (hz < lo) hz *= 2.0f;
  return hz;
}

FLASHMEM void drone_set_mode(uint8_t m) {
  drone_mode = m;
  AudioNoInterrupts();
  drum_machine.drone.tune(drone_sa_hz());
  drum_machine.drone.mode(m);
  AudioInterrupts();
}

// From the loop: Sa follows the transpose, the temperament and the master tuning as they move.
void drone_watch() {
  static uint32_t last = 0;
  static float tuned = 0;
  if (!drone_mode || millis() - last < 50) return;
  last = millis();
  float hz = drone_sa_hz();
  if (fabsf(hz - tuned) < 0.005f) return;
  tuned = hz;
  AudioNoInterrupts();
  drum_machine.drone.tune(hz);
  AudioInterrupts();
}

// A hit, from the rhythm's timer or the loop: velocity 1-127 as written (or played); loose, it takes
// the humanize (a looped hit was already played by a human)
void drum_strike(uint8_t v, uint8_t velocity, bool written, bool loose) {
  float vel = written ? (127 - (127 - velocity) * acc_accents / 100.0f) / 127.0f : velocity / 127.0f;
  uint32_t delay = 0;
  if (drum_humanize && loose) {
    delay = drum_rand() % (uint32_t)(drum_humanize * 0.15f * AUDIO_SAMPLE_RATE_EXACT / 1000.0f + 1);
    vel *= 1.0f - (drum_rand() % 1000) / 1000.0f * drum_humanize * 0.002f;
  }
  float pitch = 0;
  const drum_kit_t *kit = drum_machine.currentKit();
  if (kit && kit->v[v].follow && kit->v[v].sample >= 0) {
    pitch = dayan_tuning(drum_samples[kit->v[v].sample].pitch_hz);
  } else if (kit && kit->v[v].follow && acc_now.valid) {
    int16_t pc = ((acc_now.root + transpose_steps) % EDO + EDO) % EDO;
    pitch = c_frequency / 4 * temper_ratio(pc);   // C1 to B1: an 808's bass
  }
  drum_machine.hit(v, vel, pitch, delay);
  if (drum_midi) {
    uint8_t port = drum_midi == 2 ? 1 : 0;
    uint8_t note = drum_gm_note[v];
    queue_midi(true, note, (uint8_t)constrain(lroundf(vel * 127), 1, 127), 10, port);
    uint32_t primask = drum_primask();
    __disable_irq();
    uint8_t next = (drum_off_head + 1) % 32;
    if (next != drum_off_tail) { drum_offs[drum_off_head] = {note, port, millis() + 40}; drum_off_head = next; }
    if (!primask) __enable_irq();
  }
}

// The count-in: a stick on each beat of its bar, the first louder
static void drum_countin_window(double t0, double t1) {
  uint8_t eighths = acc_bar_eighths();
  uint32_t beats = acc_bar_beats();
  for (int64_t k = (int64_t)ceil(t0 / (ACC_BEAT / 2)); k * (ACC_BEAT / 2) < t1 && k * (ACC_BEAT / 2) < acc_countin_to; k++) {
    uint8_t e = k % eighths;
    if (beats >> e & 1) drum_strike(DRUM_RIM, e == 0 ? 124 : 90, false);
  }
}

// Tabla laya (377): a theka is written a bol a step, four (teental) or two (kehrwa, dadra) to the
// beat, which puts medium teental at 30 bpm and slow (vilambit) below the tempo's floor. Stretched,
// the tempo counts bols instead: one a beat (teental's sixteen matras over four bars, kehrwa's eight
// over two), two, or four. How many times longer than written the groove plays: 1 for the rest.
static double drum_stretch(const drum_groove_t &g) {
  if (!g.bol_steps || !tabla_laya) return 1;
  static const uint8_t per_beat[4] = {0, 1, 2, 4};
  return (double)g.bol_steps / per_beat[min(tabla_laya, (uint8_t)3)];
}

static void drum_play_event(const void *ev) {
  const drum_event_t &d = *(const drum_event_t *)ev;
  drum_strike(d.voice, d.velocity);
}

// The groove's own bars from t0 to t1, or wrapped to the time signature's (METER). A theka stretched
// over several bars starts again (sam) on the bar line its last fill, a tihai, landed on, as a tabla
// player's does.
static void drum_groove_window(const drum_groove_t &g, double stretch, double t0, double t1) {
  if (stretch == 1 && g.beats_per_bar * ACC_BEAT != acc_bar_ticks()) {   // another metre: wrapped to the bar
    acc_wrapped_window(g.events, sizeof(drum_event_t), g.count, g.length, g.beats_per_bar * ACC_BEAT, g.swing_unit, acc_origin, t0, t1, drum_play_event);
    return;
  }
  if (stretch == 1) {
    int64_t origin = (int64_t)acc_origin;
    for (uint16_t e = 0; e < g.count; e++) {
      const drum_event_t &ev = g.events[e];
      int64_t n = (int64_t)floor((t0 - origin - ev.at) / g.length);
      for (int64_t m = n; m <= n + 1; m++) {
        int64_t t = origin + ev.at + m * g.length;
        if (t < origin) continue;
        double at = acc_swung(t, g.swing_unit);
        if (at >= t0 && at < t1) drum_strike(ev.voice, ev.velocity);
      }
    }
    return;
  }
  double origin = drum_landed >= acc_origin && drum_landed <= t0 ? drum_landed : acc_origin;
  double length = g.length * stretch;
  for (uint16_t e = 0; e < g.count; e++) {
    const drum_event_t &ev = g.events[e];
    double from = ev.at * stretch;
    int64_t n = (int64_t)floor((t0 - origin - from) / length);
    for (int64_t m = n; m <= n + 1; m++) {
      double t = origin + from + m * length;
      if (t < origin) continue;
      double at = acc_swung(llround(t), g.swing_unit);
      if (at >= t0 && at < t1) drum_strike(ev.voice, ev.velocity);
    }
  }
}

// What the drums play from t0 to t1, within one section, as acc_section_window has it
static void drum_section_window(double t0, double t1) {
  if (!drums_on || (acc_follow && !acc_hands)) return;
  const drum_groove_t &g = *drum_groove_playing();
  double stretch = drum_stretch(g);
  if (acc_fill_on && t0 >= acc_fill_from && t0 < acc_fill_to) {
    uint16_t own = g.beats_per_bar * ACC_BEAT;
    if (stretch == 1 && own != acc_bar_ticks()) {   // another metre: the groove up to the fill, the fill into the bar line
      double from = acc_fill_to - own;
      if (t0 < from) drum_groove_window(g, 1, t0, min(t1, from));
      acc_wrapped_fill(g.fill, sizeof(drum_event_t), g.fill_count, own, g.swing_unit, t0, t1, drum_play_event);
      return;
    }
    double bar = acc_fill_to - acc_bar_ticks();
    own = 0;   // stretched: the theka plays on up to the fill's half, then the fill's bols as written
    if (stretch != 1) {
      own = (g.beats_per_bar - max(g.beats_per_bar / 2, 1)) * ACC_BEAT;
      if (t0 < bar + own) drum_groove_window(g, stretch, t0, min(t1, bar + own));
    }
    for (uint16_t e = 0; e < g.fill_count; e++) {
      const drum_event_t &ev = g.fill[e];
      if (ev.at < own) continue;
      double at = acc_swung((int64_t)bar + ev.at, g.swing_unit);
      if (at >= t0 && at < t1 && at >= acc_fill_from) drum_strike(ev.voice, ev.velocity);
    }
    return;
  }
  drum_groove_window(g, stretch, t0, t1);
}

// The harp's strings as the kit's first twelve voices, low string the kick, as firmly as touched;
// the looper records the hit
void drum_pad(uint8_t string, int16_t strength) {
  float firm = strength > 0 ? touch_firmness(strength) : 1.0f;
  uint8_t velocity = (uint8_t)lroundf(127 * (0.35f + 0.65f * firm));
  drum_strike(string, velocity, false);
  looper_capture(LOOP_DRUM, string, 0, 0, 0, velocity);
}

// From the loop: the kit the groove (or the setting) wants, and the MIDI notes to end
FLASHMEM void drums_update() {
  const drum_kit_t *want = drum_kit_wanted();
  if (want != drum_machine.currentKit()) drum_machine.setKit(want);
  uint32_t now = millis();
  while (drum_off_tail != drum_off_head && (int32_t)(now - drum_offs[drum_off_tail].at) >= 0) {
    queue_midi(false, drum_offs[drum_off_tail].note, 0, 10, drum_offs[drum_off_tail].port);
    drum_off_tail = (drum_off_tail + 1) % 32;
  }
}

FLASHMEM void drums_set() {
  drum_machine.level(drum_level / 100.0f);
}

// The fill's bar line reached: the other style from here, or the ending's last chord
static bool acc_intro_was = false;   // the fill finishing was the intro: no crash to land it, the groove just begins
static void acc_fill_finish() {
  acc_intro_was = acc_intro;
  acc_fill_on = false;
  acc_intro = false;
  if (drums_on && !(acc_follow && !acc_hands) && !acc_intro_was) {   // the bar line a fill lands on: a crash and the kick
    drum_strike(DRUM_CYMBAL, 118);
    drum_strike(DRUM_KICK, 118);
  }
  drum_landed = acc_fill_to;   // sam, for a stretched theka
  if (acc_ending) {
    // the chord on the bar line, every part that plays struck together and left ringing
    static const acc_event_t last[4] = {{0, 0, ACC_ROOT, 0, 0, 112, 0}, {0, 1, ACC_V2, 0, 0, 100, 0}, {0, 2, ACC_V3, 0, 0, 100, 0}, {0, 3, ACC_V4, 0, 0, 100, 0}};
    if (!(drums_on && drums_only)) for (uint8_t v = 0; v < 4; v++) acc_event(last[v]);
    acc_rest_to = 1e15;   // and nothing after it
    acc_ending_done = true;
    return;
  }
  if (acc_fill_switch) {
    acc_main_b = !acc_main_b;
    acc_origin = acc_fill_to;
  }
}

static void acc_part_window(const acc_style_t &st, double t0, double t1);

// What plays from t0 up to t1, all within one section (acc_window cuts at their edges)
static void acc_section_window(double t0, double t1) {
  if (t0 < acc_countin_to) { drum_countin_window(t0, t1); return; }
  if (t0 < acc_rest_to) return;
  drum_section_window(t0, t1);
  if (drums_on && drums_only) return;   // the accompaniment rests
  const acc_style_t &st = acc_styles[acc_style_index()];
  if (acc_fill_on && t0 >= acc_fill_from && t0 < acc_fill_to) {
    uint16_t own = st.beats_per_bar * ACC_BEAT;
    if (own != acc_bar_ticks()) {   // another metre: the part up to the fill, the fill into the bar line
      double from = acc_fill_to - own;
      if (t0 < from) acc_part_window(st, t0, min(t1, from));
      acc_wrapped_fill(st.fill, sizeof(acc_event_t), st.fill_count, own, st.swing_unit, t0, t1, acc_play_event);
      return;
    }
    double bar = acc_fill_to - acc_bar_ticks();   // the fill's bar begins here
    for (uint8_t e = 0; e < st.fill_count; e++) {
      const acc_event_t &ev = st.fill[e];
      double at = acc_swung((int64_t)bar + ev.at, st.swing_unit);
      if (at >= t0 && at < t1 && at >= acc_fill_from) acc_event(ev);
    }
    return;
  }
  acc_part_window(st, t0, t1);
}

// The style's part from t0 to t1, outside a fill
static void acc_part_window(const acc_style_t &st, double t0, double t1) {
  int64_t origin = (int64_t)acc_origin;
  uint16_t own = st.beats_per_bar * ACC_BEAT;
  int64_t bar = (int64_t)acc_bar_ticks();
  if (st.count == 0) {
    for (int64_t k = (int64_t)floor(t0 / 12) - 1; k <= (int64_t)floor(t1 / 12) + 1; k++) {
      if (k * 12 < origin) continue;
      if (own == bar) {
        double at = acc_swung(k * 12, 12);
        if (at >= t0 && at < t1) acc_step((k - origin / 12) % rythm_loop_length);
        continue;
      }
      // another metre (METER): the sixteen steps are two of the style's bars, each bar starts the next
      int64_t i = (k * 12 - origin) / bar, p = (k * 12 - origin) % bar;
      uint8_t pieces = max(1, rythm_loop_length * 12 / own);
      double at = origin + i * bar + acc_swung(p, 12);
      if (at >= t0 && at < t1) acc_step(((i % pieces) * own + p % own) / 12 % rythm_loop_length);
    }
    return;
  }
  if (own != bar) {
    acc_wrapped_window(st.events, sizeof(acc_event_t), st.count, st.length, own, st.swing_unit, origin, t0, t1, acc_play_event);
    return;
  }
  for (uint8_t e = 0; e < st.count; e++) {
    const acc_event_t &ev = st.events[e];
    int64_t n = (int64_t)floor((t0 - origin - ev.at) / st.length);
    for (int64_t m = n; m <= n + 1; m++) {
      int64_t t = origin + ev.at + m * st.length;
      if (t < origin) continue;
      double at = acc_swung(t, st.swing_unit);
      if (at >= t0 && at < t1) acc_event(ev);
    }
  }
}

// Everything that falls from t0 up to t1
void acc_window(double t0, double t1) {
  // beats first, so a chord coming in on the beat does before the beat's notes: on the eighths the
  // bar's beats begin on (METER), bars counted from the start
  uint8_t eighths = acc_bar_eighths();
  uint32_t beats = acc_bar_beats();
  for (int64_t k = (int64_t)ceil(t0 / (ACC_BEAT / 2)); k * (ACC_BEAT / 2) < t1; k++) {
    uint8_t e = k % eighths;
    if (beats >> e & 1) acc_beat(e == 0);
  }
  // cut where a section begins or ends inside the window, and move on at a fill's bar line
  double s = t0;
  while (s < t1) {
    if (acc_fill_on && s >= acc_fill_to) acc_fill_finish();
    double e = t1;
    double edges[4] = {acc_rest_to, acc_fill_on ? acc_fill_from : -1.0, acc_fill_on ? acc_fill_to : -1.0, acc_countin_to};
    for (uint8_t k = 0; k < 4; k++) if (edges[k] > s && edges[k] < e) e = edges[k];
    acc_section_window(s, e);
    s = e;
  }
  if (acc_fill_on && t1 >= acc_fill_to) acc_fill_finish();
}

// MIDI clock out (266): the rhythm's clock on to whatever is plugged in, so a drum machine or a
// computer plays in time with the minichord. A tick goes out each time the rhythm passes a 24th
// of a beat (acc_tick, from the same clock that plays the notes, so they stay together); a Start
// comes just before the tick that begins a bar, so what follows starts its bar with ours; a Stop
// when rhythm mode ends, the setting changes, or the pattern starts over from a press (and a Start
// on its first tick). Nothing goes out while another clock is coming in, or has in the last
// second: the minichord follows that one instead (see the clock in processMIDI), and two clocks
// would fight.
static inline bool midi_clock_out_free() {
  return midi_clock_out && !rythm_clock_synced && !(midi_clock_heard && millis() - midi_clock_heard_ms < 1000);
}

static void midi_clock_send(uint8_t type, uint8_t ports) {
  for (uint8_t cable = 0; cable < 2; cable++) {
    if (ports & (1 << cable)) queue_midi_event(MIDI_EVT_REALTIME, type, 0, 0, 0, cable);
  }
}

// a Stop to the ports a Start went to; from the timer, or the loop with it held off
static void midi_clock_stop() {
  if (!midi_clock_started) return;
  midi_clock_send(usbMIDI.Stop, midi_clock_started);
  midi_clock_started = 0;
}

// the ticks from t0 up to t1, before the notes there so a receiver has its tick when they arrive
static void midi_clock_window(double t0, double t1) {
  if (!midi_clock_out_free()) {
    midi_clock_stop();
    return;
  }
  uint16_t bar = (uint16_t)acc_bar_ticks();
  for (int64_t t = (int64_t)ceil(t0); t < t1; t++) {
    if (!midi_clock_started && t % bar == 0) {
      midi_clock_started = midi_clock_out & 3;
      midi_clock_send(usbMIDI.Start, midi_clock_started);
    }
    midi_clock_send(usbMIDI.Clock, midi_clock_out & 3);
  }
}

// The setting changed: a Stop where the clock was going; it starts again on the next bar
FLASHMEM void acc_clock_out_changed() {
  noInterrupts();
  midi_clock_stop();
  interrupts();
}

// The rhythm timer, every millisecond
void acc_tick() {
  uint32_t now = millis();
  for (uint8_t v = 0; v < 4; v++) {
    if (acc_voice[v].off_pending && (int32_t)(now - acc_voice[v].off_at) >= 0) {
      acc_voice[v].off_pending = false;
      acc_release(v);
    }
  }
  for (uint8_t r = 0; r < ACC_ROLL_MAX; r++) {
    acc_rolled_t &n = acc_rolled[r];
    if (!n.used || (int32_t)(now - n.at) < 0) continue;
    n.used = false;
    if (!acc_follow || acc_hands) acc_strike(n.voice, n.role, n.octave, n.length, n.velocity);
  }
  if (acc_restart) {
    acc_restart = false;
    acc_pos_dropped += acc_pos;
    acc_pos = 0;
    acc_phase_error = 0;
    // the pattern from its start on the press: the intro if it hasn't finished, else the main part
    acc_origin = acc_intro ? acc_bar_ticks() : 0;
    drum_landed = -1;
    if (acc_intro) { acc_fill_from = 0; acc_fill_to = acc_origin; }
    else if (!acc_ending) acc_fill_on = false;
    acc_rest_to = 0;
    acc_countin_to = 0;   // the press is the count
    midi_clock_stop();   // and a Start on the tick that begins it again
  }
  double step = rythm_bpm * clock_bend_ratio * ACC_BEAT / 60000.0;   // bent by clock bend (436), 1 unbent
  if (acc_phase_error != 0) {   // toward a tap: up to 30% quicker or slower until there (acc_tap)
    double c = acc_phase_error;
    if (c > 0.3 * step) c = 0.3 * step;
    if (c < -0.3 * step) c = -0.3 * step;
    step += c;
    acc_phase_error = acc_phase_error - c;
  }
  double t1 = acc_pos + step;
  if (rythm_clock_synced) {
    if (acc_clock_waiting) return;
    // as far as the clock's next tick and no further, and at least to its latest
    if (t1 > acc_clock_pos + 1) t1 = acc_clock_pos + 1;
    if (t1 <= acc_clock_pos) t1 = acc_clock_pos + 1e-6;
  }
  if (t1 <= acc_pos) return;
  midi_clock_window(acc_pos, t1);
  acc_window(acc_pos, t1);
  acc_pos = t1;
}

// Rhythm mode comes on
FLASHMEM void acc_begin() {
  noInterrupts();
  rythm_clock_synced = false;
  acc_clock_waiting = false;
  acc_restart = false;
  acc_pos = 0;
  acc_phase_error = 0;
  rythm_current_step = 0;
  acc_tap_reset();
  for (uint8_t v = 0; v < 4; v++) acc_voice[v] = acc_voice_t{false, 0, false, false, 0, 0, 0, 0, 0, ACC_NO_NOTE, ACC_NO_NOTE};
  for (uint8_t r = 0; r < ACC_ROLL_MAX; r++) acc_rolled[r].used = false;
  acc_now = acc_latest;
  acc_hands = current_line >= 0;
  acc_pressed = acc_hands;
  // style A, or the intro's bar first (the style's fill, as a pickup into it)
  acc_main_b = false;
  acc_ending = acc_ending_done = false;
  acc_rest_to = 0;
  acc_intro = acc_fill_on = rhythm_intro;
  acc_fill_switch = false;
  acc_countin_to = count_in ? acc_bar_ticks() : 0;   // a bar of sticks first, if asked
  acc_fill_from = acc_countin_to;
  acc_fill_to = acc_origin = acc_countin_to + (rhythm_intro ? acc_bar_ticks() : 0);
  drum_landed = -1;
  interrupts();
  stop_chord_voices(0xF & ~acc_held_mask());   // what was ringing gives way to the parts
  Serial.print("Rhythm style: ");
  Serial.println(acc_styles[acc_style_index()].name);
  rythm_timer.priority(254);
  rythm_timer.begin(acc_tick, 1000);
  rythm_timer_running = true;
}

// and goes off
FLASHMEM void acc_end() {
  midi_chords_clear();
  rythm_timer.end();
  rythm_timer_running = false;
  rythm_clock_synced = false;
  midi_clock_stop();
  for (uint8_t v = 0; v < 4; v++) acc_voice[v].off_pending = false;
  for (uint8_t r = 0; r < ACC_ROLL_MAX; r++) acc_rolled[r].used = false;
  // voices the rhythm played softer are back to full for the chords (play_single_note does the
  // same for one still ringing out, when it next plays)
  for (uint8_t v = 0; v < 4; v++) {
    noInterrupts();
    if (!midi_in_chord[v].owned && !chord_envelope_array[v]->isActive() && chord_voice_level[v] != 1) {
      chord_voice_level[v] = 1;
      apply_chord_voice_level(v);
    }
    interrupts();
  }
}

// A fill (or the ending's) from now to a bar line. Asked for again while one plays, a fill is
// already coming; the ending asked for during a fill makes that fill the ending's, and asked for
// again ends rhythm mode at once.
FLASHMEM void acc_fill_request(bool ending) {
  if (!rythm_mode) return;
  noInterrupts();
  if (ending && acc_ending) {
    interrupts();
    set_rythm_mode(false);
    return;
  }
  if (acc_ending || (acc_fill_on && !ending)) {
    interrupts();
    return;
  }
  if (!acc_fill_on) {
    acc_fill_from = acc_pos;
    acc_fill_to = acc_section_end(acc_pos);
  }
  acc_fill_on = true;
  acc_intro = false;
  acc_fill_switch = !ending && acc_b_valid();
  acc_ending = ending;
  acc_rest_to = 0;   // a fill ends a break
  interrupts();
  Serial.println(ending ? "Rhythm: ending" : "Rhythm: fill");
}

// A break: every part rests up to a bar line, from now; a fill coming is dropped with it
FLASHMEM void acc_break_request() {
  if (!rythm_mode) return;
  noInterrupts();
  if (acc_ending) {
    interrupts();
    return;
  }
  acc_rest_to = acc_section_end(acc_pos);
  acc_fill_on = false;
  acc_intro = false;
  for (uint8_t v = 0; v < 4; v++) acc_voice[v].off_pending = false;
  for (uint8_t r = 0; r < ACC_ROLL_MAX; r++) acc_rolled[r].used = false;
  interrupts();
  stop_chord_voices(0xF & ~acc_held_mask());
  Serial.println("Rhythm: break");
}

// The action (315), never kept: 1 a fill, 2 a break, 3 the ending
FLASHMEM void rhythm_fill_action(int16_t action) {
  current_sysex_parameters[rhythm_fill_adress] = 0;
  if (action == 1) acc_fill_request(false);
  else if (action == 2) acc_break_request();
  else if (action == 3) acc_fill_request(true);
}

// A style, part or follow setting changed: the voices the rhythm now plays start from their part
FLASHMEM void acc_settings_changed() {
  if (!rythm_mode) return;
  noInterrupts();
  for (uint8_t v = 0; v < 4; v++) acc_voice[v].off_pending = false;
  for (uint8_t r = 0; r < ACC_ROLL_MAX; r++) acc_rolled[r].used = false;
  interrupts();
  stop_chord_voices(0xF & ~acc_held_mask());
}

// The time signature changed: the parts go on from the bar line just passed, counted in the new bars
// as the beats are, so a part's bar starts where the bar does
FLASHMEM void acc_meter_changed() {
  if (!rythm_mode) return;
  noInterrupts();
  double bar = acc_bar_ticks();
  if (acc_origin <= acc_pos) acc_origin = floor(acc_pos / bar) * bar;
  interrupts();
  acc_settings_changed();
}

// MIDI Start: the pattern begins on the next clock, as MIDI has it
FLASHMEM void acc_midi_start() {
  noInterrupts();
  rythm_clock_synced = true;
  acc_clock_waiting = true;
  acc_clock_restart = true;
  interrupts();
}

// MIDI Stop: on from where it is, at the minichord's own tempo
FLASHMEM void acc_midi_stop() {
  rythm_clock_synced = false;
}

// A MIDI clock tick
FLASHMEM void acc_midi_clock() {
  noInterrupts();
  if (!rythm_clock_synced) {   // a clock already running when rhythm mode came on: a beat starts on this tick
    rythm_clock_synced = true;
    acc_clock_waiting = true;
    acc_clock_restart = false;
  }
  acc_phase_error = 0;
  if (acc_clock_waiting) {
    acc_clock_waiting = false;
    if (acc_clock_restart) acc_pos_dropped += acc_pos;
    acc_pos = acc_clock_restart ? 0 : ceil(acc_pos / ACC_BEAT) * ACC_BEAT;
    if (acc_clock_restart) {   // the pattern from its start (the intro's bar first if it is still to come)
      acc_origin = acc_intro ? acc_bar_ticks() : 0;
      drum_landed = -1;
      if (acc_intro) { acc_fill_from = 0; acc_fill_to = acc_origin; }
      else if (!acc_ending) acc_fill_on = false;
      acc_rest_to = 0;
      acc_countin_to = 0;   // a Start counts itself in
    }
    acc_clock_pos = acc_pos;
  } else {
    acc_clock_pos = acc_clock_pos + 1;
  }
  interrupts();
}

// The looper's count-in, in rhythm mode: the time of the first bar line at least at_least ms off
FLASHMEM uint32_t acc_bar_ms_after(uint32_t at_least) {
  double ticks_per_ms = rythm_bpm * ACC_BEAT / 60000.0;
  double bar = acc_bar_ticks();
  noInterrupts();
  double now_pos = acc_pos;
  uint32_t now = millis();
  interrupts();
  double target = ceil((now_pos + at_least * ticks_per_ms) / bar) * bar;
  return now + (uint32_t)lround((target - now_pos) / ticks_per_ms);
}

float acc_beats_per_bar() { return acc_bar_eighths() / 2.0f; }

// from the loop: a clock gone quiet without a Stop (the computer's player closed, the cable out)
FLASHMEM void rythm_clock_watch() {
  if (rythm_mode && rythm_clock_synced && last_midi_clock_in > 500000) {
    rythm_clock_synced = false;
    acc_clock_waiting = false;
  }
}

// Tap tempo: the hold button tapped in rhythm mode. Each tap was a sixth of the way from the tempo
// to the one tapped, so it took ten taps to get near, and a run of taps at the pulse the pattern
// moves in (eighths, as often as not) dragged the tempo up while the pattern ran away from the
// hand. Now the second tap sets the tempo and the next few refine it: the time across the last few
// taps (up to four intervals). With tap tempo counts (281) at eighths, two taps make a beat, and
// the tempo is taken across pairs of taps, so a shuffle's long and short eighths still make an
// even beat. A tap far off the run (40% from the beat it implies), or after more than two seconds,
// begins a new run. And each tap from the second draws the beat (or the eighth, where the shuffle
// puts it) halfway to where it fell: the rhythm hurries or holds back, up to 30%, until it is
// there, so no note is skipped or played twice, and a slightly uneven hand settles in the middle
// of its taps rather than being chased. Under a MIDI clock the clock has the tempo.
#define TAP_RUN 5
uint32_t tap_us[TAP_RUN];   // the run's taps, latest last
uint8_t tap_count = 0;

FLASHMEM void acc_tap_reset() { tap_count = 0; }

// ms a beat from the first `taps` taps of the run: across whole beats when taps are eighths and
// there are two or more intervals, so a shuffle's long and short eighths even out
static float tap_beat(uint8_t taps) {
  uint8_t intervals = taps - 1;
  if (tap_counts && intervals >= 2) intervals &= ~1;
  float per_beat = tap_counts ? 2 : 1;
  return (tap_us[taps - 1] - tap_us[taps - 1 - intervals]) / 1000.0f * per_beat / intervals;
}

FLASHMEM void acc_tap(bool button) {
  uint32_t now = micros();
  if (rythm_clock_synced || (tap_count > 0 && now - tap_us[tap_count - 1] > 2000000)) tap_count = 0;
  if (tap_count == TAP_RUN) {   // keep the latest
    memmove(tap_us, tap_us + 1, sizeof(uint32_t) * (TAP_RUN - 1));
    tap_count--;
  }
  tap_us[tap_count++] = now;
  if (rythm_clock_synced || tap_count < 2) return;
  // this tap's own beat (its interval, or with eighths the two taps to it) against the run's
  // before it: far off, a new run from the tap before this one
  if (tap_count >= 3) {
    uint8_t own = (tap_counts && tap_count >= 3) ? 2 : 1;
    float per_beat = tap_counts ? 2 : 1;
    float this_beat = (now - tap_us[tap_count - 1 - own]) / 1000.0f * per_beat / own;
    float before = tap_beat(tap_count - 1);
    if (fabsf(this_beat - before) > 0.4f * before) {
      tap_us[0] = tap_us[tap_count - 2];
      tap_us[1] = now;
      tap_count = 2;
    }
  }
  float beat_ms = tap_beat(tap_count);
  if (beat_ms < 200 || beat_ms > 2000) return;   // 30 to 300 bpm
  rythm_bpm = 60000.0f / beat_ms;
  noInterrupts();
  // where the tap was: the button is believed after 10 ms of steady contact (the debouncer)
  double at = acc_pos - (button ? 10 : 0) * rythm_bpm * ACC_BEAT / 60000.0;
  // the nearest beat, or with eighths the nearest eighth as the shuffle places it
  double beat = floor(at / ACC_BEAT) * ACC_BEAT;
  double nearest = (at - beat < ACC_BEAT / 2) ? beat : beat + ACC_BEAT;
  if (tap_counts) {
    double off = beat + ACC_BEAT / 2 * shuffle;
    if (fabs(at - off) < fabs(at - nearest)) nearest = off;
  }
  acc_phase_error = (nearest - at) / 2;
  interrupts();
  Serial.print("Updating the BPM to: ");
  Serial.println(rythm_bpm);
}

//>>CHORD MEMORY<<
// The Omnichord's chord memory: a progression recorded in time with the rhythm, then played back by
// itself, the accompaniment and the harp following it, so both hands are free for the strings.
// It keeps what the buttons said, not what sounded -- the root's button, the chord, the sharp, the
// slash -- and plays each back the way a press does (update_chord_notes and update_harp_notes), so
// a change of key, voicing, style or division while it plays is heard in it.
//
// Driven through setting 287, never kept (1 record, 2 play, 3 stop, 4 clear, 5 the next step),
// control changes 104-108 the same on any channel, and the double tap assigned to it, which steps:
// record, play, stop, record anew. It runs in rhythm mode, and turns it on if it is off.
// Recording starts on a bar line (one just passed, within a quarter of a beat, counts) with the
// chord as it is, and each chord the buttons make goes in on the nearest beat; it closes on the
// nearest bar line, a bar at least, and plays on from there. Playing, each chord goes in a little
// ahead of its beat, so the rhythm has it there, and a chord pressed meanwhile stands until the
// next; the rhythm sounds as if a chord were held. Stopped, it keeps the progression to play again
// from the next bar line.
struct chord_moment {
  double at;                  // 24ths of a beat from the start
  int8_t fundamental;
  uint8_t (*chord)[7];
  bool sharp, slashed;
  uint8_t slash;
};
#define CHORD_MEMORY_MAX 64
chord_moment chord_memory[CHORD_MEMORY_MAX];
uint8_t chord_memory_count = 0;
double chord_memory_start = 0;   // the rhythm's position (acc_pos) where it begins
double chord_memory_length = 0;
int16_t chord_memory_played = -1;   // the moment last put in, -1 none
bool chord_memory_replaying = false;   // putting a moment in: not a chord to record

void update_chord_notes();
void update_harp_notes();
void trigger_chord_notes();

FLASHMEM static double chord_memory_pos() {
  noInterrupts();
  double p = acc_pos, dropped = acc_pos_dropped;
  acc_pos_dropped = 0;
  interrupts();
  // The rhythm started its bar again (rhythm follows hands at 2, a MIDI Start): a recording carries
  // on where it was, and a memory playing starts again from its top with the bar.
  if (dropped != 0) {
    if (chord_memory_state == CHORD_MEMORY_RECORDING) chord_memory_start -= dropped;
    else if (chord_memory_state == CHORD_MEMORY_PLAYING) { chord_memory_start = 0; chord_memory_played = -1; }
  }
  return p;
}
FLASHMEM static double chord_memory_bar() { return acc_bar_ticks(); }

// the bank's colour back once it neither records nor plays (step_led_animation shows those)
FLASHMEM static void chord_memory_show() {
  if (chord_memory_state != CHORD_MEMORY_RECORDING && chord_memory_state != CHORD_MEMORY_PLAYING && looper_state != LOOPER_RECORDING && looper_state != LOOPER_PLAYING) {
    set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
  }
}

// The chord the buttons just made, while recording: in on the nearest beat, the latest on a beat winning
FLASHMEM void chord_memory_record_chord() {
  if (chord_memory_state != CHORD_MEMORY_RECORDING || chord_memory_replaying) return;
  double at = round((chord_memory_pos() - chord_memory_start) / ACC_BEAT) * ACC_BEAT;
  if (at < 0) at = 0;   // before the first bar line: the chord it starts with
  chord_moment m = {at, fundamental, current_chord, sharp_active, slash_chord, slash_value};
  while (chord_memory_count > 0 && chord_memory[chord_memory_count - 1].at >= at) chord_memory_count--;
  if (chord_memory_count < CHORD_MEMORY_MAX) chord_memory[chord_memory_count++] = m;
}

// A moment put in, as a press would: the live flags it needs for a moment, then back
FLASHMEM static void chord_memory_apply(const chord_moment &m) {
  bool sharp = sharp_active, slashed = slash_chord;
  uint8_t slash = slash_value;
  fundamental = m.fundamental;
  current_chord = m.chord;
  sharp_active = m.sharp;
  slash_chord = m.slashed;
  slash_value = m.slash;
  chord_memory_replaying = true;
  button_pushed = true;
  update_chord_notes();
  update_harp_notes();
  acc_serial_from_memory = acc_latest_serial;
  trigger_chord_notes();   // the held voices, if any (and it lets button_pushed go)
  chord_memory_replaying = false;
  sharp_active = sharp;
  slash_chord = slashed;
  slash_value = slash;
}

// the next bar line from now, or one just passed within a quarter of a beat
FLASHMEM static double chord_memory_bar_line() {
  double bar = chord_memory_bar(), pos = chord_memory_pos();
  double last = floor(pos / bar) * bar;
  return pos - last <= ACC_BEAT / 4 ? last : last + bar;
}

FLASHMEM static void chord_memory_record() {
  if (!rythm_mode) set_rythm_mode(true);
  chord_memory_count = 0;
  chord_memory_start = chord_memory_bar_line();
  chord_memory_state = CHORD_MEMORY_RECORDING;
  if (acc_latest.valid) {   // the chord as it is begins it
    chord_moment m = {0, fundamental, current_chord, chord_context_sharp, chord_context_slashed, slash_value};
    chord_memory[chord_memory_count++] = m;
  }
}

// Recording to playing: closed on the nearest bar line, a bar at least, and on from there
FLASHMEM static void chord_memory_close() {
  double bar = chord_memory_bar();
  double bars = round((chord_memory_pos() - chord_memory_start) / bar);
  if (bars < 1) bars = 1;
  chord_memory_length = bars * bar;
  while (chord_memory_count > 0 && chord_memory[chord_memory_count - 1].at >= chord_memory_length) chord_memory_count--;
  if (chord_memory_count == 0) { chord_memory_state = CHORD_MEMORY_EMPTY; chord_memory_show(); return; }
  chord_memory_played = -1;
  chord_memory_state = CHORD_MEMORY_PLAYING;
}

FLASHMEM static void chord_memory_play() {
  if (chord_memory_state == CHORD_MEMORY_RECORDING) { chord_memory_close(); return; }
  if (chord_memory_state != CHORD_MEMORY_STOPPED) return;
  if (!rythm_mode) set_rythm_mode(true);
  chord_memory_start = chord_memory_bar_line();
  chord_memory_played = -1;
  chord_memory_state = CHORD_MEMORY_PLAYING;
}

FLASHMEM static void chord_memory_stop() {
  if (chord_memory_state == CHORD_MEMORY_RECORDING) chord_memory_close();
  if (chord_memory_state == CHORD_MEMORY_PLAYING) chord_memory_state = CHORD_MEMORY_STOPPED;
  chord_memory_show();
}

FLASHMEM void chord_memory_step() {
  switch (chord_memory_state) {
    case CHORD_MEMORY_RECORDING: chord_memory_close(); break;
    case CHORD_MEMORY_PLAYING: chord_memory_stop(); break;
    default: chord_memory_record(); break;
  }
}

FLASHMEM void chord_memory_action(int16_t action) {
  current_sysex_parameters[chord_memory_adress] = 0;   // an action, never a value to keep
  switch (action) {
    case 1: if (chord_memory_state == CHORD_MEMORY_RECORDING) chord_memory_close(); else chord_memory_record(); break;
    case 2: chord_memory_play(); break;
    case 3: chord_memory_stop(); break;
    case 4: chord_memory_state = CHORD_MEMORY_EMPTY; chord_memory_count = 0; chord_memory_show(); break;
    case 5: chord_memory_step(); break;
  }
}

// From the loop: the moment due a little ahead puts its chord in
FLASHMEM void chord_memory_update() {
  if (chord_memory_state != CHORD_MEMORY_PLAYING) return;
  if (!rythm_mode) { chord_memory_state = CHORD_MEMORY_STOPPED; chord_memory_show(); return; }
  double ahead = 10 * rythm_bpm * ACC_BEAT / 60000.0;   // 10 ms
  double rel = chord_memory_pos() + ahead - chord_memory_start;
  if (rel < 0) return;   // waiting for its bar line
  rel = fmod(rel, chord_memory_length);
  int16_t due = -1;
  for (uint8_t i = 0; i < chord_memory_count; i++) if (chord_memory[i].at <= rel) due = i;
  if (due < 0) due = chord_memory_count - 1;   // before the first: the last carries over the join
  if (due != chord_memory_played) {
    chord_memory_played = due;
    chord_memory_apply(chord_memory[due]);
  }
}

//>>MIDI CHORDS<<
// With MIDI chords (282) on, in rhythm mode, notes sent in on the chords' port and channel play the
// accompaniment rather than its voices: what is held is read as a chord -- its root and kind, the
// bass under it -- and the rhythm plays that, in the voicing the notes were in, as it would a chord
// from the buttons. A note held (or the sustain pedal) is a hand on the buttons, for rhythm follows
// hands; the chord part's held voices (rhythm chords 1) sound it. So a computer, a keyboard or a
// sequencer can drive the styles. The harp keeps following the buttons. Outside rhythm mode, or
// with the setting off, notes in do what they did (MIDI in plays, 8).
uint32_t midi_chord_keys[4];        // notes down, a bit each
uint32_t midi_chord_sustained[4];   // let go of under the pedal
bool midi_chord_pedal = false;

// Chord kinds, as sets of semitones above the root, and their tones as the rhythm names them
struct midi_chord_kind { uint16_t tones; int8_t third, fifth, seventh, sixth; };   // tones: bit n, n semitones up
const midi_chord_kind midi_chord_kinds[] = {
  {0x091, 4, 7, 12, 9},     // major
  {0x089, 3, 7, 12, 8},     // minor
  {0x491, 4, 7, 10, 9},     // seventh
  {0x891, 4, 7, 11, 9},     // major seventh
  {0x489, 3, 7, 10, 8},     // minor seventh
  {0x291, 4, 7, 12, 9},     // sixth
  {0x289, 3, 7, 12, 9},     // minor sixth
  {0x049, 3, 6, 12, 8},     // diminished
  {0x249, 3, 6, 9, 9},      // diminished seventh
  {0x449, 3, 6, 10, 8},     // half diminished
  {0x111, 4, 8, 12, 9},     // augmented
  {0x0A1, 5, 7, 12, 9},     // sus4
  {0x085, 2, 7, 12, 9},     // sus2
  {0x4A1, 5, 7, 10, 9},     // seventh sus4
  {0x889, 3, 7, 11, 8},     // minor major seventh
  {0x081, 4, 7, 12, 9},     // a bare fifth: taken as major
};

static inline int16_t semitones_to_steps(int s) { return (int16_t)lroundf(s * EDO / 12.0f); }

// What is held, read as a chord and handed to the rhythm
FLASHMEM void midi_chord_update() {
  int16_t notes[128];
  uint8_t count = 0;
  for (uint8_t n = 0; n < 128; n++) {
    if ((midi_chord_keys[n / 32] | midi_chord_sustained[n / 32]) & (1UL << (n % 32))) notes[count++] = n;
  }
  midi_chord_held = count > 0;
  if (!count) return;   // let go: the rhythm keeps the last chord, as with the buttons
  uint16_t pcs = 0;
  for (uint8_t i = 0; i < count; i++) pcs |= 1 << (notes[i] % 12);
  int bass = notes[0] % 12;
  // the reading that explains the most of what is held and adds the least: every root held, every
  // kind; a tie goes to the root in the bass, then to the simpler kind (earlier in the list)
  int best_root = bass, best_kind = 0, best_score = -1000;
  for (int r = 0; r < 12; r++) {
    if (!(pcs & (1 << r))) continue;
    uint16_t rel = ((pcs >> r) | (pcs << (12 - r))) & 0xFFF;
    for (uint8_t k = 0; k < sizeof(midi_chord_kinds) / sizeof(midi_chord_kinds[0]); k++) {
      uint16_t t = midi_chord_kinds[k].tones;
      int score = 4 * __builtin_popcount(t & rel) - 3 * __builtin_popcount(t & ~rel) - 2 * __builtin_popcount(rel & ~t) + (r == bass ? 1 : 0);
      if (score > best_score) { best_score = score; best_root = r; best_kind = k; }
    }
  }
  const midi_chord_kind &kind = midi_chord_kinds[best_kind];
  acc_chord_t c;
  c.valid = true;
  // the voicing: the notes as they are held (the lowest and the top three of more than four), in
  // the chord voices' numbering, so the chords' MIDI out gives back the notes sent in
  int16_t v[4];
  uint8_t used = 0;
  for (uint8_t i = 0; i < count && used < 4; i++) {
    uint8_t pick = (count <= 4 || i == 0) ? i : count - 3 + (used - 1);
    v[used++] = (int16_t)lroundf((notes[pick] - midi_base_note_transposed) * EDO / 12.0f);
  }
  if (used == 1) {   // one note: the chord it is the root of, above it
    v[1] = v[0] + semitones_to_steps(kind.third);
    v[2] = v[0] + semitones_to_steps(kind.fifth);
    v[3] = v[0] + EDO;
    used = 4;
  }
  for (uint8_t i = used, j = 0; i < 4; i++, j++) v[i] = v[j] + EDO;   // fewer: the lowest again an octave up
  for (uint8_t i = 1; i < 4; i++) {
    for (uint8_t j = i; j > 0 && v[j] < v[j - 1]; j--) { int16_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
  }
  for (uint8_t i = 0; i < 4; i++) c.v[i] = v[i];
  c.root = acc_pc(semitones_to_steps(best_root) - semitones_to_steps(midi_base_note_transposed % 12));
  c.bass = acc_pc(semitones_to_steps(bass) - semitones_to_steps(midi_base_note_transposed % 12));
  c.third = semitones_to_steps(kind.third);
  c.fifth = semitones_to_steps(kind.fifth);
  c.seventh = semitones_to_steps(kind.seventh);
  c.second = semitones_to_steps(2);
  c.fourth = semitones_to_steps(5);
  c.sixth = semitones_to_steps(kind.sixth);
  acc_set_latest(c);
  // rhythm style 0 plays its seven notes: the voicing, then the second, fourth and sixth above the root
  int16_t root = acc_at_or_below(c.root, c.v[0]);
  int16_t seven[7] = {c.v[0], c.v[1], c.v[2], c.v[3], (int16_t)(root + c.second + EDO), (int16_t)(root + c.fourth + EDO), (int16_t)(root + c.sixth + EDO)};
  noInterrupts();
  for (uint8_t i = 0; i < 7; i++) {
    while (seven[i] < 0) seven[i] += EDO;
    current_applied_chord_notes[i] = seven[i];
  }
  interrupts();
  // the voices holding the chord sound it: moved there if they are, started if not
  uint8_t held = acc_held_mask();
  for (uint8_t i = 0; i < 4; i++) {
    if (!(held & (1 << i))) continue;
    if (chord_envelope_array[i]->isActive() && !chord_voice_released[i]) {
      noInterrupts();
      set_chord_voice_frequency(i, c.v[i]);
      interrupts();
    } else {
      play_single_note(i, &note_timer[i]);
      set_chord_voice_frequency(i, c.v[i]);
    }
  }
}

// A note or the pedal on the chords' port and channel, in rhythm mode with MIDI chords on: taken
// here, and true; anything else is left to the rest of processMIDI
FLASHMEM bool midi_chord_message(uint8_t type, uint8_t cable, uint8_t channel, uint8_t data1, uint8_t data2) {
  if (!midi_chords || !rythm_mode || cable != chord_port || channel != chord_channel) return false;
  uint32_t bit = 1UL << (data1 % 32);
  uint8_t word = data1 / 32;
  if (type == usbMIDI.NoteOn && data2 > 0) {
    midi_chord_keys[word] |= bit;
    midi_chord_sustained[word] &= ~bit;
  } else if (type == usbMIDI.NoteOn || type == usbMIDI.NoteOff) {
    midi_chord_keys[word] &= ~bit;
    if (midi_chord_pedal) midi_chord_sustained[word] |= bit;
  } else if (type == usbMIDI.ControlChange && data1 == 64) {
    midi_chord_pedal = data2 >= 64;
    if (!midi_chord_pedal) for (uint8_t w = 0; w < 4; w++) midi_chord_sustained[w] = 0;
  } else {
    return false;
  }
  midi_chord_update();
  return true;
}

// MIDI chords turned off, or rhythm mode ending: nothing sent in is held any more
FLASHMEM void midi_chords_clear() {
  for (uint8_t w = 0; w < 4; w++) midi_chord_keys[w] = midi_chord_sustained[w] = 0;
  midi_chord_pedal = false;
  midi_chord_held = false;
}

// A chord that arrives just after notes of the part fell due -- meant for the beat, a moment late --
// moves them to it, or plays them, if the hands were off when they fell due. With the chord coming
// in on the beat or the bar, it takes that beat or bar if it is this late for it, not the next.
FLASHMEM void acc_catch_late() {
  float grace = fminf(100.0f, 15000.0f / rythm_bpm);   // a quarter of a beat, at most 100 ms
  noInterrupts();
  bool take = acc_change == 0 || millis() - acc_latch_ms <= grace;
  if (take) acc_take_chord();
  for (uint8_t v = 0; v < 4 && acc_now.valid; v++) {
    acc_voice_t &a = acc_voice[v];
    if (!a.due || millis() - a.at > grace) continue;
    if (!a.struck) {
      acc_strike(v, a.role, a.octave, a.length, a.velocity);
    } else if (take && chord_envelope_array[v]->isActive() && !chord_voice_released[v]) {
      int16_t note = acc_note(a.role, a.octave, a.before);
      if (note != a.note) {
        set_chord_voice_frequency(v, note);
        a.note = note;
      }
    }
  }
  interrupts();
}

// From the loop, in rhythm mode, once the chord buttons have been read
FLASHMEM void acc_update() {
  if (acc_ending_done) {   // the ending's last chord is struck: rhythm mode goes off, the chord ringing on
    acc_ending_done = false;
    set_rythm_mode(false);
    return;
  }
  static uint16_t seen = 0;
  // A fill or the ending asked for (by the double tap, which needs the hands off the chords) plays out
  // as if a hand were down, and a press during it doesn't start the bar again over it
  bool fill_hands = acc_follow && ((acc_fill_on && !acc_intro) || acc_ending);
  bool hands = current_line >= 0 || midi_chord_held || chord_memory_state == CHORD_MEMORY_PLAYING || fill_hands;
  bool caught = false;
  if (hands != acc_hands) {
    acc_hands = hands;
    if (acc_follow && !hands) {   // let go: the parts stop, and the chord held with them
      noInterrupts();
      for (uint8_t v = 0; v < 4; v++) acc_voice[v].off_pending = false;
      for (uint8_t r = 0; r < ACC_ROLL_MAX; r++) acc_rolled[r].used = false;
      interrupts();
      stop_chord_voices(0xF);
    } else if (acc_follow == 2 && hands && !rythm_clock_synced && !fill_hands) {
      if (rhythm_intro && !acc_pressed) {   // the intro waited for the first press, not played to no one
        noInterrupts();
        acc_intro = acc_fill_on = true;
        interrupts();
      }
      acc_restart = true;   // the bar starts with the press
      caught = true;
    } else if (acc_follow && hands) {
      acc_catch_late();
      caught = true;
    }
    if (hands) acc_pressed = true;
  }
  if (acc_latest_serial != seen) {
    seen = acc_latest_serial;
    // not a chord memory puts in a little ahead of its beat: the notes just before it are the last chord's
    if (!caught && acc_style_index() != 0 && seen != acc_serial_from_memory) acc_catch_late();
  }
}

//--->>FILE HANDLING UTILITIES
/* ---- PRESET STORAGE ---------------------------------------------------------
 *
 * Until firmware 72 each preset was a file of its own in a 512 kB LittleFS; 72 to 76 kept the
 * twelve in one file, presets.bin, in a 256 kB one: on the Teensy 4.0 LittleFS_Program's blocks
 * are 32 kB, and a file takes a block, its copy another, so 12 kB of presets held 256 kB of flash.
 *
 * From firmware 77 the presets are in raw flash: the top 32 kB of program flash, in two halves of
 * 16 kB (four 4 kB sectors each). A half holds an image: a header, then a slot of parameter_size
 * settings for each preset, as they are in memory. A save erases the half not in use, writes every
 * slot into it (the saved one new, the others copied from the live half), then its header, with a
 * sequence number one higher and a CRC over the lot. The header goes in last, so a save cut off by
 * the power leaves the other half whole, and at boot the half with the highest sequence whose CRC
 * holds is the live one. Reading a preset is reading flash. The flash routines are the core's
 * (eepromemu_flash_write, eepromemu_flash_erase_sector, which LittleFS used too); they run with
 * interrupts off, so the DAC is muted around a save, as it was. The 224 kB the filesystem held
 * above this go to the samples.
 *
 * The first boot of 77 on a minichord with presets.bin reads it out of the old filesystem and
 * writes the store from it; LittleFS stays in this firmware for that alone. Which is there is read
 * off the flash itself (the "littlefs" a filesystem writes at the start of its first two blocks)
 * before anything is mounted, since LittleFS_Program::begin formats whatever it can't mount. A
 * firmware without LittleFS, flashed straight over 76 or older, finds no store and starts from the
 * factory presets: back up first (minictl backup), restore after.
 */
const uint32_t program_flash_size = 0x1F0000;          // the Teensy 4.0's (the EEPROM emulation sits above it)
const uint32_t preset_store_size = 32 * 1024;
const uint32_t preset_store_base = 0x60000000 + program_flash_size - preset_store_size;
const uint32_t preset_half_size = preset_store_size / 2;
const uint32_t flash_sector = 4096;
const uint32_t preset_storage_old_size = 256 * 1024;   // firmware 72 to 76: presets.bin in a LittleFS
const uint32_t preset_fs_block = 32768;                // its block (SECTOR_SIZE)
struct preset_store_header {
  char magic[4];        // "MCP2"
  uint16_t format;      // 1
  uint16_t parameters;  // settings in a slot
  uint16_t saved;       // a bit a preset: slot b holds a saved preset
  uint16_t spare;
  uint32_t sequence;    // one higher each save: the highest valid half is the live one
  uint32_t crc;         // CRC-32 of the slots, then of this header with crc 0
};
static_assert(sizeof(preset_store_header) == 20, "the preset store's header is 20 bytes");
static_assert(sizeof(preset_store_header) + preset_number * parameter_size * 2 <= preset_half_size, "the presets don't fit a half of the store");
struct preset_file_header {   // presets.bin's, firmware 72 to 76
  char magic[4];        // "MCPS"
  uint16_t format;
  uint16_t parameters;
  uint16_t saved;
  uint16_t spare;
  uint32_t spare2;
};
static const uint8_t *preset_live = nullptr;   // the live half, or null when there is none

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
// The core's flash write is one page-program command, so at most 256 bytes and within a page (LittleFS
// wrote 128 at a time); the cache is purged where the flash changed, as the core does for a write.
static void flash_program(void *addr, const void *data, uint32_t len) {
  uint8_t *a = (uint8_t *)addr;
  const uint8_t *d = (const uint8_t *)data;
  while (len) {
    uint32_t room = 256 - ((uint32_t)a & 255);
    uint32_t n = len < room ? len : room;
    eepromemu_flash_write(a, d, n);
    a += n; d += n; len -= n;
  }
}
static void flash_erase(void *addr, uint32_t len) {
  for (uint32_t at = 0; at < len; at += flash_sector) eepromemu_flash_erase_sector((uint8_t *)addr + at);
  arm_dcache_delete(addr, len);
}
static inline const uint8_t *preset_half(int h) { return (const uint8_t *)(preset_store_base + h * preset_half_size); }
static inline const uint8_t *preset_slot(const uint8_t *half, int bank, uint16_t parameters) {
  return half + sizeof(preset_store_header) + (uint32_t)bank * parameters * 2;
}
// whether a half holds a whole image: its header, and the CRC over its slots and header
FLASHMEM static bool preset_half_valid(const uint8_t *half, preset_store_header &h) {
  memcpy(&h, half, sizeof h);
  if (memcmp(h.magic, "MCP2", 4) != 0 || h.format != 1 || h.parameters == 0) return false;
  uint32_t bytes = (uint32_t)preset_number * h.parameters * 2;
  if (sizeof h + bytes > preset_half_size) return false;
  uint32_t crc = crc32_update(0, half + sizeof h, bytes);
  preset_store_header z = h;
  z.crc = 0;
  return crc32_update(crc, (const uint8_t *)&z, sizeof z) == h.crc;
}
FLASHMEM static void preset_store_find() {
  preset_live = nullptr;
  uint32_t best = 0;
  for (int k = 0; k < 2; k++) {
    preset_store_header h;
    if (preset_half_valid(preset_half(k), h) && (!preset_live || h.sequence > best)) {
      preset_live = preset_half(k);
      best = h.sequence;
    }
  }
}
// The image written afresh into the half not in use: slot b from fill(b, slot, context), which says
// whether b holds a saved preset; then the header. True once the new half reads back as the live one.
typedef bool (*preset_fill_fn)(int bank, int16_t *slot, void *context);
FLASHMEM static bool preset_store_commit(preset_fill_fn fill, void *context) {
  preset_store_header old;
  bool had = preset_live && preset_half_valid(preset_live, old);
  uint8_t *half = (uint8_t *)preset_half(preset_live == preset_half(0) ? 1 : 0);
  flash_erase(half, preset_half_size);
  preset_store_header h;
  memset(&h, 0, sizeof h);
  memcpy(h.magic, "MCP2", 4);
  h.format = 1;
  h.parameters = parameter_size;
  h.sequence = had ? old.sequence + 1 : 1;
  uint32_t crc = 0;
  DMAMEM static int16_t slot[parameter_size];   // in RAM2, where the room is
  for (int b = 0; b < preset_number; b++) {
    if (fill(b, slot, context)) h.saved |= 1 << b;
    else memset(slot, 0, sizeof slot);
    flash_program((void *)preset_slot(half, b, parameter_size), slot, sizeof slot);
    crc = crc32_update(crc, (const uint8_t *)slot, sizeof slot);
  }
  preset_store_header z = h;
  z.crc = 0;
  h.crc = crc32_update(crc, (const uint8_t *)&z, sizeof z);
  flash_program(half, &h, sizeof h);
  preset_store_find();
  return preset_live == half;
}
// One preset's settings out of its slot; false if it was never saved.
FLASHMEM bool preset_read(int bank, int16_t *values) {
  if (!preset_live) return false;
  preset_store_header h;
  memcpy(&h, preset_live, sizeof h);
  if (!(h.saved & (1 << bank)) || h.parameters != parameter_size) return false;
  memcpy(values, preset_slot(preset_live, bank, h.parameters), parameter_size * 2);
  return true;
}
// One preset's settings into its slot, the others carried over; false if the store couldn't be written.
struct preset_write_context { int bank; const int16_t *values; };
static bool preset_write_fill(int b, int16_t *slot, void *context) {
  preset_write_context *c = (preset_write_context *)context;
  if (b == c->bank) { memcpy(slot, c->values, parameter_size * 2); return true; }
  return preset_read(b, slot);
}
FLASHMEM bool preset_write(int bank, const int16_t *values) {
  preset_write_context c = {bank, values};
  return preset_store_commit(preset_write_fill, &c);
}
// every preset at once, from a table in memory (the relayout, and the move out of the old filesystem)
struct preset_table_context { int16_t (*banks)[parameter_size]; uint16_t saved; };
static bool preset_table_fill(int b, int16_t *slot, void *context) {
  preset_table_context *c = (preset_table_context *)context;
  memcpy(slot, c->banks[b], parameter_size * 2);
  return (c->saved >> b) & 1;
}
FLASHMEM void preset_store_wipe() {
  flash_erase((void *)preset_store_base, preset_store_size);
  preset_live = nullptr;
}
// A store laid out for another parameter_size: every saved slot read at its old size (what it lacks at
// its default, as a short preset always was) and the store written again at this one.
FLASHMEM void preset_store_relayout() {
  if (!preset_live) return;
  preset_store_header h;
  memcpy(&h, preset_live, sizeof h);
  if (h.parameters == parameter_size) return;
  Serial.printf("Presets hold %d settings, this firmware %d: laying the store out again\n", h.parameters, parameter_size);
  int16_t banks[preset_number][parameter_size];   // 12 kB of stack, for this boot only
  for (int b = 0; b < preset_number; b++) {
    for (uint16_t i = 0; i < parameter_size; i++) banks[b][i] = parameter_default(b, i);
    if (h.saved & (1 << b)) memcpy(banks[b], preset_slot(preset_live, b, h.parameters), min(h.parameters, parameter_size) * 2);
  }
  preset_table_context c = {banks, h.saved};
  preset_store_commit(preset_table_fill, &c);
}
// Whether a LittleFS of this size starts at the top of flash: its first or second block holds "littlefs".
FLASHMEM static bool preset_fs_present(uint32_t size) {
  const uint8_t *base = (const uint8_t *)(0x60000000 + program_flash_size - size);
  for (uint32_t block = 0; block < 2; block++) {
    for (uint32_t i = 0; i < 64; i++) {
      if (memcmp(base + block * preset_fs_block + i, "littlefs", 8) == 0) return true;
    }
  }
  return false;
}
// Firmware 72-76's presets.bin, out of its 256 kB LittleFS, into the store (see PRESET STORAGE).
FLASHMEM static void presets_migrate_file() {
  if (!preset_fs_present(preset_storage_old_size)) return;
  static LittleFS_Program oldfs;
  if (!oldfs.begin(preset_storage_old_size)) return;
  File f = oldfs.open("presets.bin", FILE_READ);
  if (!f) return;
  preset_file_header h;
  f.seek(0);
  bool valid = f.read((uint8_t *)&h, sizeof h) == sizeof h && memcmp(h.magic, "MCPS", 4) == 0 && h.parameters > 0;
  int16_t banks[preset_number][parameter_size];   // 12 kB of stack, for this boot only
  for (int b = 0; b < preset_number; b++) {
    for (uint16_t i = 0; i < parameter_size; i++) banks[b][i] = parameter_default(b, i);
    if (!valid || !(h.saved & (1 << b))) continue;
    f.seek(sizeof h + (uint32_t)b * h.parameters * 2);
    f.read((uint8_t *)banks[b], min(h.parameters, parameter_size) * 2);
  }
  f.close();
  if (!valid) return;
  Serial.printf("Moving %d presets out of the old filesystem into the preset store\n", __builtin_popcount(h.saved));
  preset_table_context c = {banks, h.saved};
  if (!preset_store_commit(preset_table_fill, &c)) Serial.println("The preset store couldn't be written");
}

/* ---- preset versioning ------------------------------------------------------
 *
 * Every preset stores all 256 slots, so an address that did not exist when the
 * preset was written reads back as a stored 0 — indistinguishable from someone
 * having set it to 0 on purpose. That is why a new parameter whose sensible
 * default is not 0 cannot be added without changing what existing presets sound
 * like, and why anything with a non-zero default has had to live outside the
 * preset array.
 *
 * The pieces to fix it were already here and not wired together: version_ID has
 * always been declared, the comment has always said address 7, parameters.json
 * has always carried introduction_version, and the editor already reads a
 * firmware version from address 7. Nothing ever wrote it.
 *
 * So: stamp the version into address 7 on save, and on load give any parameter
 * newer than the stored version its factory default instead of the stored 0.
 * Presets written before this change carry 0 there, which reads as "older than
 * everything" and defaults anything introduced after version 0 — which is the
 * right answer for them.
 */
FLASHMEM void apply_preset_version(int bank_number) {
  const int16_t stored_version = current_sysex_parameters[firmware_version_adress];
  /* A 0 here means the preset was written straight from the factory defaults and
   * never saved by a player: the handler for address 7 has existed since version
   * 2 and ignores the incoming value, so any preset a firmware has saved carries
   * the version that saved it. Defaulting such a bank is therefore right rather
   * than destructive — it is a factory bank, and this restores it to the factory
   * values of the running firmware. */
  if (stored_version >= version_ID) {
    return;   // written by this firmware or newer: nothing to migrate
  }
  // Version 18 put 24-EDO in between 19 and 31, so the divisions read in order,
  // which moved 31-EDO from temperament 11 to 12: a preset saved on 31 by earlier
  // firmware keeps 31 rather than turning into 24.
  if (stored_version > 0 && stored_version < 18 && current_sysex_parameters[237] == 11) {
    current_sysex_parameters[237] = 12;
  }
  // Version 93 numbered the released plates first, 1 the Keymaster to 4 framed strings, and moved the
  // plates not out yet from 8: zipper 24, the arcade wheel and the fretless wrap were 1 to 3, and 4 the
  // plate on another branch's firmware (90 and 91), now 11.
  if (stored_version > 0 && stored_version < 93 && current_sysex_parameters[harp_plate_adress] >= 1 && current_sysex_parameters[harp_plate_adress] <= 4) {
    current_sysex_parameters[harp_plate_adress] += 7;
  }
  uint16_t restored = 0;
  for (uint16_t i = 2; i < parameter_size; i++) {
    if (i == firmware_version_adress) continue;
    if (parameter_introduction[i] > stored_version) {
      current_sysex_parameters[i] = parameter_default(bank_number, i);
      restored++;
    }
  }
  current_sysex_parameters[firmware_version_adress] = version_ID;
  if (restored) {
    Serial.print("Preset written by version ");
    Serial.print(stored_version);
    Serial.print(", defaulted ");
    Serial.print(restored);
    Serial.println(" parameter(s) introduced since");
  }
}

FLASHMEM void save_config(int bank_number, bool default_save) {
  if (bank_number < 0 || bank_number >= preset_number) {
    Serial.printf("Error: Invalid bank_number %d in save_config\n", bank_number);
    return;
  }
  digitalWrite(_MUTE_PIN, LOW); // muting the DAC
  current_bank_number=bank_number; //save to correctly write in the memory 
  AudioNoInterrupts();
  static int16_t saving[parameter_size];
  if (default_save) {
    // if we need to put the default in memory
    Serial.println("Writing the factory preset");
    default_bank_sysex_parameters[bank_number][firmware_version_adress] = version_ID;
    for (uint16_t i = 0; i < parameter_size; i++) saving[i] = parameter_default(bank_number, i);
  } else {
    Serial.println("Saving current settings");
    // A double tap toggle is a momentary override, not part of the preset. If
    // its value were written here the preset would come back already holding it,
    // and the gesture would then toggle between two identical values and appear
    // to do nothing. So the underlying value is what gets saved.
    int16_t held_value[double_tap_pairs] = {0, 0, 0};
    for (uint8_t k = 0; k < double_tap_pairs; k++) {
      int16_t a = double_tap_engaged_adress[k];
      if (double_tap_engaged && control_can_tap(a) && current_sysex_parameters[a] == double_tap_applied[k]) {
        held_value[k] = current_sysex_parameters[a];
        current_sysex_parameters[a] = double_tap_saved[k];
      } else held_value[k] = -32768;              // untouched by the double tap, or changed since: saved as it is
    }
    memcpy(saving, current_sysex_parameters, sizeof saving);
    for (uint8_t k = 0; k < double_tap_pairs; k++) {
      int16_t a = double_tap_engaged_adress[k];
      if (double_tap_engaged && control_can_tap(a) && held_value[k] != -32768) current_sysex_parameters[a] = held_value[k];
    }
  }
  saving[0] = 0;
  saving[1] = bank_number;   // as the text files had it, for the editor's display
  if (preset_write(bank_number, saving)) Serial.printf("Saved preset %d\n", bank_number + 1);
  else Serial.printf("Error: preset %d not saved\n", bank_number + 1);

  load_config(current_bank_number); //we do a full reload to initialise values
  
  // add something to set config_bit in the parameters to zero
  AudioInterrupts();
  digitalWrite(_MUTE_PIN, HIGH); // unmuting the DAC
}

void load_config(int bank_number) {
  if (bank_number < 0 || bank_number >= preset_number) {
    Serial.printf("Error: Invalid bank_number %d in save_config\n", bank_number);
    return;
  }
  // another preset, by the preset buttons or a remote: what was pushed belonged to the last one
  if (bank_number != pushed_bank) sysex_pushed = false;
  //digitalWrite(_MUTE_PIN, LOW); // muting the DAC
  //Turn off chords notes
  for (int i = 0; i < 4; i++) {
    chord_vibrato_envelope_array[i]->noteOff();
    chord_vibrato_dc_envelope_array[i]->noteOff();
    chord_envelope_array[i]->noteOff();
    chord_envelope_filter_array[i]->noteOff();
    chord_voice_released[i] = true;
  }
  trigger_chord = true; //to be ready to retrigger if needed

  // a slot holds every setting (presets saved before the array grew were filled with defaults when
  // they moved into presets.bin, see PRESET STORAGE)
  static int16_t stored[parameter_size];
  if (preset_read(bank_number, stored)) {
    memcpy(current_sysex_parameters, stored, sizeof stored);
    apply_preset_version(bank_number);
    current_sysex_parameters[usb_audio_adress] = usb_audio_mode;   // the instrument's, whatever the preset holds
    current_sysex_parameters[upside_down_adress] = upside_down;
    lock_apply();                                                  // and the locked settings, see LOCKS
    Serial.printf("Loaded preset %d\n", bank_number + 1);
  } else {
    Serial.print("No preset, writing factory default");
    save_config(bank_number, true); // reboot with default value
  }
  // Loading the potentiometer
  chord_pot.setup(chord_volume_sysex, 100, current_sysex_parameters[chord_pot_alternate_control], current_sysex_parameters[chord_pot_alternate_range], current_sysex_parameters,current_sysex_parameters[chord_pot_alternate_storage],apply_audio_parameter,chord_pot_alternate_storage);
  harp_pot.setup(harp_volume_sysex, 100, current_sysex_parameters[harp_pot_alternate_control], current_sysex_parameters[harp_pot_alternate_range], current_sysex_parameters,current_sysex_parameters[harp_pot_alternate_storage],apply_audio_parameter,harp_pot_alternate_storage);
  mod_pot.setup(current_sysex_parameters[mod_pot_main_control], current_sysex_parameters[mod_pot_main_range], current_sysex_parameters[mod_pot_alternate_control], current_sysex_parameters[mod_pot_alternate_range], current_sysex_parameters,current_sysex_parameters[mod_pot_alternate_storage],apply_audio_parameter,mod_pot_alternate_storage);
  Serial.println("pot setup done");
  parameters_loading = true;   // the rates set at once (speed ramp)
  for (int i = 1; i < parameter_size; i++) {
    // The knobs' alternate defaults (4-6) are set without the force_update() their setting
    // runs: it brought each knob's target up to date mid-load, only for the force_updates
    // below to do it again, and that was 1.3 ms of the load (the mod knob's alone 0.9).
    if (i == 4) chord_pot.set_alternate_default(current_sysex_parameters[i]);
    else if (i == 5) harp_pot.set_alternate_default(current_sysex_parameters[i]);
    else if (i == 6) mod_pot.set_alternate_default(current_sysex_parameters[i]);
    else apply_audio_parameter(i, current_sysex_parameters[i]);
  }
  parameters_loading = false;
  if (sysex_controler_connected) {
    control_command(0, 0); // push state to a connected remote controller
  }
  chord_pot.force_update();
  harp_pot.force_update();
  mod_pot.force_update();
  // A preset load replaces every parameter, including whatever a double tap had
  // toggled, and the value it saved belongs to the preset being left. So the
  // gesture ends here rather than claiming to still hold something it does not.
  if (double_tap_engaged) {
    double_tap_engaged = false;
    if (!key_change_mode) set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
  }
  flag_save_needed=false;
  //digitalWrite(_MUTE_PIN, HIGH); // unmuting the DAC
}

FLASHMEM void setup() {   // once, at power on: run from flash to leave RAM1 to the rest (see apply_audio_parameter)
  Serial.begin(9600);
  Serial.println("Initialising audio parameters");
  AudioMemory(900);
  update_formants();   // off (amount 0) until a preset says otherwise
  //>>STATIC AUDIO PARAMETERS
  // the waveshaper
  calculate_ws_array();
  chord_waveshape.shape(wave_shape, 257);
  string_waveshape.shape(wave_shape, 257);
  string_waveshape_r.shape(wave_shape, 257);
  //the base DC value for strings
  filter_dc.amplitude(1);
  // the delay passthrough
  string_delay_mix.gain(0, 1);
  string_delay_mix_r.gain(0, 1);
  chord_delay_mix.gain(0, 1);
  // simple mixers
  string_vibrato_mixer.gain(0,0.5);
  string_vibrato_mixer.gain(1,0.5);
  envelope_string_vibrato_dc.sustain(0);
  for (int i = 0; i < 3; i++) {
    string_mixer_array[i]->gain(0, 1);
    string_mixer_array[i]->gain(1, 1);
    string_mixer_array[i]->gain(2, 1);
    string_mixer_array[i]->gain(3, 1);
    transient_mixer_array[i]->gain(0, 1);
    transient_mixer_array[i]->gain(1, 1);
    transient_mixer_array[i]->gain(2, 1);
    transient_mixer_array[i]->gain(3, 1);
  }
  for (int i = 0; i < 4; i++) {
    chord_voice_mixer_array[i]->gain(0, 1);
    chord_voice_mixer_array[i]->gain(1, 1);
    chord_voice_mixer_array[i]->gain(2, 1);
    chord_noise_array[i]->amplitude(0.5);
    //we hardcode the frequency modulation. Now intensity of the effect will be depending on the mixer gain 
    chord_osc_1_array[i]->frequencyModulation(2);
    chord_osc_2_array[i]->frequencyModulation(2);
    chord_osc_3_array[i]->frequencyModulation(2);
    //Now the max value of the vibrato is 0.25 for each component and we add 0.5 for pitch selection. With the multiplication by freqmodulation of 2, we maintain the rate we had before. 
    chord_vibrato_mixer_array[i]->gain(1,0.5); 

    chord_vibrato_dc_envelope_array[i]->sustain(0); //for the pitch bend no need for sustain
    transient_full_mix.gain(i, 1);
    all_string_mix.gain(i, 1);
    all_string_mix_r.gain(i, 1);
  }
  for(int i=0;i<12;i++){
    string_transient_envelope_array[i]->sustain(0);//don't need sustain for the transient
  }
  all_string_mix.gain(3,0.02); //for the transient
  all_string_mix_r.gain(3,0.02);
  // at spread 0 the right chain's mixers are all at 0 and send nothing, so the chain rests
  for (uint8_t k = 0; k < 3; k++) string_mixer_r_array[k]->silent_when_muted(true);
  all_string_mix_r.silent_when_muted(true);
  // the strings' and the chord voices' own filters: only their low pass goes anywhere
  for (uint8_t k = 0; k < 12; k++) string_filter_array[k]->lowpass_only(true);
  for (uint8_t k = 0; k < 4; k++) chord_voice_filter_array[k]->lowpass_only(true);
  apply_string_spreads();             // each string's share of the two sides
  // the ensemble: about 10 ms swept by 3, at a different rate a side
  chord_ensemble_l.begin(chord_ensemble_line_l, 2048, 441, 132, 0.53);
  chord_ensemble_r.begin(chord_ensemble_line_r, 2048, 529, 150, 0.71);
  set_chord_ensemble(0);

  vocoder_setup();
  phone_setup();
  phone_dj_setup();
  drum_machine.setKit(&drum_kits[0]);
  drums_set();

  // initialising the rest of the hardware
  chord_matrix.setup();
  harp_sensor.setup();
  harp_sensor.recalibrate();
  plate_id_load();
  upside_down = eeprom_read_byte((const uint8_t *)upside_down_eeprom) == 1;
  upside_down_apply();
  // The harp pads don't bounce: the touch chip already wants two readings in a row before it calls a
  // pad touched. The buttons' 10 ms was most of the harp's latency: from a pad's reading crossing
  // the touch threshold to the pluck took 17 ms (median, measured), and with 4 ms it takes 11.
  // 4 ms rather than none gives a landing finger's reading time to rise, for a pluck's velocity.
  for (int i = 0; i < 12; i++) harp_array[i].set_debounce(harp_debounce_us);
  // The chord buttons take a press after 1 ms closed, not 10: measured, presses hardly bounce and
  // releases do (up to 30 ms), so the 10 ms was all latency. Within 50 ms of a release a press still
  // waits 10, where a release's chatter could pass for one; releases keep their 10 ms throughout.
  for (int i = 0; i < 22; i++) chord_matrix_array[i].set_press(1000, 50000);
  pinMode(BATT_LBO_PIN, INPUT);
  pinMode(DOWN_PGM_PIN, INPUT);
  pinMode(UP_PGM_PIN, INPUT);
  pinMode(HOLD_BUTTON_PIN, INPUT);
  if (continuous_chord) {
    analogWrite(RYTHM_LED_PIN, 255);
  }
  // loading the preset
  Serial.println("Initialising preset storage");
  // the top 32 kB of flash, two halves (see PRESET STORAGE); the first boot of 77 moves presets.bin's
  // presets out of the old filesystem
  preset_store_find();
  if (!preset_live) presets_migrate_file();
  preset_store_relayout();
  lock_load();                 // the instrument's own settings, before the first preset loads (see LOCKS)
  Serial.println("Loading the preset");
  load_config(current_bank_number);
  // initializing the strings
  for (int i = 0; i < 12; i++) {
    current_harp_notes[i] = harp_string_note(i, slash_chord, sharp_active);
  }
  //Checking the battery 
  LBO_flag.set(digitalRead(BATT_LBO_PIN));
  uint8_t LBO_value = LBO_flag.read_value();
  if (LBO_value == 0) {
    led_blinking_flag=true;
  }


  Serial.println("Initialisation complete");
  digitalWrite(_MUTE_PIN, HIGH);
}

// Whether the chord buttons' latest change was legato (fingered glide): with a button held through it
void chord_legato_watch() {
  static uint32_t was = 0;
  static bool modifier_was = false;
  uint32_t now = 0;
  for (uint8_t i = 1; i < 22; i++) {
    if (chord_matrix_array[i].read_value() && !(key_selection_buttons & (1UL << i))) now |= 1UL << i;
  }
  bool modifier = chord_matrix_array[0].read_value();
  if (now != was || modifier != modifier_was) {
    uint32_t held_through = now & was;   // down before and still down
    if (now & ~was) chord_legato = held_through != 0;           // a press: legato if another stayed down
    else if (held_through) chord_legato = true;                 // a let-go or the modifier, under a held chord
  }
  was = now;
  modifier_was = modifier;
}

void handle_chords_button() {
  int sharp_transition = chord_matrix_array[0].read_transition();
  if (sharp_transition > 1 && current_line != -1) {
    button_pushed = true;
  }
  sharp_active = chord_matrix_array[0].read_value() && !alt_chord_layout && !modifier_holding;

  static elapsedMillis open_for[22];   // how long each chord button has read open
  for (int i = 1; i < 22; i++) {
    int value = chord_matrix_array[i].read_transition();
    // A chord button used to pick a key never starts a chord, not while the
    // mode is on and not after it ends until it has been let go.
    //
    // Discarding the transitions in handle_key_change_mode() was not enough on
    // its own. A press only becomes a transition once ten milliseconds of
    // steady contact have passed, measured against a live microsecond clock at
    // the moment it is read, and the key change mode reads near the top of
    // loop() while this reads at the bottom. A press crossing its ten
    // milliseconds between the two was "not yet" there and "pressed" here, and
    // played its chord: on some presses and not others, depending on where in
    // the pass it landed. The same press racing the end of the mode, a button
    // pressed as the preset buttons let go, did the same. Gating here, where
    // the chord would start, closes both.
    //
    // A button counts as let go once it has read open for longer than the
    // debounce, not at the first open reading: one pressed just as the preset
    // buttons are released can still be bouncing, open for a moment and then
    // closed, and would otherwise start its chord ten milliseconds later.
    // raw, like the claim: how long the contact has actually been open, not how
    // long ago the held-back value last read closed
    if (chord_matrix_array[i].read_raw()) open_for[i] = 0;
    if (key_selection_buttons & (1UL << i)) {
      if (!key_change_mode && open_for[i] > 20) key_selection_buttons &= ~(1UL << i);
      continue;
    }
    if (key_change_mode) continue;
    if (value > 1 && !inhibit_button) {
      button_pushed = true;
      chord_repressed = true;
      Serial.print("Button pushed: ");
      Serial.println(i);
      if (current_line == -1) {
        current_line = (i - 1) / 3;
        if (!continuous_chord) {
          trigger_chord = true;
        }
      }
    }
  }
}

// How firm a touch a pad's strength is, 0 to 1. Measured on the stock plate: a soft tap peaks at
// 35-120, a firm one at 300-350; strummed strings read 80-260 (the middle half), 154 typically; a
// fingertip resting lightly reads 100-200 and pressed hard 450. Plucking on touch the strength is
// read as the string sounds, by when a firm tap has already reached its peak and a soft one is
// still creeping up, so soft reads softer; a slow strum rolls onto each pad gently and reads soft,
// a quick one firm.
float touch_firmness(float strength) {
  return constrain((strength - 40) / 210, 0.0f, 1.0f);
}

// How much softer than full a firmness plays, 0 to 1: the square of how far short of full it is,
// so an ordinary strum sounds nearly full and only a really light touch is quiet. At full touch
// velocity a typical strum plays 5 dB down, a soft tap 20.
float touch_softness(float firmness) {
  return touch_velocity / 100.0f * (1 - firmness) * (1 - firmness);
}
void send_string_pressure(uint8_t i, float firmness);

//>>STEREO<<
// String spread places each harp voice between the strings' two chains (see STEREO STRINGS) by its
// gains into the left and right mixers: by pitch, the lowest of the twelve strings as they are tuned
// now to the left and the highest to the right, or neighbouring strings on alternate sides, across
// spread width. The pan keeps its loudness (constant power) and the middle is at full in both, as
// every string was before, centred where pan places the harp. A voice
// is placed as it sounds (pluck_string, midi_in_harp_on), from the frequency it plays, so a
// ribbon or incoming note lands by its pitch too.
static float harp_note_frequency(int16_t note) {
  return pow(2, harp_octave_change) * c_frequency / 4 * temper_ratio(note + transpose_steps);
}

void apply_string_spread(uint8_t i) {
  float position = 0;   // -1 left, 1 right
  if (string_spread_pattern == 1) {
    position = (i % 2) ? 1 : -1;
  } else {
    float low = log2f(harp_note_frequency(current_harp_notes[0]));
    float high = log2f(harp_note_frequency(current_harp_notes[11]));
    if (string_note_freq[i] > 0 && high > low) position = constrain(2 * (log2f(string_note_freq[i]) - low) / (high - low) - 1, -1.0f, 1.0f);
  }
  if (string_spread == 0) {   // in the middle: the left chain alone, the right side its copy
    string_mixer_array[i / 4]->gain(i % 4, 1);
    string_mixer_r_array[i / 4]->gain(i % 4, 0);
    return;
  }
  // centred where pan puts the harp unspread (left at 1, right at pan: hard left at pan 0, the middle at
  // 1), as loud there as it is, and fanned out from it within the two sides (apply_harp_bus leaves the
  // right side at full while the strings spread)
  float centre = atanf(pan), level = sqrtf(1 + pan * pan);
  float angle = constrain(centre + position * string_spread / 100.0f * (float)M_PI / 4, 0.0f, (float)M_PI / 2);
  string_mixer_array[i / 4]->gain(i % 4, level * cosf(angle));
  string_mixer_r_array[i / 4]->gain(i % 4, level * sinf(angle));
}

// The harp's and the chords' dry sides: pan (29) separates them, the harp to the left (its right side
// at pan) and the chords to the right (their left side at pan). Spread strings place themselves,
// centred where pan would put the harp (apply_string_spread), so the harp's right side stays at full.
// Pan and the reverbs each re-apply these from the values as they are now, so a knob on one doesn't
// undo a knob on another.
void apply_harp_bus() {
  string_l_stereo_gain.amplitude(string_dry, 100);
  string_r_stereo_gain.amplitude(string_dry * (string_spread ? 1 : pan), 100);
}

void apply_chord_bus() {
  chords_r_stereo_gain.amplitude(chord_dry, 100);
  chords_l_stereo_gain.amplitude(chord_dry * pan, 100);
}

// At spread 0 the two chains carried the same, so the right one, the strings' whole effects chain
// over again (its delay too), worked for nothing in every preset: it now rests, and the right side
// takes the left's (string_right_source)
void apply_string_spreads() {
  for (uint8_t i = 0; i < 12; i++) apply_string_spread(i);
  bool spread = string_spread > 0;
  all_string_mix_r.gain(3, spread ? 0.02 : 0);   // the transient, as the left has it
  string_right_source.gain(0, spread ? 1 : 0);
  string_right_source.gain(1, spread ? 0 : 1);
}

// Chord ensemble: depth 0 only the dry; at 100 half dry and half the swept delay (the flange object
// already gives half and half)
FLASHMEM void set_chord_ensemble(uint8_t depth) {
  float d = depth / 100.0f;
  // At depth 0 the swept delays only pass the chords through to a mixer input at 0, so they're left
  // to pass them through (filling their lines, so turning the depth up starts from the sound so far)
  // rather than sweep and interpolate for nothing
  if (depth == 0) {
    chord_ensemble_l.voices(FLANGE_DELAY_PASSTHRU, 0, 0);
    chord_ensemble_r.voices(FLANGE_DELAY_PASSTHRU, 0, 0);
  } else {
    chord_ensemble_l.voices(441, 132, 0.53);   // as begin() in setup has them
    chord_ensemble_r.voices(529, 150, 0.71);
  }
  chord_ensemble_mix_l.gain(0, 1 - d);
  chord_ensemble_mix_l.gain(1, d);
  chord_ensemble_mix_r.gain(0, 1 - d);
  chord_ensemble_mix_r.gain(1, d);
}

// Strum velocity: a strum is plucks on neighbouring strings (or zipper positions) one after another
// in the same direction, each under 200 ms after the last; two fingers on strings apart, or a turn
// back, start over. Its speed, the time between neighbours, says how firmly it plays: measured,
// about 150 ms a string for a slow strum and 15 for a fast one, so on a log scale from soft at 150
// to full at 15, at strum velocity's depth. The first string of a strum has nothing to measure and
// leaves it to touch velocity (full if that's off). Called at each pluck, with its position.
float strum_softness(int16_t position) {
  static int16_t last_position = -1;
  static uint32_t last_us = 0;
  static int8_t direction = 0;
  uint32_t now = micros();
  float gap_ms = (now - last_us) / 1000.0f;
  int16_t step = position - last_position;
  float softness = -1;
  if (strum_velocity && last_position >= 0 && abs(step) == 1 && gap_ms < 200 && (direction == 0 || step == direction)) {
    direction = step;
    float f = constrain(logf(150.0f / fmaxf(gap_ms, 1.0f)) / logf(10.0f), 0.0f, 1.0f);
    softness = strum_velocity / 100.0f * (1 - f) * (1 - f);
  } else {
    direction = 0;
  }
  last_position = position;
  last_us = now;
  return softness;
}

// A string sounds: tuned to the note its pad plays now, its envelopes started,
// its midi note sent.
void pluck_string(uint8_t i) {
  looper_voice_taken(true, i);
  string_strum_softness[i] = pluck_strum;
  midi_in_harp[i].owned = false;   // the harp takes the voice back from an incoming note
  string_pluck_firmness[i] = pluck_strength < 0 ? 1 : touch_firmness(pluck_strength);
  string_firmness[i] = string_pluck_firmness[i];
  for (uint8_t k = 0; k < press_slots; k++) press_recent[i][k] = string_pluck_firmness[i];
  if (harp_has_forms() && !harp_ribbon_active()) {   // the ribbon follows its slide instead
    harp_motion(harp_voice_position[i]);
    current_harp_notes[i] = calculate_note_harp(harp_voice_position[i], chord_context_slashed, chord_context_sharp);
  }
  set_harp_voice_frequency(i, current_harp_notes[i]);
  harp_sample_start(i);
  string_resting[i] = false;
  apply_string_firmness(i);
  apply_string_spread(i);
  AudioNoInterrupts();
  string_pluck_array[i]->pluck(pluck_brightness(i));
  envelope_string_vibrato_lfo.noteOn();
  envelope_string_vibrato_dc.noteOn();
  string_enveloppe_filter_array[i]->noteOn();
  string_enveloppe_array[i]->noteOn();
  string_transient_envelope_array[i]->noteOn();
  AudioInterrupts();
  harp_midi_on(i);
  if (touch_pressure) send_string_pressure(i, string_pluck_firmness[i]);   // a channel starts from this note's pressure, not the last one's
  cantus_follow(i);
  // as heard: a looped note plays at full touch velocity, so it keeps the softness it had here
  looper_capture(LOOP_START, i, string_note_freq[i], 1 - sqrtf(string_softness(i)), harp_started_notes[i], harp_note_velocity(i));
}

// A held string pressed harder than it was plucked swells, as far as it would have sounded plucked
// that firmly, and its pressure goes out: each voice's channel pressure in MPE, polyphonic
// aftertouch otherwise.
void press_string(uint8_t i, float strength) {
  if (!touch_pressure) return;
  static uint32_t slot_clock = 0;
  uint32_t now_slot = millis() / press_slot_ms;
  if (now_slot != slot_clock) {
    slot_clock = now_slot;
    press_slot = (press_slot + 1) % press_slots;
    for (uint8_t k = 0; k < 12; k++) press_recent[k][press_slot] = 0;
  }
  press_recent[i][press_slot] = fmaxf(press_recent[i][press_slot], touch_firmness(strength));
  float firmness = 0;
  for (uint8_t k = 0; k < press_slots; k++) firmness = fmaxf(firmness, press_recent[i][k]);
  // swelling only, it keeps the firmest it has been pressed until it is let go
  float swelled = fmaxf(touch_pressure == 2 ? string_firmness[i] : string_pluck_firmness[i], firmness);
  if (fabsf(swelled - string_firmness[i]) >= 0.02f) {
    string_firmness[i] = swelled;
    apply_string_firmness(i);
  }
  uint8_t value = (uint8_t)lroundf(firmness * 127);
  if (abs(value - string_pressure_sent[i]) >= 2 || (value == 0 && string_pressure_sent[i] != 0)) send_string_pressure(i, firmness);
}

void send_string_pressure(uint8_t i, float firmness) {
  if (harp_started_notes[i] == 0) return;
  uint8_t value = (uint8_t)lroundf(firmness * 127);
  if (mpe_mode) queue_midi_event(MIDI_EVT_PRESSURE, 0, value, 0, harp_started_channel[i], harp_started_port[i]);
  else queue_midi_event(MIDI_EVT_POLY, harp_started_notes[i], value, 0, harp_started_channel[i], harp_started_port[i]);
  string_pressure_sent[i] = value;
}

// A string is let go to ring out through its release. With note-off on lift
// its midi note ends here; otherwise it ends with the sound, below.
void release_string(uint8_t i) {
  AudioNoInterrupts();
  string_enveloppe_array[i]->noteOff();
  string_transient_envelope_array[i]->noteOff();
  string_enveloppe_filter_array[i]->noteOff();
  AudioInterrupts();
  if (harp_note_off_on_lift) harp_midi_off(i);
  looper_capture(LOOP_RELEASE, i, 0, 0, 0, 0);
}

// A string is stopped, by a palm or a finger, with the palm mute release
// rather than its own, which is put back for the next pluck. noteOff on a string
// already in its release restarts it from where it is, so a ringing string is
// cut short as well as a held one.
void damp_string(uint8_t i) {
  looper_capture(LOOP_RELEASE, i, 0, 0, 0, 0);
  string_plucked[i] = false;
  AudioNoInterrupts();
  if (string_enveloppe_array[i]->isActive()) {
    string_enveloppe_array[i]->release(palm_mute_release);
    string_enveloppe_array[i]->noteOff();
    string_enveloppe_array[i]->release(string_release);
  }
  AudioInterrupts();
  harp_midi_off(i);
}

void palm_mute() {
  for (uint8_t i = 0; i < 12; i++) {
    damp_string(i);
    pad_voice[i] = -1;   // a hammer has nothing left to take over
  }
  if (rythm_mode && rhythm_palm_break) acc_break_request();   // the band stops with the hand
}

// ---- the zipper plates: a finger between zones ----
// A finger on a band touches both its zones, and the balance between their
// readings says where it is, so a touch is found as the centre of the readings
// across the zones it touches, not from which zones count as touched. That
// matters at this spacing: a fingertip on a core often reads on both
// neighbours too.
const uint16_t plate_end_memory_ms = 1000;   // how long the end a touch left on zipper 24 is remembered (the positions)
const float harp_position_hysteresis = 0.2;  // positions past the halfway mark before a touch moves on, so it doesn't flicker
const float harp_lift_ratio = 0.7;           // a touch reading under this much of its peak is lifting, and stays where it was
const float harp_peak_relax_ms = 80;         // how fast the peak follows a lighter touch, so a strum that eases off still moves
const uint8_t harp_landing_ms = 4;           // a finger pressing down reads weak and uncertain at first; it sounds once it has settled

const uint8_t harp_position_count = 24;

// The ribbon reads any strip but the wheel, which is not a line. Harp drums (343) take the pads back
// from the ribbon, the zipper plates, the hammer and the palm: each pad is a drum, whatever plays the strings.
bool harp_ribbon_active() {
  return harp_ribbon && harp_plate != plate_arcade_wheel && !harp_drums;
}

bool harp_positions_active() {
  return harp_plate == plate_zipper_24 && !harp_drums && !harp_ribbon_active() && (chromatic_harp_mode || scalar_harp_selection != 0);
}

// Whether voice i has a finger on it: on a zipper plate or the ribbon a touch
// sounding it, otherwise its own pad.
bool string_held(uint8_t i) {
  // a key the neighbour guard holds silent (pad_shadow) holds no string, though a finger's edge is on it
  return (harp_positions_active() || harp_ribbon_active()) ? harp_voice_held[i] :
    harp_array[harp_voice_position[i]].read_value() && !pad_shadow[harp_voice_position[i]];
}

// ---- hammer-on ----
// A strum lands on each string beside one plucked a moment before, still held, so only a string
// that has rung hammer_after_ms (hammer-on time, 376; 180 unless set) is taken over: longer than a
// slow strum's 150 ms from string to string. Shorter, a grace note or a trill can follow its string
// at once (a piper's cut, an oud's trill want 40-60 ms), and a slow strum's strings beside one held
// start to be moved instead. A slide goes on from there, since the string it moves is the one
// plucked long ago.

bool hammer_active() {
  return hammer_on && !harp_drums && !harp_pluck_on_lift && !harp_ribbon_active() && !harp_positions_active() && harp_plate != plate_arcade_wheel;
}

// String v moves to pad p's note without a pluck. Over midi the new note starts before the old one
// ends, so a synth that plays legato slides or hammers too; the looper hears a retune.
void hammer_to(uint8_t v, uint8_t p) {
  if (harp_has_forms()) harp_motion(p);
  harp_voice_position[v] = p;
  current_harp_notes[v] = calculate_note_harp(p, chord_context_slashed, chord_context_sharp);
  set_harp_voice_frequency(v, current_harp_notes[v]);
  uint8_t old = harp_started_notes[v];
  if (old != 0 && old != midi_base_note_transposed + midi_out_note(current_harp_notes[v])) {
    uint8_t channel = harp_started_channel[v], port = harp_started_port[v];
    harp_started_notes[v] = 0;
    harp_midi_on(v);
    queue_midi(false, old, harp_release_velocity, channel, port);
    if (touch_pressure) send_string_pressure(v, string_firmness[v]);
  }
  looper_capture(LOOP_RETUNE, v, string_note_freq[v], 1 - sqrtf(string_softness(v)), harp_started_notes[v], harp_note_velocity(v));
  cantus_follow(v);
}

// Pad p lands: beside a held string that has rung long enough, it takes that string over (the
// one plucked last, if there are two). Whether it did.
bool hammer_land(uint8_t p) {
  int8_t taken = -1;
  for (int8_t q = p - 1; q <= p + 1; q += 2) {
    if (q < 0 || q > 11 || !harp_array[q].read_value()) continue;
    int8_t v = pad_voice[q];
    if (v < 0 || midi_in_harp[v].owned || !string_enveloppe_array[v]->isActive()) continue;
    if (millis() - voice_plucked_ms[v] < hammer_after_ms) continue;
    if (taken < 0 || voice_plucked_ms[v] > voice_plucked_ms[taken]) taken = v;
  }
  if (taken < 0) return false;
  pad_voice[p] = taken;
  hammer_to(taken, p);
  return true;
}

// Pad p plucks its own string, which no other pad has hold of from now on.
void hammer_pluck(uint8_t p) {
  for (uint8_t x = 0; x < 12; x++) if (pad_voice[x] == p) pad_voice[x] = -1;
  pad_voice[p] = p;
  voice_plucked_ms[p] = millis();
  if (harp_voice_position[p] != p) {
    harp_voice_position[p] = p;
    current_harp_notes[p] = calculate_note_harp(p, chord_context_slashed, chord_context_sharp);
  }
}

// Pad p lifts. Only the pad a string sounds lets it go: back to the last landed of its other pads
// still held (a pull-off), or to ring out.
void hammer_lift(uint8_t p, const uint32_t *landed) {
  int8_t v = pad_voice[p];
  pad_voice[p] = -1;
  if (v < 0 || harp_voice_position[v] != p) return;
  int8_t back = -1;
  for (uint8_t x = 0; x < 12; x++) {
    if (pad_voice[x] != v || !harp_array[x].read_value()) continue;
    if (back < 0 || landed[x] > landed[back]) back = x;
  }
  if (back >= 0 && !midi_in_harp[v].owned && string_enveloppe_array[v]->isActive()) hammer_to(v, back);
  else release_string(v);
}

// Hammer-on on or off, or the strip read another way. Going off, strings moved by a hammer ring
// out from where they are (a pad lifted later lets go of its own string, not the one it moved),
// and every pad has its own string again. Going on, each pad held has hold of its own; the ribbon
// and zipper plates put their strings back themselves as they go off.
FLASHMEM void hammer_reset(bool on) {
  for (uint8_t i = 0; i < 12; i++) {
    if (!on && harp_voice_position[i] != i) {
      if (harp_array[harp_voice_position[i]].read_value()) release_string(i);
      harp_voice_position[i] = i;
      current_harp_notes[i] = calculate_note_harp(i, chord_context_slashed, chord_context_sharp);
    }
    pad_voice[i] = on && harp_array[i].read_value() ? i : -1;
  }
}

// Tune voice p % 12 to position p, as it plays now.
uint8_t harp_position_voice(uint8_t p) {
  uint8_t v = p % 12;
  harp_voice_position[v] = p;
  current_harp_notes[v] = calculate_note_harp(p, chord_context_slashed, chord_context_sharp);
  return v;
}

// Sounding positions, as the zone transitions do on the stock plate: plucking
// on touch, landing on a position plucks it and leaving it lets it ring;
// plucking on lift, landing on one stops it and leaving it plucks it, whether
// the finger leaves by lifting or by moving on to the next.
void harp_position_land(uint8_t p) {
  uint8_t v = harp_position_voice(p);
  if (harp_pluck_on_lift) {
    damp_string(v);
  } else {
    string_plucked[v] = false;
    pluck_strum = strum_softness(p);
    pluck_string(v);
  }
}

void harp_position_leave(uint8_t p) {
  uint8_t v = harp_position_voice(p);
  if (harp_pluck_on_lift) {
    pluck_strum = strum_softness(p);
    pluck_string(v);
    string_plucked[v] = true;
  } else {
    release_string(v);
  }
}

// Leave all touches without sounding them and give each voice its own string
// back, for a change of plate or of harp mode.
void harp_positions_reset() {
  for (uint8_t t = 0; t < harp_touch_max; t++) {
    if (harp_touches[t].active && !harp_touches[t].palmed && !harp_touches[t].landing && !harp_pluck_on_lift) {
      release_string(harp_touches[t].position % 12);
    }
    harp_touches[t].active = false;
  }
  for (uint8_t i = 0; i < 12; i++) {
    harp_voice_position[i] = i;
    harp_voice_held[i] = false;
    current_harp_notes[i] = calculate_note_harp(i, chord_context_slashed, chord_context_sharp);
  }
}

// Each run of touched zones along a line is a finger. Its centre, in zones
// along the line, takes in one zone more either side, which reads a little
// before it counts as touched, so a finger nearing a band starts to move
// before the far zone crosses its threshold. Line place m is harp zone m % 12.
struct harp_run { float centre; float total; bool palmed; bool matched; };
uint8_t harp_find_runs(const bool *line_down, uint8_t line_length, const bool *zone_down,
                       const int16_t *strength, harp_run *runs, uint8_t max_runs) {
  uint8_t run_count = 0;
  for (uint8_t k = 0; k < line_length && run_count < max_runs; k++) {
    if (!line_down[k]) continue;
    uint8_t lo = k;
    while (k + 1 < line_length && line_down[k + 1]) k++;
    uint8_t hi = k;
    float total = 0, moment = 0;
    bool palmed = false;
    for (int8_t m = (int8_t)lo - 1; m <= (int8_t)hi + 1; m++) {
      if (m < 0 || m >= line_length) continue;
      uint8_t zone = m % 12;
      bool inside = m >= lo && m <= hi;
      if (!inside && zone_down[zone]) continue;   // a neighbour another finger is on
      if (inside) palmed |= string_palmed[zone];
      total += strength[zone];
      moment += strength[zone] * m;
    }
    float centre = total > 0 ? moment / total : (lo + hi) / 2.0;
    runs[run_count++] = harp_run{centre, total, palmed, false};
  }
  return run_count;
}

// Whether the bottom zone, touched without its upper neighbour, is the island above the top zone: it is
// when the top zone is touched with it, or reads clearly more than the bottom zone's upper neighbour (a
// finger at an end leans a little on the zone beside it). On zipper 24 a finger at the very top touches
// the island and reaches the top zone's teeth only lightly: modelled, the top zone stays under the touch
// threshold on 3 touches of 72 there, and those read as the bottom string when only the top zone touched
// counted. Clearly more there, by 10: the chip's noise at the bottom end, where neither neighbour reads,
// flipped a touch on the bottom string to the top (3 of 72 at the bottom, with no margin). The fretless
// wrap's two ends are alike (the bottom zone with its neighbour's teeth tapering in, the island with the
// top zone's), so neither end is favoured there: a finger landing past the middle of an end place, where
// both neighbours read only noise, may be read at the other end (modelled, 1 touch in 100 along the strip,
// all of them within a millimetre or so of the plate's ends), unless a touch is already near one end.
bool wrap_island(const bool *zone_down, const int16_t *strength) {
  int16_t margin = harp_plate == plate_zipper_24 ? 10 : 0;
  return zone_down[0] && !zone_down[1] && (zone_down[11] || strength[11] > strength[1] + margin);
}

void handle_harp_positions() {
  // Zone i of the line is zone i of the harp, and the line has a thirteenth,
  // the bottom zone's island above the top zone. The bottom zone is read as the
  // island when the top zone is touched with it and its own upper neighbour
  // isn't.
  bool zone_down[12];
  bool any_down = false;
  for (uint8_t z = 0; z < 12; z++) {
    zone_down[z] = harp_array[z].read_value();
    any_down |= zone_down[z];
    // the palm's zones are free once lifted, as on the stock plate
    if (harp_array[z].read_transition() == 1) string_palmed[z] = false;
  }
  bool any_touch = false;
  for (uint8_t t = 0; t < harp_touch_max; t++) any_touch |= harp_touches[t].active;
  if (!any_down && !any_touch) return;   // nothing to read: spare the bus

  const uint8_t line_length = 13;
  bool line_down[13];
  for (uint8_t k = 0; k < 12; k++) line_down[k] = zone_down[k];
  line_down[12] = false;
  int16_t strength[12];
  harp_sensor.read_strength(strength);
  // The bottom zone touched without its upper neighbour is the island when the top zone reads clearly more. As a
  // finger lands on or lifts off the very top it reads lightly, and neither neighbour may: on the real plate
  // (plate_cal, 2026-10-17) 3 slides of 4 sounded the bottom string softly as they landed or lifted at the top. So
  // where the two neighbours are within 10 of each other, a touch already near one end is still there, and with
  // none, the end the last touch left within plate_end_memory_ms.
  static bool last_end_top = false;
  static uint32_t last_end_at = 0;
  bool island = wrap_island(zone_down, strength);
  if (zone_down[0] && !zone_down[1] && !zone_down[11] && abs(strength[11] - strength[1]) <= 10) {
    float nearest = harp_position_count;
    bool found_touch = false;
    for (uint8_t t = 0; t < harp_touch_max; t++) {
      if (!harp_touches[t].active) continue;
      float c = harp_touches[t].centre, to_top = harp_position_count - 1 - c;
      if (min(c, to_top) >= nearest) continue;
      nearest = min(c, to_top);
      island = to_top < c;
      found_touch = true;
    }
    if (!found_touch && millis() - last_end_at < plate_end_memory_ms) island = last_end_top;
  }
  if (island) {
    line_down[0] = false;
    line_down[12] = true;
  }

  harp_run runs[7];
  uint8_t run_count = harp_find_runs(line_down, line_length, zone_down, strength, runs, 7);
  for (uint8_t r = 0; r < run_count; r++) runs[r].centre *= 2;   // in positions, two a zone

  uint32_t now = millis();
  const float last = harp_position_count - 1;
  auto settle = [last](float x) -> uint8_t {
    if (x < 0) x = 0;
    if (x > last) x = last;
    return (uint8_t)lroundf(x);
  };
  // Each touch follows the nearest run within reach; a touch with none has
  // lifted, and a run with none is a finger landing.
  for (uint8_t t = 0; t < harp_touch_max; t++) {
    harp_touch &touch = harp_touches[t];
    if (!touch.active) continue;
    int8_t best = -1;
    float best_distance = 3;
    for (uint8_t r = 0; r < run_count; r++) {
      if (runs[r].matched) continue;
      float distance = fabsf(runs[r].centre - touch.centre);
      if (distance < best_distance) { best_distance = distance; best = r; }
    }
    if (best < 0) {
      touch.active = false;
      last_end_top = touch.position > harp_position_count / 2;   // the end it left, for a touch landing there next
      last_end_at = now;
      if (touch.palmed) continue;
      // a tap too quick to settle still sounds, where it was read
      pluck_strength = touch.peak;
      if (touch.landing) {
        touch.position = settle(touch.weight_sum > 0 ? touch.centre_sum / touch.weight_sum : touch.position);
        harp_position_land(touch.position);
      }
      harp_position_leave(touch.position);
      continue;
    }
    harp_run &found = runs[best];
    found.matched = true;
    touch.centre = found.centre;
    touch.palmed |= found.palmed;
    if (touch.landing) {
      touch.centre_sum += found.centre * found.total;
      touch.weight_sum += found.total;
      touch.peak = max(touch.peak, found.total);
      touch.peak_at = now;
      if (now - touch.landed_at < harp_landing_ms) {
        touch.position = settle(found.centre);
        continue;
      }
      touch.landing = false;
      touch.position = settle(touch.weight_sum > 0 ? touch.centre_sum / touch.weight_sum : found.centre);
      pluck_strength = touch.peak;
      if (!touch.palmed) harp_position_land(touch.position);
      continue;
    }
    float relaxed = touch.peak - touch.peak * (now - touch.peak_at) / harp_peak_relax_ms;
    touch.peak = max(found.total, relaxed);
    touch.peak_at = now;
    pluck_strength = touch.peak;
    if (!touch.palmed && !harp_pluck_on_lift) press_string(touch.position % 12, found.total);
    // A finger rising off a band lets go of one zone before the other, which
    // looks like a move onto the one it still touches. Lifting, its readings
    // fall away; moving, they hold. So a touch only moves while it reads firm.
    if (found.total < harp_lift_ratio * touch.peak) continue;
    if (fabsf(found.centre - touch.position) < 0.5 + harp_position_hysteresis) continue;
    uint8_t to = settle(found.centre);
    if (to == touch.position) continue;
    // through every position on the way, as a strum faster than a reading
    // still crosses each string
    if (!touch.palmed) {
      int8_t step = to > touch.position ? 1 : -1;
      for (uint8_t p = touch.position; p != to; p += step) {
        harp_position_leave(p);
        harp_position_land(p + step);
      }
    }
    touch.position = to;
  }
  for (uint8_t r = 0; r < run_count; r++) {
    if (runs[r].matched) continue;
    for (uint8_t t = 0; t < harp_touch_max; t++) {
      harp_touch &touch = harp_touches[t];
      if (touch.active) continue;
      touch = harp_touch{true, settle(runs[r].centre), runs[r].centre, runs[r].total, now, runs[r].palmed,
                         true, now, runs[r].centre * runs[r].total, runs[r].total};
      break;
    }
  }
  for (uint8_t i = 0; i < 12; i++) harp_voice_held[i] = false;
  for (uint8_t t = 0; t < harp_touch_max; t++) {
    if (harp_touches[t].active && !harp_touches[t].landing) harp_voice_held[harp_touches[t].position % 12] = true;
  }
}

// ---- the ribbon: the strip as one fretless string ----
// A touch is found as on the zipper plates, as the centre of the readings
// across the zones it covers, but its place is not settled onto a position: it
// is followed continuously, and the voice it sounds is bent to the pitch there,
// between the notes of the strings either side. A touch lands as a pluck does,
// and lifting lets the voice ring out at the pitch it had; it is a ribbon, so
// it sounds on touch whether or not the strings pluck on lift.
const float ribbon_follow_zones = 2;   // how far a touch may move between two readings and still be the same finger
const float ribbon_string_hysteresis = 0.15;   // strings past halfway before the midi note moves on, outside MPE

// How many strings the strip covers, bottom to top. Chord mode has only the
// twelve it spreads the chord over.
// The fretless wrap plate (10), like zipper 24 (8), has an island of the bottom
// zone above the top one, so the ribbon has thirteen places: a whole octave in
// chromatic mode.
bool ribbon_wrapped() {
  return harp_plate == plate_zipper_24 || harp_plate == plate_fretless_wrap;
}

uint8_t ribbon_strings() {
  uint8_t n = ribbon_span ? ribbon_span : (ribbon_wrapped() ? 13 : 12);
  if (n < 2) n = 2;
  if (!chromatic_harp_mode && scalar_harp_selection == 0 && n > 12) n = 12;
  return n;
}

// A place on the strip, in zones from the bottom one's middle, as a place
// among the strings, held toward the nearest string's note by ribbon snap:
// that much of each string's width plays its note, and the rest of the way
// between two strings the pitch slides at the rate it makes up.
// x held toward the nearest whole number: within snap/2 of it (snap 0-1) x is
// that number, and the rest of the way to the next the value slides at the rate
// it makes up, so it never jumps. Snap 0 leaves x as it is, 1 steps.
float snap_to_whole(float x, float snap) {
  float n = roundf(x);
  if (snap <= 0) return x;
  if (snap >= 1) return n;
  float d = x - n;
  float m = fabsf(d) <= snap / 2 ? 0 : (fabsf(d) - snap / 2) / (1 - snap);
  return n + copysignf(m, d);
}

float ribbon_string_at(float place) {
  float s = place * (ribbon_strings() - 1) / (ribbon_wrapped() ? 12.0f : 11.0f);
  float last = ribbon_strings() - 1;
  if (s < 0) s = 0;
  if (s > last) s = last;
  return snap_to_whole(s, ribbon_snap / 100.0f);
}

// The frequency ratio at a place among the strings, from the strings' own
// notes either side, evenly in pitch between them, in the live tuning. With
// ribbon step snap, the slide between two strings a scale step apart is held,
// more lightly, on each step of the tuning it passes: in 31 the strings of a
// major scale are five steps apart, and the four between are found as easily
// as a finger can rest, while the strings' own notes keep the stronger hold.
float ribbon_ratio_at(float s) {
  uint8_t last = ribbon_strings() - 1;
  uint8_t k = (uint8_t)floorf(s);
  if (k >= last) k = last - 1;
  float f = s - k;
  int16_t n0 = calculate_note_harp(k, chord_context_slashed, chord_context_sharp);
  int16_t n1 = calculate_note_harp(k + 1, chord_context_slashed, chord_context_sharp);
  float l0 = log2f(temper_ratio(n0 + transpose_steps));
  float l1 = log2f(temper_ratio(n1 + transpose_steps));
  float l = l0 + (l1 - l0) * f;
  if (ribbon_step_snap > 0 && abs(n1 - n0) > 1) {
    int16_t lo = min(n0, n1), hi = max(n0, n1);
    float below = log2f(temper_ratio(lo + transpose_steps));
    for (int16_t j = lo; j < hi; j++) {
      float above = log2f(temper_ratio(j + 1 + transpose_steps));
      if (l <= above || j == hi - 1) {
        float g = above > below ? (l - below) / (above - below) : 0;
        l = below + (above - below) * snap_to_whole(constrain(g, 0.0f, 1.0f), ribbon_step_snap / 100.0f);
        break;
      }
      below = above;
    }
  }
  return exp2f(l);
}

// Tune a touch's voice to where it is. The voice keeps the note it landed on,
// as its midi note, and is bent from there: in MPE the bend goes out on the
// voice's own channel. Outside MPE nothing can bend one voice of the harp
// channel alone, so the midi note follows the nearest string instead, note by
// note, while the sound here still slides.
float ribbon_last_place[12];   // where each voice's touch last turned, for melodic direction
const float ribbon_turn = 0.25;  // strings a slide goes back before the melody has turned

void ribbon_tune(ribbon_touch &touch) {
  uint8_t v = touch.voice;
  float s = ribbon_string_at(touch.place);
  if (harp_has_forms()) {
    if (s > ribbon_last_place[v] + ribbon_turn) { harp_descending = false; ribbon_last_place[v] = s; }
    else if (s < ribbon_last_place[v] - ribbon_turn) { harp_descending = true; ribbon_last_place[v] = s; }
    else if (harp_descending ? s < ribbon_last_place[v] : s > ribbon_last_place[v]) ribbon_last_place[v] = s;
  }
  if (touch.string > ribbon_strings() - 1) touch.string = ribbon_strings() - 1;   // the span shrank under it
  if (!mpe_mode && fabsf(s - touch.string) > 0.5f + ribbon_string_hysteresis) {
    touch.string = (uint8_t)lroundf(s);
    harp_voice_position[v] = touch.string;
    current_harp_notes[v] = calculate_note_harp(touch.string, chord_context_slashed, chord_context_sharp);
    if (harp_started_notes[v] != 0) harp_midi_on(v);
  }
  // A chord change retunes the strings, and the note under the touch with them.
  current_harp_notes[v] = calculate_note_harp(touch.string, chord_context_slashed, chord_context_sharp);
  float glide = 12.0f * log2f(ribbon_ratio_at(s) / temper_ratio(current_harp_notes[v] + transpose_steps));
  if (fabsf(glide - harp_voice_glide[v]) < 0.001f && current_harp_notes[v] == harp_voice_current_note[v]) return;
  harp_voice_glide[v] = glide;
  set_harp_voice_frequency(v, current_harp_notes[v]);
}

// A voice for a new touch: one no touch holds, silent if one is, otherwise
// the one that has rung longest.
uint8_t ribbon_voice() {
  static uint8_t next = 0;
  for (uint8_t pass = 0; pass < 2; pass++) {
    for (uint8_t k = 0; k < 12; k++) {
      uint8_t v = (next + k) % 12;
      if (harp_voice_held[v]) continue;
      if (pass == 0 && string_enveloppe_array[v]->isActive()) continue;
      next = (v + 1) % 12;
      return v;
    }
  }
  return next;
}

void ribbon_land(ribbon_touch &touch) {
  uint8_t v = ribbon_voice();
  touch.voice = v;
  touch.sounding = true;
  harp_voice_held[v] = true;
  float s = ribbon_string_at(touch.place);
  ribbon_last_place[v] = s;
  touch.string = (uint8_t)lroundf(s);
  harp_voice_position[v] = touch.string;
  current_harp_notes[v] = calculate_note_harp(touch.string, chord_context_slashed, chord_context_sharp);
  harp_voice_glide[v] = 12.0f * log2f(ribbon_ratio_at(s) / temper_ratio(current_harp_notes[v] + transpose_steps));
  string_plucked[v] = false;
  pluck_string(v);
}

// Let every touch go and bring the voices back to their own strings, for a
// change of plate or of the ribbon itself.
void harp_ribbon_reset() {
  for (uint8_t t = 0; t < harp_touch_max; t++) {
    ribbon_touch &touch = ribbon_touches[t];
    if (touch.active && touch.sounding && !touch.palmed) release_string(touch.voice);
    touch.active = false;
  }
  for (uint8_t i = 0; i < 12; i++) {
    harp_voice_glide[i] = 0;
    harp_voice_position[i] = i;
    harp_voice_held[i] = false;
    current_harp_notes[i] = calculate_note_harp(i, chord_context_slashed, chord_context_sharp);
  }
}

void handle_harp_ribbon() {
  bool zone_down[12];
  bool any_down = false;
  for (uint8_t z = 0; z < 12; z++) {
    zone_down[z] = harp_array[z].read_value();
    any_down |= zone_down[z];
    if (harp_array[z].read_transition() == 1) string_palmed[z] = false;
  }
  bool any_touch = false;
  for (uint8_t t = 0; t < harp_touch_max; t++) any_touch |= ribbon_touches[t].active;
  if (!any_down && !any_touch) return;
  int16_t strength[12];
  harp_sensor.read_strength(strength);
  // With the island above the top zone the line has a thirteenth place. The
  // bottom zone is the island when it is touched with the top zone and not
  // with its own upper neighbour. Touched alone it could be either end: it is
  // the end a touch is already at, or failing that the end whose neighbour
  // reads more, since a finger at one end leans a little on the zone beside it.
  bool wrapped = ribbon_wrapped();
  uint8_t line_length = wrapped ? 13 : 12;
  bool line_down[13];
  for (uint8_t k = 0; k < 12; k++) line_down[k] = zone_down[k];
  line_down[12] = false;
  if (wrapped && zone_down[0] && !zone_down[1]) {
    bool island = wrap_island(zone_down, strength);
    if (!zone_down[11]) {
      float nearest = 13;
      for (uint8_t t = 0; t < harp_touch_max; t++) {
        const ribbon_touch &touch = ribbon_touches[t];
        if (!touch.active) continue;
        float to_bottom = touch.centre, to_island = 12 - touch.centre;
        if (min(to_bottom, to_island) >= nearest) continue;
        nearest = min(to_bottom, to_island);
        island = to_island < to_bottom;
      }
    }
    if (island) {
      line_down[0] = false;
      line_down[12] = true;
    }
  }
  harp_run runs[7];
  uint8_t run_count = harp_find_runs(line_down, line_length, zone_down, strength, runs, 7);

  uint32_t now = millis();
  uint32_t now_us = micros();
  for (uint8_t t = 0; t < harp_touch_max; t++) {
    ribbon_touch &touch = ribbon_touches[t];
    if (!touch.active) continue;
    int8_t best = -1;
    float best_distance = ribbon_follow_zones;
    for (uint8_t r = 0; r < run_count; r++) {
      if (runs[r].matched) continue;
      float distance = fabsf(runs[r].centre - touch.centre);
      if (distance < best_distance) { best_distance = distance; best = r; }
    }
    if (best < 0) {
      touch.active = false;
      if (touch.palmed) continue;   // the palm already stopped it
      // a tap too quick to settle still sounds, where it was read
      if (touch.landing) {
        touch.place = touch.weight_sum > 0 ? touch.centre_sum / touch.weight_sum : touch.centre;
        pluck_strength = touch.peak;
        ribbon_land(touch);
      }
      release_string(touch.voice);
      continue;
    }
    harp_run &found = runs[best];
    found.matched = true;
    touch.centre = found.centre;
    touch.palmed |= found.palmed;
    if (touch.landing) {
      touch.centre_sum += found.centre * found.total;
      touch.weight_sum += found.total;
      touch.peak = max(touch.peak, found.total);
      touch.peak_at = now;
      if (now - touch.landed_at < harp_landing_ms) continue;
      touch.landing = false;
      touch.place = touch.weight_sum > 0 ? touch.centre_sum / touch.weight_sum : found.centre;
      touch.moved_at = now_us;
      pluck_strength = touch.peak;
      if (!touch.palmed) ribbon_land(touch);
      continue;
    }
    if (touch.palmed) continue;
    float relaxed = touch.peak - touch.peak * (now - touch.peak_at) / harp_peak_relax_ms;
    touch.peak = max(found.total, relaxed);
    touch.peak_at = now;
    // A finger rising lets go of one zone before the other, which looks like a
    // slide onto the one it still touches, so the pitch holds while it lifts.
    float dt = (now_us - touch.moved_at) / 1000.0f;
    touch.moved_at = now_us;
    if (found.total >= harp_lift_ratio * touch.peak) {
      float glide = ribbon_glide_ms ? ribbon_glide_ms : 10;
      touch.place += (found.centre - touch.place) * dt / (glide + dt);
    }
    ribbon_tune(touch);
    press_string(touch.voice, found.total);
  }
  for (uint8_t r = 0; r < run_count; r++) {
    if (runs[r].matched) continue;
    for (uint8_t t = 0; t < harp_touch_max; t++) {
      ribbon_touch &touch = ribbon_touches[t];
      if (touch.active) continue;
      touch = ribbon_touch{true, runs[r].palmed, true, false, 0, 0, runs[r].centre, runs[r].centre, runs[r].total,
                           now, now, now_us, runs[r].centre * runs[r].total, runs[r].total};
      break;
    }
  }
  // A voice is held while a touch sounds it; a palmed one is free once stopped.
  for (uint8_t i = 0; i < 12; i++) harp_voice_held[i] = false;
  for (uint8_t t = 0; t < harp_touch_max; t++) {
    const ribbon_touch &touch = ribbon_touches[t];
    if (touch.active && touch.sounding && !touch.palmed) harp_voice_held[touch.voice] = true;
  }
}

// ---- the arcade wheel: where a finger is round the ring ----
// The ring's ten zones, clockwise from the top: each one's harp zone (zone i is
// T(12 - i)), the compass bearing of its middle in degrees, 0 at the top, and
// its width. The arrows' outer zones are 30 degrees wide, the side arrows' 45.
// Upside down the harp's zones are numbered from the other end, so a segment's
// zone is 11 - zone, and the ring's top is where its bottom was: the bearing
// turns half round (clockwise is still clockwise).
struct wheel_segment { uint8_t zone; float bearing; float width; };
const wheel_segment wheel_ring[10] = {
  {10, 0, 30}, {9, 30, 30}, {4, 67.5, 45}, {3, 112.5, 45}, {2, 150, 30},
  {1, 180, 30}, {0, 210, 30}, {7, 247.5, 45}, {8, 292.5, 45}, {11, 330, 30},
};
const uint8_t wheel_angle_cc = 23;   // the bearing, 0-127 clockwise from the top, while the ring is touched
const uint8_t wheel_touch_cc = 24;   // 127 as a finger lands on the ring, 0 as it leaves

// The hub's two zones, A (T6) over the top right and B (T7) below, with a zipper on the seam between.
const uint8_t wheel_hub[2] = {6, 5};

// The bearing is the centre of the readings round the ring, the strongest zone and its neighbours each at
// its middle's bearing, corrected: a fingertip is wider than a 30 degree segment (6 mm of arc at the ring's
// middle), and the centre is pulled towards the middles of the zones it covers. Modelled with a round
// contact over the plate's copper (~/minichord-bench/places, wheel_fit.py), the strongest zone and its
// stronger neighbour, as fw 25-97 read it, were 4.8 degrees out (rms, 7 mm, up to 11, worst on the 45
// degree segments); the centre is 3.8, and corrected by this table 1.8 (up to 7), under a step of the
// control change (2.8). The correction is in tenths of a degree at every 5 degrees of the centre, from
// 2.5; a real board's readings may want it refitted.
static const int8_t wheel_correction[72] = {
  7, 11, 7, 2, -4, -4, 0, -4, -15, -29, -45, -57, -41, 1, 45, 55, 36, 16, -8, -27, -44, -34, 6, 49,
  62, 50, 35, 20, 7, 4, 9, 9, 3, -3, -9, -6, 3, 6, 1, -5, -11, -11, -6, -9, -21, -37, -52, -64, -48, -5,
  37, 49, 32, 12, -13, -31, -50, -37, 4, 45, 61, 50, 35, 21, 9, 7, 11, 12, 7, 0, -6, -2};

// The bearing a finger on the ring is at, from the readings (by where the zones are on the plate)
FLASHMEM float wheel_bearing(const int16_t *strength) {
  uint8_t peak = 0;
  for (uint8_t k = 1; k < 10; k++) {
    if (strength[wheel_ring[k].zone] > strength[wheel_ring[peak].zone]) peak = k;
  }
  float sx = 0, sy = 0;
  for (int8_t j = -1; j <= 1; j++) {
    const wheel_segment &g = wheel_ring[(peak + 10 + j) % 10];
    float w = max(strength[g.zone], (int16_t)0);
    sx += w * sinf(g.bearing * (float)M_PI / 180);
    sy += w * cosf(g.bearing * (float)M_PI / 180);
  }
  float centre = atan2f(sx, sy) * 180 / (float)M_PI;
  if (centre < 0) centre += 360;
  float f = centre / 5 - 0.5f;
  float lo = floorf(f);
  uint8_t i0 = ((int)lo % 72 + 72) % 72, i1 = (i0 + 1) % 72;
  float a = f - lo;
  return centre + (wheel_correction[i0] * (1 - a) + wheel_correction[i1] * a) / 10;
}

// The arcade wheel's zones whose copper meets zone i's (as the harp reads it), as bits: a ring zone's two
// neighbours round the ring and the hub, a hub zone the whole ring and the other hub zone. The neighbour
// guard (416) keeps a ring zone silent that lands under half of a held hub zone: modelled, a press on A or
// B puts a ring zone past the touch threshold one time in ten (a quarter of the time for a broad finger),
// at under half the hub's reading seven times in eight.
FLASHMEM uint16_t wheel_neighbours(uint8_t i) {
  auto at = [](uint8_t zone) -> uint8_t { return upside_down ? 11 - zone : zone; };
  uint8_t p = at(i);
  uint16_t ring = 0, hub = (1u << wheel_hub[0]) | (1u << wheel_hub[1]), mask = 0;
  for (uint8_t k = 0; k < 10; k++) ring |= 1u << wheel_ring[k].zone;
  if (hub >> p & 1) mask = ring | (hub & ~(1u << p));
  for (uint8_t k = 0; k < 10; k++) {
    if (wheel_ring[k].zone != p) continue;
    mask = hub | (1u << wheel_ring[(k + 1) % 10].zone) | (1u << wheel_ring[(k + 9) % 10].zone);
  }
  uint16_t read = 0;   // back to the zones as the harp reads them
  for (uint8_t q = 0; q < 12; q++) if (mask >> q & 1) read |= 1u << at(q);
  return read;
}

// The ring sends its bearing on the harp channel as a control change, for a game to steer or aim with;
// the zones still play their notes as they always have. A finger pressing A or B reaches the ring's inner
// edge too (modelled, a ring zone passes the touch threshold on one press in ten, more for a broad finger),
// but always reads less there than on the hub, and a finger on the ring always reads more there: so the
// ring is touched only while its strongest zone reads more than the hub's.
FLASHMEM void handle_wheel() {
  static bool was_touched = false;
  static int16_t sent = -1;
  static elapsedMillis since;
  // the zones by where they are on the plate: upside down the harp reads them from the other end
  auto at = [](uint8_t zone) -> uint8_t { return upside_down ? 11 - zone : zone; };
  bool down = false;
  for (uint8_t k = 0; k < 10; k++) down |= harp_array[at(wheel_ring[k].zone)].read_value();
  if (down && since < 5) return;   // as often as anything steering needs, and no more
  bool touched = false;
  int16_t strength[12] = {0}, raw[12];
  if (down) {
    since = 0;
    harp_sensor.read_strength(raw);
    for (uint8_t p = 0; p < 12; p++) strength[p] = raw[at(p)];
    int16_t ring = 0, hub = max(strength[wheel_hub[0]], strength[wheel_hub[1]]);
    for (uint8_t k = 0; k < 10; k++) ring = max(ring, strength[wheel_ring[k].zone]);
    touched = ring > hub;
  }
  if (!touched) {
    if (was_touched) queue_midi_cc(wheel_touch_cc, 0, harp_channel, harp_port);
    was_touched = false;
    sent = -1;
    return;
  }
  float bearing = wheel_bearing(strength) + (upside_down ? 180 : 0);
  float steps = bearing * 128 / 360;
  if (!was_touched) queue_midi_cc(wheel_touch_cc, 127, harp_channel, harp_port);
  was_touched = true;
  // as the knobs do, a step past the last value sent before sending, so a
  // finger resting on a boundary doesn't flicker between two
  if (sent >= 0) {
    float moved = fmodf(steps - sent + 128 + 64, 128) - 64;   // the short way round
    if (fabsf(moved) <= 0.7f) return;
  }
  int16_t value = ((int16_t)lroundf(steps) % 128 + 128) % 128;
  if (value != sent) queue_midi_cc(wheel_angle_cc, value, harp_channel, harp_port);
  sent = value;
}

// ---- koto press: bending a ringing string by pressing it ----
// A finger is measured from the lightest it has read since it settled, so
// resting on a string bends nothing however firm the resting finger is, and a
// finger easing off as it lifts brings the pitch back down with it. A full
// press is a fixed firmness, not a fixed amount above the rest: measured from
// the rest, a finger that landed firmly had to push past a hard press to reach
// the top of the bend.
const uint8_t koto_settle_ms = 30;   // a finger pressing down is still landing this long
const int16_t koto_dead = 30;        // strength above the rest that still bends nothing, a finger flattening
const int16_t koto_full = 380;       // strength of a full press: firm, short of a hard one (a light finger reads 100-200, a hard one 430-490)
const int16_t koto_span_min = 100;   // strength from the slack to a full press, at least, for a finger that lands firm
const float koto_pressed = 0.1;      // of a full press, past which the finger has pressed, and its lift plucks nothing

// Bend voice i this many semitones above the note it sounds.
void koto_bend(uint8_t i, float semitones) {
  if (fabsf(semitones - harp_voice_glide[i]) < 0.005f) return;
  harp_voice_glide[i] = semitones;
  set_harp_voice_frequency(i, harp_voice_current_note[i]);
}

void koto_follow(uint8_t i, int16_t strength, uint32_t held_ms) {
  if (held_ms < koto_settle_ms) return;
  if (press_rest[i] < 0 || strength < press_rest[i]) press_rest[i] = strength;
  int16_t span = max(koto_full - press_rest[i] - koto_dead, koto_span_min);
  float amount = constrain((strength - press_rest[i] - koto_dead) / (float)span, 0.0f, 1.0f);
  if (amount >= koto_pressed) string_pressed[i] = true;
  float bend = koto_press * amount;   // in semitones, or in steps of the division
  bend = snap_to_whole(bend, koto_press_snap / 100.0f);
  if (koto_press_steps) bend *= 12.0f / EDO;
  koto_bend(i, bend);
}

// The piano plate's keys whose copper meets, as bits from C: chromatic neighbours, and the two white keys
// either side of a black one, whose fronts touch beside it (B and C are at opposite ends). Upside down the
// plate is read from the other end, so the keys are found by where they sit on it, not by what they now
// play: the second row is the first turned round.
static const uint16_t piano_neighbours[2][12] = {
  {0x006, 0x005, 0x01b, 0x014, 0x02c, 0x0d0, 0x0a0, 0x360, 0x280, 0xd80, 0xa00, 0x600},
  {0x006, 0x005, 0x01b, 0x014, 0x06c, 0x050, 0x0b0, 0x340, 0x280, 0xd80, 0xa00, 0x600}};

// The plates whose strings are twelve zones in a row, each beside the next, read as strings: the stock strip,
// framed strings, and the fretless wrap, whose tapered teeth put a fingertip on two zones wherever it is
// (modelled, half the taps on a string's tick sounded the next string too at 7 mm, nearly all at 9; with
// the guard, none). The strip guard is theirs (on twin rows the harp's order zig-zags between two columns,
// and it would hold back strums: 5 of 192 strum notes in replay; the keymaster's pads never come near each
// other, so it needs none).
FLASHMEM bool strip_family() {
  return harp_plate == plate_stock || harp_plate == plate_framed_strings || harp_plate == plate_fretless_wrap;
}

// The guard's share in tenths for the plate fitted, 0 where it is off
FLASHMEM uint8_t neighbour_guard_level() {
  return harp_plate == plate_piano || harp_plate == plate_arcade_wheel ? neighbour_guard : strip_family() ? strip_guard : 0;
}

// The touch and release thresholds (241, 242): those when set, otherwise the plate's own. The stock strip's
// own, while the strip guard is on and it is read as strings, are 15 and 7, so a light touch on the narrow tips
// of its teeth plays (at 40, 39 of 132 light taps there played nothing) and the guard keeps what more it lets
// through from sounding twice; every other plate's, and the strip's with the guard off, are the chip's stock
// 40 and 20. The keymaster played 12 hard presses of 12 clean at 40, and at 15 a hard press sounded the next
// pad. Framed strings gains next to nothing at 15 (2 of 168 more taps play), so it keeps 40 with its guard.
FLASHMEM bool strip_own_thresholds() {
  return harp_plate == plate_stock && strip_guard && !harp_ribbon_active() && !harp_positions_active();
}

// The touch threshold in force (the chip's own stock one is 40)
FLASHMEM uint8_t harp_touch_threshold_now() {
  return harp_touch_threshold ? harp_touch_threshold : strip_own_thresholds() ? 15 : 40;
}

FLASHMEM void apply_harp_thresholds() {
  bool strip = strip_own_thresholds();
  harp_sensor.set_thresholds(harp_touch_threshold ? harp_touch_threshold : strip ? 15 : 0,
                             harp_release_threshold ? harp_release_threshold : strip ? 7 : 0);
}

FLASHMEM bool neighbour_guard_active() {
  return neighbour_guard_level();
}

// The pads whose copper meets pad i's, as bits: on the stock strip the pad either side
FLASHMEM uint16_t guard_neighbours(uint8_t i) {
  if (harp_plate == plate_arcade_wheel) return wheel_neighbours(i);
  // on the fretless wrap the bottom zone's island meets the top zone too
  uint16_t wrap = harp_plate == plate_fretless_wrap ? (i == 0 ? 1u << 11 : i == 11 ? 1u : 0) : 0;
  return strip_family() ? (((1 << i >> 1) | (1 << i << 1)) & 0xfff) | wrap : piano_neighbours[upside_down][i];
}

// Where the black keys are played, every key on the piano plate is a band 5.4 mm tall at a 6 mm pitch,
// narrower than a fingertip, so a finger reaching for one key lands on its neighbours' copper too, and they
// sounded with it. Measured on the first plate (2026-10-14), one tap in three put a neighbour past the
// touch threshold, at up to three quarters of the key meant; but the key meant always crossed first, so by
// the time its neighbour lands it is well ahead: a stray lands at under half of it, where a second finger
// lands near level with the first, or has an attack of its own. So a key that lands under neighbour_guard
// tenths of a held neighbour is that finger's edge and stays silent while held (a shadow). It plays (lands
// then) if within neighbour_guard_attack_ms it reads half its strongest held neighbour and has grown
// neighbour_guard_attack counts more than all its held neighbours together since it landed: a second
// finger's own attack, where an edge only grows as its finger does. Or, any time, if it grows to
// neighbour_guard_promote tenths of its strongest neighbour that sounds (a finger sliding over); not with
// harp drums, where a late hit is a flam. Only a key that sounds can let a shadow off that way, and only
// while the shadow reads past the touch threshold itself: as a finger lifts every reading falls to a few
// counts, and a stray measured against another stray fading beside it would sound as the finger left (it
// did, on the first try, 70 ms into a tap on C#). On the three recordings (neighbour_sim.py guard_v2 with
// R_old 0.5, which this follows; the attack from one of three rules tried against the recordings), 212
// strays became 13 and 1 of 210 notes of deliberate seconds went missing, where the first try missed 7.
// What strays are left are a finger's edge at over half the key meant, as strong as a lighter second
// finger: telling those apart would take the plate's geometry, not a rule.
// The guard decides in two passes over the keys, so that no key's decision hangs on the order they are
// read in (upside down reads them the other way): first every landing and lift, then whether a shadow held
// from before is let off, measured against the shadows as they stand after the landings.
// On the stock strip (the strip guard, 417) the pads are 5 mm apart, each beside the next, and a fingertip
// covers two of them as well; but there a finger dragged across the strings, a strum, is the instrument's
// own gesture, and each string it reaches lands beside a stronger one, the string it is leaving. Guarded
// as above, 86 of 216 strum notes waited over 10 ms to catch up with the string before, up to 114 ms (18
// strums, 2026-10-14). So on the strip a landing beside a stronger neighbour plays at once if it is the next
// step of a run: the pad beyond that neighbour was already held when the neighbour landed, so the finger
// came to it from there (came_from). A finger's edge lands beside the pad meant with nothing held beyond it,
// and a tap after a strum lands with nothing held, so no time is measured. On four recordings of the strip
// that held 516 of 582 strum steps (the rest a strum's first two strings) and 1 of 546 strays. A strum's
// second string had nothing before it and lands as a shadow; the run reaching the third string carries it,
// and it lands then too (guard_carried), or it would lift unheard as the finger moved on. Otherwise it plays
// as it catches up, 12-15 ms late in a fast strum and up to 140 in a slow one, the only strum note held back.
// Pass one takes a pad's transition (harp_array's read_transition: 2 landed, 1 lifted, 0 neither) and gives
// what the harp does with it.
// held_before: the pads held before this pass's landings, as bits.
FLASHMEM int neighbour_guard_land(uint8_t i, int value, const int16_t (&strength)[12], uint32_t now, uint16_t held_before) {
  uint16_t neighbours = guard_neighbours(i);
  if (value == 2) {
    int16_t strongest = 0;
    int8_t k = -1;
    for (uint8_t j = 0; j < 12; j++) {
      if ((neighbours >> j & 1) && harp_array[j].read_value() && strength[j] > strongest) {
        strongest = strength[j];
        k = j;
      }
    }
    pad_shadow[i] = strongest && strength[i] * 10 < strongest * neighbour_guard_level();
    int8_t beyond = 2 * k - i;
    if (strip_family() && k >= 0 && (held_before >> k & 1) && beyond >= 0 && beyond < 12 && (came_from[k] >> beyond & 1)) {   // a run
      pad_shadow[i] = false;
      if (pad_shadow[k]) {
        pad_shadow[k] = false;
        guard_carried[k] = true;
      }
    }
    if (pad_shadow[i]) {
      shadow_since[i] = now;
      for (uint8_t j = 0; j < 12; j++) shadow_from[i][j] = strength[j];
    }
    return pad_shadow[i] ? 0 : 2;
  }
  if (!pad_shadow[i]) return value;
  if (value == 1 || !harp_array[i].read_value()) pad_shadow[i] = false;   // lifted unheard: nothing to let go
  return 0;
}

// Pass two: whether shadow i, held since before this pass, is let off now (lands now)
FLASHMEM bool neighbour_guard_let_off(uint8_t i, const int16_t (&strength)[12], uint32_t now) {
  uint16_t neighbours = guard_neighbours(i);
  int16_t strongest = 0, sounding = 0, grown = strength[i] - shadow_from[i][i];
  for (uint8_t j = 0; j < 12; j++) {
    if (!(neighbours >> j & 1) || !harp_array[j].read_value()) continue;
    strongest = max(strongest, strength[j]);
    if (!pad_shadow[j]) sounding = max(sounding, strength[j]);
    grown -= max(0, strength[j] - shadow_from[i][j]);
  }
  bool early = now - shadow_since[i] <= neighbour_guard_attack_ms;
  int16_t least = harp_touch_threshold_now();
  // On the stock strip two fingertips don't fit on teeth 5 mm apart, and a finger across two teeth reads
  // near level on both and grows on both, so there no second finger's attack lets a shadow off, and one
  // plays only once it has caught up with the string that sounds (a finger rolled over onto it): with
  // these the strip played one note for 93 taps of 131 at a guard of 7, where with the piano's 69 did.
  bool strip = strip_family();
  bool attack = !strip && early && strongest && strength[i] * 2 >= strongest && grown > neighbour_guard_attack;
  bool caught_up = (early || !harp_drums) && sounding && strength[i] >= least &&
    strength[i] * 10 >= sounding * (strip ? strip_guard_promote : neighbour_guard_promote);
  return attack || caught_up;
}

// Every key's transition this pass, as the guard has it: pad_move[i] is what the harp does (2 lands, 1
// lifts, 0 neither). A shadow let off lands now, for the palm, koto and hammer-on's timing too (landed).
FLASHMEM void neighbour_guard_pass(uint8_t (&pad_move)[12], const int16_t (&strength)[12], uint32_t *landed, uint32_t now) {
  bool held_shadow[12];
  uint8_t raw[12];
  uint16_t held_before = 0;
  for (uint8_t i = 0; i < 12; i++) {
    raw[i] = harp_array[i].read_transition();
    if (harp_array[i].read_value() && raw[i] != 2) held_before |= 1 << i;
  }
  for (uint8_t i = 0; i < 12; i++) {
    held_shadow[i] = pad_shadow[i];
    pad_move[i] = neighbour_guard_land(i, raw[i], strength, now, held_before);
    if (raw[i] == 1 && pad_move[i] != 1) string_palmed[i] = false;   // a shadow under a palm lifts unheard, and unpalmed
    held_shadow[i] &= pad_shadow[i];
  }
  for (uint8_t i = 0; i < 12; i++) if (raw[i] == 2) came_from[i] = held_before;
  for (uint8_t i = 0; i < 12; i++) {   // carried by a strum on the strip, whichever side it came from
    if (!guard_carried[i]) continue;
    guard_carried[i] = held_shadow[i] = false;
    pad_move[i] = 2;
    landed[i] = now;
  }
  bool let_off[12];
  for (uint8_t i = 0; i < 12; i++) let_off[i] = held_shadow[i] && neighbour_guard_let_off(i, strength, now);
  for (uint8_t i = 0; i < 12; i++) {
    if (!let_off[i]) continue;
    pad_shadow[i] = false;
    pad_move[i] = 2;
    landed[i] = now;
  }
}

// With the guard off on another plate, no key is held silent. Turned off on the piano plate itself (416 at 0)
// with a shadow still held, the shadow's lift is still swallowed (handle_harp), or it would pluck on lift a
// note that never sounded.
FLASHMEM void neighbour_guard_settle(bool stock) {
  for (uint8_t i = 0; i < 12; i++) if (!stock || !harp_array[i].read_value()) pad_shadow[i] = false;
}

//>>PLATE SWAP<<
// A plate taken off or put on with the power on. The touch chip reads each zone against a resting level (its
// baseline) that follows a change only a count at a time, and hardly at all while the zone reads as touched.
// So a plate with more copper put on reads, on every zone at once, as twelve fingers held down: the strings
// stuck held and nothing new played, until the baseline crawled down (for minutes). One with less copper read
// above its rest, and a finger had to press far harder to count, for seconds. Plate detect couldn't help, as
// it waited for an untouched harp. And a short on a line as a plate goes on can make the chip flag over
// current, which stops it until the flag is cleared; it was never cleared, and the harp went silent (harp.cpp).
// So the swap is watched, from the chip's readings every plate_swap_poll_ms:
//  - coming off: plate_swap_zones zones or more read over plate_swap_lift above their rest. A touch only ever
//    brings a reading down, so nothing else does that, but a hand lifted after a recalibration made under it,
//    which wants recalibrating too.
//  - going on: plate_swap_zones zones or more land within plate_swap_land_ms of each other. A hand rolls on
//    over tens of milliseconds and seldom covers ten zones; a plate's copper lands on all twelve at once.
//    Those landings are swallowed, so the twelve strings don't all sound as it goes on. But a plate pressed
//    onto its pins can land slower than that: on the first real swap (2026-10-17, the piano plate onto bare
//    pins) it did, and the harp sat silent 28 s, all twelve held, until the stuck rule below caught it. So
//    once a swap settles with the plate off (its readings over plate_swap_bare_counts above where they were before),
//    the harp is bare, and plate_swap_zones zones or more falling plate_swap_drop under their rest is a plate
//    going on, however slowly it lands. Measured on that swap: off, 61-126 counts up; on, 134-186 down.
//  - left stuck: all twelve zones touched and still to within plate_swap_steady_counts for plate_swap_stuck_ms.
//  - the over current flag.
// Then the strings are let go and the harp waits, playing nothing, until every zone holds still to within
// plate_swap_steady_counts for plate_swap_settle_ms, recalibrates the chip there, swallows what the zones do as
// they settle, and asks plate detect to look at once. Each step is said on the serial port, with the readings,
// so the thresholds can be measured on real swaps: none of them has been yet.
const uint16_t plate_swap_poll_ms = 20;
const int16_t plate_swap_lift = 15;          // counts above rest
const int16_t plate_swap_drop = 30;          // counts under rest, on a bare harp
const int16_t plate_swap_bare_counts = 20;   // counts, on average, a settled swap reads above the plate before it
const uint8_t plate_swap_left_zones = 3;     // zones left above their rest, untouched ...
const uint16_t plate_swap_left_ms = 300;     // ... this long: recalibrated too
const uint8_t plate_swap_zones = 10;
const uint8_t plate_swap_land_ms = 30;
const uint8_t plate_swap_steady_counts = 4;
const uint16_t plate_swap_settle_ms = 300;
const uint16_t plate_swap_stuck_ms = 3000;
const uint16_t plate_swap_after_ms = 60;     // after recalibrating, how long the zones' transitions are swallowed
enum { SWAP_NONE, SWAP_SETTLING, SWAP_AFTER };

// every string let go and every way of playing set back, for a swap
FLASHMEM static void harp_quiet() {
  for (uint8_t i = 0; i < 12; i++) {
    harp_array[i].read_transition();   // what landed with the plate is swallowed
    pad_shadow[i] = string_palmed[i] = false;
    if (string_enveloppe_array[i]->isActive()) release_string(i);
  }
  harp_positions_reset();
  harp_ribbon_reset();
}

FLASHMEM static void plate_swap_begin(const char *why, const uint16_t (&f)[12], const uint16_t (&b)[12]) {
  Serial.printf("Plate swap: %s; zones above (+) or below (-) their rest:", why);
  for (uint8_t z = 0; z < 12; z++) Serial.printf(" %+d", (int)f[z] - (int)b[z]);
  Serial.println();
  harp_quiet();
  plate_swap_state = SWAP_SETTLING;
  if (plate_id_learn_wanted) Serial.println("Plate detect: the plate being learned was swapped: not learned");
  plate_id_learn_wanted = false;   // the plate it was to learn is coming off, or another is going on
}

// From handle_harp, after the chip is read: true while a swap is being waited out (the harp does nothing else)
FLASHMEM bool plate_swap_watch() {
  static elapsedMillis since_poll, steady_for, since_after;
  static uint16_t lo[12], hi[12];
  static bool was_down[12] = {false};
  static uint32_t landed_at[12] = {0};
  static elapsedMillis all_still;
  static uint16_t still_lo[12], still_hi[12];
  static bool coming_off = false;            // this swap began as a plate coming off
  static bool going_on = false;              // this swap began as a plate going on
  static elapsedMillis left_for;             // how long plate_swap_left_zones or more have been left above rest
  static int32_t before = 0;                 // the readings' sum before it (the plate's rest)
  static int32_t bare_sum = 0;               // the readings' sum on the bare pins, once a swap has settled there
  if (plate_swap_state == SWAP_AFTER) {
    for (uint8_t i = 0; i < 12; i++) {
      harp_array[i].read_transition();
      was_down[i] = harp_array[i].read_value();
    }
    if (since_after < plate_swap_after_ms) return true;
    plate_swap_state = SWAP_NONE;
    plate_id_check_wanted = true;
    Serial.println("Plate swap: playing again");
    return false;
  }
  // going on: the landings this pass, and how many landed within plate_swap_land_ms
  uint32_t now = millis();
  uint8_t landing = 0;
  bool all_down = true;
  for (uint8_t i = 0; i < 12; i++) {
    bool down = harp_array[i].read_value();
    if (down && !was_down[i]) landed_at[i] = now;
    was_down[i] = down;
    all_down &= down;
    if (down && now - landed_at[i] <= plate_swap_land_ms) landing++;
  }
  bool poll = since_poll >= plate_swap_poll_ms || plate_swap_state != SWAP_NONE || harp_sensor.over_current || landing >= plate_swap_zones;
  if (!poll) return false;
  since_poll = 0;
  uint16_t f[12], b[12];
  if (harp_sensor.over_current) {
    harp_sensor.over_current = false;
    uint16_t zero[12] = {0};
    plate_swap_begin("the touch chip flagged over current", zero, zero);
    harp_sensor.recalibrate();   // it stopped: start it again before there is anything to read
    steady_for = 0;
    for (uint8_t z = 0; z < 12; z++) lo[z] = 0xFFFF, hi[z] = 0;
    return true;
  }
  harp_sensor.read_levels(f, b);
  if (plate_swap_state == SWAP_NONE) {
    uint8_t lifted = 0, dropped = 0;
    for (uint8_t z = 0; z < 12; z++) {
      lifted += f[z] > b[z] + plate_swap_lift;
      dropped += f[z] + plate_swap_drop < b[z];
    }
    // all twelve held, and still: kept against the readings since all twelve went down
    if (!all_down) all_still = 0;
    bool still = all_down;
    for (uint8_t z = 0; z < 12 && all_down; z++) {
      if (all_still < plate_swap_poll_ms + 2) still_lo[z] = still_hi[z] = f[z];
      still_lo[z] = min(still_lo[z], f[z]);
      still_hi[z] = max(still_hi[z], f[z]);
      if (still_hi[z] - still_lo[z] > plate_swap_steady_counts) { all_still = 0; still = false; }
    }
    bool any_down = false;
    for (uint8_t i = 0; i < 12; i++) any_down |= harp_array[i].read_value();
    if (lifted < plate_swap_left_zones || any_down) left_for = 0;
    coming_off = going_on = false;
    if (lifted >= plate_swap_zones || left_for >= plate_swap_left_ms) {
      // a plate coming off; or some zones left above their rest, untouched: the rest of a plate pulled off one
      // header at a time, or a hand lifted after a recalibration made under it
      plate_swap_begin(lifted >= plate_swap_zones ? "a plate coming off" : "zones left above their rest", f, b);
      coming_off = true;
      before = 0;
      for (uint8_t z = 0; z < 12; z++) before += b[z];
    }
    else if (plate_swap_bare && dropped >= plate_swap_zones) {
      plate_swap_begin("a plate going on, onto the bare pins", f, b);
      going_on = true;
    }
    else if (landing >= plate_swap_zones) {
      plate_swap_begin("a plate going on", f, b);
      going_on = true;
    }
    else if (still && all_still >= plate_swap_stuck_ms) plate_swap_begin("all twelve zones held still", f, b);
    else return false;
    left_for = 0;
    steady_for = 0;
    for (uint8_t z = 0; z < 12; z++) lo[z] = hi[z] = f[z];
    return true;
  }
  // settling: wait for every zone to hold still, then recalibrate there
  bool steady = true;
  for (uint8_t z = 0; z < 12; z++) {
    lo[z] = min(lo[z], f[z]);
    hi[z] = max(hi[z], f[z]);
    steady &= hi[z] - lo[z] <= plate_swap_steady_counts;
  }
  for (uint8_t i = 0; i < 12; i++) harp_array[i].read_transition();   // nothing plays meanwhile
  if (!steady) {
    steady_for = 0;
    for (uint8_t z = 0; z < 12; z++) lo[z] = hi[z] = f[z];
    return true;
  }
  if (steady_for < plate_swap_settle_ms) return true;
  int32_t sum = 0;
  for (uint8_t z = 0; z < 12; z++) sum += f[z];
  // off and still off (not put straight back on, or another plate on, before it settled)
  // coming off and settled well above the plate it left: no plate on. A plate pulled off one header at a time
  // (logged 2026-10-17 08:07) settles half off first, and the rest is the swap below of zones left above rest,
  // still a plate coming off
  // Or near where the bare pins read before: a plate touched to the pins and lifted off again before it
  // settled (logged 2026-10-17 08:12:45) began as one going on and settled bare, and plate detect, told a plate
  // was on, matched the bare pins to a print learned from them
  bool off_again = bare_sum && sum + 12 * plate_swap_bare_counts / 2 > bare_sum;
  plate_swap_bare = (coming_off && sum > before + 12 * plate_swap_bare_counts) || off_again;
  if (plate_swap_bare) bare_sum = max(bare_sum, sum);   // the highest: a plate pulled off in halves settles half bare first
  Serial.printf("Plate swap: settled%s; recalibrating at", plate_swap_bare ? ", no plate on" : "");
  for (uint8_t z = 0; z < 12; z++) Serial.printf(" %d", f[z]);
  Serial.println();
  harp_sensor.recalibrate();
  plate_swap_state = SWAP_AFTER;
  since_after = 0;
  return true;
}

void handle_harp() {
  harp_sensor.update(harp_array);
  if (plate_swap_watch()) return;
  if (harp_sensor.thresholds_pending()) {
    bool clear = true;
    for (int i = 0; i < 12; i++) clear &= !harp_array[i].read_value();
    if (clear) harp_sensor.apply_thresholds();
  }
  // before the ribbon and zipper plates take the strings over, so a hammered string is let go first
  static bool hammer_was = false;
  bool hammer = hammer_active();
  if (hammer != hammer_was) {
    hammer_reset(hammer);
    hammer_was = hammer;
  }
  static bool positions_were_active = false;
  static bool ribbon_was_active = false;
  static uint8_t plate_was = 0;
  bool ribbon = harp_ribbon_active();
  if (ribbon != ribbon_was_active || (ribbon && harp_plate != plate_was)) {
    harp_ribbon_reset();
    ribbon_was_active = ribbon;
  }
  bool positions = harp_positions_active();
  if (positions != positions_were_active || (positions && harp_plate != plate_was)) {
    harp_positions_reset();
    positions_were_active = positions;
  }
  plate_was = harp_plate;
  // A fingertip strum holds two or three pads at a time and moves on; a hand
  // laid flat holds many at once. The pads the hand landed on before the count
  // was reached have already sounded, and are stopped with the rest, which is
  // the muted chuck. The palm is the pads down as it lands, and the few that
  // arrive just after, since a flat hand does not land all in one instant;
  // those stay silent until lifted. Any other pad plays as usual, so fingers
  // can hold the mute while the rest of the hand strums. The palm is over once
  // all of its pads have lifted, and only then can another one land, so
  // strumming beside a held palm never mutes again.
  // When each pad last landed, for the palm when plucking on lift.
  static uint32_t pad_landed[12] = {0};
  static bool pad_was_down[12] = {false};
  uint32_t now = millis();
  for (int i = 0; i < 12; i++) {
    bool down = harp_array[i].read_value();
    if (down && !pad_was_down[i]) pad_landed[i] = now;
    pad_was_down[i] = down;
  }
  // Touch velocity and pressure read the pads' strengths while the stock plate is touched. Plucking
  // on lift, a string sounds as firmly as it was held at its firmest.
  static int16_t pad_strength[12] = {0};
  static int16_t pad_firmest[12] = {0};
  bool stock = !positions && !ribbon;
  bool guard = stock && neighbour_guard_active();
  if (!guard) neighbour_guard_settle(stock);
  if (stock && (touch_velocity || touch_pressure || (koto_press && harp_pluck_on_lift) || harp_drums || guard)) {
    bool any = false;
    for (int i = 0; i < 12; i++) any |= harp_array[i].read_value() || harp_array[i].read_raw();
    if (any) {
      harp_sensor.read_strength(pad_strength);
      for (int i = 0; i < 12; i++) if (harp_array[i].read_value()) pad_firmest[i] = max(pad_firmest[i], pad_strength[i]);
    }
  }
  // The neighbour guard decides every landing first, so the palm counts only keys that sound; a key it
  // lets off later lands then, for the palm, koto and hammer-on's timing too.
  uint8_t pad_move[12];
  if (guard) neighbour_guard_pass(pad_move, pad_strength, pad_landed, now);
  if (palm_mute_pads && !harp_drums) {   // pads hit together are a flam, not a palm
    static bool palm_down = false;
    static uint32_t palm_landed = 0;
    // Plucking on lift, fingers placed on strings are a chord waiting to be
    // played, and a chord of four placed pads looks like a palm by count alone.
    // A palm lands all at once and placed fingers one by one, so only pads that
    // landed together count, and only they are silenced: strings placed before
    // the palm keep their pluck.
    // On the ribbon and the zipper plates a finger reads up to three neighbouring zones, so there a run
    // of three or fewer is one finger, and only a wider run (a hand) counts zone by zone
    uint8_t held = 0, run = 0;
    bool line = ribbon || positions;
    for (int i = 0; i <= 12; i++) {
      bool counts = i < 12 && harp_array[i].read_value() && !pad_shadow[i] && (!harp_pluck_on_lift || now - pad_landed[i] <= palm_spread_ms);
      if (counts) { run++; continue; }
      held += (line && run <= 3) ? (run > 0) : run;
      run = 0;
    }
    if (!palm_down && held >= palm_mute_pads) {
      palm_mute();
      palm_down = true;
      palm_landed = now;
    }
    if (palm_down) {
      bool palm_held = false;
      for (int i = 0; i < 12; i++) {
        if (!harp_array[i].read_value()) continue;
        bool landed_with_palm = !harp_pluck_on_lift || pad_landed[i] + palm_spread_ms >= palm_landed;
        uint8_t landing = harp_pluck_on_lift ? palm_landing_lift_ms : palm_landing_ms;
        if (now - palm_landed < landing && landed_with_palm) string_palmed[i] = true;
        if (string_palmed[i]) palm_held = true;
      }
      if (!palm_held) palm_down = false;
    }
  }
  if (ribbon) handle_harp_ribbon();
  if (positions) handle_harp_positions();
  if (harp_plate == plate_arcade_wheel) handle_wheel();
  // A press let go of by a change of mode: the string goes back to its pitch.
  for (int i = 0; i < 12; i++) {
    if (string_pressing[i] && (!stock || !harp_pluck_on_lift || !koto_press)) {
      string_pressing[i] = false;
      koto_bend(i, 0);
    }
  }
  for (int i = 0; i < 12 && stock; i++) {
    int value = guard ? pad_move[i] : harp_array[i].read_transition();
    if (!guard && pad_shadow[i]) {   // the guard turned off under a shadow: its lift stays unheard
      if (value == 1) pad_shadow[i] = string_palmed[i] = false;
      value = 0;
    }
    if (harp_drums) {   // the strings are the kit's pads: a hit on the touch, nothing pitched
      string_palmed[i] = false;   // a palm from before the drums came on
      if (value == 2) drum_pad(i, pad_strength[i]);
      continue;
    }
    if (value == 2 && string_palmed[i]) {
      // landed under the palm: silent until lifted
    } else if (value == 1 && string_palmed[i]) {
      string_palmed[i] = false;   // the palm already stopped it
    } else if (harp_pluck_on_lift) {
      // As on a harp, the finger on a string stops it and the string sounds as
      // the finger leaves it. Nothing sounds while a pad is held, so strings can
      // be placed ahead of time, under a chord yet to come, and lifted together
      // or one by one.
      // With koto press a finger on a ringing string presses it instead, and
      // bends it as far as it is pressed.
      if (value == 2) {
        pad_firmest[i] = pad_strength[i];
        if (koto_press && string_enveloppe_array[i]->isActive() && !midi_in_harp[i].owned) {
          string_pressing[i] = true;
          string_pressed[i] = false;
          press_rest[i] = -1;
        } else {
          damp_string(i);
        }
      } else if (value == 1) {
        bool pressed = string_pressing[i] && string_pressed[i];
        if (string_pressing[i]) koto_bend(i, 0);
        string_pressing[i] = false;
        if (!pressed) {
          pluck_strength = pad_firmest[i];
          pluck_strum = strum_softness(i);
          pluck_string(i);
          string_plucked[i] = true;
        }
      } else if (string_pressing[i] && harp_array[i].read_value()) {
        koto_follow(i, pad_strength[i], now - pad_landed[i]);
      }
    } else if (value == 2) {
      if (!hammer || !hammer_land(i)) {
        string_plucked[i] = false;
        pluck_strength = pad_strength[i];
        pluck_strum = strum_softness(i);
        if (hammer) hammer_pluck(i);
        pluck_string(i);
      }
    } else if (value == 1) {
      if (hammer) hammer_lift(i, pad_landed);
      else release_string(i);
    }
    // a held pad presses the string it sounds; one a hammer has moved on from presses nothing
    if (!harp_pluck_on_lift && harp_array[i].read_value() && !string_palmed[i] && !pad_shadow[i]) {
      int8_t v = hammer ? pad_voice[i] : i;
      if (v >= 0 && harp_voice_position[v] == i) press_string(v, pad_strength[i]);
    }
  }
  pluck_strength = -1;   // a pluck from anywhere else is full
  pluck_strum = -1;
  for (int i = 0; i < 12; i++) {
    // A string plucked on lift has no finger on it to hold it at sustain: once
    // it has come through its decay it rings out through its release, as a
    // quick touch does when plucking on touch.
    if (string_plucked[i] && string_enveloppe_array[i]->isSustain()) {
      string_plucked[i] = false;
      release_string(i);
    }
    // A string let go of keeps its midi note until its release has run out, so
    // the far end hears the ring the strings here make.
    if (harp_started_notes[i] != 0 && !string_held(i) && !string_enveloppe_array[i]->isActive()) {
      harp_midi_off(i);
    }
  }
}


bool rollover_pending = false;      // a chord change by overlap: the new line is set, the chord follows next pass
bool chord_release_pending = false; // the chord hand lifted while slashing; waiting to see if the bass follows
const uint16_t slash_release_grace = 80; // ms to tell a full slash release from a deliberate exit
void handle_chord_type(bool button_maj, bool button_min, bool button_seventh) {
  static uint8_t previous_button_count = 0;   // the set of type buttons currently believed
  static elapsedMillis shrink_timer;
  static elapsedMillis believed_for;     // how long the believed set has stood
  static bool growth_pending = false;    // a button added to an established set, not yet believed
  static elapsedMillis growth_timer;
  static bool press_held_back = false;   // a press's rebuild swallowed while waiting
  uint8_t count = (uint8_t)button_maj + (uint8_t)button_min + (uint8_t)button_seventh;

  if (count == 0) {
    previous_button_count = 0;
    growth_pending = false;
    press_held_back = false;
    // Releasing one chord while already pressing the next is a chord change,
    // not a release. The new line's press transition fired while this line
    // still owned the chord and was consumed doing nothing, so without this
    // the old chord kept ringing and the new one could not be selected short
    // of releasing everything and pressing again.
    if (!inhibit_button) {
      for (int i = 1; i < 22; i++) {
        if (key_selection_buttons & (1UL << i)) continue;   // still held from picking a key: not the next chord
        if (chord_matrix_array[i].read_value()) {
          // While a slash is engaged, the chord hand lifting is only half a
          // gesture: if the bass follows within the release grace this was the
          // slash being let go, and it should end as itself, not become the
          // bass line's chord on the way out. The slash keeps sounding while
          // we wait, so the wait is inaudible.
          if (slash_chord) {
            static elapsedMillis chord_release_timer;
            if (!chord_release_pending) {
              chord_release_pending = true;
              chord_release_timer = 0;
              return;
            }
            if (chord_release_timer < slash_release_grace) return;
            chord_release_pending = false;
          }
          // the gesture that ends here is over: the new line is a chord, not a bass
          slash_chord = false;
          current_line = (i - 1) / 3;
          rollover_pending = true; // pick the chord up next pass, once this line's type buttons are read
          return;
        }
      }
    }
    rollover_pending = false;
    chord_release_pending = false;
    // Whatever slash state the gesture ended in is over with it; a stale flag
    // here would slash the next chord played from silence.
    slash_chord = false;
    current_line = -1;
    return;
  }
  if (rollover_pending) {
    rollover_pending = false;
    button_pushed = true;
    if (!continuous_chord) {
      trigger_chord = true;
    }
  }

  // Moving between chords on the same line overlaps the way moving between
  // lines does: going from C to C7, the seventh goes down a moment before the
  // major comes up. For those tens of milliseconds both are held, which is the
  // combination for a major seventh. Believed at once, every C to C7 played
  // legato sounded Cmaj7 for the overlap, and since nothing rebuilt the chord
  // when the major then lifted, it stayed Cmaj7.
  //
  // So once a chord is established, a change to its set of type buttons has to
  // stand for chord_type_grace before it is believed, the same way a slash has
  // to (slash_grace, in detect_slash()):
  //
  // - A button added to an established set waits. If one of the held buttons
  //   lets go within the grace, it was a change of chord (C to C7), and the new
  //   set is taken as soon as the count is back where it was. If nothing lets
  //   go, it was an addition (C, then add the seventh for Cmaj7), taken after
  //   the grace.
  // - A button taken away waits too. Letting go of a combination, the buttons
  //   lift a few milliseconds apart, and the chord must not become the smaller
  //   one on the way out; if the rest follow within the grace, it was a release.
  //
  // A chord pressed from silence is not established, so its buttons are taken
  // as they land, the way they always were: a Cmaj7 played as one gesture
  // sounds at once. And whenever the believed chord changes while a line is
  // sounding, the sounding chord is rebuilt, which is what makes C to C7 arrive
  // at C7 rather than stopping at whatever the overlap left behind.
  bool resolved = false;

  if (count < previous_button_count) {
    if (growth_pending) { growth_pending = false; resolved = true; }
    if (shrink_timer < chord_type_grace) return;
  } else {
    shrink_timer = 0;
    if (count > previous_button_count) {
      if (!growth_pending && previous_button_count > 0 && believed_for >= chord_type_grace) {
        growth_pending = true;
        growth_timer = 0;
      }
      if (growth_pending) {
        if (growth_timer < chord_type_grace) {
          // The press that started this already asked for a rebuild. Hold it
          // back: rebuilding now would sound the old chord again (a retrigger,
          // with retrigger on), and the rebuild comes when this resolves.
          if (button_pushed) { press_held_back = true; button_pushed = false; }
          return;
        }
        growth_pending = false;
        resolved = true;
      }
    } else if (growth_pending) {
      // back to the believed count with a different button: a change of chord
      growth_pending = false;
      resolved = true;
    }
  }
  if (count != previous_button_count) believed_for = 0;
  previous_button_count = count;

  uint8_t (*chosen)[7] = current_chord;
  if (alt_chord_layout) {
    if (button_maj && !button_min && !button_seventh)            chosen = alt_chord_for(0);
    else if (!button_maj && button_min && !button_seventh)       chosen = alt_chord_for(1);
    else if (!button_maj && !button_min && button_seventh)       chosen = alt_chord_for(2);
    else if (button_maj && !button_min && button_seventh)        chosen = alt_chord_for(3);
    else if (!button_maj && button_min && button_seventh)        chosen = alt_chord_for(4);
    else if (button_maj && button_min && !button_seventh)        chosen = alt_chord_for(5);
    else if (button_maj && button_min && button_seventh)         chosen = alt_chord_for(6);
  } else if (button_maj && !button_min && !button_seventh) {
    chosen = barry_harris_mode ? &maj_sixth : &major;
  } else if (!button_maj && button_min && !button_seventh) {
    chosen = barry_harris_mode ? &min_sixth : &minor;
  } else if (!button_maj && !button_min && button_seventh) {
    chosen = &seventh;
  } else if (button_maj && !button_min && button_seventh) {
    chosen = &maj_seventh;
  } else if (!button_maj && button_min && button_seventh) {
    chosen = &min_seventh;
  } else if (button_maj && button_min && !button_seventh) {
    chosen = barry_harris_mode ? &full_dim : &dim;
  } else if (button_maj && button_min && button_seventh) {
    chosen = &aug;
  }
  bool changed = chosen != current_chord;
  current_chord = chosen;
  if (changed || (resolved && press_held_back)) button_pushed = true;
  if (resolved || !growth_pending) press_held_back = false;
}

// A second line held together with the chord selects a slash bass. Raw overlap
// alone is not intent, though: releasing one chord while pressing the next
// overlaps for a few tens of milliseconds in ordinary legato playing, and
// believing it immediately swapped the bass on every crossover. So the overlap
// has to persist for slash_grace before a slash engages. A crossover never gets
// that far, because handle_chord_type() rolls the line over as soon as the old
// line lets go.
const uint16_t slash_grace = 60; // ms of overlap before a slash engages; tune on hardware
// Letting a slash chord go entirely means two lines release a few tens of
// milliseconds apart, in whichever order the fingers land. Neither release may
// be believed on its own while a slash is engaged, or the chord is forced to
// become something on the way out: the bass line promoted to a chord, or the
// plain chord rebuilt, for the length of the release tail. So while slashing,
// both exits wait out slash_release_grace; if the other line lets go within
// it, the slash simply ends as itself. Legato is unaffected, since a crossover
// never engages the slash in the first place. The constant lives with the
// rollover state above handle_chord_type(), which also needs it.
void detect_slash() {
  static elapsedMillis overlap_timer;
  static int8_t overlap_line = -1;
  int8_t held_line = -1;
  for (int i = 1; i < 22; i++) {
    if (key_selection_buttons & (1UL << i)) continue;   // still held from picking a key: not a bass
    if (chord_matrix_array[i].read_value()) {
      int8_t line = (i - 1) / 3;
      if (line != current_line) held_line = line;
    }
  }
  static elapsedMillis bass_release_timer;
  static bool bass_release_pending = false;
  if (held_line < 0) {
    overlap_line = -1;
    if (slash_chord) {
      // The bass was let go while the chord is still held. If the chord hand
      // follows within the release grace, this was a full release of the slash
      // and nothing should be rebuilt; if the chord stays held, the player
      // dropped the bass on purpose and the plain chord comes back.
      if (!bass_release_pending) {
        bass_release_pending = true;
        bass_release_timer = 0;
      } else if (bass_release_timer >= slash_release_grace) {
        bass_release_pending = false;
        slash_chord = false;
        button_pushed = true;
      }
    }
    return;
  }
  bass_release_pending = false;
  if (held_line != overlap_line) {
    overlap_line = held_line;
    overlap_timer = 0;
    if (!slash_chord) return; // a new overlap starts its grace period
  }
  if (!slash_chord) {
    if (overlap_timer >= slash_grace) {
      slash_chord = true;
      slash_value = held_line;
      button_pushed = true;
    }
  } else if (slash_value != held_line) {
    // already slashing and the bass moved to another line: deliberate, follow it
    slash_value = held_line;
    button_pushed = true;
  }
}

// Builds the chord notes from the harmonic context and leads the voices if that
// is on. Everything that rebuilds a chord goes through here -- update_chord_notes
// below, and every parameter whose method revoices. A second way to build a
// chord is how a voicing gets quietly thrown away: before this existed, writing
// the inversion, the spacing, the shuffling or the octave rebuilt the chord with
// plain calculate_note_chord, and a voice led chord collapsed back to root
// position on any such write -- including an editor echoing parameters back.
//
// The sharp and slash come from the stored context rather than the buttons,
// because a chord can be sounding with nothing held: under the hold button the
// live flags say no sharp and no slash, and a rebuild from those strips both.
void build_chord_notes() {
  // With slash voice on, the four chord voices are built as the plain chord and
  // the slash is applied to one of them afterwards (apply_slash_voice()), which
  // also decides the slot rhythm mode's extra voices and the harp replace. So
  // those are built after it.
  const bool by_voice = chord_context_slashed && slash_voice != 0;
  slash_slot_effective = note_slash_level;
  for (int i = 0; i < 4; i++) {
    current_chord_notes[i] = calculate_note_chord(i, chord_context_slashed && !by_voice, chord_context_sharp);
  }
  // A slash chord names its own bass: it is led too, but with the bass kept on
  // the note the player asked for (apply_slash_voice_leading()). With slash
  // voice on, the plain chord is led and the slash applied to it after.
  if (voice_leading && !inversion_stepped) {   // a step climbs from the voicing there was: the next chord leads from it
    uint8_t led[4];
    bool ok = (chord_context_slashed && !by_voice) ? apply_slash_voice_leading(led)
                                                   : apply_voice_leading(chord_context_sharp, led);
    if (ok) {
      for (int i = 0; i < 4; i++) current_chord_notes[i] = led[i];
    }
  }
  if (by_voice) apply_slash_voice(chord_context_sharp);
  if (!apply_brothers()) apply_cantus();
  for (int i = 4; i < 7; i++) {
    current_chord_notes[i] = calculate_note_chord(i, chord_context_slashed, chord_context_sharp);
  }
  for (int i = 0; i < 4; i++) previous_voicing[i] = current_chord_notes[i];
  acc_snapshot();
}

// Rebuild what is sounding and retune the voices still active. This is what a
// parameter method calls; it never starts a note.
void refresh_chord_voicing() {
  build_chord_notes();
  for (int i = 0; i < 4; i++) {
    if (chord_envelope_array[i]->isActive()) {
      set_chord_voice_frequency(i, current_chord_notes[i]);
    }
  }
}

// The held chord pressed again in hold mode climbs it a step (repress_mode); any other chord starts
// over from chord inversion. Same chord is the same line, kind, sharp and slash; again is a press with
// no other chord button down (chord_legato), so rolling onto it from another doesn't count.
void inversion_repress() {
  static int8_t line = -1;
  static uint8_t (*chord)[7] = nullptr;
  static bool sharp = false, slashed = false;
  static uint8_t slash = 0;
  bool same = line == fundamental && chord == current_chord && sharp == chord_context_sharp
              && slashed == chord_context_slashed && (!slashed || slash == slash_value);
  inversion_stepped = false;
  if (!same) {
    inversion_step = 0;
    inversion_direction = 1;
  } else if (chord_repressed && !chord_legato && continuous_chord && !rythm_mode && repress_mode) {
    uint8_t tones[4];
    uint8_t top = max(collect_chord_tones(current_chord, tones), (uint8_t)1);   // an octave of inversions
    switch (repress_mode) {
      case 1: inversion_step = inversion_step >= top ? 0 : inversion_step + 1; break;
      case 2:
        if (inversion_step >= top) inversion_direction = -1;
        else if (inversion_step == 0) inversion_direction = 1;
        inversion_step += inversion_direction;
        break;
      case 3: inversion_step = inversion_step ? 0 : top; break;
    }
    inversion_stepped = true;
  }
  chord_repressed = false;
  line = fundamental; chord = current_chord; sharp = chord_context_sharp; slashed = chord_context_slashed; slash = slash_value;
}

void update_chord_notes() {
  if (button_pushed) {
    chord_context_sharp = sharp_active;
    chord_context_slashed = slash_chord;
    purity_follow_chord(true);
    inversion_repress();
    // A chord change keeps the cantus when the new chord has its note, the way
    // a singer holds a common tone, and lets it go otherwise.
    if (cantus_pc >= 0 && !cantus_is_chord_tone()) cantus_pc = -1;
    // Brothers keeps the top note the same way, when the new chord has it.
    if (brothers_note >= 0 && brothers_role(brothers_note % EDO) != 1) brothers_note = -1;
    build_chord_notes();
    chord_memory_record_chord();
    Serial.println("Updating frequencies");
    if (!rythm_mode && !trigger_chord && !retrigger_chord) {
      for (int i = 0; i < 4; i++) {
        set_chord_voice_frequency(i, current_chord_notes[i]);
      }
    } else {
      for (int i = 0; i < 7; i++) {
        current_applied_chord_notes[i] = current_chord_notes[i];
      }
      // in rhythm mode, the voices holding the chord move to the new one as they would outside it
      if (rythm_mode && !trigger_chord && !retrigger_chord) {
        uint8_t held = acc_held_mask();
        for (int i = 0; i < 4; i++) {
          if (held & (1 << i)) set_chord_voice_frequency(i, current_chord_notes[i]);
        }
      }
    }
  }
}

void update_harp_notes() {
  if (button_pushed) {
    chord_context_sharp = sharp_active;
    chord_context_slashed = slash_chord;
    for (int i = 0; i < 12; i++) {
      current_harp_notes[i] = harp_string_note(i, slash_chord, sharp_active);
      // Only strings still held: one ringing out after the finger has lifted
      // keeps the note it was struck on.
      if (change_held_strings && harp_started_notes[i] != 0 && string_held(i)) {
        harp_midi_on(i);
        if (string_enveloppe_array[i]->isSustain()) {
          set_harp_voice_frequency(i, current_harp_notes[i]);
        }
      }
    }
  }
}

void stop_chord_notes() {
  cantus_pc = -1;   // the chord is over, and the harp's hold on it with it
  brothers_note = -1;
  brothers_drop = 0;   // and the next starts close again
  brothers_drop_dir = 1;
  stop_chord_voices(0xF);
}

// Lets go of the chord voices in mask (bit i, voice i)
//>>PANIC AND REVERT<<
// The panic: every voice stops now, as fast as a palm stops the strings, whatever plays it (the
// buttons, the rhythm, the looper, notes sent in), and every note the minichord sent ends at the far
// end, with All Notes Off on all sixteen channels of both ports. The looper and chord memory stop
// playing, keeping what they hold, since they would only play again. Modes stay as they are:
// rhythm mode goes on from its next note, hold waits for a chord. Modifier and up (322), the
// double tap on 323 at 2, or 323 written 2.
FLASHMEM void panic() {
  if (looper_state == LOOPER_PLAYING || looper_state == LOOPER_RECORDING || looper_state == LOOPER_COUNTIN) looper_action(3);
  if (chord_memory_state == CHORD_MEMORY_PLAYING || chord_memory_state == CHORD_MEMORY_RECORDING) chord_memory_action(3);
  noInterrupts();
  for (uint8_t v = 0; v < 4; v++) acc_voice[v].off_pending = false;
  for (uint8_t r = 0; r < ACC_ROLL_MAX; r++) acc_rolled[r].used = false;
  interrupts();
  for (uint8_t i = 0; i < 12; i++) {
    midi_in_harp[i].owned = midi_in_harp[i].key_down = midi_in_harp[i].sustained = false;
    damp_string(i);
    pad_voice[i] = -1;
  }
  for (uint8_t i = 0; i < 4; i++) {
    note_timer[i].end();
    midi_in_chord[i].owned = midi_in_chord[i].key_down = midi_in_chord[i].sustained = false;
    AudioNoInterrupts();
    chord_envelope_array[i]->release(palm_mute_release);
    chord_vibrato_envelope_array[i]->noteOff();
    chord_vibrato_dc_envelope_array[i]->noteOff();
    chord_envelope_array[i]->noteOff();
    chord_envelope_filter_array[i]->noteOff();
    chord_envelope_array[i]->release(current_sysex_parameters[141]);   // its own release, for the next
    chord_voice_released[i] = true;
    AudioInterrupts();
    if (chord_started_notes[i] != 0) {
      queue_midi(false, chord_started_notes[i], chord_release_velocity, mpe_chord_channel(i), chord_port);
      chord_started_notes[i] = 0;
    }
  }
  for (uint8_t port = 0; port < 2; port++) {
    for (uint8_t channel = 1; channel <= 16; channel++) queue_midi_cc(123, 0, channel, port);
  }
  Serial.println("Panic: everything stopped");
}

// Revert: the preset as it was saved, dropping what the knobs, an editor or the double tap changed
// since it loaded, and with nothing left to save. Modifier and down (322), the double tap on 323
// (at 1, or unset), or 323 written 1.
FLASHMEM void preset_revert() {
  double_tap_engaged = false;
  for (uint8_t k = 0; k < double_tap_pairs; k++) double_tap_engaged_adress[k] = -1;
  flag_save_needed = false;
  load_config(current_bank_number);
  set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
  Serial.println("Preset reverted");
}

// The action (323), never kept: 1 revert, 2 panic
FLASHMEM void preset_action(int16_t action) {
  current_sysex_parameters[preset_action_adress] = 0;
  if (action == 1) preset_revert();
  else if (action == 2) panic();
}

void stop_chord_voices(uint8_t mask) {
  // Cancel pending retrigger timers — prevents NoteOn firing after NoteOff already sent
  for (int i = 0; i < 4; i++) if (mask & (1 << i)) note_timer[i].end();
  AudioNoInterrupts();
  for (int i = 0; i < 4; i++) {
    if (!(mask & (1 << i))) continue;
    // Released in any stage, not only sustain: a chord let go during a long
    // attack, hold or decay used to play that stage out before it started to
    // release, so the release came seconds after the hand did. The sustain
    // test stays as the safety net it always was: a voice caught in its
    // retrigger ramp ignores a note off, and it is released once it settles.
    noInterrupts();
    if (midi_in_chord[i].owned) { interrupts(); continue; }   // an incoming note's, not the chord's
    if (chord_envelope_array[i]->isSustain() || (chord_envelope_array[i]->isActive() && !chord_voice_released[i])) {
      chord_vibrato_envelope_array[i]->noteOff();
      chord_vibrato_dc_envelope_array[i]->noteOff();
      chord_envelope_array[i]->noteOff();
      chord_envelope_filter_array[i]->noteOff();
      chord_voice_released[i] = true;
      looper_capture(LOOP_RELEASE, 12 + i, 0, 0, 0, 0);
    }
    interrupts();
    // Sent regardless of the internal envelope: an external synth holds the note
    // until it receives the Note Off.
    if (chord_started_notes[i] != 0) {
      queue_midi(false, chord_started_notes[i], chord_release_velocity, mpe_chord_channel(i), chord_port);
      chord_started_notes[i] = 0;
    }
  }
  AudioInterrupts();
}

void handle_continuous_mode() {
  bool one_button_active = false;
  int line_accumulator[3] = {0, 0, 0};
  for (int i = 1; i < 22; i++) {
    bool active = chord_matrix_array[i].read_value();
    one_button_active |= active;
    if (active) {
      line_accumulator[i % 3]++;
    }
  }
  if (line_accumulator[0] > 2 || line_accumulator[1] > 2 || line_accumulator[2] > 2) {
    current_line = -1;
    inhibit_button = true;
  }
  if (!one_button_active) {
    inhibit_button = false;
    stop_chord_notes();
  }
}

void handle_hold_button() {
  uint8_t hold_transition = hold_button.read_transition();
  if (hold_transition == 2) {
    if (rythm_mode && rhythm_fill_combo && chord_matrix_array[0].read_value()) {
      acc_fill_request(false);    // the modifier held: a fill, not a tap of the tempo
      modifier_combo_used = true;
    } else if (!rythm_mode) {
      Serial.println("Switching mode");
      continuous_chord = !continuous_chord;
      analogWrite(RYTHM_LED_PIN, 255 * continuous_chord);
      if (current_line == -1) {
        trigger_chord = true;
      }
    } else {
      acc_tap();
    }
    since_last_button_push = 0;
  } else if (hold_transition == 1 && since_last_button_push > 800) {
    Serial.println("Long push, switching rhythm mode");
    if (rythm_mode && rhythm_ending) acc_fill_request(true);   // the ending first (again: off at once)
    else set_rythm_mode(!rythm_mode);
  }
}

// Rhythm mode on or off: a long press on hold, control command 8, control change 102
FLASHMEM void set_rythm_mode(bool on) {
  if (on == rythm_mode) return;
  rythm_mode = on;
  continuous_chord = false;
  analogWrite(RYTHM_LED_PIN, 255 * continuous_chord);
  if (rythm_mode) {
    Serial.println("Starting rhythm timers");
    acc_begin();
  } else {
    Serial.println("Stopping rhythm timers");
    acc_end();
  }
}


// Holding both preset buttons enters key change mode; the chord buttons then pick
// a key signature. Rows run F C G D A E B from the user's perspective, columns are
// sharp, natural, flat, giving all 21 keys.
void handle_key_change_mode(uint8_t up_transition, uint8_t down_transition, bool up_state, bool down_state) {
  static bool up_pressed = false, down_pressed = false;
  static elapsedMillis up_press_time, down_press_time;
  static bool logged_mode = false;
  static bool chord_pressed = false;
  static int selected_key = -1;

  static const int8_t key_by_column[3][7] = {
    // rows F C G D A E B, as the player sees them
    {KEY_SIG_Fs, KEY_SIG_Cs, KEY_SIG_Gs, KEY_SIG_Ds, KEY_SIG_As, KEY_SIG_Es, KEY_SIG_Bs}, // sharp
    {KEY_SIG_F,  KEY_SIG_C,  KEY_SIG_G,  KEY_SIG_D,  KEY_SIG_A,  KEY_SIG_E,  KEY_SIG_B }, // natural
    {KEY_SIG_Fb, KEY_SIG_Cb, KEY_SIG_Gb, KEY_SIG_Db, KEY_SIG_Ab, KEY_SIG_Eb, KEY_SIG_Bb}  // flat
  };

  if (up_transition == 2)   { up_pressed = true;  up_press_time = 0; }
  if (down_transition == 2) { down_pressed = true; down_press_time = 0; }
  if (up_transition == 1)   { up_pressed = false; }
  if (down_transition == 1) { down_pressed = false; }

  if (!key_change_mode && up_pressed && down_pressed &&
      up_press_time < SIMULTANEOUS_WINDOW && down_press_time < SIMULTANEOUS_WINDOW) {
    key_change_mode = true;
    key_button_was_down = 0;
    for (int i = 1; i <= 21; i++) {
      if (chord_matrix_array[i].read_value()) key_button_was_down |= (1UL << i);
    }
    preset_inhibit = true;
    preset_inhibit_timer = 0;
    chord_pressed = false;
    selected_key = -1;
    if (!logged_mode) { Serial.println("Entered key change mode"); logged_mode = true; }
  }

  // Releasing either preset button ends the gesture. There is deliberately no
  // timeout: holding both buttons is an explicit, sustained request.
  if (key_change_mode && (!up_state || !down_state)) {
    key_change_mode = false;
    set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
    // The combo changes address 35 without the host seeing anything: a re-keyed
    // chord is byte-identical over MIDI to a different chord already on the
    // grid, so a remote cannot infer it. Report the settled value once, on the
    // way out, rather than on every selection while the player auditions keys.
    // Report it too when a key was picked that the minichord was already in: to
    // a host that set the key itself, choosing it again is still an answer (a
    // game asking the player to name a key, say, where the answer is C).
    if (selected_key != -1 || key_change_reported != key_signature_selection) {
      key_change_reported = key_signature_selection;
      if (sysex_controler_connected) control_command(0, 0);
    }
    preset_inhibit = true;
    preset_inhibit_timer = 0;
    chord_pressed = false;
    selected_key = -1;
    key_change_shown = -1;
    Serial.println("Exited key change mode");
    logged_mode = true;
  }

  if (key_change_mode || (up_state && down_state)) {
    if (!preset_inhibit) preset_inhibit_timer = 0;
    preset_inhibit = true;
  }

  if (key_change_mode) {
    // Selection reads the raw pin edges rather than read_transition(). The
    // transition only fires after ten milliseconds of continuously stable
    // contact, which is why picking a key needed a squarer press than playing
    // does: in normal play only the first press from silence goes through the
    // debounced path, everything after responds to the raw value. Selecting a
    // key is idempotent, so contact bounce is harmless here in a way it is not
    // for starting notes. The transitions are still consumed, discarded, so a
    // press in this mode cannot fall through and start the chord.
    for (int i = 1; i <= 21; i++) {
      chord_matrix_array[i].read_transition();
      bool now = chord_matrix_array[i].read_value();
      bool was = key_button_was_down & (1UL << i);
      if (now) key_button_was_down |= (1UL << i); else key_button_was_down &= ~(1UL << i);
      // claimed from the raw contact: a button pressed in the gesture's last
      // moments only becomes a value once it has held, after the mode may
      // already have ended, and would then start its chord
      if (chord_matrix_array[i].read_raw()) key_selection_buttons |= (1UL << i);
      if (now && !was) {
        chord_pressed = true;
        int user_row = 6 - ((i - 1) / 3); // hardware rows run B E A D G C F
        int col = (i - 1) % 3;            // 0 sharp, 1 natural, 2 flat
        selected_key = key_by_column[col][user_row];
      }
    }

    if (chord_pressed && selected_key != -1 && selected_key != key_signature_selection) {
      key_signature_selection = selected_key;
      current_sysex_parameters[35] = selected_key;
      update_harp_notes();
      update_chord_notes();
      // A re-keying is a deliberate setting change, so it persists the way a
      // pot move does: written back by the save that runs on the next preset
      // change, once no controller is connected.
      flag_save_needed = true;
    }

    // Hold the chosen key's colour steady while its button is down. A flash was
    // too brief to read against the blink, and holding it means the colour can
    // be compared against the next key before committing to it.
    bool still_down = false;
    for (int i = 1; i <= 21; i++) {
      if (chord_matrix_array[i].read_value()) { still_down = true; break; }
    }
    key_change_shown = (still_down && selected_key != -1) ? selected_key : -1;
  }


  if (!key_change_mode && preset_inhibit && preset_inhibit_timer > PRESET_INHIBIT_DELAY &&
      !(up_state && down_state)) {
    preset_inhibit = false;
    logged_mode = false;
  }
}

void handle_preset_change(uint8_t up_transition, uint8_t down_transition, bool up_state, bool down_state) {
  static elapsedMillis single_press_timer;
  static bool pending_preset_change = false;
  static bool pending_up = false;

  if (key_change_mode || preset_inhibit || (up_state && down_state)) {
    pending_preset_change = false;
    return;
  }

  // A single press arms a change rather than performing one, so the other button
  // still has SIMULTANEOUS_WINDOW to arrive and claim the gesture for key change
  // mode instead.
  if (up_transition == 2 && !down_state && !pending_preset_change) {
    pending_preset_change = true;
    pending_up = true;
    single_press_timer = 0;
  } else if (down_transition == 2 && !up_state && !pending_preset_change) {
    pending_preset_change = true;
    pending_up = false;
    single_press_timer = 0;
  }

  // Settle on release, since once a button is up the combo can no longer happen
  // and waiting out the window would only feel sluggish; otherwise settle once
  // the window has passed with the button still held.
  bool released = (pending_up && up_transition == 1) || (!pending_up && down_transition == 1);
  bool window_elapsed = single_press_timer >= SIMULTANEOUS_WINDOW;
  bool only_one_held = (!up_state != !down_state);

  if (pending_preset_change && (released || (window_elapsed && only_one_held))) {
    pending_preset_change = false;
    Serial.println(pending_up ? "Switching to next preset" : "Switching to last preset");
    if (!sysex_controler_connected && flag_save_needed) {
      save_config(current_bank_number, false);
    }
    current_bank_number = pending_up ? (current_bank_number + 1) % 12
                                     : (current_bank_number - 1 + 12) % 12;
    load_config(current_bank_number);
    set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
  }
}

void handle_low_battery() {
  uint8_t LBO_transition = LBO_flag.read_transition();
  if (LBO_transition == 1) {
    led_blinking_flag = true;
  } else if (LBO_transition == 2) {
    led_blinking_flag = false;
    set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
  }
  if (led_blinking_flag) {
    set_led_color(bank_led_hue, bank_led_saturation(), 0.6 + 0.4 * sin(color_led_blink_val));
    color_led_blink_val += 0.005;
  }
}

void trigger_chord_notes() {
  uint8_t voices = rythm_mode ? acc_held_mask() : 0xF;   // in rhythm mode, the voices it leaves to hold the chord
  if ((trigger_chord || (button_pushed && retrigger_chord)) && voices) {
    Serial.println("Triggering chord notes");
    for (int i = 0; i < 4; i++) {
      note_timer[i].priority(253);
    }
    // Each voice's start, delayed for the strum. There are four timer channels to share, and a
    // timer that finds none begins nothing and says so: that voice then starts now, unstrummed,
    // rather than not at all.
    if ((voices & 1) && !note_timer[0].begin([] { play_single_note(0, &note_timer[0]); }, 10+chord_retrigger_release*1000)) play_single_note(0, &note_timer[0]);
    if ((voices & 2) && !note_timer[1].begin([] { play_single_note(1, &note_timer[1]); }, 10 +chord_retrigger_release*1000+ inter_string_delay + random(random_delay))) play_single_note(1, &note_timer[1]);
    if ((voices & 4) && !note_timer[2].begin([] { play_single_note(2, &note_timer[2]); }, 10 + chord_retrigger_release*1000+inter_string_delay * 2 + random(random_delay))) play_single_note(2, &note_timer[2]);
    if ((voices & 8) && !note_timer[3].begin([] { play_single_note(3, &note_timer[3]); }, 10 + chord_retrigger_release*1000+inter_string_delay * 3 + random(random_delay))) play_single_note(3, &note_timer[3]);
    trigger_chord = false;
  }
  button_pushed = false;
}


// A loop the double tap started, still counting in, recording or playing, in a preset with no looper
// of its own on the double tap (one on the computer, value 7, isn't): the double tap stays the
// looper's until the loop stops (see LOOPER)
static bool looper_keeps_double_tap() {
  uint8_t state = looper_state;
  if (!looper_double_tap_owned || (state != LOOPER_COUNTIN && state != LOOPER_RECORDING && state != LOOPER_PLAYING)) return false;
  for (uint8_t k = 0; k < double_tap_pairs; k++) {
    if (current_sysex_parameters[double_tap_control_adress[k]] == looper_adress && current_sysex_parameters[double_tap_value_adress[k]] != 7) return false;
  }
  return true;
}

// Applies the chosen values, or puts back what was there before. The actions (the looper, chord
// memory, a rhythm fill) take their step only when asked to: a tap and hold letting go puts the
// values back and leaves them be.
void toggle_double_tap_target(bool actions = true) {
  // A running loop kept from another preset takes the next step and nothing else does: the preset's
  // own assignments wait until it stops. A tap and hold's let-go goes where its press went.
  static bool pressed_for_looper = false;
  if (!actions && pressed_for_looper) { pressed_for_looper = false; return; }
  pressed_for_looper = actions && looper_keeps_double_tap();
  if (pressed_for_looper) { looper_step(modifier_pressed_ms); return; }
  // The looper, assigned to the double tap, takes its next step, from where the second tap landed;
  // with the pair's value at 7 the step goes to a looper on the computer instead (see LOOPER)
  for (uint8_t k = 0; k < double_tap_pairs && actions; k++) {
    if (current_sysex_parameters[double_tap_control_adress[k]] != looper_adress) continue;
    if (current_sysex_parameters[double_tap_value_adress[k]] == 7) looper_send_step(millis() - modifier_pressed_ms);
    else { looper_step(modifier_pressed_ms); looper_double_tap_owned = true; }
    break;
  }
  // chord memory, assigned to it, takes its next step
  for (uint8_t k = 0; k < double_tap_pairs && actions; k++) {
    if (current_sysex_parameters[double_tap_control_adress[k]] != chord_memory_adress) continue;
    chord_memory_step();
    break;
  }
  // revert or panic, assigned to it: the revert, or the panic with its value at 2
  for (uint8_t k = 0; k < double_tap_pairs && actions; k++) {
    if (current_sysex_parameters[double_tap_control_adress[k]] != preset_action_adress) continue;
    preset_action(current_sysex_parameters[double_tap_value_adress[k]] == 2 ? 2 : 1);
    return;   // a revert puts the double tap back too: nothing left for it to toggle
  }
  // rhythm fill, assigned to it: a fill, or a break or the ending if its value says (1-3)
  for (uint8_t k = 0; k < double_tap_pairs && actions; k++) {
    if (current_sysex_parameters[double_tap_control_adress[k]] != rhythm_fill_adress) continue;
    int16_t action = current_sysex_parameters[double_tap_value_adress[k]];
    rhythm_fill_action(action >= 1 && action <= 3 ? action : 1);
    break;
  }
  // the phone deck, assigned to it: the tape stop (and the song back in, the next time), or a grab at 2
  for (uint8_t k = 0; k < double_tap_pairs && actions; k++) {
    if (current_sysex_parameters[double_tap_control_adress[k]] != phone_deck_adress) continue;
    phone_deck_action(current_sysex_parameters[double_tap_value_adress[k]] == 2 ? 2 : 1);
    break;
  }
  if (double_tap_engaged) {
    // Restore to the addresses latched at engage time, last applied first: the
    // assignments can be rewritten while the toggle is held, and each restore
    // belongs to the parameter that was actually toggled, not to a new target.
    // A setting changed since the tap (by the key change combo, or an editor)
    // keeps that change: releasing undoes the double tap, not the player.
    for (int8_t k = double_tap_pairs - 1; k >= 0; k--) {
      int16_t a = double_tap_engaged_adress[k];
      if (a < 0) continue;
      if (current_sysex_parameters[a] != double_tap_applied[k]) { double_tap_engaged_adress[k] = -1; continue; }   // changed since the tap (the key change combo, an editor): that change stands
      current_sysex_parameters[a] = double_tap_saved[k];
      apply_audio_parameter(a, double_tap_saved[k]);
      double_tap_engaged_adress[k] = -1;
    }
    double_tap_engaged = false;
    set_led_color(bank_led_hue, bank_led_saturation(), 1 - led_attenuation);
  } else {
    bool any = false;
    for (uint8_t k = 0; k < double_tap_pairs; k++) {
      int16_t adress = current_sysex_parameters[double_tap_control_adress[k]];
      double_tap_engaged_adress[k] = -1;
      if (adress == looper_adress || adress == chord_memory_adress || adress == rhythm_fill_adress || adress == preset_action_adress || adress == phone_deck_adress) continue;   // stepped above
      if (!control_can_tap(adress)) continue;      // 0 means this pair is unassigned
      bool repeat = false;                          // two pairs on one parameter: the first one wins
      for (uint8_t j = 0; j < k; j++) if (double_tap_engaged_adress[j] == adress) repeat = true;
      if (repeat) continue;
      double_tap_saved[k] = current_sysex_parameters[adress];
      double_tap_engaged_adress[k] = adress;
      int16_t value = current_sysex_parameters[double_tap_value_adress[k]];
      int16_t lo, hi;                              // the double tap's value is a raw number: keep it within the target's own range
      if (parameter_range(adress, lo, hi)) value = constrain(value, lo, hi);
      double_tap_applied[k] = value;
      current_sysex_parameters[adress] = value;
      apply_audio_parameter(adress, value);
      any = true;
    }
    if (!any) return;                               // nothing assigned: the gesture does nothing
    double_tap_engaged = true;
  }
  // The gesture changes a parameter with nothing on the wire to show it, so a
  // remote editor keeps displaying the value the player has just toggled away
  // from. Report the new state so the control on screen follows the gesture.
  // save_config still writes the underlying value, so a save while engaged is
  // unaffected by this. Only when a controller is listening: an unsolicited
  // dump lands on the performance port of whatever else is connected.
  if (sysex_controler_connected) control_command(0, 0);
}

// Repopulate the live tables from the generated ones. Everything downstream —
// the chord catalogue, inversion, spacing, the alternate layout — keeps working
// on the same flat arrays it always did, so only these copies know about
// temperament at all.
FLASHMEM void apply_modifier_step() {
  modifier_step = modifier_step_choice == 1 ? 1 : modifier_step_choice == 2 ? edo_sharp[edo_index] : edo_modifier[edo_index];
}

FLASHMEM void apply_temperament(uint8_t t) {
  if (t > own_temperament) t = 0;
  uint8_t previous_edo = EDO;
  temperament_selection = t;
  edo_index = t == own_temperament ? 0 : temperament_profiles[t].edo_index;
  EDO = edo_steps[edo_index];
  sharp_step = edo_sharp[edo_index];
  apply_modifier_step();
  transpose_steps = (transpose_semitones * EDO + 6) / 12; // one semitone of transposition is EDO/12 steps here
  chord_note_floor = EDO;      // the spacing rails are octaves, so they move with the division
  chord_note_ceiling = 8 * EDO;
  memcpy(base_notes, edo_base_notes[edo_index], sizeof(base_notes));
  memcpy(scale_root_offsets, edo_scale_root_offsets[edo_index], sizeof(scale_root_offsets));
  memcpy(scale_intervals, edo_scale_intervals[edo_index], sizeof(scale_intervals));
  memcpy(chord_scale_intervals, edo_chord_scale_intervals[edo_index], sizeof(chord_scale_intervals));
  rebuild_generator_scale();   // its generator counts steps of the new division
  rebuild_custom_scale();      // and the custom scale's degrees are steps of it too
  rebuild_maqam();
  purity_follow_chord();       // the root and the just steps, in the new division
  memcpy(major, edo_major[edo_index], 7);
  memcpy(minor, edo_minor[edo_index], 7);
  memcpy(maj_sixth, edo_maj_sixth[edo_index], 7);
  memcpy(min_sixth, edo_min_sixth[edo_index], 7);
  memcpy(seventh, edo_seventh[edo_index], 7);
  memcpy(maj_seventh, edo_maj_seventh[edo_index], 7);
  memcpy(half_dim, edo_half_dim[edo_index], 7);
  memcpy(sus_fourth, edo_sus_fourth[edo_index], 7);
  memcpy(sus_second, edo_sus_second[edo_index], 7);
  memcpy(seventh_sus, edo_seventh_sus[edo_index], 7);
  memcpy(major_ninth, edo_major_ninth[edo_index], 7);
  memcpy(minor_ninth, edo_minor_ninth[edo_index], 7);
  memcpy(added_ninth, edo_added_ninth[edo_index], 7);
  memcpy(six_nine, edo_six_nine[edo_index], 7);
  memcpy(min_seventh, edo_min_seventh[edo_index], 7);
  memcpy(aug, edo_aug[edo_index], 7);
  memcpy(dim, edo_dim[edo_index], 7);
  memcpy(full_dim, edo_full_dim[edo_index], 7);
  memcpy(neutral, edo_neutral[edo_index], 7);
  memcpy(harmonic_7th, edo_harmonic_7th[edo_index], 7);
  memcpy(subminor, edo_subminor[edo_index], 7);
  memcpy(supermajor, edo_supermajor[edo_index], 7);
  memcpy(subminor_seventh, edo_subminor_seventh[edo_index], 7);
  memcpy(utonal_tetrad, edo_utonal_tetrad[edo_index], 7);
  memcpy(harmonic_ninth, edo_harmonic_ninth[edo_index], 7);
  memcpy(neutral_seventh, edo_neutral_seventh[edo_index], 7);
  memcpy(otonal_hexad, edo_otonal_hexad[edo_index], 7);
  memcpy(just_augmented, edo_just_augmented[edo_index], 7);
  memcpy(supermajor_seventh, edo_supermajor_seventh[edo_index], 7);

  /* Every note number just changed meaning: in 31 a fifth is 18 rather than 7.
   * So the note arrays are recomputed unconditionally — update_chord_notes and
   * update_harp_notes are both gated on button_pushed, which a held chord does
   * not set — and then pushed into whatever is still sounding, the same way a
   * master tuning change is.
   *
   * set_chord_voice_frequency also sends a MIDI note off and on when the note
   * number moves, and going from 12 to 31 moves all of them, so a held chord
   * retriggers over MIDI here. That is right for a pitch change of this size and
   * matches what master tuning already does, but it is why this belongs on a
   * deliberate setting change and nowhere near a knob sweep.
   */
  //
  // The recalculation used to run only while a chord button was down. A chord
  // still sounding after release, or held by the hold button, kept its old note
  // numbers, which were then read in the new division: a fifth of 7 is nearly a
  // quarter-octave lower in 31, and 18 from 31 read in 12 lands an octave and a
  // half up. Recalculate from the context the notes were last built with
  // instead of the live buttons, which a released chord no longer holds.
  //
  // Each sounding voice is then moved to the NEW number for the note it was
  // playing, found by position in the old arrays, rather than to
  // current_chord_notes[voice]: in rhythm mode a voice plays whichever chord
  // degree the pattern gave it, not the one at its own index.
  bool division_changed = (EDO != previous_edo);
  uint8_t old_chord_notes[7], old_harp_notes[12];
  memcpy(old_chord_notes, current_chord_notes, sizeof(old_chord_notes));
  memcpy(old_harp_notes, current_harp_notes, sizeof(old_harp_notes));
  for (int i = 0; i < 7; i++) current_chord_notes[i] = calculate_note_chord(i, chord_context_slashed, chord_context_sharp);
  // The remembered voicing is in steps of the division that just ended, so it
  // would be nonsense to lead from it. Take the rebuilt chord instead.
  for (int i = 0; i < 4; i++) previous_voicing[i] = current_chord_notes[i];
  for (int i = 0; i < 12; i++) current_harp_notes[i] = harp_string_note(i, chord_context_slashed, chord_context_sharp);
  // the chord the rhythm plays from, in the new division, at once
  acc_snapshot();
  noInterrupts();
  if (rythm_mode) acc_take_chord();
  interrupts();

  auto remap = [](const uint8_t *from, const uint8_t *to, uint8_t n, uint16_t note, uint16_t &out) {
    for (uint8_t j = 0; j < n; j++) {
      if (from[j] == note) { out = to[j]; return true; }
    }
    out = note;
    return false;
  };

  // the notes the rhythm engine plays from next, updated with timer interrupts
  // held so a step never reads half of each
  noInterrupts();
  for (int i = 0; i < 7; i++) {
    uint16_t n;
    remap(old_chord_notes, current_chord_notes, 7, current_applied_chord_notes[i], n);
    current_applied_chord_notes[i] = n;
    remap(old_chord_notes, current_chord_notes, 7, rythm_freeze_current_chord_notes[i], n);
    rythm_freeze_current_chord_notes[i] = n;
  }
  interrupts();

  // A voice whose note is not in the old arrays is left at the pitch it has
  // when the division changes, since its number means nothing in the new one.
  for (int i = 0; i < 4; i++) {
    noInterrupts();
    if (chord_envelope_array[i]->isActive()) {
      uint16_t n;
      if (remap(old_chord_notes, current_chord_notes, 7, chord_voice_current_note[i], n) || !division_changed) {
        set_chord_voice_frequency(i, n);
      }
    }
    interrupts();
  }
  for (int i = 0; i < 12; i++) {
    if (string_enveloppe_array[i]->isActive()) {
      uint16_t n;
      if (remap(old_harp_notes, current_harp_notes, 12, harp_voice_current_note[i], n) || !division_changed) {
        set_harp_voice_frequency(i, n);
      }
    }
  }
  update_chord_notes();
  update_harp_notes();
  midi_in_retune();
}

//>>HOVER<<
// The harp plate as a fourth knob, played by holding a hand over it. The touch chip's thirteenth
// channel reads the twelve pads together as one antenna (harp::read_proximity), which a hand pulls
// down from several centimetres away. Measured on the stock plate, at the charge set_proximity_charge
// finds: a palm pulls it about 1 count at 5 cm, 3.5 at 2 cm and 300 resting on the plate. Smoothed,
// the chip's own noise is 0.04 counts, but a player moving about beside it stirs it by 0.1 or so,
// which is what limits the reach. Hover runs from 0 at hover reach to 1 at 2 cm, on a log scale
// between, so it moves about evenly as the hand comes down and is full before the hand gets in the
// way of playing. With no hand near, the setting is as the preset has it; at 2 cm it reaches hover
// value. Like the knobs, what it sets is applied but not saved.
//
// Playing the harp puts a hand on the plate, the closest hover there is. So while the plate is
// touched hover holds where it was before the hand came down, and after it lifts, it waits for the
// hand to rise back through that height (pickup, as the knob layer does) before following again.
// A strum's quick swoop down shouldn't count as where the hand was, so hover only rises once the
// hand has stayed that close for a moment (hover_rise_ms): it follows a hand away at once, and
// towards the plate a beat behind. With "knobs send MIDI" on, it also goes out as control change
// 25 on the chord channel, beside the knobs' 20-22 (23 and 24 are the arcade wheel's, on the harp
// channel, which may be the same one).
const uint8_t hover_cc = 25;
const uint16_t hover_rise_ms = 200;
const uint8_t hover_slot_ms = 10;
const float hover_full_cm = 2;
const float hover_touch_counts = 20.0f;           // past this it's a hand on the plate (more on a plate that pulls harder)
const float hover_least_pull = 0.30f;             // no reach is set nearer the idle than this: the player's own movements stir about 0.1
float hover_position = 0;                         // 0 to 1, after the hold: what the setting follows

// How far a palm d cm above the plate pulls the reading, for an idle reading of idle: pull_5cm per count
// of the idle reading at 5 cm (the pull grows with the charge), falling off as the distance to k_near
// within 5 cm and k_far beyond, as the plate starts to look small. Each plate is its own antenna: the
// stock strip's numbers are from 2026-10-07; the others from 2026-10-14, a palm held by eye over each
// (~/minichord-bench hover_cal.py), interim until a measurement against the stock strip with fixed heights.
// The piano's wide keys pull four to six times the stock strip's near the plate and fall off far faster
// (at 2 cm 23 counts, past the 20 that meant a hand on the plate, so hover froze short of full), framed
// strings near the stock strip, the Keymaster's twelve small pads barely past 3 cm. Twin rows keeps the
// stock strip's until it is measured with its charge fitted (its fit had clipped). The pull of a hand
// actually on the plate scales with the plate too, so touched is 2.5 times the pull at full, at least 20.
struct HoverPlate { float pull_5cm, k_near, k_far; };
#define HOVER_STOCK {1.09f / 709.0f, 1.3f, 1.85f}
static const HoverPlate hover_plates[harp_plate_count] = {
  HOVER_STOCK,              // 0 the stock strip
  {2.3e-4f, 3.7f, 3.7f},    // 1 the Keymaster
  {6.1e-3f, 1.8f, 4.5f},    // 2 the piano plate
  HOVER_STOCK,              // 3 twin rows, not yet measured
  {1.05e-3f, 2.4f, 3.2f},   // 4 framed strings
  HOVER_STOCK, HOVER_STOCK, HOVER_STOCK,
  // 8 zipper 24 and 9 the arcade wheel, measured 2026-10-17 (results/hover_zipper24_1212.json, hover_wheel_1206.json):
  // both barely past 3 cm. Zipper 24 pulls 3.9 counts at 2 cm (0, 8.9, 2.9 in three passes), 1.6 at 3, 0.5 at 4,
  // 0.2 at 5 and nothing further, a fifth of the stock strip from 5 cm (its low touch rest is its teeth's
  // capacitance to each other and the ground, not a bigger antenna for a hand); the wheel's small ring 5.9 at 2 cm
  // and 0.3 at 3. Rough: the passes disagreed at 2-3 cm.
  {2.9e-4f, 3.5f, 3.7f},    // 8 zipper 24
  {1.4e-5f, 7.0f, 7.0f},    // 9 the arcade wheel
  HOVER_STOCK, HOVER_STOCK};
static float hover_pull(float d, float idle) {
  const HoverPlate &p = hover_plates[harp_plate < harp_plate_count ? harp_plate : 0];
  return p.pull_5cm * idle * powf(5.0f / d, d <= 5 ? p.k_near : p.k_far);
}

// What hover may move: what a knob may (its own settings, the double tap's and USB audio among the
// ones none may)
static bool hover_can_move(int16_t adress) {
  return control_can_sweep(adress);
}

void hover_set_target(int16_t adress) {
  if (adress == hover_target) return;
  if (hover_can_move(hover_target)) apply_audio_parameter(hover_target, current_sysex_parameters[hover_target]);  // the old one back as the preset has it
  hover_target = adress;
  hover_reapply = true;
}

void handle_hover(bool alternate) {
  static elapsedMicros since_read;
  if (since_read < 2000) return;                   // the chip has a new reading each millisecond; every other one is plenty
  since_read = 0;
  float reading = harp_sensor.read_proximity();
  if (reading <= 0) return;                        // no proximity channel on this touch chip
  static float level = -1, baseline = -1;
  static uint16_t fits = 0;                        // a charge fit moves the idle: start again from it (it
  if (harp_sensor.proximity_fits != fits) {        // would otherwise read as a hand that never leaves)
    fits = harp_sensor.proximity_fits;
    level = baseline = -1;
  }
  if (level < 0) level = baseline = reading;
  level += (reading - level) * 0.05f;              // about 40 ms of smoothing
  float near = baseline - level;                   // how far a hand pulls the reading down
  float pull_full = hover_pull(hover_full_cm, baseline);
  float pull_edge = fmaxf(hover_pull(hover_reach, baseline), hover_least_pull);
  bool touched = near > fmaxf(hover_touch_counts, 2.5f * pull_full);
  for (int i = 0; i < 12 && !touched; i++) touched = harp_array[i].read_value();
  // The baseline is what the plate reads with no hand. It follows the reading up quickly (a hand
  // leaving, or the air drying) and down slowly, so a hand held still isn't soaked up but a drift
  // with nobody near is; slower still within reach, and not at all while the plate is touched.
  float rate = near < 0 ? 0.02f : touched ? 0 : near < pull_edge ? 0.0004f : 0.00002f;  // 0.1 s, 5 s, 100 s
  baseline -= near * rate;

  float live = touched ? 1 : near <= pull_edge ? 0 : fminf(logf(near / pull_edge) / logf(pull_full / pull_edge), 1);
  // Rises only once the hand has been that close all of hover_rise_ms: the lowest of the last
  // stretch, kept in slots holding each one's lowest.
  static float recent[hover_rise_ms / hover_slot_ms] = {0};
  static uint8_t slot = 0;
  static elapsedMillis since_slot;
  const uint8_t slots = hover_rise_ms / hover_slot_ms;
  if (since_slot >= hover_slot_ms) {
    since_slot = 0;
    slot = (slot + 1) % slots;
    recent[slot] = live;
  } else recent[slot] = fminf(recent[slot], live);
  float follow = live;
  for (uint8_t k = 0; k < slots; k++) follow = fminf(follow, recent[k]);

  static bool holding = false;
  if (touched) holding = true;
  else if (holding && follow <= hover_position + 0.02f) holding = false;   // back down to where it was held: pickup
  if (!holding) {
    hover_position += (follow - hover_position) * 0.033f;   // 60 ms more, to calm the far end
    if (follow == 0 && hover_position < 0.001f) hover_position = 0;
  }

  static elapsedMillis since_apply;
  if (since_apply < 10) return;
  since_apply = 0;
  // the alternate layer's setting while it is up, if hover has one; the setting it leaves goes back
  // as the preset has it, as when the hand leaves
  bool alt = alternate && hover_alt_target != 0;
  int16_t target = alt ? hover_alt_target : hover_target;
  static int16_t moving = 0;
  if (target != moving) {
    if (hover_can_move(moving)) apply_audio_parameter(moving, current_sysex_parameters[moving]);
    moving = target;
    hover_reapply = true;
  }
  if (hover_can_move(target)) {
    static int16_t applied = 0;
    static elapsedMillis since_refresh;
    int16_t from = current_sysex_parameters[target];
    int16_t to = alt ? hover_alt_value : hover_value;
    int16_t lo, hi;
    if (parameter_range(target, lo, hi)) to = constrain(to, lo, hi);
    int16_t value = (int16_t)lroundf(from + (to - from) * hover_position);
    // a preset load or an editor puts the stored value back: while hover is up, apply it again now and then
    if (hover_position > 0 && since_refresh > 250) hover_reapply = true;
    if (value != applied || hover_reapply) {
      apply_audio_parameter(target, value);
      applied = value;
      hover_reapply = false;
      since_refresh = 0;
    }
  }
  static int16_t sent = -1;
  if (!knob_midi) sent = -1;                       // sent again whenever it's switched back on
  else {
    int16_t cc = (int16_t)lroundf(hover_position * 127);
    if (cc != sent) queue_midi_cc(hover_cc, cc, chord_channel, chord_port);
    sent = cc;
  }
}

//>>VOCODER<<
// Sixteen bands from 100 Hz to 8 kHz, a third of an octave and a bit apart, each two bandpass
// stages so they overlap without smearing. The incoming sound's level in each band, read as it is
// measured (every block), rises fast and falls slower, and sets its band's gain in the carrier.
const uint8_t vocoder_band_count = 16;
const float vocoder_makeup = 10.0f;   // a band's level is a small part of full scale
const float vocoder_hiss_makeup = 2.0f;

FLASHMEM void vocoder_setup() {
  // Off, the vocoder takes nothing in (vocoder_set), and these two send nothing then rather than
  // silence, so its 32 filters and 16 level meters rest instead of working on zeros
  vocoder_carrier_mix.silent_when_muted(true);
  vocoder_modulator.silent_when_muted(true);
  for (uint8_t k = 0; k < vocoder_band_count; k++) {
    float f = 100.0f * powf(80.0f, k / (float)(vocoder_band_count - 1));
    const float q = 3.4f;   // neighbouring bands meet about where each is 3 dB down
    vocoder_analysis[k].setBandpass(0, f, q);
    vocoder_analysis[k].setBandpass(1, f, q);
    vocoder_synthesis[k].setBandpass(0, f, q);
    vocoder_synthesis[k].setBandpass(1, f, q);
    vocoder_bands[k / 4].gain(k % 4, 0);
  }
  for (uint8_t m = 0; m < 4; m++) vocoder_bands_mix.gain(m, 1);
  vocoder_hiss.setHighpass(0, 5000, 0.707);
  vocoder_hiss.setHighpass(1, 5000, 0.707);
  vocoder_set();
}

// The returns, the carrier and the consonants, from the settings. Carrying both, the harp's
// vocoded part comes back through the chords, since there is one bank.
void vocoder_set() {
  float a = vocoder_amount / 100.0f;
  bool chords = vocoder_carrier != 1;
  bool harp = vocoder_carrier != 0;
  // Off (amount 0), nothing goes in. It used to take every chord in all the same, through the
  // whole bank, for the returns to throw the result away
  bool on = vocoder_amount > 0;
  vocoder_carrier_mix.gain(0, on && chords ? 1 : 0);
  vocoder_carrier_mix.gain(1, on && harp ? 1 : 0);
  vocoder_modulator.gain(0, on ? 0.5 : 0);
  vocoder_modulator.gain(1, on ? 0.5 : 0);
  vocoder_chord_return.gain(0, chords ? 1 - a : 1);
  vocoder_chord_return.gain(1, chords ? a : 0);
  vocoder_string_return.gain(0, harp ? 1 - a : 1);
  vocoder_string_return.gain(1, vocoder_carrier == 1 ? a : 0);
  vocoder_string_return_r.gain(0, harp ? 1 - a : 1);
  vocoder_string_return_r.gain(1, vocoder_carrier == 1 ? a : 0);
  vocoder_out.gain(0, 1);
  vocoder_out.gain(1, vocoder_consonants / 100.0f * vocoder_hiss_makeup);
}

// Called from loop(): each band's gain follows the incoming sound's level in it
void vocoder_update() {
  static float level[vocoder_band_count] = {0};
  if (vocoder_amount == 0) return;
  for (uint8_t k = 0; k < vocoder_band_count; k++) {
    if (!vocoder_level[k].available()) continue;
    float r = vocoder_level[k].read();
    level[k] += (r - level[k]) * (r > level[k] ? 0.5f : 0.08f);   // about 5 ms up, 35 ms down
    vocoder_bands[k / 4].gain(k % 4, level[k] * vocoder_makeup);
  }
}

//>>PHONE<<
// The phone follower. Whatever plays into the minichord over USB (a phone, a computer), heard or
// not (USB audio 0 or 1), is measured three ways each block (2.9 ms): the whole sound, under 150 Hz
// (the kick) and over 6 kHz (the hats), each as a level in dB.
//
// Loud is the music's own: each band is read against the loudest it has been lately (falling 6 dB
// over 5 s), over phone_range_db below that, so the phone's volume doesn't matter and a quiet song
// swings as far as a loud one; under phone_floor_db is silence.
//
// Two followers move a setting each, the way hover does: from the preset's value with the music
// quiet to their own value at its loudest. Each rises with the music and falls back over its
// release: short for a filter that opens on each hat, a few hundred ms for chords ducking under
// the kick and swelling back.
//
// A kick is the bass band jumping phone_kick_jump_db over three blocks, near the top of its range,
// at most once each phone_kick_gap_ms. It can strike the held chord again (outside rhythm mode,
// which has its own time) and send a midi note on the chord channel.
//
// The beat: how much the three bands rise, summed, every two blocks over the last six seconds, and
// every half second the period at which that most resembles itself (70 to 180 bpm, leaning toward
// the rhythm's own tempo where a song could be heard at half or twice it) and where its beats fall,
// the kicks counting most. In rhythm mode the rhythm takes that tempo and is drawn toward those
// beats, as taps draw it (acc_tap); a MIDI clock coming in has the rhythm instead. It finds the
// beat, not the bar: the pattern's first beat lands on whichever beat it was on. Tuned on a model
// of this code run on drum tracks at known tempos and on recordings: tempo within 0.3%, beats
// placed within 30 ms nearly always, kicks found 5 ms after they land and bass notes passed over.
const float phone_floor_db = -60;       // quieter than this is no music
const float phone_range_db = 24;        // a follower's swing below the music's recent loudest
const float phone_peak_fall_db = 6.0f * 2.9f / 5000.0f;   // a block's fall of the recent loudest: 6 dB in 5 s
const float phone_kick_jump_db = 6;     // a kick's rise over three blocks
const float phone_kick_near_db = 6;     // ...to within this of the bass's recent loudest: a bass line's notes stay under it
const uint16_t phone_kick_gap_ms = 120; // kicks closer than this are one
const uint8_t phone_kick_note_ms = 30;  // a kick note's length
const uint8_t phone_follow_cc = 26;     // with knobs send midi on, the followers go out as 26 and 27 on the chord channel
#define PHONE_OSS 1024                  // the beat's memory, a value each two blocks: 5.9 s
const float phone_oss_ms = 2 * 128 * 1000.0f / AUDIO_SAMPLE_RATE_EXACT;
const float phone_bpm_low = 70, phone_bpm_high = 180;
const float phone_beat_sure = 0.25f;    // how much the music must resemble itself a beat on to have one: music 0.3-0.7, speech under 0.25
const float phone_beat_lead_ms = 28;    // a beat is found this late: the rise is counted in the blocks after it
                                        // (8 ms in the model), and on the device the rhythm's beats fell ~20 ms
                                        // after the music's with only that (phone_test.py, against its kicks)
const float phone_kick_weight = 2;      // a kick's say in where the beats fall, against the rise's spread
DMAMEM float phone_oss[PHONE_OSS];
DMAMEM float phone_work[PHONE_OSS];    // a still copy to search, without its mean
DMAMEM uint8_t phone_kicks[PHONE_OSS];      // a kick in that slot
DMAMEM uint8_t phone_kick_work[PHONE_OSS];  // ...as copied
uint16_t phone_oss_at = 0;             // where the next value goes
uint32_t phone_oss_ms_at = 0;          // when the last one went in
float phone_band_db[3] = {-100, -100, -100};
float phone_peak_db[3] = {-100, -100, -100};
float phone_follow_position[2] = {0, 0};
float phone_bpm = 0;                   // the music's tempo as found, 0 none
bool phone_listening = false;

// The input on only while something listens: muted, its filters and meters rest
void phone_set() {
  phone_listening = phone_follow[0].target || phone_follow[1].target || phone_beat || phone_kick_stab || phone_kick_note || phone_strum;
  phone_in.gain(0, phone_listening ? 0.5f : 0);
  phone_in.gain(1, phone_listening ? 0.5f : 0);
}

FLASHMEM void phone_setup() {
  phone_in.silent_when_muted(true);
  phone_bass.setLowpass(0, 150, 0.707);
  phone_bass.setLowpass(1, 150, 0.707);
  phone_treble.setHighpass(0, 6000, 0.707);
  phone_treble.setHighpass(1, 6000, 0.707);
  for (uint16_t n = 0; n < PHONE_OSS; n++) { phone_oss[n] = 0; phone_kicks[n] = 0; }
  phone_set();
}

// A follower's setting changes: the old one back as the preset has it
void phone_set_target(uint8_t slot, int16_t adress) {
  phone_follow_slot &f = phone_follow[slot];
  if (adress == f.target) return;
  if (control_can_sweep(f.target)) apply_audio_parameter(f.target, current_sysex_parameters[f.target]);
  f.target = adress;
  f.reapply = true;
  phone_set();
}

// A kick: the held chord again, and its note
static void phone_kick(float strength) {
  static elapsedMillis since;
  static bool note_on = false;
  if (strength < 0) {   // from the loop: the note's end
    if (note_on && since >= phone_kick_note_ms) {
      queue_midi(false, phone_kick_note, 0, chord_channel, chord_port);
      note_on = false;
    }
    return;
  }
  if (phone_kick_stab && !rythm_mode) {
    if (current_line >= 0) trigger_chord = true;
    else if (continuous_chord) {   // the chord kept with hold: no button down for the loop to strike it, so struck here
      trigger_chord = true;
      trigger_chord_notes();
    }
  }
  if (phone_kick_note) {
    if (note_on) queue_midi(false, phone_kick_note, 0, chord_channel, chord_port);
    queue_midi(true, phone_kick_note, (uint8_t)constrain(lroundf(64 + 63 * strength), 1, 127), chord_channel, chord_port);
    note_on = true;
    since = 0;
  }
}

// The beat in the last six seconds: its period and where its last beat fell, if it has one. The
// search is spread over the loop's passes (phone_search), a few lags each, so no pass waits long.
#define PHONE_LAG_MAX 300            // twice the longest beat's lag (70 bpm: 148), and room
static int16_t phone_lag_low, phone_lag_high, phone_lag_next = -1;
static float phone_ac[PHONE_LAG_MAX];   // resemblance at each lag
static uint32_t phone_search_ms;     // when the copy searched was taken

static float phone_resemblance(int16_t lag) {
  float s = 0;
  for (int16_t n = lag; n < PHONE_OSS; n++) s += phone_work[n] * phone_work[n - lag];
  return s / (PHONE_OSS - lag);
}

uint32_t phone_beat_anchor_ms = 0;   // a beat of the music, as last found
float phone_beat_period_ms = 0;

FLASHMEM static void phone_found(float lag, float sure) {
  static float candidate = 0;
  if (sure < phone_beat_sure) { phone_bpm = 0; candidate = 0; return; }
  float bpm = 60000.0f / (lag * phone_oss_ms);
  // a tempo near the one held moves it halfway; another must be found twice running to take over
  if (phone_bpm > 0 && fabsf(bpm - phone_bpm) < 0.04f * phone_bpm) phone_bpm += (bpm - phone_bpm) * 0.5f;
  else if (candidate > 0 && fabsf(bpm - candidate) < 0.04f * candidate) { phone_bpm = bpm; candidate = 0; }
  else { candidate = bpm; return; }
  // where the beats fall: the phase whose beats, back through the copy, land on the most rise,
  // the latest beats counting most
  // A kick counts for more: the off-beats have their hats and bass notes too, the beats their kicks.
  float period = 60000.0f / (phone_bpm * phone_oss_ms);
  float kick = phone_kick_weight * sqrtf(fmaxf(phone_ac[0], 0));
  int16_t best = 0;
  float best_sum = -1e9f;
  for (int16_t phase = 0; phase < (int16_t)period; phase++) {
    float sum = 0, weight = 1;
    for (float back = phase; back < PHONE_OSS - 2; back += period, weight *= 0.85f) {
      int16_t n = PHONE_OSS - 1 - (int16_t)lroundf(back);
      int16_t m = n + (n < PHONE_OSS - 1);
      sum += weight * (phone_work[n] + 0.5f * (phone_work[n - 1] + phone_work[m]) + kick * (phone_kick_work[n] + phone_kick_work[n - 1] + phone_kick_work[m]));
    }
    if (sum > best_sum) { best_sum = sum; best = phase; }
  }
  uint32_t beat_ms = phone_search_ms - (uint32_t)lroundf(best * phone_oss_ms + phone_beat_lead_ms);
  phone_beat_anchor_ms = beat_ms;   // for the harp's strum on the beat
  phone_beat_period_ms = 60000.0f / phone_bpm;
  if (!(phone_beat && rythm_mode) || rythm_clock_synced) return;
  rythm_bpm = constrain(phone_bpm, 30.0f, 300.0f);
  // The rhythm drawn halfway to a beat close by (an eighth of a beat); one further off (an off-beat
  // taken for the beat, now and then) only once three searches running find it, then all the way.
  static uint8_t jumps = 0;
  static double jump_error = 0;
  noInterrupts();
  double at = acc_pos - (millis() - beat_ms) * (double)rythm_bpm * ACC_BEAT / 60000.0;
  interrupts();
  double beat = floor(at / ACC_BEAT) * ACC_BEAT;
  double error = ((at - beat < ACC_BEAT / 2) ? beat : beat + ACC_BEAT) - at;
  double correction = 0;
  if (fabs(error) < ACC_BEAT / 8) { correction = error / 2; jumps = 0; }
  else {
    jumps = (jumps && fabs(error - jump_error) < ACC_BEAT / 8) ? jumps + 1 : 1;
    jump_error = error;
    if (jumps >= 3) { correction = error; jumps = 0; }
  }
  noInterrupts();
  acc_phase_error = correction;
  interrupts();
}

// A few lags of the search each pass; a new search each half second
static void phone_search() {
  if (!phone_beat && !phone_strum) { phone_bpm = 0; return; }
  if (phone_lag_next < 0) {
    static elapsedMillis since;
    if (since < 500) return;
    since = 0;
    float mean = 0;
    for (uint16_t n = 0; n < PHONE_OSS; n++) mean += phone_oss[n];
    mean /= PHONE_OSS;
    for (uint16_t n = 0; n < PHONE_OSS; n++) {   // oldest first
      phone_work[n] = phone_oss[(phone_oss_at + n) % PHONE_OSS] - mean;
      phone_kick_work[n] = phone_kicks[(phone_oss_at + n) % PHONE_OSS];
    }
    phone_search_ms = phone_oss_ms_at;
    phone_lag_low = (int16_t)floorf(60000.0f / (phone_bpm_high * phone_oss_ms));
    phone_lag_high = (int16_t)ceilf(60000.0f / (phone_bpm_low * phone_oss_ms));
    phone_ac[0] = phone_resemblance(0);
    phone_lag_next = phone_lag_low;
    return;
  }
  for (uint8_t k = 0; k < 8 && phone_lag_next <= 2 * phone_lag_high + 1; k++, phone_lag_next++) {
    phone_ac[phone_lag_next] = phone_resemblance(phone_lag_next);
  }
  if (phone_lag_next <= 2 * phone_lag_high + 1) return;
  phone_lag_next = -1;
  if (phone_ac[0] <= 0) { phone_found(0, 0); return; }
  // each lag with its double (a beat's resemblance carries on to the next), leaning toward the
  // rhythm's own tempo (the preset's, or one tapped), so the player says whether a song is 75 or
  // 150, 87 or 174; 120 where it's out of reach
  float centre = (rythm_bpm >= phone_bpm_low && rythm_bpm <= phone_bpm_high) ? rythm_bpm : 120.0f;
  int16_t best = phone_lag_low;
  float best_score = -1e9f;
  for (int16_t lag = phone_lag_low; lag <= phone_lag_high; lag++) {
    float bpm = 60000.0f / (lag * phone_oss_ms);
    float octaves = log2f(bpm / centre);
    float score = (phone_ac[lag] + 0.5f * phone_ac[2 * lag]) * expf(-0.5f * octaves * octaves);
    if (score > best_score) { best_score = score; best = lag; }
  }
  float lag = best;
  if (best > phone_lag_low && best < phone_lag_high) {   // between lags, from the curve through its neighbours
    float a = phone_ac[best - 1], b = phone_ac[best], c = phone_ac[best + 1];
    float d = a - 2 * b + c;
    if (d < 0) lag += constrain(0.5f * (a - c) / d, -0.5f, 0.5f);
  }
  phone_found(lag, phone_ac[best] / phone_ac[0]);
}

void phone_dj_update();
// Called from loop()
void phone_update() {
  phone_kick(-1);
  phone_dj_update();
  if (!phone_listening) return;
  static bool block = false;    // the second of a pair of blocks, for the beat's memory
  static float rise = 0;
  static float bass_back[3] = {-100, -100, -100};   // the bass three blocks back
  static uint8_t bass_at = 0;
  static elapsedMillis since_kick;
  static uint8_t kicked = 0;
  if (phone_level[0].available() && phone_level[1].available() && phone_level[2].available()) {
    for (uint8_t k = 0; k < 3; k++) {
      float r = phone_level[k].read();
      float db = r > 0.00001f ? 20 * log10f(r) : -100;
      if (db > phone_floor_db && db > phone_band_db[k]) rise += (k == 2 ? 0.5f : 1.0f) * fminf(db - phone_band_db[k], 20);
      phone_band_db[k] = db;
      phone_peak_db[k] = fmaxf(db, fmaxf(phone_peak_db[k] - phone_peak_fall_db, phone_floor_db));
    }
    float bass = phone_band_db[1];
    if (bass > phone_floor_db && bass - bass_back[bass_at] > phone_kick_jump_db && bass > phone_peak_db[1] - phone_kick_near_db && since_kick >= phone_kick_gap_ms) {
      since_kick = 0;
      kicked = 1;
      phone_kick(constrain(1 - (phone_peak_db[1] - bass) / phone_kick_near_db, 0.0f, 1.0f));
    }
    bass_back[bass_at] = bass;
    bass_at = (bass_at + 1) % 3;
    block = !block;
    if (!block) {
      phone_oss[phone_oss_at] = rise;
      phone_kicks[phone_oss_at] = kicked;
      phone_oss_at = (phone_oss_at + 1) % PHONE_OSS;
      phone_oss_ms_at = millis();
      rise = 0;
      kicked = 0;
    }
    for (uint8_t s = 0; s < 2; s++) {
      phone_follow_slot &f = phone_follow[s];
      float db = phone_band_db[f.band], top = phone_peak_db[f.band];
      float live = top <= phone_floor_db ? 0 : constrain((db - (top - phone_range_db)) / phone_range_db, 0.0f, 1.0f);
      float &p = phone_follow_position[s];
      p += (live - p) * (live > p ? 0.5f : 1 - expf(-2.9f / fmaxf(f.release_ms, 10)));
    }
  }
  phone_search();

  static elapsedMillis since_apply;
  if (since_apply < 5) return;
  since_apply = 0;
  for (uint8_t s = 0; s < 2; s++) {
    phone_follow_slot &f = phone_follow[s];
    float p = phone_follow_position[s];
    if (control_can_sweep(f.target)) {
      static int16_t applied[2] = {0, 0};
      static elapsedMillis since_refresh[2];
      int16_t from = current_sysex_parameters[f.target];
      int16_t to = f.value;
      int16_t lo, hi;
      if (parameter_range(f.target, lo, hi)) to = constrain(to, lo, hi);
      int16_t value = (int16_t)lroundf(from + (to - from) * p);
      // a preset load or an editor puts the stored value back: while it's up, apply it again now and then
      if (p > 0.001f && since_refresh[s] > 250) f.reapply = true;
      if (value != applied[s] || f.reapply) {
        apply_audio_parameter(f.target, value);
        applied[s] = value;
        f.reapply = false;
        since_refresh[s] = 0;
      }
    }
    static int16_t sent[2] = {-1, -1};
    if (!knob_midi || !f.target) sent[s] = -1;
    else {
      int16_t cc = (int16_t)lroundf(p * 127);
      if (cc != sent[s]) queue_midi_cc(phone_follow_cc + s, cc, chord_channel, chord_port);
      sent[s] = cc;
    }
  }
}

//>>PHONE DJ<<
// The song through the minichord on its way to the speaker (USB audio 1, play along), as a DJ's
// mixer has it: a filter (one control, low pass below the middle and high pass above it, the song as
// it is in the middle), an echo in time (sent from the song and fed back, at a beat's division of the
// music's tempo as found, or the rhythm's), and the deck's tape stop. And the harp plays the song: a
// grab takes its last second for the strings to play (voice source 5), and the strum plays the held
// chord on the strings on every beat the music has, or down and up in eighths.
const uint16_t phone_tape_stop_ms = 800;
const uint16_t phone_echo_max_ms = 500;    // its delay line comes out of the audio memory: 500 ms is 173 blocks

void phone_dj_set() {
  int16_t v = phone_dj_filter_value;
  float q = 0.707f + phone_dj_resonance / 100.0f * 3.3f;
  bool low = v < 98, high = v > 102;
  for (uint8_t c = 0; c < 2; c++) {
    if (low) phone_dj_filter[c].frequency(16000.0f * powf(200.0f / 16000.0f, (100 - v) / 100.0f));
    if (high) phone_dj_filter[c].frequency(20.0f * powf(4000.0f / 20.0f, (v - 100) / 100.0f));
    phone_dj_filter[c].resonance(q);
    phone_dj_dry[c].gain(0, low || high ? 0 : 1);
    phone_dj_dry[c].gain(1, low ? 1 : 0);
    phone_dj_dry[c].gain(2, high ? 1 : 0);
    phone_dj_out[c].gain(0, 1);
    phone_dj_out[c].gain(1, 1);
  }
  float send = phone_echo_send / 100.0f;
  phone_echo_in.gain(0, send * 0.5f);
  phone_echo_in.gain(1, send * 0.5f);
  phone_echo_in.gain(2, min(phone_echo_feedback, (uint8_t)90) / 100.0f);
}

FLASHMEM void phone_dj_setup() {
  // DMAMEM is not cleared at power-up: a preset saved with the song as a voice would play whatever RAM2 held
  memset(song_pcm, 0, sizeof(song_pcm));
  memset(phone_ring, 0, sizeof(phone_ring));
  phone_deck.begin(phone_ring, SONG_SAMPLE_LENGTH);
  for (uint8_t c = 0; c < 2; c++) phone_dj_filter[c].octaveControl(0);
  phone_echo.delay(0, 250);
  for (uint8_t k = 1; k < 8; k++) phone_echo.disable(k);
  phone_dj_set();
}

// A second of the song onto the harp: from the deck, as loud as it goes, and the harp set to play it
FLASHMEM void phone_grab() {
  AudioNoInterrupts();
  for (uint8_t i = 0; i < 12; i++) if (harp_voice_source == 5) harp_sample_array[i]->stop();
  for (uint8_t i = 0; i < 4; i++) if (chord_source_of[i] == 5) chord_sample_array[i]->stop();
  phone_deck.grab(song_pcm, SONG_SAMPLE_LENGTH);
  AudioInterrupts();
  int32_t peak = 1;
  for (uint32_t k = 0; k < SONG_SAMPLE_LENGTH; k++) peak = max(peak, (int32_t)abs(song_pcm[k]));
  float g = 29000.0f / peak;
  // nothing coming in (under -40 dB) stays quiet, not hiss brought up to full scale
  if (g > 1 && peak >= 300) for (uint32_t k = 0; k < SONG_SAMPLE_LENGTH; k++) song_pcm[k] = (int16_t)(song_pcm[k] * g);
  song_grabbed = true;
  if (harp_voice_source != 5) {
    current_sysex_parameters[264] = 5;
    set_harp_source(5);
  }
  Serial.println("Phone: a second of the song grabbed onto the harp");
}

// The action (332), never kept: 1 the tape stop (and the song back in, the next time), 2 a grab
FLASHMEM void phone_deck_action(int16_t action) {
  current_sysex_parameters[332] = 0;
  if (action == 1) {
    if (phone_deck.live()) phone_deck.stop(phone_tape_stop_ms);
    else phone_deck.start();
  } else if (action == 2) phone_grab();
}

// A string the strum plucks: as a finger lifting would, so it rings out by itself
static void phone_strum_pluck(uint8_t i) {
  if (harp_array[i].read_value() || midi_in_harp[i].owned) return;   // a finger on it, or a note sent in
  // the ribbon's and the zipper's voices are fingers, not strings, and harp drums have no notes: it rests
  if (harp_drums || harp_ribbon_active() || harp_positions_active()) return;
  if (harp_voice_position[i] != i) {   // a hammer had moved it
    harp_voice_position[i] = i;
    current_harp_notes[i] = calculate_note_harp(i, chord_context_slashed, chord_context_sharp);
  }
  pluck_strength = -1;
  pluck_strum = -1;
  pluck_string(i);
  string_plucked[i] = true;
}

void phone_dj_update() {
  // the echo's time follows the tempo: the music's as found, or the rhythm's
  static elapsedMillis since_time;
  if (phone_echo_send && since_time > 100) {
    since_time = 0;
    static const float beats[5] = {0.5f, 0.75f, 1.0f, 0.25f, 1.0f / 3};
    float bpm = phone_bpm > 0 ? phone_bpm : rythm_bpm;
    float ms = constrain(60000.0f / bpm * beats[min(phone_echo_time, (uint8_t)4)], 30.0f, (float)phone_echo_max_ms);
    static float set_ms = 0;
    if (fabsf(ms - set_ms) > 2) { phone_echo.delay(0, ms); set_ms = ms; }
  }
  // the strum: six strings in the middle of the harp, a string every 12 ms, low to high on the beat
  static int8_t queue[6];
  static uint32_t due[6];
  static uint8_t queued = 0;
  static uint32_t last_at = 0;
  uint32_t now = millis();
  for (uint8_t k = 0; k < queued; k++) {
    if ((int32_t)(now - due[k]) < 0) continue;
    phone_strum_pluck(queue[k]);
    queue[k] = queue[--queued];
    due[k] = due[queued];
    k--;
  }
  if (!phone_strum || phone_bpm <= 0 || phone_beat_period_ms <= 0 || (current_line < 0 && !continuous_chord) || queued) return;   // a chord held, or kept with hold
  float unit = phone_beat_period_ms / (phone_strum == 2 ? 2 : 1);
  int32_t since = (int32_t)(now - phone_beat_anchor_ms);
  uint32_t index = (uint32_t)floorf(since / unit);
  uint32_t at = phone_beat_anchor_ms + (uint32_t)lroundf(index * unit);
  // right on a beat (or an eighth), and once: the beat found moves every half second, so it is told
  // by its time, not its count. Counted from a beat, an even one is on the beat: a down strum
  if (since < 0 || now - at > 30 || (int32_t)(at - last_at) < (int32_t)(unit / 2)) return;
  last_at = at;
  bool down = phone_strum == 1 || index % 2 == 0;
  for (uint8_t k = 0; k < 6; k++) {
    queue[queued] = down ? 3 + k : 8 - k;
    due[queued++] = now + k * 12;
  }
}

void loop() {
  usb_audio_update();
  lock_update();
  // Process incoming MIDI messages
  // A chord sent in arrives as several notes at once: take what has come, up to a handful a pass
  for (uint8_t k = 0; k < 16 && usbMIDI.read(); k++) {
    processMIDI();
  }
  midi_in_settle();
  harp_vibrato_settle();
  harp_strings_settle();
  rest_chord_voices();
  chord_touch_glide();
  bend_watch();
  // Check sysex controller connection
  if (sysex_controler_connected && bitRead(USB1_PORTSC1, 7)) {
    sysex_controler_connected = false;
  }

  // Update debouncers
  hold_button.set(digitalRead(HOLD_BUTTON_PIN));
  up_button.set(digitalRead(up_button_pin()));
  down_button.set(digitalRead(down_button_pin()));
  LBO_flag.set(digitalRead(BATT_LBO_PIN));
  chord_matrix.update(chord_matrix_array);
  chord_legato_watch();
  drone_watch();

  // Handle low battery indicator
  handle_low_battery();

  // Handle hold button for mode switching and rhythm
  handle_hold_button();

  // Handle preset changes
  uint8_t up_transition = up_button.read_transition();
  uint8_t down_transition = down_button.read_transition();
  bool up_state = up_button.read_value();
  bool down_state = down_button.read_value();
  // the modifier held: down reverts the preset, up is the panic, and neither changes the preset
  if (modifier_preset_combo && chord_matrix_array[0].read_value() && !(up_state && down_state) && (up_transition == 2 || down_transition == 2)) {
    modifier_combo_used = true;
    preset_action(down_transition == 2 ? 1 : 2);
    up_transition = down_transition = 0;   // consumed: no preset change arms on this press
  }
  handle_key_change_mode(up_transition, down_transition, up_state, down_state);
  handle_preset_change(up_transition, down_transition, up_state, down_state);

  // Handle potentiometer updates
  bool alternate = knob_layer || (chord_matrix_array[0].read_value() && !modifier_holding);   // on knob layer 1 the alternates, whatever the modifier does
  flag_save_needed |= chord_pot.update_parameter(alternate);
  flag_save_needed |= harp_pot.update_parameter(alternate);
  flag_save_needed |= mod_pot.update_parameter(alternate);
  if (knob_midi) send_knob_ccs();
  handle_hover(alternate);

  // Two quick taps of the modifier toggle whatever the player has assigned to
  // the gesture. Only when no chord button is down, so it never competes with
  // sharpening: a tap with a chord held is a sharpen, not a gesture.
  {
    static bool modifier_was_down = false;
    static elapsedMillis press_length;
    static elapsedMillis since_first_tap;
    static uint8_t tap_count = 0;
    bool modifier_down = chord_matrix_array[0].read_value();
    if (modifier_down && !modifier_was_down) {
      press_length = 0;
      modifier_pressed_ms = millis();
      // tap and hold: the second press acts as it lands, and its let-go says whether it was a tap
      if (double_tap_hold && tap_count == 1 && since_first_tap < modifier_tap_gap && current_line == -1) {
        tap_count = 0;
        modifier_holding = true;
        toggle_double_tap_target();
      }
    } else if (!modifier_down && modifier_was_down && modifier_holding) {
      modifier_holding = false;
      if (press_length >= modifier_tap_max) toggle_double_tap_target(false);   // held: back as it was; a tap stays
    } else if (!modifier_down && modifier_was_down && modifier_combo_used) {
      modifier_combo_used = false;   // held for a fill: not a tap
      tap_count = 0;
    } else if (!modifier_down && modifier_was_down) {
      if (press_length < modifier_tap_max && current_line == -1) {
        if (tap_count == 1 && since_first_tap < modifier_tap_gap) {
          tap_count = 0;
          toggle_double_tap_target();
        } else {
          tap_count = 1;
          since_first_tap = 0;
        }
      } else {
        tap_count = 0;   // a hold, or a chord was down: not part of a gesture
      }
    }
    if (tap_count == 1 && since_first_tap > modifier_tap_gap) tap_count = 0;
    modifier_was_down = modifier_down;
  }

  step_led_animation();
  rythm_led_settle();
  rythm_clock_watch();

  // Handle continuous mode logic
  if (!continuous_chord && !rythm_mode) {
    handle_continuous_mode();
  }

  // Handle chord logic
  if (current_line >= 0) {
    fundamental = current_line;
    detect_slash();
    bool button_maj = chord_matrix_array[1 + current_line * 3].read_value();
    bool button_min = chord_matrix_array[2 + current_line * 3].read_value();
    bool button_seventh = chord_matrix_array[3 + current_line * 3].read_value();
    handle_chord_type(button_maj, button_min, button_seventh);
    update_chord_notes(); // Replaced updateNotes() with update_chord_notes()
    update_harp_notes();  // Added call to update_harp_notes()
    trigger_chord_notes();
  }
  if (rythm_mode) acc_update();   // after the chord is built, so a press is heard with its chord
  chord_memory_update();

  // Handle chord button transitions
  handle_chords_button();

  // Handle harp functions
  handle_harp();
  plate_id_update();
  looper_update();
  vocoder_update();
  phone_update();
  drums_update();
  samples_update();
  chord_samples_glide();
  lfo_rates_update();
  // The only point at which this firmware transmits MIDI. Must stay last, and must
  // stay in loop() -- see the MIDI OUTPUT QUEUE comment above.
  mpe_update_glide();
  drain_midi_queue();
}
