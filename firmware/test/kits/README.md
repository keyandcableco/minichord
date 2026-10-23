# User drum kit test vectors

The files every implementation of the user drum kit (drum kit 13, "your own") is tested against: the
firmware (include/user_kit.h), minicontrol (kits.js) and minictl (kit.py). generator/user_kit.md is the
contract they all follow; generator/user_kit.py, its Python reference, writes everything here. Don't edit
these files by hand: change user_kit.py and run

    python3 generator/user_kit.py vectors     # writes test/kits/ again (the same bytes every time)
    python3 generator/user_kit.py selftest    # tests user_kit.py, and that test/kits/ is what it writes

from the firmware folder. Python 3's standard library is all it needs. The synthetic kit's sound is made
in integers only, so the files come out the same to the bit on any machine.

## The synthetic kit

| File | What it is |
| --- | --- |
| synthetic.json | The kit: `base_kit` 1, `name` "synthetic test kit", `meta` (what its .mckit carries) and `voices`, 13 in drum_machine.h's order. Each voice is null (empty) or its entry's fields as the image has them (`offset`, `length`, `rate`, `level`, `pan`, `choke`, `flags`, `pitch`) and `samples`, base64 of 16-bit signed little-endian PCM |
| synthetic.image | The image every builder must make from synthetic.json, byte for byte: 66,492 bytes (66,236 of sound), CRC-32 19A640C5 |
| synthetic.mckit | The .mckit every builder must make from synthetic.json's `meta` and the image: the JSON written with `json.dumps(meta, sort_keys=True, separators=(',', ':'))`. 66,827 bytes |
| synthetic.frames | Every message of the image's upload, a line each, in lowercase hex with no spaces: BEGIN, 297 DATA pieces from offset 0 (296 of 269 bytes, the last of 228), COMMIT. 299 lines |
| small.image | The small valid kit the broken images are made from (444 bytes, CRC-32 C675026D): base_kit 5, name "small kit", voices 0 and 1 filled, 7 and 8 sharing a hit, its sound 188 bytes (voice 0 at 0, voice 1 at 80 with a 2-byte gap after its 33 samples, voices 7 and 8 at 148) |

Voices 2 (rim) and 12 (shaker) are empty, the other eleven filled with sums of decaying sines and noise
from a fixed linear congruential generator. The high tom (8) plays the low tom's hit (7) at its own rate,
33,075 Hz against 22,050. The fields take in both ends of their rules (rate 4,000 and 65,535, pan -100
and 100, level 255, choke group 7) and both values of the press-bends flag; pitch is 0 throughout, as
version 1 writes it. Nine of the ten hits are an odd number of samples, so the packing's gaps are tested.

To build the image from synthetic.json a builder packs the hits as minicontrol does: in voice order from
the start of the sound, each on a 4-byte boundary with zeros in the gap, and a voice whose samples are
the same as an earlier voice's points at that voice's hit instead of taking a second copy (that is how
voices 7 and 8 share one). The `offset` and `length` in the JSON are what that gives, for a builder to
check its own packing against; INFO's filled voices for this kit is 4091 (every bit but 2 and 12).

## The broken images

broken/*.image are images that must fail `check_image` (with data_base 0 and the file capacity, 262,144)
with the reason broken/expected.json gives for each, as `{"file name": reason}`. Each is small.image with
one fault, its header_crc made right again unless the fault is the CRC; the `order_` cases have two
faults each and test that the checks go in the contract's order (the first that fails names the reason).

| File | Reason | What is wrong |
| --- | --- | --- |
| size_0.image | 5 | an empty file |
| size_255.image | 5 | size < 256: the header cut short |
| size_not_4.image | 5 | size not a multiple of 4 (2 bytes more) |
| order_size_before_magic.image | 5 | bad magic and size not a multiple of 4: the size is checked first |
| bad_magic.image | 2 | magic MCK2 |
| order_magic_before_header_crc.image | 2 | magic mck1 and header_crc not made again: magic comes first |
| layout_2.image | 2 | layout 2 |
| format_1.image | 2 | format 1 (mu-law, reserved) |
| voices_12.image | 2 | voices 12 |
| base_kit_0.image | 2 | base_kit 0 |
| base_kit_13.image | 2 | base_kit 13 |
| header_crc.image | 3 | voice 0's level changed, header_crc not made again |
| order_header_crc_before_data_base.image | 3 | data_base nonzero and header_crc not made again |
| data_base_nonzero.image | 4 | data_base 0x601A0000 (as in flash) in a file |
| order_data_base_before_rules.image | 4 | data_base nonzero and reserved nonzero: data_base first |
| data_bytes_less.image | 5 | data_bytes 4 less than size - 256 |
| data_bytes_more.image | 5 | data_bytes 4 more than size - 256 |
| data_bytes_not_4.image | 5 | data_bytes not a multiple of 4 (size - 256 - 2) |
| name_0x80.image | 5 | a name byte 0x80 |
| name_0x7f.image | 5 | a name byte 0x7F (not printable) |
| name_0x1f.image | 5 | a name byte 0x1F (not printable) |
| name_char_after_nul.image | 5 | a character after a NUL in the name |
| name_char_at_end.image | 5 | the name NUL-padded but its 20th byte a character |
| reserved_nonzero.image | 5 | reserved 0x01000000 (its last byte) |
| voice_offset_not_4.image | 5 | voice 0's offset 2 |
| voice_past_end.image | 5 | voice 7's hit one sample past the end of the sound (the last hit) |
| voice_offset_at_end.image | 5 | voice 1's offset at the end of the sound |
| voice_wraps_32_bits.image | 5 | voice 0 at 0x80000000, 0x40000002 samples: its end wraps to 4 in 32 bits |
| voice_length_wraps_32_bits.image | 5 | voice 0's length 0x80000002: 2 * length wraps to 4 in 32 bits |
| empty_voice_level.image | 5 | empty voice 2 with level 100 |
| empty_voice_offset.image | 5 | empty voice 2 with offset 4 |
| empty_voice_pitch.image | 5 | empty voice 12 with pitch 1 |
| length_1.image | 5 | voice 0's length 1 |
| rate_3999.image | 5 | voice 0's rate 3,999 |
| rate_0.image | 5 | voice 1's rate 0 |
| pan_101.image | 5 | voice 0's pan 101 |
| pan_minus_101.image | 5 | voice 0's pan -101 |
| choke_8.image | 5 | voice 0's choke group 8 |
| flags_bit0.image | 5 | voice 0's flags 1 (pitch follow, reserved) |
| flags_bit2.image | 5 | voice 1's flags 6 (bit 2 with press bends) |
| flags_bit7.image | 5 | voice 0's flags 0x80 |
| data_crc.image | 6 | a bit of voice 0's first sample flipped |
| data_crc_gap.image | 6 | a bit flipped in the 2-byte gap after voice 1's hit (the CRC covers gaps) |
| order_rules_before_data_crc.image | 5 | reserved nonzero and a sound bit flipped: the rules first |

The two `wraps_32_bits` cases pass a C check that works `offset + 2 * length` out in 32 bits: the
firmware works it out in 64 (or checks the parts first). A nonzero pitch on a filled voice is not a fault
(version 1 writes 0 but the contract makes no rule of it for a reader), so there is no case for it; on an
empty voice it is one, as every field of an empty entry must be 0.

## Other commands

    python3 generator/user_kit.py check FILE    # the reason an image or a .mckit's image fails, 0 valid
    python3 generator/user_kit.py show FILE     # its header, voice by voice
    python3 generator/user_kit.py frames FILE   # its upload, a message a line in hex, as synthetic.frames

`check` takes `--data-base` (the data_base a copy from flash must have) and `--capacity`.
