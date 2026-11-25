# Progression upload (F0 7D 70)

A chord progression sent to the minichord over SysEx goes into chord memory, in place of the one there,
exactly as if it had been recorded: RAM only (gone at power off), played by rhythm mode and driven by the
same controls (setting 287, control changes 104–108, the double tap assigned to 287). It can be read back,
whether it was sent or recorded.

This file is the contract. `include/progression_core.h` follows it and `test/progression_pc` checks the
two against each other; where they disagree, this wins and the code is fixed.

## Messages

Every message is `F0 7D 70 <op> … F7`, at least 7 bytes, every data byte under 0x80. Numbers that need
more than 7 bits travel as groups of 7 bits, least significant first (`n5` is five groups), as the user
kit's messages carry them. The CRC is zlib's CRC-32.

| Op | Name | Message | Length |
| --- | --- | --- | --- |
| 01 | WRITE | `F0 7D 70 01 proto flags count crc:n5 entries… F7` | 13 + 4 × count |
| 02 | READ | `F0 7D 70 02 00 00 F7` | 7 |
| 05 | INFO | `F0 7D 70 05 00 00 F7` | 7 |

- **proto** is 1.
- **flags**: bit 0 = play from the next bar line once it's in. The other bits are 0.
- **count**: 1 to 64 entries (chord memory's size).
- **crc**: CRC-32 of the entries' bytes, 4 × count of them.

### An entry, 4 bytes

| Byte | Content | Values |
| --- | --- | --- |
| 0 | Button, in the order the player sees them: column × 3 + row | Column 0–6 is F C G D A E B, row 0–2 major, minor, seventh; so 0–20 |
| 1 | Chord type | 0 = what that button plays on its own in the layout as it is (chord layout 39, the alternate slots 202–208, Barry Harris mode); 1–29 = the alternate layout's numbering (1 major, 2 minor, 3 dominant 7 … 29), chord_catalogue's |
| 2 | Modifiers | bit 0 = sharp; bits 1–3 = the slash bass's column + 1 (0 = no slash); bits 4–6 = 0 |
| 3 | Length | 1–127 eighths |

Lengths are in eighths, not beats, because time signature (393) has 5/8, 7/8, 9/8 and 11/8. A chord longer
than 127 eighths is sent as more entries of the same chord.

The firmware's lines run the other way (B to F), so the button's column becomes `line = 6 − column`, and
the slash's column the same way.

## Replies

All replies have an op of 0x40 and up and are never answered (two minichords, or an app that echoes,
can't set each other off).

| Reply | Message | When |
| --- | --- | --- |
| STATUS | `F0 7D 70 (40 \| op) status count F7` | After a WRITE, and after any message refused. count = the entries chord memory holds after it |
| READ's answer | `F0 7D 70 42 proto 00 count crc:n5 entries… F7` | WRITE's layout. count 0 when chord memory is empty or recording |
| INFO's answer | `F0 7D 70 45 proto 64 state count bar F7` | state as chord memory's: 0 empty, 1 recording, 2 playing, 3 stopped; bar = the meter's eighths now |

No answer to INFO within a second means the firmware has no progression upload.

### Status

The same numbers as the user kit's where they mean the same.

| Status | Meaning |
| --- | --- |
| 0 | OK |
| 1 | Busy: chord memory is recording |
| 2 | Too big: count over 64 |
| 3 | Bad message: not framed, a length that doesn't match count, count 0, a flag or byte out of range, an unknown op |
| 4 | Unsupported: proto isn't 1 |
| 7 | CRC: the entries don't match crc |

A WRITE is checked in this order: framing (3), shorter than 13 bytes (3), proto (4), flags (3), count over 64 (2), count 0 or the
length (3), crc's groups (3), the CRC (7), then each entry (3), then busy (1). A refused WRITE leaves chord
memory as it was.

## What a WRITE does

1. Each entry becomes a chord memory moment, as a press would make it: the line, the chord (type 0
   resolved now, in the layout as it is), sharp, and the slash bass if any.
2. Each starts where the entries before it end, in eighths.
3. The whole is rounded up to whole bars of the meter as it is now, the last chord stretched to fill,
   since chord memory starts and loops on bar lines. It always loops.
4. If chord memory was playing, or flags bit 0 is set, it plays from the next bar line (rhythm mode turns
   on if it's off). Otherwise it is stopped, ready for a play.

A READ gives each moment back as its column's major button with its chord type spelled out (1–29), since
chord memory keeps the chord, not the row it came from.

## Example

Dm, G7, C, four beats (8 eighths) each, play now:

    F0 7D 70 01 01 01 03 <crc:n5> 0A 00 00 08  08 00 00 08  03 00 00 08 F7

Buttons 10 (D minor), 8 (G seventh) and 3 (C major), type 0 (the buttons' own chords).
