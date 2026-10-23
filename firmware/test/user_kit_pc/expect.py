#!/usr/bin/env python3
# What the PC harness checks the kit build against, read from test/kits/synthetic.json and written as plain
# text so the harness needs no JSON reader of its own: one line per voice, in drum_machine.h's order,
#   v offset length rate level pan choke flags pitch samples_crc     (a filled voice)
#   v empty                                                          (an empty one)
# then a line "kit base_kit name", the name with its spaces as '_'. samples_crc is zlib's CRC-32 of the
# voice's samples as the JSON has them (16-bit little-endian PCM), which the harness compares with the
# CRC of 2 * length bytes of its fake flash at data_base + offset. Standard library only.
import base64
import json
import sys
import zlib

kit = json.load(open(sys.argv[1]))
for v, voice in enumerate(kit["voices"]):
    if voice is None:
        print(v, "empty")
        continue
    samples = base64.b64decode(voice["samples"])
    assert len(samples) == 2 * voice["length"], f"voice {v}: {len(samples)} bytes of samples for length {voice['length']}"
    print(v, voice["offset"], voice["length"], voice["rate"], voice["level"], voice["pan"], voice["choke"], voice["flags"],
          voice["pitch"], zlib.crc32(samples))
print("kit", kit["base_kit"], kit["name"].replace(" ", "_"))
