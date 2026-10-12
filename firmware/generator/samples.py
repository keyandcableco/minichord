#!/usr/bin/env python3
"""Builds the sampled instruments (lib/minichord_samples) from one-note-per-file recordings.

The recordings are the General MIDI instruments rendered one note per file by the
midi-js-soundfonts project (https://github.com/gleitz/midi-js-soundfonts), as the
Minichord Lab keeps them in its samples/ folder: FluidR3_GM (CC BY 3.0) and Musyng Kite
(CC BY-SA 3.0). Run with that folder's path:

    python3 samples.py ~/minichord-lab/samples

It needs ffmpeg and numpy. For each instrument it takes a few notes across its range,
trims the silence before each, mixes it to mono and resamples it, and gives a sustained
one a seamless loop as the Lab does: the end of the loop crossfaded (equal power) with the
audio just before its start, so wrapping round is continuous, and the loop's start
repeated past its end for the player's interpolation. A plucked one plays once, to where
it has died away. Each note is levelled to the same loudness. The result is the Teensy
wavetable player's data (AudioSynthWavetableMulaw::instrument_data), each note sounding across
the notes nearest it, with an envelope that just passes the sound on: the minichord's own
envelope shapes it.

A recording is pitched from the note its file is named for, less any offset in TUNING_CENTS:
some of the renderings are off, and every note played from one carries its error, which a pure
third against it (in 31-EDO, or just chords) makes plain.

Flash is the limit: the firmware has about 560 kB besides the presets' storage and the first four,
so the loops are shorter than the Lab's (whose are about two seconds), the instruments with little
above 8 kHz are kept at 16 kHz, and the sizes are printed at the end.

The later four (vibraphone, bass, sitar, electric piano; voice source 6-9, after the song at 5) are
struck or plucked and looped, and two things differ for them. Each loop is a whole number of the
recording's periods long (aligned), so the loop's own repetition rate falls on the note's harmonics
instead of pulling the pitch onto its own grid (the choir's and strings' offsets below are partly
that grid). And each is levelled by its first 0.4 s, as the pizzicato is, not by its loop: a struck
note's loop sits well below its strike, and levelling by the loop would leave the strike clipped to
the limit and the note quiet.
"""
import math
import os
import subprocess
import sys

import numpy as np

AUDIO_RATE = 44117.64706          # the Teensy audio library's sample rate
# From firmware 78 the instruments are kept as 8-bit G.711 mu-law (lib/wavetable_mulaw decodes them as
# they are read), half the flash of 16-bit PCM; an instrument with fmt='pcm16' stays as it was.
FORMAT = 'mulaw'
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib', 'minichord_samples', 'src')

# name, files (folder, notes), rate, kind, and for a sustained one where its loop starts, how long
# it is and its crossfade (seconds from the note's start); for a plucked one how long it may be
INSTRUMENTS = [
    dict(name='piano', title='piano (FluidR3)', rate=22050, notes=[('piano', n) for n in (42, 54, 66, 78)],
         loop=(0.55, 0.25, 0.12)),
    # Musyng Kite's pizzicato has a broken key zone, 48 to 62: every note rendered from it sounds two
    # sections a semitone apart at about the same strength (a layer mapped to the wrong key), so a pluck
    # there is a minor second. FluidR3_GM's (quartet/pizz-fluid) is clean there and takes its place.
    dict(name='pizzicato', title='pizzicato strings (Musyng Kite, FluidR3 in the middle)', rate=22050,
         notes=[('quartet/pizz', 40), ('quartet/pizz-fluid', 50), ('quartet/pizz-fluid', 60), ('quartet/pizz', 70),
                ('quartet/pizz', 82)], one_shot=0.7),
    dict(name='choir', title='choir aah (Musyng Kite)', rate=16000,
         notes=[('choir/musyng/aah', n) for n in (44, 54, 64, 76)], loop=(0.6, 0.5, 0.2)),
    dict(name='strings', title='string quartet (Musyng Kite): cello, viola, violin', rate=16000,
         notes=[('quartet/cello', 40), ('quartet/cello', 50), ('quartet/viola', 60), ('quartet/violin', 70),
                ('quartet/violin', 82)], loop=(0.5, 0.5, 0.2)),
    # The vibraphone has its motor on, a steady 4.2 Hz tremolo of about 3.6 dB in every recording, so its
    # loop is one turn of the motor (0.238 s) and the tremolo goes round seamlessly; little of it is above
    # 8 kHz. Musyng Kite's has no steady motor, its partials beating unevenly instead.
    dict(name='vibraphone', title='vibraphone (FluidR3)', rate=16000, aligned=True, level_from_start=True,
         notes=[('vibraphone/fluid', n) for n in (50, 58, 66, 74, 82)], loop=(0.4, 0.238, 0.1)),
    # A fingered electric bass, for the chords' first note under the others (first note voice); Musyng
    # Kite's is two layers a semitone apart from 66 up.
    dict(name='bass', title='fingered bass (FluidR3)', rate=16000, aligned=True, level_from_start=True,
         notes=[('bass/fluid', n) for n in (36, 48, 60)], loop=(0.35, 0.3, 0.12)),
    # Musyng Kite's sitar rings on steadily with its bridge's buzz (FluidR3's dies away much sooner), at
    # 22 kHz for the buzz.
    dict(name='sitar', title='sitar (Musyng Kite)', rate=22050, aligned=True, level_from_start=True,
         notes=[('sitar/musyng', n) for n in (44, 56, 68, 80)], loop=(0.4, 0.25, 0.1)),
    # A Rhodes-style electric piano (General MIDI's Electric Piano 1); mellow, so at 16 kHz.
    dict(name='epiano', title='electric piano (FluidR3)', rate=16000, aligned=True, level_from_start=True,
         notes=[('epiano/fluid', n) for n in (42, 54, 66, 78)], loop=(0.55, 0.25, 0.12)),
]
TARGET_RMS = 0.2
# Recordings that sound away from the note they are named for, in cents, measured on the minichord
# (the recording played at its own note, so at the speed it was recorded) and offline, the two agreeing
# within 5 cents. Offsets under 3 cents are left alone. Measured with the chords' output filter flat:
# its default 500 Hz band-pass reweights an ensemble's harmonics and moves a fit by several cents
# (minichord-bench/pizz_tune.py, 2026-10-09).
# - pizzicato, played once: the pitch through the whole note, each 60 ms weighted by its energy, so
#   mostly the pluck and the first 200 ms, which is what a short harp pluck is (a fit to 100-400 ms
#   alone left the top recording, 82, 13 cents flat in every pluck). A section's players beat, so
#   short windows scatter by 30 cents once a note has dropped 5-10 dB; the weighting rides over that
#   (minichord-bench/harp_tune.py measures each note as the harp plays it, 2026-10-10).
# - choir and string quartet, looped: a loop only makes frequencies at whole multiples of one over its
#   length (2 Hz for 0.5 s: 33 cents at the choir 44's note), so a fit to the strongest peak snaps to one
#   of them, and two such fits agree by snapping alike. These are the energy-weighted mean frequency
#   around each harmonic over a held loop instead, on the minichord, and offline over the stretch the
#   loop repeats (for the choir 76, whose crossfade brings in earlier singing, over the loop as built).
#   The string quartet is within 4 cents throughout, so none (the viola's earlier +3 left it 4 flat).
# - piano: its fundamentals are within a few cents; its overtones run sharp, as a piano's do, so it is
#   pitched by the fundamental and the lowest overtones, not by a fit to all of them.
TUNING_CENTS = {
    ('quartet/pizz-fluid', 60): 10.0,  # the harp's C4 to F4
    ('quartet/pizz', 70): -4.0,        # the harp's F#4 to E5
    ('quartet/pizz', 82): -13.0,       # the harp's top
    ('choir/musyng/aah', 54): -5.3,
    ('choir/musyng/aah', 64): -4.2,
    ('choir/musyng/aah', 76): -15.0,
    ('piano', 78): 6.4,
    # - vibraphone: its fundamental's peak, offline, through the strike and the stretch the loop repeats
    #   (they agree within 2 cents). Not the energy-weighted fit: the strike's upper partials, a bar's and
    #   not harmonics, pull that 25 cents sharp in the low recordings, and an aligned loop (whole periods
    #   of the pitch given here) cut to a wrong pitch fights the real one: at 25 cents out, recording 50's
    #   held notes wandered 60 cents on the minichord (fw73_test.py hold). The others within 3 cents.
    # - bass, sitar and electric piano: the energy-weighted pitch over the first 0.8 s, within 3 cents
    #   throughout, so none; on the minichord every note within 5 cents (fw73_test.py harp).
    ('vibraphone/fluid', 66): -4.5,
}


def decode(path, rate):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', '1', '-ar', str(rate), '-'],
                         check=True, stdout=subprocess.PIPE).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def find_file(root, folder, note):
    for ext in ('mp3', 'ogg', 'wav'):
        path = os.path.join(root, folder, '%d.%s' % (note, ext))
        if os.path.exists(path):
            return path
    raise SystemExit('missing %s/%d' % (folder, note))


def build_note(root, inst, folder, note, root_hz):
    rate = inst['rate']
    x = decode(find_file(root, folder, note), rate)
    onset = np.argmax(np.abs(x) > 0.01 * np.max(np.abs(x)))
    x = x[onset:]
    if 'loop' in inst:
        start_s, length_s, fade_s = inst['loop']
        ls, le, xf = int(start_s * rate), int((start_s + length_s) * rate), int(fade_s * rate)
        if inst.get('aligned'):                        # a whole number of the note's periods
            period = rate / root_hz
            le = ls + int(round(round(length_s * rate / period) * period))
        if le + 4 > len(x) or ls - xf < 0:
            raise SystemExit('%s/%d too short for its loop' % (folder, note))
        x = x[:le + 4].copy()
        t = np.arange(xf) / xf
        x[le - xf:le] = x[le - xf:le] * np.cos(t * np.pi / 2) + x[ls - xf:ls] * np.sin(t * np.pi / 2)
        x[le:le + 4] = x[ls:ls + 4]                    # the player reads a sample past the end at the wrap
        level = np.sqrt(np.mean(x[:int(0.4 * rate)] ** 2) if inst.get('level_from_start') else np.mean(x[ls:le] ** 2))
        loop = (ls, le)
    else:
        end = min(len(x), int(inst['one_shot'] * rate))
        envelope = np.abs(x[:end])
        peak = envelope.max()
        loud = np.nonzero(envelope > peak * 10 ** (-45 / 20))[0]
        end = min(end, loud[-1] + 1) if len(loud) else end
        x = x[:end].copy()
        fade = min(len(x), int(0.02 * rate))
        x[-fade:] *= np.linspace(1, 0, fade)
        level = np.sqrt(np.mean(x[:int(0.4 * rate)] ** 2))
        loop = None
    x *= TARGET_RMS / level if level > 0 else 1
    peak = np.max(np.abs(x))
    if peak > 0.98:
        x *= 0.98 / peak
    pcm = np.round(np.clip(x, -1, 1) * 32767).astype(np.int16)
    pcm = np.append(pcm, np.int16(0))                  # and one past the last for a note played once
    return pcm, loop


def mulaw_encode(pcm):
    """16-bit PCM to G.711 mu-law bytes (the standard encoder on the 14-bit value)"""
    x = (pcm.astype(np.int32) >> 2)
    sign = np.where(x < 0, 0x80, 0)
    mag = np.minimum(np.abs(x), 8159) + 0x84
    exp = np.floor(np.log2(mag)).astype(np.int32) - 7
    mant = (mag >> (exp + 3)) & 0x0F
    return (~(sign | (exp << 4) | mant) & 0xFF).astype(np.uint8)


def c_array(name, pcm, fmt):
    data = mulaw_encode(pcm) if fmt == 'mulaw' else pcm
    lines = []
    for i in range(0, len(data), 24):
        lines.append('  ' + ','.join(str(int(v)) for v in data[i:i + 24]) + ',')
    ctype = 'uint8_t' if fmt == 'mulaw' else 'int16_t'
    return 'PROGMEM const %s %s[%d] __attribute__((aligned(4))) = {\n%s\n};\n' % (ctype, name, len(data), '\n'.join(lines))


def sample_struct(array, pcm, loop, root_hz, rate, fmt):
    length = len(pcm)
    index_bits = max(1, math.ceil(math.log2(length)))
    shift = 32 - index_bits
    per_hertz = (1 << shift) * (rate / AUDIO_RATE) / root_hz
    release = int(0.02 * AUDIO_RATE / 8)               # 20 ms, for a voice silenced once its envelope is done
    if loop:
        ls, le = loop
        looped, loop_end, loop_length = 'true', le << shift, (le - ls) << shift
    else:
        looped, loop_end, loop_length = 'false', 0, 0
    fields = [('nullptr', array) if fmt == 'mulaw' else (array, 'nullptr'), looped, index_bits, '%.9gf' % per_hertz, (length - 1) << shift, loop_end, loop_length, 65535,
              0, 1, 0, 1, release, 0,                    # delay, attack, hold, decay, release, sustain: pass it on
              0, 0, '0.0f', '0.0f', 0, 0, '0.0f', '0.0f', 0, 0]   # no vibrato or tremolo of its own
    fields = list(fields[0]) + fields[1:]            # sample (PCM) or mulaw, the other null
    return '  {' + ', '.join(str(f) for f in fields) + '},'


def main():
    root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/minichord-lab/samples')
    os.makedirs(OUT_DIR, exist_ok=True)
    cpp = ['// generated by generator/samples.py from the midi-js-soundfonts renderings - do not edit',
           '// FluidR3_GM: CC BY 3.0; Musyng Kite: CC BY-SA 3.0. See LICENSE in this folder.',
           '// 8-bit mu-law (lib/wavetable_mulaw decodes it as it plays), unless an instrument says pcm16',
           '#include "minichord_samples.h"', '']
    header = ['// generated by generator/samples.py - do not edit', '#ifndef MINICHORD_SAMPLES_H',
              '#define MINICHORD_SAMPLES_H', '#include <Audio.h>', '#include "wavetable_mulaw.h"', '',
              '// The sampled instruments, in voice source order (harp voice and chord voice 1-4, and after the song at 5, 6-%d)'
              % (len(INSTRUMENTS) + 1)]
    total = 0
    report = []
    for inst in INSTRUMENTS:
        structs, ranges, size = [], [], 0
        notes = [n for _, n in inst['notes']]
        fmt = inst.get('fmt', FORMAT)
        for k, (folder, note) in enumerate(inst['notes']):
            root_hz = 440 * 2 ** ((note - 69 + TUNING_CENTS.get((folder, note), 0) / 100) / 12)
            pcm, loop = build_note(root, inst, folder, note, root_hz)
            array = 'sample_%s_%d' % (inst['name'], note)
            cpp.append(c_array(array, pcm, fmt))
            structs.append(sample_struct(array, pcm, loop, root_hz, inst['rate'], fmt))
            ranges.append(127 if k == len(notes) - 1 else (note + notes[k + 1]) // 2)
            size += len(pcm) * (1 if fmt == 'mulaw' else 2)
        cpp.append('static const AudioSynthWavetableMulaw::sample_data %s_samples[%d] = {\n%s\n};' % (
            inst['name'], len(structs), '\n'.join(structs)))
        cpp.append('static const uint8_t %s_ranges[] = {%s};' % (inst['name'], ', '.join(map(str, ranges))))
        cpp.append('const AudioSynthWavetableMulaw::instrument_data %s_instrument = {%d, %s_ranges, %s_samples};\n' % (
            inst['name'], len(structs), inst['name'], inst['name']))
        header.append('extern const AudioSynthWavetableMulaw::instrument_data %s_instrument;   // %s' % (inst['name'], inst['title']))
        report.append('%-10s %2d notes at %5d Hz, %s: %6.1f kB' % (inst['name'], len(notes), inst['rate'], fmt, size / 1024))
        total += size
    header += ['', '#endif', '']
    with open(os.path.join(OUT_DIR, 'minichord_samples.cpp'), 'w') as f:
        f.write('\n'.join(cpp))
    with open(os.path.join(OUT_DIR, 'minichord_samples.h'), 'w') as f:
        f.write('\n'.join(header))
    print('\n'.join(report))
    print('total %.1f kB of sample data' % (total / 1024))


if __name__ == '__main__':
    main()
