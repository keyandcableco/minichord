# The user kit's slot, tested on a computer

include/user_kit_core.h is the part of the user drum kit (drum kit 13, "your own") with nothing of the
Teensy in it: the image's checks and the whole slot, BEGIN to COMMIT, ABORT, INFO, CLEAR and the 5 s of
silence. harness.cpp compiles it with the computer's g++ against a fake Teensy and sends it every message
generator/user_kit.md has, checking each reply byte for byte against the contract. Nothing here talks
to a minichord.

    make                     # builds build/harness (ASan and UBSan on, warnings are errors) and runs it
    make BUILD=/tmp/somewhere   # the same, built outside the repository
    make cross               # minictl's kit.py and minicontrol's kits.js against the same vectors

It needs g++ (C++17) and python3; `make cross` also needs node for minicontrol. The run takes about
15 s, nearly all of it the power cuts. The last line says how many checks failed; the exit status is 1
if any did.

## The fake Teensy

- **Flash** from 0x601A0000 to 0x601E0FFF: the slot's 256 kB of sound and the header's sector, held
  as two allocations of exactly their size, so a read that runs off the end of the sound (on the Teensy,
  into the header) or off the header's sector is a heap overflow that ASan stops at. The slot may only
  read, erase or program from its data_base to 0x601E0000 and in the header's sector: below data_base is
  the firmware's on the Teensy. An erase must be a whole 4 kB sector on a 4 kB boundary and sets it to FF.
  A program writes 1 to 256 bytes inside one 256-byte page and can only clear bits (programming over bytes
  that weren't erased is a failure). Reads go through a pointer, as on the Teensy, and are counted. Every
  erase and program must happen with the DAC muted and with no kit built from flash.
- **Replies** are kept and compared with what the contract says. The harness builds them with its own
  CRC, 7-bit numbers and 8-to-7 packing, not the core's.
- **The clock** (now_us) is a number the test sets.
- **The mute, the kit and the end of upload mode** are recorded: mutes and unmutes must pair up, the DAC
  must be back on before upload mode ends, and ended(kit) must agree with whether a kit was built. It
  also notes the flash reads made by then. The kit build (kit_apply) records each voice's address,
  length, rate, level, pan, choke, flags and pitch.
- **The upload's buffers** (work_t) are lent at each BEGIN, as the Teensy's upload loop lends them from
  its stack, filled with junk first. The slot must hold them exactly while an upload is open.
- **The loop's pass**: normally a BEGIN noted is opened straight after its message. Deferred, it is left
  waiting, as the Teensy leaves it until drums_update's next pass, and more messages can come first.

expect.py turns test/kits/synthetic.json into plain lines (each voice's fields and its samples' CRC)
so the harness can check the kit build against the JSON without reading JSON itself.

## What it runs

1. **The synthetic kit**: synthetic.frames, which must be the harness's own framing of
   synthetic.image, is sent line by line. Every reply must be exact: BEGIN gives 0 with next 0, each
   DATA gives 0 with the next offset, an INFO in the middle gives state 2. COMMIT gives 0 and then an INFO
   with state 1, capacity 262,144, data_base 0x601A0000, size 66,492, crc 19A640C5, base kit 1,
   filled 4091 and the name. The flash must hold the image with data_base filled in. Even on blank flash
   every sound sector is erased (an erase cut short can read FF), after the header's sector, and there is
   one program per 256 bytes of sound plus the header.
   The kit build must get every voice's fields, and each hit in flash must be that voice's samples. A
   reboot must find the same kit and give the same INFO. Sending the kit again over the stored one must
   erase each sound sector before writing it.
2. **Power cuts**: the upload is cut at every erase and every program, in three ways: before the
   operation, halfway through it (half a page programmed, half a sector erased) and just after it. The
   harness then reboots on the flash as it is. The boot must find no kit (state 0 or 3, nothing built)
   or the whole new kit. The old kit may survive only when the power went before the first erase. An
   upload after every cut must still go through. This runs from blank flash and again from flash that
   holds small.image with the rest of the sound area written. That is 1,668 cuts.
3. **The broken images**: the core's check_image must give expected.json's reason for every one.
   - Sent as an upload (over the stored synthetic kit), the four size cases are refused at BEGIN with 2,
     and the stored kit is left untouched.
   - The other 40 are sent whole, and COMMIT must refuse them with 10, never program the header, and
     leave no kit. COMMIT runs all of check_image on the image as it was sent, so data_base_nonzero and
     data_crc are among them.
   - Each image is also placed in flash as the minichord would hold it, with its own data_base, or a
     moved one for the data_base cases. The boot must then give state 3 with the expected reason, and
     INFO must say so, except for the two whose header doesn't start MCK1: a header sector without MCK1
     holds no kit, so they give state 0, reason 1. This covers 38 of the 44: the size cases and
     data_bytes_less/more can't exist in flash.
4. **The edge cases**:
   - A repeated piece gives 0 with no second write, also for the last piece.
   - A piece that is no longer the previous one, or one that skips ahead, gives 6 with the right next.
   - A bad piece CRC, or a changed payload byte, gives 7. A piece one packed group short gives 3. A
     piece at offset = size, once every byte is in, gives 6.
   - BEGIN while open gives 1. COMMIT with bytes missing gives 3 and ends the upload. DATA, COMMIT and
     ABORT with no upload give 5, a malformed COMMIT or ABORT too (5 comes before the shape; in an upload
     a malformed one is 3).
   - ABORT gives 0 and leaves no kit.
   - BEGIN 4 bytes over capacity, under 256, or not a multiple of 4 gives 2. Proto 2 gives 4. Busy gives
     1, and none of these refusals touch the kit. BEGIN of exactly the capacity is taken.
   - BEGIN's CRC wrong gives 9 at COMMIT.
   - A page that doesn't read back gives 8: a sound page, the last partial page at COMMIT, and the
     header page.
   - CLEAR with a kit, with none, and in an upload gives 0, and a CLEAR without its "CL" gives 3.
   - 5 s of silence: none at 4,999,999 µs, the upload ended at 5,000,000 µs, and a DATA restarts the
     count. It also holds over the clock's 32-bit wrap. INFO asked every second doesn't keep an abandoned
     upload open past 5 s, nor do BEGIN, a DATA that can't be read (3), a malformed COMMIT or ABORT, or an
     unknown op. A DATA answered 6 or 7 does restart it: the computer is there, sending.
   - A disabled slot (0 and 32 kB of room) gives state 4, reason 7, and 11 to BEGIN and CLEAR, with no
     flash touched. Exactly 64 kB is enabled.
   - data_base moved by a bigger firmware gives state 3, reason 4, and the old data_base finds the kit
     again.
   - Malformed messages (unknown op below 40, wrong lengths, a byte over 7F, no F7, a 6-byte kit
     message) give 3. Ops 40 and above (a reply's shape: status and INFO replies sent back in, or F7
     where the op should be) get no reply at all. Another ID's SysEx isn't taken as a kit message. A
     COMMIT with no F7, or a DATA with a byte over 7F, gives 3 with no upload open, not 5: a message that
     isn't framed is 3 before any op's own rule.
   - A CRC over 32 bits in its 35, in BEGIN and in DATA, gives 3, and so does a last piece whose last
     group's first byte has a bit for a byte the group doesn't have (the piece's CRC is the same
     without it).
   - Every COMMIT that stores a kit, here and in every other step, reads no flash from ended(kit) to
     the end of its handling: the INFO it sends takes its CRC from BEGIN's. An INFO after it reads none
     either.
   - A header sector holding something not a kit's (an old filesystem's bytes) boots as state 0,
     reason 1 (no kit header), and an upload over it goes through. A header starting MCK1 that fails a check is state 3.
   - **A BEGIN waiting to open** (the board deferred): it touches nothing until the loop's pass. An
     ABORT then calls it off (0), and it never opens; a malformed ABORT gives 3 and leaves it waiting.
     A CLEAR calls it off and clears. DATA and COMMIT give 5. A second BEGIN gives 1, even one too big
     (1 comes before 2), or 4 with proto 2, or 3 with a CRC over 32 bits. INFO answers the kit stored.
     The pass then opens the first BEGIN, whose upload goes through. Refused at the pass (the rhythm
     running, or a CRC over 32 bits), nothing is touched.
5. **Sizes**: kits of 0, 4, 256, 4,096, 4,100, 65,536 bytes of sound and the room less 4 and the
   room, uploaded over flash full of old data and over blank flash, at data_base 0x601A0000 (256 kB of
   room), 0x601B0000 (192 kB) and 0x601D0000 (64 kB). Each must give the exact COMMIT and INFO replies and
   the exact erase and program counts. Nothing below data_base or past the sound may be touched, a
   reboot must find the kit, and the INFO after it must read no flash (its CRC worked out at boot). A
   word over the room gives 2. The synthetic kit uploaded at 0x601B0000 must build each voice there.

Last, **kit 13's stand-in**: user_kit::STAND_IN_KIT, what kit 13 plays with no kit stored, must name
studio (drum kit 4) in drum_kits[] of include/drum_patterns.h, which the Makefile hands the harness.

`make cross` (cross_check.py) checks that minictl's kit.py frames synthetic.mckit and synthetic.image
as synthetic.frames, and that its user_kit.py is generator/user_kit.py. It also checks that
minicontrol's kits.js builds synthetic.json into synthetic.image, synthetic.frames and synthetic.mckit,
and gives expected.json's reason for every broken image. Their frames are synthetic.frames to the byte,
so step 1's upload is theirs too. A builder that isn't checked out (~/minictl-userkit,
~/minicontrol-userkit, or MINICTL and MINICONTROL) is skipped.

## What it can't see

include/user_kit.h, the Teensy's side: the USB MIDI loop, replies on the incoming cable, the cycle
counter, the real mute and LED, the conversion of a header to the drum machine's voices, the MIDI sent
before the mute (the queue and the drums' note-offs) and the chord's note_timers ended there, the drums'
note-offs sent, the rest of the queue emptied and the drum machine's queued hits dropped when upload mode
ends, and the notes held from MIDI in released then (main.cpp's midi_in_release_all, as All Notes Off).

## Readings the harness takes where the contract is quiet

- **A refused COMMIT leaves the header sector blank**, so INFO says state 0, reason 1 (no kit header).
  The image's own reason can't be read back over SysEx; it is checked through check_image and the flash
  placement in step 3.
