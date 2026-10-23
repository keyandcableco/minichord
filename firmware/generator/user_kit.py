#!/usr/bin/env python3
"""The user drum kit's Python reference (drum kit 13, "your own"): the image, its checks, the SysEx
messages that carry it and the .mckit file, as generator/user_kit.md says them. That file is the
contract and this one its working copy: it writes the test vectors in test/kits/ that the firmware
(include/user_kit.h), minicontrol (kits.js) and minictl (kit.py) are tested against, and when the two
disagree the contract wins and this is fixed. Python 3's standard library only, so it runs anywhere:

    python3 user_kit.py vectors [DIR]   writes test/kits/ (or DIR): the synthetic kit, its image, .mckit
                                        and upload, and the broken images with the reasons they must fail
    python3 user_kit.py check FILE      the reason an image, or a .mckit's image, fails (0: it is valid)
    python3 user_kit.py show FILE       its header, voice by voice
    python3 user_kit.py frames FILE     every message of its upload, BEGIN to COMMIT, a line each in hex
    python3 user_kit.py selftest        packing, groups, images, frames, replies and the broken cases, and
                                        whether test/kits/ is what this file writes; non-zero on a failure

A kit to build is a dict: base_kit (1 to 12, setting 337's numbering), name (up to 20 printable ASCII
characters) and voices, 13 of them in drum_machine.h's order, each None (empty: the base kit's own voice
plays) or a dict of samples (a list of 16-bit signed numbers, or their bytes little-endian) with rate,
level, pan, choke, flags and pitch as the voice entry has them (missing ones are 22,050 Hz, level 100
and 0). The hits are packed in voice order from the start of the sound, each on a 4-byte boundary with
zeros in the gap. A voice whose samples are the same as an earlier voice's points at that voice's hit
instead of taking a second copy: that is how two voices share one, and every builder does the same, so
that each makes the same image from the same kit.

The synthetic kit (test/kits/synthetic.json) is made here from sums of decaying sines and noise, in
integers only: a magic-circle oscillator for each sine, an exponential decay, a one-pole filter and a
32-bit linear congruential generator for the noise. No floating point goes into a sample, so it comes
out the same to the bit on any machine and any Python.
"""
import argparse
import base64
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
KITS_DIR = os.path.normpath(os.path.join(HERE, '..', 'test', 'kits'))

# ---- the image (user_kit.md section 1) -----------------------------------------------------------
MAGIC = b'MCK1'
LAYOUT = 1
FORMAT_PCM16 = 0             # 1 is mu-law, reserved: version 1 refuses it
VOICES = 13
HEADER_BYTES = 256
FILE_CAPACITY = 262144       # a file has no capacity of its own: it is held to the most any minichord has
NAME_BYTES = 20
VOICE_AT = 44                # the 13 voice entries, 16 bytes each
HEADER_CRC_AT = 252
RATE_MIN = 4000
FLAG_PITCH_FOLLOW = 1        # reserved: must be 0 in version 1
FLAG_PRESS_BENDS = 2         # pressing the pad on plate 13 bends the hit; 0 = a press chokes it

HEADER_FORMAT = '<4sBBBBIII20sI'   # magic to reserved: the first 44 bytes
VOICE_FORMAT = '<IIHBbBBH'
VOICE_KEYS = ('offset', 'length', 'rate', 'level', 'pan', 'choke', 'flags', 'pitch')
# where each field is, and how it is written, for reading one or setting one (the broken cases)
HEADER_FIELDS = {'magic': (0, '4s'), 'layout': (4, 'B'), 'format': (5, 'B'), 'voices': (6, 'B'), 'base_kit': (7, 'B'),
                 'data_base': (8, '<I'), 'data_bytes': (12, '<I'), 'data_crc': (16, '<I'), 'name': (20, '20s'),
                 'reserved': (40, '<I'), 'header_crc': (252, '<I')}
VOICE_FIELDS = {'offset': (0, '<I'), 'length': (4, '<I'), 'rate': (8, '<H'), 'level': (10, 'B'), 'pan': (11, 'b'),
                'choke': (12, 'B'), 'flags': (13, 'B'), 'pitch': (14, '<H')}

# drum_machine.h's voice order, with the GM note drum MIDI out sends for each
VOICE_NAMES = ['kick', 'snare', 'rim', 'clap', 'hat', 'open hat', 'cymbal', 'low tom', 'high tom', 'low conga',
               'high conga', 'bell', 'shaker']
GM_NOTE = [36, 38, 37, 39, 42, 46, 49, 45, 50, 64, 63, 56, 70]

# check_image's reasons, the same numbers INFO's reason gives (section 3). REASON_NO_HEADER is INFO's only:
# the header sector doesn't start MCK1, blank or holding other bytes, so the minichord has no kit (state 0)
REASON_OK, REASON_NO_HEADER, REASON_HEADER, REASON_HEADER_CRC, REASON_DATA_BASE, REASON_RULE, REASON_DATA_CRC, \
    REASON_ROOM = range(8)
REASONS = ['valid', 'no kit header (blank, or other bytes)', 'bad magic, layout, format, voices or base_kit',
           'header CRC', 'data_base moved', 'a size, name, reserved byte or voice entry out of its rule', 'data CRC',
           'too little room (disabled)']
STATES = ['empty', 'valid', 'receiving', 'damaged', 'disabled']
STATUSES = ['ok', 'busy', 'too big', 'bad message or length', 'unsupported proto', 'no upload begun',
            'out of order', 'bad piece CRC', 'flash read-back mismatch', 'image CRC mismatch at COMMIT',
            'the image fails check_image at COMMIT', 'slot disabled']


def crc32(data):
    """The reflected 0xEDB88320 CRC with init and final value inverted, as the firmware's crc32_update(0, p, n)."""
    return zlib.crc32(bytes(data)) & 0xFFFFFFFF


def pcm_bytes(samples):
    """A hit's samples as 16-bit little-endian bytes: from a list of numbers, or bytes as they are."""
    if isinstance(samples, (bytes, bytearray)):
        if len(samples) % 2:
            raise ValueError('16-bit samples are an even number of bytes')
        return bytes(samples)
    try:
        return struct.pack('<%dh' % len(samples), *samples)
    except struct.error:
        raise ValueError('a sample is not a 16-bit signed number')


def samples_of(pcm):
    return list(struct.unpack('<%dh' % (len(pcm) // 2), pcm))


def name_field(name):
    """The header's 20 name bytes: printable ASCII, then NULs."""
    if isinstance(name, str):
        try:
            name = name.encode('ascii')
        except UnicodeEncodeError:
            raise ValueError('a kit name is printable ASCII')
    name = bytes(name)
    if len(name) > NAME_BYTES or any(not 0x20 <= c <= 0x7E for c in name):
        raise ValueError('a kit name is up to 20 printable ASCII characters')
    return name.ljust(NAME_BYTES, b'\0')


def name_ok(field):
    """A NUL may only be followed by NULs, and everything before the first is printable (empty is allowed)."""
    text = bytes(field).rstrip(b'\0')
    return all(0x20 <= c <= 0x7E for c in text)   # a NUL left inside fails here, as it is below 0x20


def build_image(kit, capacity=FILE_CAPACITY):
    """The image (header + sound, data_base 0) of a kit as the module's docstring describes it."""
    voices = list(kit['voices'])
    if len(voices) != VOICES:
        raise ValueError('a kit has 13 voices (None for an empty one)')
    base_kit = kit['base_kit']
    if not 1 <= base_kit <= 12:
        raise ValueError('base_kit is 1 to 12')
    sound = bytearray()
    hits = {}                     # a hit's bytes: where it is, so a voice with the same samples shares it
    entries = []
    for v, voice in enumerate(voices):
        if voice is None:
            entries.append(bytes(16))
            continue
        pcm = pcm_bytes(voice['samples'])
        length = len(pcm) // 2
        if length < 2:
            raise ValueError(f'voice {v} ({VOICE_NAMES[v]}): a hit is at least 2 samples')
        if pcm not in hits:
            hits[pcm] = len(sound)
            sound += pcm
            sound += bytes(-len(sound) % 4)       # the next hit starts on a 4-byte boundary
        rate, level, pan = voice.get('rate', 22050), voice.get('level', 100), voice.get('pan', 0)
        choke, flags, pitch = voice.get('choke', 0), voice.get('flags', 0), voice.get('pitch', 0)
        if not RATE_MIN <= rate <= 65535 or not 0 <= level <= 255 or not -100 <= pan <= 100 or not 0 <= choke <= 7 \
                or flags & ~FLAG_PRESS_BENDS or flags < 0 or not 0 <= pitch <= 65535:
            raise ValueError(f'voice {v} ({VOICE_NAMES[v]}): rate 4,000 to 65,535, level 0 to 255, pan -100 to 100, '
                             'choke 0 to 7, flags 0 or 2, pitch 0 to 65,535')
        entries.append(struct.pack(VOICE_FORMAT, hits[pcm], length, rate, level, pan, choke, flags, pitch))
    if len(sound) > capacity:
        raise ValueError(f'the sound is {len(sound)} bytes, more than the {capacity} there is room for')
    header = struct.pack(HEADER_FORMAT, MAGIC, LAYOUT, FORMAT_PCM16, VOICES, base_kit, 0, len(sound), crc32(sound),
                         name_field(kit.get('name', '')), 0) + b''.join(entries)
    header += struct.pack('<I', crc32(header))
    image = header + bytes(sound)
    assert check_image(image, capacity=capacity) == REASON_OK
    return image


def read_header(image):
    """The header's fields as a dict, the voice entries as a list of dicts (the image at least 256 bytes)."""
    magic, layout, fmt, voices, base_kit, data_base, data_bytes, data_crc, name, reserved = \
        struct.unpack_from(HEADER_FORMAT, image, 0)
    entries = [dict(zip(VOICE_KEYS, struct.unpack_from(VOICE_FORMAT, image, VOICE_AT + 16 * v))) for v in range(VOICES)]
    return dict(magic=magic, layout=layout, format=fmt, voices=voices, base_kit=base_kit, data_base=data_base,
                data_bytes=data_bytes, data_crc=data_crc, name=name, reserved=reserved, entries=entries,
                header_crc=struct.unpack_from('<I', image, HEADER_CRC_AT)[0])


def entry_ok(e, data_bytes):
    """A voice entry as its rules say: empty (length 0, and so every field), or a hit inside the sound."""
    if e['length'] == 0:
        return not any(e[k] for k in VOICE_KEYS)
    # Python's numbers don't wrap: a C reader works offset + 2 * length out in 64 bits, or the broken
    # cases voice_wraps_32_bits and voice_length_wraps_32_bits pass it
    return (e['offset'] % 4 == 0 and e['length'] >= 2 and e['offset'] + 2 * e['length'] <= data_bytes
            and e['rate'] >= RATE_MIN and -100 <= e['pan'] <= 100 and e['choke'] <= 7
            and not e['flags'] & ~FLAG_PRESS_BENDS)


def check_image(image, data_base=0, capacity=FILE_CAPACITY):
    """The reason an image fails, the first of the contract's checks in its order; 0 when it passes them all.
    data_base is what the header's must be: 0 in a file or on the wire, the minichord's own in its flash."""
    image = bytes(image)
    size = len(image)
    if size < HEADER_BYTES or size % 4:
        return REASON_RULE
    h = read_header(image)
    if h['magic'] != MAGIC or h['layout'] != LAYOUT or h['format'] != FORMAT_PCM16 or h['voices'] != VOICES \
            or not 1 <= h['base_kit'] <= 12:
        return REASON_HEADER
    if crc32(image[:HEADER_CRC_AT]) != h['header_crc']:
        return REASON_HEADER_CRC
    if h['data_base'] != data_base:
        return REASON_DATA_BASE
    data_bytes = h['data_bytes']
    if data_bytes != size - HEADER_BYTES or data_bytes % 4 or data_bytes > capacity or h['reserved'] \
            or not name_ok(h['name']) or not all(entry_ok(e, data_bytes) for e in h['entries']):
        return REASON_RULE
    if crc32(image[HEADER_BYTES:]) != h['data_crc']:
        return REASON_DATA_CRC
    return REASON_OK


def set_field(image, field, value, voice=None, fix_crc=True):
    """The image with one header field (or voice's field) changed, and its header_crc made right again
    unless fix_crc is False: how the broken cases are made, one fault each."""
    image = bytearray(image)
    at, fmt = HEADER_FIELDS[field] if voice is None else VOICE_FIELDS[field]
    struct.pack_into(fmt, image, at + (0 if voice is None else VOICE_AT + 16 * voice), value)
    if fix_crc:
        struct.pack_into('<I', image, HEADER_CRC_AT, crc32(image[:HEADER_CRC_AT]))
    return bytes(image)


def image_info(image):
    """What an INFO reply says of a stored image: its size, its CRC as it was sent (data_base 0, so a copy
    from flash has its data_base taken out and header_crc made again), base_kit, filled voices and name."""
    if read_header(image)['data_base']:
        image = set_field(image, 'data_base', 0)
    h = read_header(image)
    return dict(size=len(image), crc=crc32(image), base_kit=h['base_kit'], name=h['name'],
                filled=sum(1 << v for v, e in enumerate(h['entries']) if e['length']))


# ---- SysEx (section 3) ---------------------------------------------------------------------------
SYSEX = bytes([0xF0, 0x7D, 0x6B])     # the fork's own non-commercial ID, then "k"
PROTO = 1
PIECE = 224                           # raw bytes in a DATA piece: 256 packed
BEGIN, DATA, COMMIT, ABORT, INFO, CLEAR = 1, 2, 3, 4, 5, 6
OPS = {BEGIN: 'BEGIN', DATA: 'DATA', COMMIT: 'COMMIT', ABORT: 'ABORT', INFO: 'INFO', CLEAR: 'CLEAR'}
REPLY = 0x40                          # a reply's op is 0x40 | the op it answers
INFO_REPLY_BYTES = 49
STATUS_REPLY_BYTES = 9


def encode_n(value, groups):
    """A number as 7-bit groups, least significant first."""
    if not 0 <= value < 1 << (7 * groups):
        raise ValueError(f'{value} does not fit {groups} groups of 7 bits')
    return bytes((value >> (7 * i)) & 0x7F for i in range(groups))


def decode_n(data):
    data = bytes(data)
    if any(b > 0x7F for b in data):
        raise ValueError('a 7-bit group has bit 7 set')
    return sum(b << (7 * i) for i, b in enumerate(data))


def n2(value):
    return encode_n(value, 2)


def n3(value):
    return encode_n(value, 3)


def n5(value):
    return encode_n(value, 5)


def pack87(raw):
    """8-to-7 packing: each group of up to 7 raw bytes becomes a byte of their bit 7s (bit j for byte j),
    then the bytes with bit 7 cleared."""
    raw = bytes(raw)
    out = bytearray()
    for at in range(0, len(raw), 7):
        group = raw[at:at + 7]
        out.append(sum((b >> 7) << j for j, b in enumerate(group)))
        out += bytes(b & 0x7F for b in group)
    return bytes(out)


def packed_length(n):
    """How many bytes n raw bytes pack to: 224 to 256."""
    return n // 7 * 8 + (n % 7 + 1 if n % 7 else 0)


def unpack87(packed):
    """pack87 undone; a ValueError for bytes pack87 never makes (bit 7 set, a lone high-bits byte at the end,
    a high bit for a byte the last group doesn't have)."""
    packed = bytes(packed)
    if len(packed) % 8 == 1:
        raise ValueError('a packed group ends after its high-bits byte')
    if any(b > 0x7F for b in packed):
        raise ValueError('a packed byte has bit 7 set')
    out = bytearray()
    for at in range(0, len(packed), 8):
        high, body = packed[at], packed[at + 1:at + 8]
        if high >> len(body):
            raise ValueError('a high bit for a byte the group does not have')
        out += bytes(b | ((high >> j) & 1) << 7 for j, b in enumerate(body))
    return bytes(out)


def message(op, body):
    return SYSEX + bytes([op]) + bytes(body) + b'\xF7'


def begin_message(size, crc):
    return message(BEGIN, bytes([PROTO]) + n3(size) + n5(crc))


def data_message(image, offset):
    """DATA for the piece of the image at offset: image[offset : offset + min(224, size - offset)]."""
    piece = bytes(image[offset:offset + min(PIECE, len(image) - offset)])
    return message(DATA, n3(offset) + n5(crc32(piece)) + pack87(piece))


def commit_message():
    return message(COMMIT, b'\0\0')


def abort_message():
    return message(ABORT, b'\0\0')


def info_message():
    return message(INFO, bytes([PROTO, 0]))


def clear_message():
    return message(CLEAR, b'\x00\x43\x4C')     # "CL", so a stray byte can't clear a kit


def frames(image):
    """Every message of an image's upload, in order: BEGIN, its DATA pieces from offset 0, COMMIT. It frames
    what it is given; a sender runs check_image first."""
    image = bytes(image)
    return [begin_message(len(image), crc32(image))] + \
        [data_message(image, at) for at in range(0, len(image), PIECE)] + [commit_message()]


def parse_message(msg):
    """A message from the computer as a dict, op and its fields (DATA's piece unpacked), for a fake minichord
    and the selftest. A ValueError for one that isn't a well-formed kit message."""
    msg = bytes(msg)
    if len(msg) < 7 or msg[:3] != SYSEX or msg[-1] != 0xF7 or any(b > 0x7F for b in msg[1:-1]):
        raise ValueError('not a kit message')
    op, body = msg[3], msg[4:-1]
    if op == BEGIN and len(body) == 9:
        return dict(op=op, proto=body[0], size=decode_n(body[1:4]), crc=decode_n(body[4:9]))
    if op == DATA and len(body) > 9:
        return dict(op=op, offset=decode_n(body[0:3]), crc=decode_n(body[3:8]), piece=unpack87(body[8:]))
    if op in (COMMIT, ABORT) and body == b'\0\0':
        return dict(op=op)
    if op == INFO and len(body) == 2 and body[1] == 0:
        return dict(op=op, proto=body[0])
    if op == CLEAR and body == b'\x00\x43\x4C':
        return dict(op=op)
    raise ValueError(f'a malformed or unknown kit message (op {op:02X}, {len(msg)} bytes)')


def status_reply(op, status, next_offset=0):
    """The minichord's status reply to a message with this op: 9 bytes."""
    return message(REPLY | op, bytes([status]) + n3(next_offset))


def info_reply(state, reason, capacity, data_base, size=0, crc=0, base_kit=0, filled=0, name=b'', proto=PROTO,
               largest=PIECE):
    """The minichord's INFO reply, 49 bytes, for a fake minichord. With a kit stored, image_info(image) gives
    size, crc, base_kit, filled and name: info_reply(1, 0, capacity, data_base, **image_info(image))."""
    name = name.encode('ascii') if isinstance(name, str) else bytes(name)
    name = bytes(c & 0x7F for c in name[:NAME_BYTES]).ljust(NAME_BYTES, b'\0')
    return message(REPLY | INFO, bytes([proto, state, reason]) + n3(capacity) + n5(data_base) + n3(size) + n5(crc)
                   + n2(largest) + bytes([base_kit]) + n2(filled) + name)


def parse_reply(msg):
    """A reply from the minichord as a dict: kind 'status' (op, status, next) or 'info' (proto, state, reason,
    capacity, data_base, size, crc, largest, base_kit, filled, name). None for a message that isn't a kit
    reply at all (another SysEx, or a computer's kit message), a ValueError for a kit reply that is
    malformed. A status reply and an INFO reply are told apart by their length, as a malformed INFO gets a
    status reply with op 0x45 too."""
    msg = bytes(msg)
    if len(msg) < 5 or msg[:3] != SYSEX or not msg[3] & REPLY:
        return None
    if msg[-1] != 0xF7 or any(b > 0x7F for b in msg[1:-1]):
        raise ValueError('a malformed kit reply')
    if len(msg) == STATUS_REPLY_BYTES:
        return dict(kind='status', op=msg[3] & 0x3F, status=msg[4], next=decode_n(msg[5:8]))
    if len(msg) == INFO_REPLY_BYTES and msg[3] == REPLY | INFO:
        b = msg[4:-1]
        return dict(kind='info', proto=b[0], state=b[1], reason=b[2], capacity=decode_n(b[3:6]),
                    data_base=decode_n(b[6:11]), size=decode_n(b[11:14]), crc=decode_n(b[14:19]),
                    largest=decode_n(b[19:21]), base_kit=b[21], filled=decode_n(b[22:24]),
                    name=b[24:44].split(b'\0')[0].decode('ascii'))
    raise ValueError(f'a kit reply of {len(msg)} bytes (op {msg[3]:02X})')


# ---- the .mckit file (section 6) -----------------------------------------------------------------
MCKIT_MAGIC = b'MCKF'
MCKIT_VERSION = 1


def write_mckit(meta, image):
    """"MCKF", version 1, three 0s, the JSON's length, the JSON (keys sorted, no spaces), the image verbatim."""
    text = json.dumps(meta, sort_keys=True, separators=(',', ':')).encode('utf-8')
    return MCKIT_MAGIC + bytes([MCKIT_VERSION, 0, 0, 0]) + struct.pack('<I', len(text)) + text + bytes(image)


def read_mckit(data):
    """(meta, image) from a .mckit's bytes; a ValueError if it isn't one. The image is not checked here: a
    reader runs check_image on it before using or sending it."""
    data = bytes(data)
    if len(data) < 12 or data[:4] != MCKIT_MAGIC:
        raise ValueError('not a .mckit file')
    if data[4] != MCKIT_VERSION or data[5:8] != b'\0\0\0':
        raise ValueError(f'a .mckit of version {data[4]}: this reads version 1')
    n = struct.unpack_from('<I', data, 8)[0]
    if 12 + n > len(data):
        raise ValueError('the .mckit ends inside its JSON')
    meta = json.loads(data[12:12 + n].decode('utf-8'))
    if not isinstance(meta, dict):
        raise ValueError("the .mckit's JSON is not an object")
    return meta, data[12 + n:]


def read_image_file(path):
    """An image from a file: the image itself, or the one inside a .mckit."""
    data = _read(path)
    return read_mckit(data)[1] if data[:4] == MCKIT_MAGIC else data


# ---- a kit as JSON (synthetic.json) --------------------------------------------------------------
def kit_json(kit, meta, image):
    """A kit written out: base_kit, name, the .mckit's meta, and per voice its entry's fields as the image
    has them plus its samples as base64 of 16-bit little-endian PCM (null for an empty voice)."""
    entries = read_header(image)['entries']
    voices = [None if voice is None else dict(entries[v], samples=base64.b64encode(pcm_bytes(voice['samples'])).decode())
              for v, voice in enumerate(kit['voices'])]
    return json.dumps(dict(base_kit=kit['base_kit'], name=kit['name'], meta=meta, voices=voices),
                      indent=1, sort_keys=True) + '\n'


def read_kit_json(text):
    """(kit, meta) from kit_json's text: what build_image and write_mckit take. A voice's offset and length
    in it are what the image has, for a reader to check its own packing against."""
    doc = json.loads(text)
    voices = []
    for voice in doc['voices']:
        if voice is not None:
            voice = dict(voice, samples=samples_of(base64.b64decode(voice['samples'])))
        voices.append(voice)
    return dict(base_kit=doc['base_kit'], name=doc['name'], voices=voices), doc.get('meta')


# ---- the synthetic kit (section 7) ---------------------------------------------------------------
class Lcg:
    """Numerical Recipes' 32-bit linear congruential generator: the same numbers on every machine."""
    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def __call__(self):
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.state >> 16) - 32768            # -32,768 to 32,767


LN_1000 = 452710          # 65536 times ln(1000): a decay that falls 60 dB in n samples takes this // n a sample
TWO_PI = 411775           # 65536 times 2 pi: a sine of f Hz turns TWO_PI * f // rate (in 65536ths) a sample


def synth(n, rate, seed, tones=(), noise=0, noise_decay=1, noise_low=0, strikes=(0,)):
    """n samples of a hit, in integers only. Each tone is (from Hz, to Hz, sweep, decay, level): a sine
    from a magic-circle oscillator whose pitch falls from the first to the second over the sweep (samples
    to fall 60 dB of the way), dying away 60 dB in decay samples, at level 256ths. The noise is the
    generator's, less its one-pole low-pass (noise_low, in 65536ths: 0 keeps it all) so only what is above
    it is left, struck at each of strikes (sample numbers) and dying 60 dB in noise_decay samples, at noise
    256ths. The sum is clipped to 16 bits."""
    oscs = []
    for f0, f1, sweep, decay, level in tones:
        oscs.append([1 << 30, 0, TWO_PI * f0 // rate, TWO_PI * f1 // rate, 1 << 30, LN_1000 // sweep if sweep else 0,
                     1 << 30, LN_1000 // decay, level])
    rnd = Lcg(seed)
    low = 0
    env = 0
    fall = LN_1000 // noise_decay
    out = []
    for i in range(n):
        s = 0
        for o in oscs:
            x, y, e0, e1, sw, sw_fall, amp, amp_fall, level = o
            e = e1 + ((e0 - e1) * (sw >> 14) >> 16)
            x -= (e * y) >> 16
            y += (e * x) >> 16
            s += ((y >> 15) * (amp >> 15) >> 15) * level >> 8
            o[0], o[1] = x, y
            o[4] = sw - ((sw * sw_fall) >> 16)
            o[6] = amp - ((amp * amp_fall) >> 16)
        if noise:
            if i in strikes:
                env = 1 << 30
            r = rnd()
            low += ((r - low) * noise_low) >> 16
            s += ((r - low) * (env >> 15) >> 15) * noise >> 8
            env -= (env * fall) >> 16
        out.append(max(-32768, min(32767, s)))
    return out


# Voice by voice: its entry's rate, level, pan, choke and flags, and its sound for synth(). Voices 2 (rim)
# and 12 (shaker) are empty; the high tom (8) plays the low tom's hit (7) half as fast again, which is how a
# pitch change is baked into a rate. The rates take in both ends, 4,000 and 65,535, the pans -100 and 100,
# a level 255 and a choke group 7, and nine of the ten hits are an odd number of samples, so they have gaps.
SYNTHETIC = [
    dict(rate=22050, level=120, pan=0, choke=0, flags=0, n=5001, seed=1, tones=[(160, 50, 700, 5001, 230)],
         noise=90, noise_decay=160),
    dict(rate=22050, level=100, pan=-10, choke=0, flags=0, n=3001, seed=2, tones=[(240, 185, 300, 1500, 110)],
         noise=170, noise_decay=3001, noise_low=20000),
    None,
    dict(rate=11025, level=90, pan=-20, choke=0, flags=0, n=1999, seed=3, noise=200, noise_decay=700,
         noise_low=24000, strikes=(0, 100, 200, 300)),
    dict(rate=44100, level=70, pan=35, choke=1, flags=0, n=2205, seed=4, tones=[(7040, 7040, 0, 1200, 40)],
         noise=150, noise_decay=2205, noise_low=50000),
    dict(rate=32000, level=70, pan=35, choke=1, flags=0, n=4801, seed=5,
         tones=[(5300, 5300, 0, 4801, 30), (7900, 7900, 0, 4000, 20)], noise=140, noise_decay=4801, noise_low=45000),
    dict(rate=65535, level=60, pan=-100, choke=0, flags=0, n=6000, seed=6,
         tones=[(3100, 3100, 0, 6000, 40), (4730, 4730, 0, 5000, 40), (6870, 6870, 0, 4000, 30)],
         noise=150, noise_decay=6000, noise_low=40000),
    dict(rate=22050, level=255, pan=-60, choke=0, flags=FLAG_PRESS_BENDS, n=3999, seed=7,
         tones=[(130, 98, 900, 3999, 220)], noise=40, noise_decay=200),
    dict(rate=33075, level=200, pan=60, choke=0, flags=0, same_as=7),
    dict(rate=16000, level=110, pan=-45, choke=2, flags=FLAG_PRESS_BENDS, n=2501, seed=9,
         tones=[(225, 196, 250, 2501, 220)], noise=50, noise_decay=120),
    dict(rate=16000, level=110, pan=45, choke=2, flags=FLAG_PRESS_BENDS, n=2001, seed=10,
         tones=[(330, 294, 200, 2001, 220)], noise=50, noise_decay=100),
    dict(rate=4000, level=80, pan=100, choke=7, flags=0, n=1601, seed=11,
         tones=[(540, 540, 0, 1601, 140), (800, 800, 0, 1200, 100)]),
    None,
]
SYNTHETIC_NAME = 'synthetic test kit'
SYNTHETIC_MADE = '2026-10-22'


def synthetic_kit():
    """(kit, meta): the synthetic kit for build_image, and the meta its .mckit carries."""
    voices = []
    sources = []
    for v, d in enumerate(SYNTHETIC):
        if d is None:
            voices.append(None)
            sources.append(None)
            continue
        if 'same_as' in d:
            samples, source = voices[d['same_as']]['samples'], sources[d['same_as']]
        else:
            samples = synth(d['n'], d['rate'], d['seed'], d.get('tones', ()), d.get('noise', 0),
                            d.get('noise_decay', 1), d.get('noise_low', 0), d.get('strikes', (0,)))
            source = f'synthetic {VOICE_NAMES[v]}'
        voices.append(dict(samples=samples, rate=d['rate'], level=d['level'], pan=d['pan'], choke=d['choke'],
                           flags=d['flags'], pitch=0))
        sources.append(source)
    meta = dict(name=SYNTHETIC_NAME, author='minichord test', licence='CC0', sources=sources, made=SYNTHETIC_MADE)
    return dict(base_kit=1, name=SYNTHETIC_NAME, voices=voices), meta


# ---- the broken images (section 7) ---------------------------------------------------------------
def small_kit():
    """A small valid kit the broken images are made from, a fault each: base_kit 5, voices 0 and 1 filled
    (voice 1's 33 samples leaving a 2-byte gap after it), 7 and 8 sharing a hit, the rest empty. Its sound is
    188 bytes: voice 0 at 0, voice 1 at 80, voices 7 and 8 at 148."""
    rnd = Lcg(99)
    hit = lambda n: [rnd() // 2 for _ in range(n)]
    a, b, c = hit(40), hit(33), hit(20)
    voices = [None] * VOICES
    voices[0] = dict(samples=a, rate=22050, level=100, pan=0, choke=0, flags=0, pitch=0)
    voices[1] = dict(samples=b, rate=11025, level=90, pan=-50, choke=3, flags=FLAG_PRESS_BENDS, pitch=0)
    voices[7] = dict(samples=c, rate=22050, level=140, pan=-30, choke=0, flags=FLAG_PRESS_BENDS, pitch=0)
    voices[8] = dict(samples=c, rate=33075, level=120, pan=30, choke=0, flags=0, pitch=0)
    return dict(base_kit=5, name='small kit', voices=voices)


def _sound_byte_flipped(image, at):
    image = bytearray(image)
    image[HEADER_BYTES + at] ^= 0x01
    return bytes(image)


def _size(img):
    return len(img) - HEADER_BYTES


# file name, the reason check_image must give, what is wrong, and how it is made from the small image
BROKEN = [
    ('size_0', REASON_RULE, 'an empty file', lambda img: b''),
    ('size_255', REASON_RULE, 'size < 256: the header cut short', lambda img: img[:255]),
    ('size_not_4', REASON_RULE, 'size not a multiple of 4 (2 bytes more)', lambda img: img + b'\0\0'),
    ('order_size_before_magic', REASON_RULE, 'bad magic and size not a multiple of 4: the size is checked first',
     lambda img: set_field(img, 'magic', b'MCK2') + b'\0\0'),
    ('bad_magic', REASON_HEADER, 'magic MCK2', lambda img: set_field(img, 'magic', b'MCK2')),
    ('order_magic_before_header_crc', REASON_HEADER, 'magic mck1 and header_crc not made again: magic comes first',
     lambda img: set_field(img, 'magic', b'mck1', fix_crc=False)),
    ('layout_2', REASON_HEADER, 'layout 2', lambda img: set_field(img, 'layout', 2)),
    ('format_1', REASON_HEADER, 'format 1 (mu-law, reserved)', lambda img: set_field(img, 'format', 1)),
    ('voices_12', REASON_HEADER, 'voices 12', lambda img: set_field(img, 'voices', 12)),
    ('base_kit_0', REASON_HEADER, 'base_kit 0', lambda img: set_field(img, 'base_kit', 0)),
    ('base_kit_13', REASON_HEADER, 'base_kit 13', lambda img: set_field(img, 'base_kit', 13)),
    ('header_crc', REASON_HEADER_CRC, "voice 0's level changed, header_crc not made again",
     lambda img: set_field(img, 'level', 101, voice=0, fix_crc=False)),
    ('order_header_crc_before_data_base', REASON_HEADER_CRC, 'data_base nonzero and header_crc not made again',
     lambda img: set_field(img, 'data_base', 0x601A0000, fix_crc=False)),
    ('data_base_nonzero', REASON_DATA_BASE, 'data_base 0x601A0000 (as in flash) in a file',
     lambda img: set_field(img, 'data_base', 0x601A0000)),
    ('order_data_base_before_rules', REASON_DATA_BASE, 'data_base nonzero and reserved nonzero: data_base first',
     lambda img: set_field(set_field(img, 'data_base', 0x601A0000), 'reserved', 1)),
    ('data_bytes_less', REASON_RULE, 'data_bytes 4 less than size - 256',
     lambda img: set_field(img, 'data_bytes', _size(img) - 4)),
    ('data_bytes_more', REASON_RULE, 'data_bytes 4 more than size - 256',
     lambda img: set_field(img, 'data_bytes', _size(img) + 4)),
    ('data_bytes_not_4', REASON_RULE, 'data_bytes not a multiple of 4 (size - 256 - 2)',
     lambda img: set_field(img, 'data_bytes', _size(img) - 2)),
    ('name_0x80', REASON_RULE, 'a name byte 0x80', lambda img: set_field(img, 'name', b'small kit\x80')),
    ('name_0x7f', REASON_RULE, 'a name byte 0x7F (not printable)', lambda img: set_field(img, 'name', b'small\x7fkit')),
    ('name_0x1f', REASON_RULE, 'a name byte 0x1F (not printable)', lambda img: set_field(img, 'name', b'\x1fsmall kit')),
    ('name_char_after_nul', REASON_RULE, 'a character after a NUL in the name',
     lambda img: set_field(img, 'name', b'small\x00kit')),
    ('name_char_at_end', REASON_RULE, 'the name NUL-padded but its 20th byte a character',
     lambda img: set_field(img, 'name', b'small kit'.ljust(19, b'\0') + b'x')),
    ('reserved_nonzero', REASON_RULE, 'reserved 0x01000000 (its last byte)',
     lambda img: set_field(img, 'reserved', 0x01000000)),
    ('voice_offset_not_4', REASON_RULE, "voice 0's offset 2", lambda img: set_field(img, 'offset', 2, voice=0)),
    ('voice_past_end', REASON_RULE, "voice 7's hit one sample past the end of the sound (the last hit)",
     lambda img: set_field(img, 'length', (_size(img) - 148) // 2 + 1, voice=7)),
    ('voice_offset_at_end', REASON_RULE, "voice 1's offset at the end of the sound",
     lambda img: set_field(img, 'offset', _size(img), voice=1)),
    ('voice_wraps_32_bits', REASON_RULE, "voice 0 at 0x80000000, 0x40000002 samples: its end wraps to 4 in 32 bits",
     lambda img: set_field(set_field(img, 'offset', 0x80000000, voice=0), 'length', 0x40000002, voice=0)),
    ('voice_length_wraps_32_bits', REASON_RULE, "voice 0's length 0x80000002: 2 * length wraps to 4 in 32 bits",
     lambda img: set_field(img, 'length', 0x80000002, voice=0)),
    ('empty_voice_level', REASON_RULE, 'empty voice 2 with level 100', lambda img: set_field(img, 'level', 100, voice=2)),
    ('empty_voice_offset', REASON_RULE, 'empty voice 2 with offset 4', lambda img: set_field(img, 'offset', 4, voice=2)),
    ('empty_voice_pitch', REASON_RULE, 'empty voice 12 with pitch 1', lambda img: set_field(img, 'pitch', 1, voice=12)),
    ('length_1', REASON_RULE, "voice 0's length 1", lambda img: set_field(img, 'length', 1, voice=0)),
    ('rate_3999', REASON_RULE, "voice 0's rate 3,999", lambda img: set_field(img, 'rate', 3999, voice=0)),
    ('rate_0', REASON_RULE, "voice 1's rate 0", lambda img: set_field(img, 'rate', 0, voice=1)),
    ('pan_101', REASON_RULE, "voice 0's pan 101", lambda img: set_field(img, 'pan', 101, voice=0)),
    ('pan_minus_101', REASON_RULE, "voice 0's pan -101", lambda img: set_field(img, 'pan', -101, voice=0)),
    ('choke_8', REASON_RULE, "voice 0's choke group 8", lambda img: set_field(img, 'choke', 8, voice=0)),
    ('flags_bit0', REASON_RULE, "voice 0's flags 1 (pitch follow, reserved)",
     lambda img: set_field(img, 'flags', 1, voice=0)),
    ('flags_bit2', REASON_RULE, "voice 1's flags 6 (bit 2 with press bends)",
     lambda img: set_field(img, 'flags', 6, voice=1)),
    ('flags_bit7', REASON_RULE, "voice 0's flags 0x80", lambda img: set_field(img, 'flags', 0x80, voice=0)),
    ('data_crc', REASON_DATA_CRC, "a bit of voice 0's first sample flipped", lambda img: _sound_byte_flipped(img, 0)),
    ('data_crc_gap', REASON_DATA_CRC, "a bit flipped in the 2-byte gap after voice 1's hit (the CRC covers gaps)",
     lambda img: _sound_byte_flipped(img, 146)),
    ('order_rules_before_data_crc', REASON_RULE, 'reserved nonzero and a sound bit flipped: the rules first',
     lambda img: _sound_byte_flipped(set_field(img, 'reserved', 1), 0)),
]


def broken_images():
    """{file name: (image, reason, what)} for every broken case."""
    small = build_image(small_kit())
    return {name + '.image': (make(small), reason, what) for name, reason, what, make in BROKEN}


# ---- the vectors ---------------------------------------------------------------------------------
def vector_files():
    """{path under test/kits: bytes}: everything 'vectors' writes."""
    kit, meta = synthetic_kit()
    image = build_image(kit)
    files = {
        'synthetic.json': kit_json(kit, meta, image).encode(),
        'synthetic.image': image,
        'synthetic.mckit': write_mckit(meta, image),
        'synthetic.frames': ''.join(m.hex() + '\n' for m in frames(image)).encode(),
        'small.image': build_image(small_kit()),
    }
    broken = broken_images()
    for name, (img, _, _) in broken.items():
        files['broken/' + name] = img
    files['broken/expected.json'] = (json.dumps({name: reason for name, (_, reason, _) in broken.items()},
                                                indent=1, sort_keys=True) + '\n').encode()
    return files


def write_vectors(out_dir):
    files = vector_files()
    os.makedirs(os.path.join(out_dir, 'broken'), exist_ok=True)
    for name in os.listdir(os.path.join(out_dir, 'broken')):      # a case renamed or dropped leaves no file behind
        if name.endswith('.image') and 'broken/' + name not in files:
            os.remove(os.path.join(out_dir, 'broken', name))
    for name, data in files.items():
        with open(os.path.join(out_dir, name), 'wb') as f:
            f.write(data)
    image = files['synthetic.image']
    h = read_header(image)
    print(f"synthetic kit: {len(image)} bytes ({h['data_bytes']} of sound), "
          f"{sum(1 for e in h['entries'] if e['length'])} voices filled, {len(frames(image))} messages")
    print(f"{len(BROKEN)} broken images, small.image {len(files['small.image'])} bytes, written to {out_dir}")


def _read(path):
    with open(path, 'rb') as f:
        return f.read()


# ---- the self test -------------------------------------------------------------------------------
def selftest():
    failures = []

    def expect(ok, what):
        if not ok:
            failures.append(what)

    def raises(f, what):
        try:
            f()
        except ValueError:
            return
        failures.append(what + ': no ValueError')

    expect(crc32(b'123456789') == 0xCBF43926, 'crc32 check value')

    # 7-bit groups
    for groups in (2, 3, 5):
        top = (1 << (7 * groups)) - 1
        for value in (0, 1, 127, 128, 0x3FFF, top // 3, top):
            if value <= top:
                b = encode_n(value, groups)
                expect(len(b) == groups and all(c < 0x80 for c in b) and decode_n(b) == value, f'n{groups} {value}')
        raises(lambda: encode_n(top + 1, groups), f'n{groups} too big')
        raises(lambda: encode_n(-1, groups), f'n{groups} negative')
    expect(n5(0xFFFFFFFF) == bytes([0x7F, 0x7F, 0x7F, 0x7F, 0x0F]), 'n5 of a CRC')
    expect(n3(448) == bytes([0x40, 0x03, 0x00]), 'n3 448')
    raises(lambda: decode_n(b'\x80'), 'decode_n bit 7')

    # 8-to-7 packing
    rnd = Lcg(7)
    for n in list(range(0, 30)) + [223, 224, 225]:
        raw = bytes(rnd() & 0xFF for _ in range(n))
        p = pack87(raw)
        expect(len(p) == packed_length(n) and all(c < 0x80 for c in p) and unpack87(p) == raw, f'pack87 {n} bytes')
    expect(pack87(b'\xff' * 224) == b'\x7f' * 256, 'pack87 224 0xFF to 256 0x7F')
    expect(packed_length(224) == 256 and packed_length(4) == 5 and packed_length(7) == 8, 'packed_length')
    expect(pack87(b'\x80\x01') == b'\x01\x00\x01', 'pack87 high bit')
    raises(lambda: unpack87(b'\x00' * 9), 'unpack87 lone high-bits byte')
    raises(lambda: unpack87(b'\x80\x00'), 'unpack87 bit 7')
    raises(lambda: unpack87(b'\x02\x00'), 'unpack87 high bit for a missing byte')

    # the synthetic kit's image
    kit, meta = synthetic_kit()
    image = build_image(kit)
    h = read_header(image)
    e = h['entries']
    expect(check_image(image) == REASON_OK, 'synthetic image valid')
    expect(40000 <= h['data_bytes'] <= 80000, f"synthetic sound {h['data_bytes']} bytes, wanted 40 to 80 kB")
    expect(len(image) == HEADER_BYTES + h['data_bytes'] and len(image) % 4 == 0, 'image size')
    expect(e[2] == dict.fromkeys(VOICE_KEYS, 0) and e[12] == dict.fromkeys(VOICE_KEYS, 0), 'voices 2 and 12 empty')
    expect(e[7]['offset'] == e[8]['offset'] and e[7]['length'] == e[8]['length'], 'voices 7 and 8 share a hit')
    expect(e[7]['rate'] != e[8]['rate'], 'voices 7 and 8 at their own rates')
    expect(all(x['offset'] % 4 == 0 for x in e), 'hits on 4-byte boundaries')
    filled = [v for v in range(VOICES) if e[v]['length']]
    expect(filled == [0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11], 'filled voices')
    expect([e[v]['offset'] for v in filled] == sorted(e[v]['offset'] for v in filled), 'hits packed in voice order')
    expect(any(e[v]['length'] % 2 for v in filled), 'an odd length (a gap)')
    for v in filled:
        expect(image[HEADER_BYTES + e[v]['offset']:][:2 * e[v]['length']] == pcm_bytes(kit['voices'][v]['samples']),
               f'voice {v} samples in place')
        for k in ('rate', 'level', 'pan', 'choke', 'flags', 'pitch'):
            expect(e[v][k] == kit['voices'][v][k], f'voice {v} {k}')
    for k, values in (('rate', {4000, 65535}), ('pan', {-100, 100, 0}), ('choke', {0, 7}), ('flags', {0, 2}),
                      ('level', {255})):
        expect(values <= {e[v][k] for v in filled}, f'synthetic {k} takes in {sorted(values)}')
    expect(build_image(kit) == image and synthetic_kit()[0] == kit, 'synthetic kit deterministic')
    expect(h['name'] == name_field(SYNTHETIC_NAME) and h['base_kit'] == 1, 'name and base_kit')
    rk, rmeta = read_kit_json(kit_json(kit, meta, image))
    expect(build_image(rk) == image and rmeta == meta, 'kit JSON round trip')

    # data_base and capacity
    flash = set_field(image, 'data_base', 0x601A0000)
    expect(check_image(flash, data_base=0x601A0000) == REASON_OK, 'flash copy valid at its data_base')
    expect(check_image(flash) == REASON_DATA_BASE and check_image(image, data_base=0x601B0000) == REASON_DATA_BASE,
           'data_base moved')
    expect(check_image(image, capacity=h['data_bytes']) == REASON_OK, 'sound exactly the capacity')
    expect(check_image(image, capacity=h['data_bytes'] - 4) == REASON_RULE, 'sound over the capacity')
    expect(image_info(flash) == image_info(image) and image_info(image)['crc'] == crc32(image), 'image_info crc')

    # other kits, and ones build_image refuses
    empty = build_image(dict(base_kit=12, name='', voices=[None] * VOICES))
    expect(len(empty) == HEADER_BYTES and check_image(empty) == REASON_OK, 'an all-empty kit is 256 bytes and valid')
    one = [None] * VOICES
    one[12] = dict(samples=[1, -1])
    expect(check_image(build_image(dict(base_kit=3, name='x' * 20, voices=one))) == REASON_OK, 'shortest hit, longest name')
    expect(read_header(build_image(dict(base_kit=3, voices=one)))['entries'][12]['rate'] == 22050, 'rate default')

    def refused(change, what, **voice):
        k = dict(base_kit=1, name='ok', voices=[None] * VOICES)
        k['voices'][0] = dict(dict(samples=[0, 0, 0, 0]), **voice)
        k.update(change)
        raises(lambda: build_image(k), 'build_image refuses ' + what)
    refused(dict(base_kit=0), 'base_kit 0')
    refused(dict(base_kit=13), 'base_kit 13')
    refused(dict(name='x' * 21), 'a 21-character name')
    refused(dict(name='café'), 'a non-ASCII name')
    refused(dict(name='a\tb'), 'a tab in the name')
    refused(dict(voices=[None] * 12), '12 voices')
    refused({}, 'rate 3999', rate=3999)
    refused({}, 'flags 1', flags=1)
    refused({}, 'pan 101', pan=101)
    refused({}, 'choke 8', choke=8)
    refused({}, 'a 1-sample hit', samples=[5])
    refused({}, 'a sample over 16 bits', samples=[0, 40000])
    big = [None] * VOICES
    big[0] = dict(samples=bytes(FILE_CAPACITY + 4))
    raises(lambda: build_image(dict(base_kit=1, voices=big)), 'build_image refuses a sound over the capacity')

    # the broken cases, and that check_image puts the checks in order
    for name, (img, reason, what) in broken_images().items():
        got = check_image(img)
        expect(got == reason, f'broken {name} ({what}): reason {got}, wanted {reason}')
    expect(check_image(build_image(small_kit())) == REASON_OK, 'small kit valid')

    # frames
    fs = frames(image)
    pieces = (len(image) + PIECE - 1) // PIECE
    expect(len(fs) == pieces + 2, 'frame count')
    expect(all(m[:3] == SYSEX and m[-1] == 0xF7 and all(c < 0x80 for c in m[1:-1]) and len(m) >= 7 for m in fs),
           'every frame F0 7D 6B ... F7, 7-bit, at least 7 bytes')
    expect(len(fs[0]) == 14 and len(fs[-1]) == 7 and max(len(m) for m in fs[1:-1]) == 269, 'frame sizes')
    b = parse_message(fs[0])
    expect(b == dict(op=BEGIN, proto=1, size=len(image), crc=crc32(image)), 'BEGIN fields')
    rebuilt = bytearray()
    for m in fs[1:-1]:
        d = parse_message(m)
        expect(d['op'] == DATA and d['offset'] == len(rebuilt) and d['crc'] == crc32(d['piece'])
               and len(d['piece']) == min(PIECE, len(image) - d['offset']), f"DATA at {d['offset']}")
        rebuilt += d['piece']
    expect(bytes(rebuilt) == image, 'frames rebuild the image')
    expect(parse_message(fs[-1]) == dict(op=COMMIT), 'COMMIT')
    for m, n, op in ((abort_message(), 7, ABORT), (info_message(), 7, INFO), (clear_message(), 8, CLEAR)):
        expect(len(m) == n and parse_message(m)['op'] == op, f'{OPS[op]} message')
    expect(clear_message() == bytes.fromhex('f07d6b0600434cf7'), 'CLEAR bytes')
    raises(lambda: parse_message(bytes.fromhex('f07d6b060043f7')), 'parse_message: CLEAR without "CL"')
    raises(lambda: parse_message(bytes.fromhex('f07d6b07000000f7')), 'parse_message: unknown op')

    # replies
    for op, status, nxt in ((BEGIN, 0, 0), (DATA, 6, 448), (COMMIT, 10, 0), (0x07, 3, 0), (INFO, 3, 0)):
        r = status_reply(op, status, nxt)
        expect(len(r) == 9 and parse_reply(r) == dict(kind='status', op=op, status=status, next=nxt),
               f'status reply {op} {status} {nxt}')
    info = image_info(image)
    expect(info['filled'] == 0b0111111111011 and info['size'] == len(image), 'image_info filled and size')
    r = info_reply(1, 0, 196608, 0x601B0000, **info)
    expect(len(r) == 49, 'INFO reply 49 bytes')
    expect(parse_reply(r) == dict(kind='info', proto=1, state=1, reason=0, capacity=196608, data_base=0x601B0000,
                                  size=len(image), crc=crc32(image), largest=224, base_kit=1, filled=0b0111111111011,
                                  name=SYNTHETIC_NAME), 'INFO reply round trip')
    expect(parse_reply(info_reply(0, 1, 262144, 0x601A0000)) == dict(
        kind='info', proto=1, state=0, reason=1, capacity=262144, data_base=0x601A0000, size=0, crc=0, largest=224,
        base_kit=0, filled=0, name=''), 'INFO reply with no kit')
    expect(info_reply(3, 5, 0, 0, name=b'\xc1bc')[28:31] == b'Abc', "INFO's name with bit 7 clear")
    expect(parse_reply(bytes.fromhex('f07d6d0102f7')) is None and parse_reply(commit_message()) is None,
           'parse_reply passes over what is not a kit reply')
    raises(lambda: parse_reply(status_reply(DATA, 0)[:-1] + b'\x00'), 'parse_reply: no F7')
    raises(lambda: parse_reply(r[:-2] + b'\xF7'), 'parse_reply: a short INFO reply')

    # the .mckit
    mk = write_mckit(meta, image)
    expect(read_mckit(mk) == (meta, image), '.mckit round trip')
    n = struct.unpack_from('<I', mk, 8)[0]
    expect(mk[12:12 + n] == json.dumps(meta, sort_keys=True, separators=(',', ':')).encode(), '.mckit JSON')
    expect(read_mckit(mk[:12 + n]) == (meta, b''), '.mckit with no image reads (and fails its check)')
    raises(lambda: read_mckit(b'MCKX' + mk[4:]), '.mckit bad magic')
    raises(lambda: read_mckit(mk[:4] + b'\x02' + mk[5:]), '.mckit version 2')
    raises(lambda: read_mckit(mk[:12 + n - 1]), '.mckit cut inside its JSON')

    # the vectors on disk are what this file writes
    if os.path.isdir(KITS_DIR):
        files = vector_files()
        for name, data in files.items():
            path = os.path.join(KITS_DIR, name)
            expect(os.path.exists(path) and _read(path) == data,
                   f'test/kits/{name} is not what user_kit.py writes: run "python3 user_kit.py vectors"')
        stale = [n for n in os.listdir(os.path.join(KITS_DIR, 'broken')) if 'broken/' + n not in files]
        expect(not stale, f'test/kits/broken has files user_kit.py does not write: {stale}')
        readme = _read(os.path.join(KITS_DIR, 'README.md')).decode()
        expect(all(f'| {n} | {r} |' in readme for n, (_, r, _) in broken_images().items()),
               "test/kits/README.md's table of broken images is not BROKEN's")
        for name, reason in json.loads(_read(os.path.join(KITS_DIR, 'broken', 'expected.json'))).items():
            expect(check_image(read_image_file(os.path.join(KITS_DIR, 'broken', name))) == reason, f'{name} on disk')
        expect(check_image(read_image_file(os.path.join(KITS_DIR, 'synthetic.mckit'))) == REASON_OK, 'synthetic.mckit')
        kit2, meta2 = read_kit_json(_read(os.path.join(KITS_DIR, 'synthetic.json')))
        expect(build_image(kit2) == files['synthetic.image'] and write_mckit(meta2, build_image(kit2)) ==
               files['synthetic.mckit'], 'synthetic.json builds synthetic.image and synthetic.mckit')
    else:
        print(f'no {KITS_DIR} yet: run "python3 user_kit.py vectors"')

    for f in failures:
        print('FAIL', f)
    print(f'selftest: {len(failures)} failures' if failures else 'selftest: all passed')
    return not failures


# ---- the command line ----------------------------------------------------------------------------
def show(image):
    if len(image) < HEADER_BYTES:
        print(f'{len(image)} bytes: shorter than a header')
        return
    h = read_header(image)
    print(f"magic {h['magic']!r}, layout {h['layout']}, format {h['format']}, voices {h['voices']}, "
          f"base_kit {h['base_kit']}, name {h['name'].rstrip(bytes(1))!r}")
    print(f"data_base 0x{h['data_base']:08X}, data_bytes {h['data_bytes']} (size {len(image)}), "
          f"data_crc {h['data_crc']:08X}, reserved {h['reserved']}, header_crc {h['header_crc']:08X}")
    for v, e in enumerate(h['entries']):
        if e['length'] == 0 and not any(e.values()):
            print(f'  {v:2} {VOICE_NAMES[v]:10}  empty')
        else:
            print(f"  {v:2} {VOICE_NAMES[v]:10}  at {e['offset']:6}, {e['length']:6} samples at {e['rate']:5} Hz, "
                  f"level {e['level']:3}, pan {e['pan']:4}, choke {e['choke']}, flags {e['flags']}, pitch {e['pitch']}")


def main():
    p = argparse.ArgumentParser(description="The user drum kit's reference: images, checks, frames, vectors.")
    sub = p.add_subparsers(dest='command', required=True)
    v = sub.add_parser('vectors', help='write the test vectors')
    v.add_argument('dir', nargs='?', default=KITS_DIR)
    c = sub.add_parser('check', help="print the reason an image or a .mckit's image fails (0 valid)")
    c.add_argument('file')
    c.add_argument('--data-base', type=lambda s: int(s, 0), default=0, help="the data_base it must have (flash's)")
    c.add_argument('--capacity', type=lambda s: int(s, 0), default=FILE_CAPACITY)
    s = sub.add_parser('show', help='print the header, voice by voice')
    s.add_argument('file')
    f = sub.add_parser('frames', help='print every message of the upload in hex, one per line')
    f.add_argument('file')
    sub.add_parser('selftest', help='test this file and the vectors on disk')
    a = p.parse_args()
    if a.command == 'vectors':
        write_vectors(a.dir)
        return 0
    if a.command == 'selftest':
        return 0 if selftest() else 1
    try:
        image = read_image_file(a.file)
    except ValueError as err:
        print(f'{a.file}: {err}', file=sys.stderr)
        return 2
    if a.command == 'check':
        reason = check_image(image, a.data_base, a.capacity)
        print(reason, REASONS[reason])
        return 0 if reason == REASON_OK else 1
    if a.command == 'show':
        show(image)
        return 0
    reason = check_image(image)
    if reason:
        print(f'{a.file}: fails its check, reason {reason} ({REASONS[reason]}): not framed', file=sys.stderr)
        return 1
    for m in frames(image):
        print(m.hex())
    return 0


if __name__ == '__main__':
    sys.exit(main())
