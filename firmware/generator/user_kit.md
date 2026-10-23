# The user drum kit: image, flash slot and SysEx protocol (version 1)

Drum kit 13 ("your own", setting 337 = 13) plays up to 13 of the player's own one-shots in place of the
drum machine's 13 voices. minicontrol (or minictl) builds one **image** from the player's files and
sends it over SysEx; the minichord keeps it in flash and plays it from there. This file is the contract
the firmware (include/user_kit.h), the Python reference (generator/user_kit.py, which writes the test
vectors in test/kits/), minicontrol (kits.js) and minictl (kit.py) all follow. When one of them
disagrees with this file, this file wins and the code is fixed.

All multi-byte numbers in the image are little-endian. CRC-32 everywhere is the reflected 0xEDB88320
CRC with init and final value inverted: Python's `zlib.crc32`, the firmware's `crc32_update(0, p, n)`.

## 1. The image

`image = header (256 bytes) + sound (data_bytes bytes)`. Its size is `256 + data_bytes`.

### Header, 256 bytes

| Offset | Bytes | Field | Rule |
| --- | --- | --- | --- |
| 0 | 4 | magic | `MCK1` |
| 4 | 1 | layout | 1 |
| 5 | 1 | format | 0 = 16-bit signed mono PCM. 1 = mu-law, reserved: version 1 refuses it |
| 6 | 1 | voices | 13 |
| 7 | 1 | base_kit | 1 to 12, in setting 337's numbering (`drum_kits[base_kit - 1]`). Empty voices play this kit's own voice, and the kit's grit (bits, hold, drive) comes from it |
| 8 | 4 | data_base | **0 in every file and on the wire.** The minichord writes the flash address its sound starts at into its own copy at COMMIT, and recomputes header_crc over that copy |
| 12 | 4 | data_bytes | A multiple of 4, at most the minichord's capacity |
| 16 | 4 | data_crc | CRC-32 of the sound bytes |
| 20 | 20 | name | Printable ASCII 0x20 to 0x7E, then NUL padding to 20 bytes. A NUL may only be followed by NULs. Empty is allowed |
| 40 | 4 | reserved | 0 |
| 44 | 208 | voice[13] | 16 bytes each, in voice order (below) |
| 252 | 4 | header_crc | CRC-32 of bytes 0 to 251 |

### Voice entry, 16 bytes

| Offset | Bytes | Field | Rule |
| --- | --- | --- | --- |
| 0 | 4 | offset | Where the hit starts, in bytes from the start of the sound. A multiple of 4 |
| 4 | 4 | length | In samples. 0 = empty: the base kit's own voice plays, and every other field of the entry must be 0. Otherwise at least 2, and `offset + 2 * length <= data_bytes` |
| 8 | 2 | rate | Playback rate in Hz, 4,000 to 65,535 (any pitch change is baked into the samples). minicontrol writes 22,050 |
| 10 | 1 | level | Hundredths: 100 = 1.0, 255 = 2.55 (the drum voice's `level`) |
| 11 | 1 | pan | Signed, -100 (left) to 100 (right) = the drum voice's `pan` -1 to 1 |
| 12 | 1 | choke | Choke group 0 to 7, 0 = none. A voice struck fades every other sounding voice in its group |
| 13 | 1 | flags | Bit 0: pitch follow (reserved, must be 0 in version 1). Bit 1: press bends (1 = pressing the pad on plate 13 bends it, 0 = a press chokes it). Bits 2 to 7: 0 |
| 14 | 2 | pitch | In 0.1 Hz, 0 = untuned. Version 1 writes 0, and readers don't check it on a filled voice |

Voice order (drum_machine.h), with the GM note drum MIDI out sends for it: 0 kick 36, 1 snare 38,
2 rim 37, 3 clap 39, 4 hat 42, 5 open hat 46, 6 cymbal 49, 7 low tom 45, 8 high tom 50, 9 low conga 64,
10 high conga 63, 11 bell 56, 12 shaker 70.

Hits are packed in any order, each starting on a 4-byte boundary; builders pack them in voice order.
Two voices may point at the same hit: a builder does that exactly when a voice's samples are
byte-identical to an earlier voice's, and then the later voice points at the earlier one's hit.

### Checks (`check_image`): the same in every implementation

In this order; the first that fails names the reason (numbers as in INFO's reason, section 4):

1. Size at least 256 and a multiple of 4; else reason 5.
2. Magic `MCK1`, layout 1, format 0, voices 13, base_kit 1 to 12; else reason 2.
3. header_crc; else reason 3.
4. data_base: 0 for an image in a file or on the wire. In the minichord's flash, equal to the
   data_base it computes now (section 2); else reason 4.
5. data_bytes = size - 256 and within capacity (a file has no capacity: 262,144); reserved bytes 0;
   name as the rule says; every voice entry as its rules say; else reason 5.
6. data_crc over the sound; else reason 6.

## 2. Where it lives in flash (firmware only)

| Name | Value |
| --- | --- |
| KIT_HEADER | 0x601E0000: one 4 kB sector of its own, the header in its first 256 bytes. It lies in the presets' 64 kB block (the presets are at 0x601E8000), which a firmware upload never erases |
| KIT_TOP | 0x601E0000: the sound ends below it |
| KIT_FLOOR | 0x601A0000 |
| data_base | `max(KIT_FLOOR, align_up(0x60000000 + _flashimagelen, 65536))` |
| capacity | `KIT_TOP - data_base`, so at most 262,144 bytes |
| disabled | capacity below 65,536: no kit can be stored or played, INFO says state 4 |

Image byte `i` (i >= 256) is stored at `data_base + (i - 256)`. The header is held in RAM during an
upload and written to KIT_HEADER last, with data_base filled in.

## 3. SysEx messages

Every message is `F0 7D 6B <op> ... F7`. `7D` is the fork's own non-commercial ID (pages use 6D,
locks 6C) and `6B` is "k". Every message the computer sends is **at least 7 bytes**, so the minichord's
6-byte settings path never takes one for a setting. The minichord never answers a kit message whose op
is 0x40 or above (the shape of its own replies), so two minichords or an app that echoes can't set off
a ping-pong; any other op it doesn't know gets status 3. A kit message that isn't framed (under 7
bytes, no closing F7, a data byte over 7F) is answered 3 whatever its op below 0x40, before any op's own rule (ops 0x40 and above still get no reply). Stock firmware and fork firmware before 113 don't
answer: no reply to INFO within 1 s means "this firmware has no kit slot".

Numbers travel as 7-bit groups, least significant first: **n2** = 2 groups (14 bits), **n3** = 3
groups (21 bits), **n5** = 5 groups (35 bits, for a CRC-32 or a flash address).

**8-to-7 packing** (DATA's payload): the raw bytes in groups of 7 (the last may be shorter). Each
group of k bytes becomes k + 1 bytes: first a byte whose bit j (j = 0 to k-1) is bit 7 of raw byte j,
then the k raw bytes with bit 7 cleared. 224 raw bytes become 256.

### Computer to minichord

| Op | Name | Bytes after F0 7D 6B op | Total |
| --- | --- | --- | --- |
| 01 | BEGIN | proto (1, = 1), size n3 (the image's), crc n5 (CRC-32 of the whole image, data_base 0) | 14 |
| 02 | DATA | offset n3, crc n5 (CRC-32 of this piece's raw bytes), packed payload | at most 269 |
| 03 | COMMIT | 00 00 | 7 |
| 04 | ABORT | 00 00 | 7 |
| 05 | INFO | proto (1), 00 | 7 |
| 06 | CLEAR | 00 43 4C ("CL", so a stray byte can't clear a kit) | 8 |

A DATA piece is `image[offset : offset + len]` with `len = min(224, size - offset)`. Pieces go in
order from offset 0, one at a time.

### Minichord to computer

**Status reply** to BEGIN, DATA, COMMIT, ABORT and CLEAR, and to any malformed or unknown kit
message: `F0 7D 6B <0x40 | op> status next(n3) F7`, 9 bytes. `next` is the offset of the next piece
the minichord expects (0 when there is no upload).

**INFO reply**: `F0 7D 6B 45` then

| Field | Bytes |
| --- | --- |
| proto (the minichord's, 1) | 1 |
| state | 1 |
| reason | 1 |
| capacity | n3 |
| data_base (the minichord's, whether or not a kit is stored) | n5 |
| image size (0 with no kit) | n3 |
| image crc (CRC-32 of the stored image with data_base 0, as it was sent; 0 with no kit) | n5 |
| largest piece (224) | n2 |
| base_kit (0 with no kit) | 1 |
| filled voices (bit v = voice v has a hit; bits 0 to 6 in the first group, 7 to 12 in the second) | n2 |
| name (20 bytes, the header's, bit 7 clear) | 20 |

then `F7`: 49 bytes in all. A COMMIT that succeeds sends its status reply, then an INFO reply; a refused COMMIT sends only its status reply. With state 0, 3 or 4,
image size, image crc, base_kit, filled voices and name are all 0.

**States**: 0 empty, 1 valid, 2 receiving (an upload is open), 3 damaged, 4 disabled.

**Reasons** (with state 0, 3 or 4; 0 with state 1 or 2): 0 none, 1 no kit header (the header sector
doesn't start `MCK1`: blank, or other bytes), 2 bad magic, layout, format, voices or base_kit, 3 header CRC, 4 data_base moved (a bigger
firmware moved the sound's start), 5 a size, name, reserved byte or voice entry out of its rule,
6 data CRC, 7 too little room (disabled). A header sector whose first 4 bytes aren't `MCK1` holds no
kit: state 0, reason 1, whether it is blank or holds something else (an old filesystem's bytes, a header
write cut short). A header that starts `MCK1` but fails a check is state 3 with its reason.

**Status codes**: 0 ok, 1 busy, 2 too big, 3 bad message or length, 4 unsupported proto (a bad
format is only seen at COMMIT, as 10), 5 no upload begun, 6 out of order (`next` says what is expected),
7 bad piece CRC, 8 flash read-back mismatch, 9 image CRC mismatch at COMMIT, 10 the image fails
check_image at COMMIT (any step: the header, a data_base that isn't 0, the sound's CRC), 11 slot disabled.
The computer checks an image before sending it, so 10 means the image was damaged on the way.

## 4. What the minichord does

- **BEGIN**: refused with 4 unless proto is 1; with 1 while another BEGIN is waiting to open; with 11 when disabled; with 2 when `size < 256` or
  `size - 256 > capacity` or size is not a multiple of 4; with 1 (busy) while an upload is open, the
  rhythm is running, or the looper is counting in, recording or playing. Otherwise the upload opens: the DAC is
  muted (as a preset save mutes it), the stored kit is marked invalid and the drum machine moves off
  it, then the header sector is erased, then the reply goes out (status 0, next 0). The upload opens on
  the minichord's next loop pass; until then an ABORT cancels it (answered 0), a CLEAR cancels it and
  clears, and a DATA or COMMIT is answered 5. Before the mute, whatever MIDI the minichord has already
  queued is sent, and the drum note-offs still waiting are sent at once.
- **While an upload is open** the minichord runs its upload loop and nothing else: it reads USB MIDI
  as fast as it comes, answers kit messages, drops every other message, and stops the rhythm timer's
  work. The bank LED pulses.
- **DATA**: status 5 if no upload is open. The offset must be the expected one; a repeat of the
  previous piece (same offset, same CRC) is answered 0 without writing again; any other offset is 6,
  and so is a piece at offset = size, once every byte is in.
  The packed length must match `len` (else 3) and the CRC the raw bytes (else 7). Image bytes 0 to 255
  go into the header buffer in RAM. Every other byte goes to flash: each 4 kB sector is erased the
  first time data reaches it in this upload, even when it already reads all FF (an erase cut short by a
  power cut can read FF and still not hold a page well), each 256-byte page is
  programmed when full and read back (a mismatch is 8 and ends the upload with no kit).
- **COMMIT**: status 5 if no upload is open, checked before anything else. A COMMIT or ABORT of the wrong length, or with bytes
  other than 00 00 after the op, is 3 and leaves the upload open. The last partial page is programmed.
  All `size` bytes must have arrived (else 3). The CRC of header buffer + sound in flash must equal
  BEGIN's crc (else 9). Header buffer + sound must pass all of `check_image`, with data_base 0 as sent
  (else 10). The minichord then writes data_base into the header buffer, recomputes header_crc,
  programs the header page, reads it back, runs the full check on flash at its data_base, builds the
  kit, unmutes, leaves upload mode, and replies 0 then INFO. Any failure from the "All size bytes"
  check on ends the upload with no kit; a failure after the header page was written erases the header
  sector again, so the next boot finds no kit either.
- **ABORT**: ends any upload with no kit, unmutes, replies 0 (or 5 if none was open).
- **CLEAR**: status 11 when the slot is disabled. Otherwise it ends any upload, erases the header
  sector under the same mute BEGIN uses (queued MIDI sent first, pending chord strums dropped), leaves
  no kit, and replies 0.
- **INFO**: answered at any time, during an upload too (state 2).
- **Silence**: 5 s with no DATA, COMMIT, ABORT or CLEAR during an upload ends it with no kit. INFO,
  BEGIN and malformed messages don't count, so an app that keeps asking INFO can't hold an abandoned
  upload (and the mute) open. The minichord times this
  with the CPU's cycle counter, because `millis()` loses the erase times.
- **On leaving upload mode** for any reason: the DAC is unmuted, the drum note-offs still waiting
  are sent (a late note-off is harmless), the rest of the MIDI output queue is emptied, any drum hit
  still queued is dropped, notes still held from MIDI in are released as All Notes Off (CC 123)
  releases them (their note-offs may have been dropped during the upload), and the LED shows green
  (kit stored) or red (no kit) briefly.
- **At boot**, after the presets load, the stored kit is checked in full (`check_image` on flash).
- **Kit 13**: with a valid kit, the drum machine plays it; with none, it plays drum kit 4 (studio).
  Firmware before 113 has no kit 13 and plays its last kit (cliqhop).
- **Choke groups** are shared with the base kit's voices left in place: a hit in group 1 chokes the
  base kit's open hat in group 1, as a real kit's hats would.

## 5. Timers on the computer's side

| Waiting for | Time | Then |
| --- | --- | --- |
| BEGIN's reply | 3 s | send ABORT (in case the upload opened and only the reply was lost), give up |
| A DATA reply | 1 s | send the same piece again; after 3 tries send ABORT and give up |
| COMMIT's reply | 3 s | ask INFO: state 1 with the same size and crc means it landed; state 2 means COMMIT was lost, so send ABORT; then give up |
| INFO's reply | 1 s | "this firmware has no kit slot" |

A DATA answered 3 or 7 is sent again, counting toward its 3 tries. Any other non-zero DATA status ends
the upload: the computer sends ABORT, unless the status was 5 or 8, where the minichord has already
ended it. A COMMIT answered with anything but 0 or 5 is followed by ABORT too (a COMMIT damaged on the
way is answered 3 and leaves the upload open), and so is a COMMIT whose reply and INFO both go
unanswered. If the computer is stopped (Ctrl-C, a closed tab) any time after it sent BEGIN and before
COMMIT was answered, it sends ABORT on the way out if it can. For about a second after its own upload
ends it ignores stray BEGIN and DATA replies (late answers to its own last piece), rather than taking
them for another app's upload. A BEGIN answered busy (1) means the rhythm or the looper runs: the computer stops them (control
command 8 with 0, and the looper's setting 256 = 3), says so, and sends BEGIN once more. It pauses its
own other traffic until the upload ends. A reply it didn't ask for (`F0 7D 6B 42 ...` from another app's
upload) means another app is sending a kit: pause and say so until a COMMIT, ABORT or CLEAR reply or
6 s of silence.

## 6. The .mckit file

`"MCKF"` (4 bytes), version (1 byte, = 1), 3 bytes 0, json_length (4 bytes, little-endian), then
json_length bytes of JSON, then **the image verbatim** (data_base 0), to the end of the file.

The JSON is written as Python's `json.dumps(meta, sort_keys=True, separators=(',', ':'))`: keys sorted
at every level, everything outside ASCII escaped as `\uXXXX`. It holds `name`, `author`, `licence` (the
licence text found, or "unknown"), `sources` (13 entries by voice: the file name used, null for an empty
voice, the earlier voice's name again for a shared hit), and `made` (an ISO date). Readers ignore keys
they don't know, and refuse a version other than 1 or reserved bytes that aren't 0. A reader runs
`check_image` on the image before using or sending it. Tools may also accept a bare image (a file that
starts `MCK1`).

## 7. Test vectors (test/kits/, written by generator/user_kit.py)

- `synthetic.json`: a deterministic 13-voice kit, its .mckit meta under the key `meta` (made by user_kit.py from sums of decaying sines and
  noise from a fixed generator, no files needed): base_kit, name, and per voice the fields above plus
  its samples as base64 of 16-bit little-endian PCM (or null for an empty voice). Voices 2 and 12 are
  empty; voices 7 and 8 share one hit.
- `synthetic.image`, `synthetic.mckit`: what every builder must produce from `synthetic.json`, byte
  for byte (the .mckit's JSON written with `json.dumps(meta, sort_keys=True, separators=(',', ':'))`).
- `synthetic.frames`: every message of its upload, BEGIN to COMMIT, one per line in lowercase hex.
- `small.image`: a small valid kit, the base the broken images are made from.
- `broken/*.image` and `broken/expected.json` (a flat `{"name.image": reason}`): images that must fail
  `check_image` with the reason given.
