#!/usr/bin/env python3
"""Builds include/drum_patterns.h: the drum machine's kits and grooves (see DRUMS in main.cpp and
include/drum_machine.h for the voices). Run it after changing a kit or a groove:
    python3 drums.py

A kit is the thirteen voices' settings over a plain default for each, so a kit only says what it
changes. A voice is synthesized: a pitched body that falls from tone_bend times its pitch to it, and a
burst of noise and square-wave metal through a filter, each with its own decay (in ms, to -60 dB). Or it
plays a recorded stroke, sample='na' (one of drum_samples.py's STROKES, built into drum_samples.h by it);
follow=1 retunes a stroke recorded at a known pitch to the key, as a tabla's dayan is.

For glitch's blips and clicks (the cliqhop kit) a voice can also take: wave=1 a square body, wave=2 an FM
body (fm_ratio, fm_index in radians, dying in fm_ms); source=1 a one-sample click for the burst (high
passed a click, band passed with a high filter_q a ping), source=2 dust (clicks at random, dust a
second); ring=1 the body rung by a square at metal_hz; scatter=n each hit's pitch times one of the first
n of 1, 2, 1.5, 3, 4, 0.5, 6, 8; stutter=n the voice struck again n times, every stutter_blocks audio
blocks (2.9 ms), its pitch times stutter_ratio and velocity times stutter_fade each time; decimate=n its
samples held n times; follow=2 the chord's root, in the octave nearest tone_hz (a ping's filter moves too).

A groove is written as a grid, a line a voice, a character a step, bars separated by |:
    . rest   g a whisper   o a ghost   x a hit   X an accent   r a roll: two hits in the step
    2-6 a ratchet: that many hits in the step (as many as divide it: a sixteenth's 24ths are 6)
steps is how many to the beat: 4 sixteenths, 3 triplets (shuffle, swing, 12/8), 2 eighths. The voices:
    K kick  S snare  R rim (claves, ka)  C clap  H hat (triangle)  O open hat  Y cymbal (crash, ride)
    T low tom  U high tom  L low conga (bayan)  M high conga (bongo, dayan)  B bell  Z shaker
A tabla groove can be written in bols instead, the strokes' own names (BOLS below), a step each, spaced
and with bars separated by |:
    dha  a stroke   DHA accented   (dha) a ghost   -  a rest   ti/ra  two in the step, ti/ra/ki  three
A groove's fill is a bar: its first half the groove's own, its second the fill, written out or one of
the families below. Grooves are re-played in our own sounds; the famous ones are named for the style
they recall ("amen-style").
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drum_samples import STROKES

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'include', 'drum_patterns.h')

VOICES = ['KICK', 'SNARE', 'RIM', 'CLAP', 'HAT', 'OPEN', 'CYMBAL', 'TOM_LO', 'TOM_HI', 'CONGA_LO', 'CONGA_HI',
          'BELL', 'SHAKER']
LETTER = dict(zip('KSRCHOYTULMBZ', range(13)))
# General MIDI's drum map, channel 10: kick, side stick, snare, clap, closed and open hat, crash, low and
# high tom, low and high conga, cowbell, maracas
GM_NOTE = [36, 38, 37, 39, 42, 46, 49, 45, 50, 64, 63, 56, 70]
VELOCITY = {'g': 30, 'o': 56, 'x': 96, 'X': 124}
SAMPLE = {stroke[0]: i for i, stroke in enumerate(STROKES)}

# The tabla's bols, on the 'tabla and doumbek' kit's recorded strokes (see it below): each the voices
# struck together, as letters. Dha is Na with Ge, Dhin Tin with Ge: both hands at once.
BOLS = {
    'dha': 'ML', 'dhaa': 'YL', 'dhin': 'OL', 'dhi': 'OL',
    'na': 'M', 'ta': 'M', 'naa': 'Y', 'taa': 'Y', 'tin': 'O', 'tun': 'T', 'tu': 'T',
    'te': 'H', 'ti': 'H', 'ne': 'U', 'ra': 'U', 're': 'Z',
    'ge': 'L', 'ghe': 'L', 'ga': 'L', 'gee': 'B',   # gee: Ge pushed up by the wrist
    'ka': 'C', 'ke': 'C', 'ki': 'C', 'kat': 'C',
}

# ---- kits ----------------------------------------------------------------------------------------
def V(**kw):
    v = dict(tone_hz=0, tone_bend=1.0, bend_ms=10, tone_ms=100, tone_level=1.0, noise_level=0, metal_level=0,
             metal_hz=540, metal_ratio=1.4815, metal_count=2, filter=1, filter_hz=2000, filter_q=0.7, noise_ms=50,
             retrigs=0, retrig_ms=10, level=0.8, pan=0.0, choke=0, follow=0, sample=-1,
             wave=0, fm_ratio=1.0, fm_index=0, fm_ms=50, source=0, dust=0, ring=0, scatter=0,
             stutter=0, stutter_blocks=4, stutter_ratio=1.0, stutter_fade=1.0, decimate=1)
    v.update(kw)
    return v

DEFAULT = {
    'KICK': V(tone_hz=55, tone_bend=3.0, bend_ms=12, tone_ms=350, noise_level=0.15, filter=0, filter_hz=3000, noise_ms=8, level=1.0),
    'SNARE': V(tone_hz=185, tone_bend=1.4, bend_ms=15, tone_ms=120, tone_level=0.6, noise_level=0.9, filter=2, filter_hz=1500,
               noise_ms=180, level=0.8, pan=0.05),
    'RIM': V(tone_hz=1700, tone_ms=30, tone_level=0.7, noise_level=0.3, filter=1, filter_hz=3000, filter_q=3, noise_ms=20, level=0.6, pan=0.1),
    'CLAP': V(noise_level=1.0, filter=1, filter_hz=1200, filter_q=1.5, noise_ms=220, retrigs=3, retrig_ms=9, level=0.8, pan=-0.05),
    'HAT': V(noise_level=0.4, metal_level=0.7, metal_hz=410, metal_count=4, filter=2, filter_hz=8000, filter_q=0.9, noise_ms=45,
             level=0.5, pan=0.3, choke=1),
    'OPEN': V(noise_level=0.4, metal_level=0.7, metal_hz=410, metal_count=4, filter=2, filter_hz=8000, filter_q=0.9, noise_ms=320,
              level=0.5, pan=0.3, choke=1),
    'CYMBAL': V(noise_level=0.6, metal_level=0.6, metal_hz=380, metal_count=4, filter=2, filter_hz=5500, noise_ms=1400, level=0.5, pan=-0.3),
    'TOM_LO': V(tone_hz=98, tone_bend=1.6, bend_ms=40, tone_ms=380, noise_level=0.1, filter=0, filter_hz=2000, noise_ms=15, level=0.8, pan=-0.25),
    'TOM_HI': V(tone_hz=150, tone_bend=1.6, bend_ms=35, tone_ms=320, noise_level=0.1, filter=0, filter_hz=2500, noise_ms=15, level=0.8, pan=0.2),
    'CONGA_LO': V(tone_hz=196, tone_bend=1.15, bend_ms=15, tone_ms=260, noise_level=0.12, filter=1, filter_hz=2000, noise_ms=12,
                  level=0.7, pan=-0.35),
    'CONGA_HI': V(tone_hz=294, tone_bend=1.12, bend_ms=12, tone_ms=200, noise_level=0.12, filter=1, filter_hz=2500, noise_ms=12,
                  level=0.7, pan=0.35),
    'BELL': V(metal_level=1.0, metal_hz=540, metal_ratio=1.4815, metal_count=2, filter=1, filter_hz=900, filter_q=1.5, noise_ms=180,
              level=0.45, pan=0.2),
    'SHAKER': V(noise_level=1.0, filter=2, filter_hz=6500, noise_ms=55, level=0.35, pan=-0.2),
}

def kit(name, bits=16, hold=1, drive=1.0, **voices):
    v = {k: dict(DEFAULT[k]) for k in VOICES}
    for k, change in voices.items():
        v[k].update(change)
        if isinstance(v[k]['sample'], str):   # a recorded stroke: nothing synthesized under it
            v[k].update(sample=SAMPLE[v[k]['sample']], tone_hz=0, noise_level=0, metal_level=0, retrigs=0)
    return dict(name=name, bits=bits, hold=hold, drive=drive, voices=v)

KITS = [
    kit('808', KICK=dict(tone_hz=50, tone_bend=2.2, bend_ms=18, tone_ms=900, noise_level=0.05),
        SNARE=dict(tone_hz=238, tone_level=0.5, filter_hz=2000, noise_ms=160),
        RIM=dict(tone_hz=1000, noise_level=0),
        HAT=dict(metal_hz=205.3, noise_level=0.2, filter_hz=7500, noise_ms=40),
        OPEN=dict(metal_hz=205.3, noise_level=0.2, filter_hz=7500, noise_ms=380),
        CYMBAL=dict(metal_hz=205.3, noise_ms=1800),
        TOM_LO=dict(tone_bend=1.3, noise_level=0), TOM_HI=dict(tone_bend=1.3, noise_level=0),
        CONGA_LO=dict(tone_hz=220, noise_level=0), CONGA_HI=dict(tone_hz=330, noise_level=0)),
    kit('909', drive=1.3, KICK=dict(tone_hz=55, tone_bend=4.0, bend_ms=8, tone_ms=420, noise_level=0.3, filter_hz=6000, noise_ms=6),
        SNARE=dict(tone_hz=200, noise_level=0.9, filter_hz=1200, noise_ms=220),
        HAT=dict(noise_level=0.9, metal_level=0.2, filter_hz=9000, noise_ms=50),
        OPEN=dict(noise_level=0.9, metal_level=0.2, filter_hz=9000, noise_ms=400),
        CYMBAL=dict(noise_level=0.8, metal_level=0.3, noise_ms=1600)),
    kit('CR-78', KICK=dict(tone_hz=70, tone_bend=1.5, tone_ms=200, noise_level=0),
        SNARE=dict(tone_hz=330, tone_ms=60, noise_level=0.5, filter=1, filter_hz=3000, filter_q=1, noise_ms=100),
        RIM=dict(tone_hz=2000, noise_level=0),
        HAT=dict(metal_level=0.9, metal_hz=600, noise_level=0.1, filter_hz=7000, noise_ms=30, level=0.4),
        OPEN=dict(metal_level=0.9, metal_hz=600, noise_level=0.1, filter_hz=7000, noise_ms=250, level=0.4),
        BELL=dict(metal_hz=700, metal_ratio=1.5), SHAKER=dict(noise_ms=40),
        CONGA_LO=dict(tone_hz=250, noise_level=0), CONGA_HI=dict(tone_hz=370, noise_level=0)),
    kit('studio', KICK=dict(tone_hz=60, tone_bend=1.8, tone_ms=300, noise_level=0.25, filter_hz=4000, noise_ms=15),
        SNARE=dict(tone_hz=190, tone_ms=140, noise_level=1.0, filter=1, filter_hz=2500, filter_q=0.6, noise_ms=230),
        HAT=dict(noise_level=0.7, metal_level=0.3, filter_hz=7000, noise_ms=60),
        OPEN=dict(noise_level=0.7, metal_level=0.3, filter_hz=7000, noise_ms=500),
        CYMBAL=dict(noise_level=0.7, metal_level=0.4, filter_hz=4000, noise_ms=1800),
        TOM_LO=dict(tone_hz=90, tone_ms=450), TOM_HI=dict(tone_hz=140, tone_ms=400)),
    kit('brushes', KICK=dict(tone_hz=60, tone_bend=1.4, tone_ms=250, noise_level=0.05, level=0.7),
        SNARE=dict(tone_level=0.15, noise_level=0.8, filter=0, filter_hz=4000, noise_ms=350, level=0.5),
        RIM=dict(tone_hz=1500, level=0.45),
        HAT=dict(noise_level=0.5, metal_level=0.2, filter_hz=6000, noise_ms=40, level=0.35),
        CYMBAL=dict(noise_level=0.3, metal_level=0.7, filter=1, filter_hz=5000, filter_q=1.5, noise_ms=900, level=0.45),
        TOM_LO=dict(tone_ms=300, level=0.6), TOM_HI=dict(tone_ms=260, level=0.6), SHAKER=dict(filter_hz=5000, noise_ms=90)),
    kit('boom bap', bits=10, hold=2, drive=1.4, KICK=dict(tone_hz=52, tone_bend=2.5, tone_ms=400, noise_level=0.2),
        SNARE=dict(tone_hz=180, noise_level=1.0, filter=1, filter_hz=1800, filter_q=0.8, noise_ms=200, level=0.9),
        HAT=dict(noise_level=0.6, metal_level=0.3, filter_hz=6000, noise_ms=40),
        OPEN=dict(noise_level=0.6, metal_level=0.3, filter_hz=6000, noise_ms=300)),
    kit('world percussion', KICK=dict(tone_hz=70, tone_bend=1.2, bend_ms=30, tone_ms=500, noise_level=0.05, level=0.9),
        SNARE=dict(tone_hz=520, tone_bend=1.05, tone_ms=90, tone_level=0.8, noise_level=0.4, filter=1, filter_hz=3500, filter_q=2, noise_ms=40),
        RIM=dict(tone_hz=2500, tone_ms=50, noise_level=0, level=0.5),
        HAT=dict(metal_level=0.8, metal_hz=3200, metal_ratio=2.76, metal_count=2, noise_level=0, filter=1, filter_hz=6000,
                 filter_q=4, noise_ms=250, level=0.25),
        OPEN=dict(metal_level=0.8, metal_hz=3200, metal_ratio=2.76, metal_count=2, noise_level=0, filter=1, filter_hz=6000,
                  filter_q=4, noise_ms=900, level=0.25),
        TOM_LO=dict(tone_hz=330, tone_bend=1.1, tone_ms=300, metal_level=0.2, noise_level=0, filter=1, filter_hz=3000, noise_ms=120),
        TOM_HI=dict(tone_hz=440, tone_bend=1.1, tone_ms=260, metal_level=0.2, noise_level=0, filter=1, filter_hz=3500, noise_ms=120),
        CONGA_LO=dict(tone_hz=175, tone_ms=300, noise_level=0.1), CONGA_HI=dict(tone_hz=262, tone_ms=260, noise_level=0.1),
        BELL=dict(metal_hz=900, metal_ratio=1.5)),
    kit('tabla and doumbek', KICK=dict(tone_hz=95, tone_bend=1.15, bend_ms=25, tone_ms=420, noise_level=0.08, level=0.9),
        SNARE=dict(tone_hz=640, tone_bend=1.0, tone_ms=110, tone_level=0.7, noise_level=0.5, filter=1, filter_hz=4000, filter_q=2.5,
                   noise_ms=45, level=0.7),
        RIM=dict(tone_hz=900, tone_ms=25, noise_level=0.4, filter=1, filter_hz=2500, filter_q=2, noise_ms=20, level=0.45),
        # the tabla, recorded (drum_samples.py): the dayan's strokes choke each other, as do the bayan's,
        # as a stroke on a skin stops the last one ringing; the bayan to the left, the dayan to the right.
        # The dayan follows: tuned to the key (dayan_tuning in main.cpp)
        CLAP=dict(sample='ke', choke=3, level=1.75, pan=-0.25),
        HAT=dict(sample='te', choke=2, follow=1, level=1.5, pan=0.25),
        OPEN=dict(sample='tin', choke=2, follow=1, level=1.75, pan=0.25),
        CYMBAL=dict(sample='na open', choke=2, follow=1, level=1.9, pan=0.25),
        TOM_LO=dict(sample='tun', choke=2, follow=1, level=1.75, pan=0.25),
        TOM_HI=dict(sample='ne', choke=2, follow=1, level=1.4, pan=0.25),
        CONGA_LO=dict(sample='ge', choke=3, level=2.1, pan=-0.25),
        CONGA_HI=dict(sample='na', choke=2, follow=1, level=1.9, pan=0.25),
        BELL=dict(sample='ge bent', choke=3, level=2.1, pan=-0.25),
        SHAKER=dict(sample='re', choke=2, follow=1, level=1.25, pan=0.25)),
    kit('trap', drive=1.6, KICK=dict(tone_hz=45, tone_bend=1.6, bend_ms=25, tone_ms=1400, tone_level=0.9, noise_level=0.1, follow=1),
        SNARE=dict(tone_hz=210, noise_level=0.8, filter=1, filter_hz=1600, noise_ms=180),
        CLAP=dict(noise_ms=260),
        HAT=dict(noise_level=0.8, metal_level=0.2, filter_hz=9000, noise_ms=25, level=0.45),
        OPEN=dict(noise_level=0.8, metal_level=0.2, filter_hz=9000, noise_ms=250, level=0.45)),
    kit('house', drive=1.2, KICK=dict(tone_hz=52, tone_bend=3.0, bend_ms=10, tone_ms=380, noise_level=0.2),
        CLAP=dict(filter_hz=1100, noise_ms=250),
        HAT=dict(noise_level=0.9, metal_level=0.1, filter_hz=9000, noise_ms=35),
        OPEN=dict(noise_level=0.9, metal_level=0.1, filter_hz=9000, noise_ms=220),
        SHAKER=dict(noise_ms=45)),
    kit('glitch', bits=6, hold=4, drive=1.5, KICK=dict(tone_hz=50, tone_bend=2.5, tone_ms=300),
        SNARE=dict(tone_hz=300, noise_ms=90), HAT=dict(metal_hz=205.3, noise_ms=20), OPEN=dict(metal_hz=205.3, noise_ms=150),
        CLAP=dict(retrigs=5, retrig_ms=6), CYMBAL=dict(noise_ms=700)),
    # Glitch's clicks and cuts, done clean: blips, bleeps, pings and crackle, the sounds of SuperCollider
    # and Pure Data patches rather than of a drum. The pitched ones follow the chord (follow=2: its root,
    # in their own octave) and scatter among its root and fifth, so they bleep in tune
    kit('cliqhop', drive=1.2,
        # a tight sine thud, a click on top
        KICK=dict(tone_hz=48, tone_bend=6.0, bend_ms=3, tone_ms=170, noise_level=0.5, source=1, filter=2, filter_hz=4000,
                  noise_ms=3, level=1.0),
        # a digital snap: an FM chirp, crushed, and a hiss
        SNARE=dict(wave=2, tone_hz=330, tone_bend=1.5, bend_ms=8, fm_ratio=2.71, fm_index=7, fm_ms=18, tone_ms=60, tone_level=0.6,
                   noise_level=0.8, filter=2, filter_hz=3500, noise_ms=55, decimate=3, level=0.75),
        # the click itself: one sample, high passed
        RIM=dict(tone_hz=0, source=1, noise_level=0.9, filter=2, filter_hz=6000, filter_q=0.7, noise_ms=2, level=0.7, pan=0.15),
        # a zipper: a short burst struck six times, fading
        CLAP=dict(noise_level=1.0, filter=1, filter_hz=2400, filter_q=2.0, noise_ms=10, retrigs=0, stutter=5, stutter_blocks=3,
                  stutter_fade=0.82, level=1.0, pan=-0.1),
        # a tick: a click rung high
        HAT=dict(noise_level=0.35, metal_level=0, source=1, filter=1, filter_hz=9500, filter_q=8, noise_ms=30, level=0.5, pan=0.3),
        # an arpeggio: a square blip struck again a fifth up, and again
        OPEN=dict(wave=1, tone_hz=880, tone_ms=30, tone_level=0.4, noise_level=0, metal_level=0, stutter=3, stutter_blocks=4,
                  stutter_ratio=1.5, stutter_fade=0.8, follow=2, choke=0, level=0.45, pan=0.35),
        # a crackle that settles, over an FM bell
        CYMBAL=dict(wave=2, tone_hz=1046, fm_ratio=3.5, fm_index=3, fm_ms=600, tone_ms=1600, tone_level=0.35, noise_level=0.9,
                    metal_level=0, source=2, dust=3000, filter=2, filter_hz=2500, noise_ms=1100, follow=2, level=0.5, pan=-0.3),
        # a rising bloop and a falling bip
        TOM_LO=dict(tone_hz=220, tone_bend=0.5, bend_ms=25, tone_ms=160, noise_level=0, follow=2, level=0.7, pan=-0.25),
        TOM_HI=dict(wave=1, tone_hz=660, tone_bend=1.5, bend_ms=6, tone_ms=55, tone_level=0.4, noise_level=0, follow=2, decimate=2,
                    level=0.6, pan=0.25),
        # pings: a click ringing a high q filter at the chord's root, or its fifth or octave
        CONGA_LO=dict(tone_hz=523, tone_level=0, noise_level=0.6, source=1, filter=1, filter_hz=523, filter_q=160, noise_ms=900,
                      follow=2, scatter=3, level=0.55, pan=-0.35),
        CONGA_HI=dict(tone_hz=1046, tone_level=0, noise_level=0.5, source=1, filter=1, filter_hz=1046, filter_q=220, noise_ms=700,
                      follow=2, scatter=5, level=0.45, pan=0.35),
        # the bleep: an FM blip, somewhere among the chord's harmonics each time
        BELL=dict(wave=2, tone_hz=1046, fm_ratio=1.0, fm_index=3, fm_ms=25, tone_ms=80, tone_level=0.6, metal_level=0, noise_level=0,
                  follow=2, scatter=5, level=0.45, pan=0.2),
        # dust: a moment's crackle
        SHAKER=dict(noise_level=0.7, source=2, dust=5000, filter=1, filter_hz=5000, filter_q=1.0, noise_ms=45, level=0.4, pan=-0.2)),
]
KIT_INDEX = {k['name']: i for i, k in enumerate(KITS)}

# ---- fills ----------------------------------------------------------------------------------------
def ramp(n, lo=60, hi=120):
    return [round(lo + (hi - lo) * i / max(n - 1, 1)) for i in range(n)]

TIHAI = 'ti/ra ki/ta DHA ti/ra ki/ta DHA ti/ra ki/ta'

def bol_steps(tokens):
    """bols, a step each -> per step [(where in the step, 0 to 1, letter, velocity)]"""
    out = []
    for tok in tokens:
        hits = []
        parts = tok.split('/')
        for j, part in enumerate(parts):
            if part == '-':
                continue
            vel = 56 if part.startswith('(') else 124 if part.isupper() else 96
            bol = part.strip('()').lower()
            assert bol in BOLS, f'{bol!r} is not a bol (BOLS)'
            hits += [(j / len(parts), letter, vel) for letter in BOLS[bol]]
        out.append(hits)
    return out

def fill_family(family, n):
    """The second half of a fill bar, n steps: {voice: [(step, velocity)]}"""
    v = ramp(n)
    third = max(n // 3, 1)
    f = {}
    def put(letter, step, vel):
        f.setdefault(letter, []).append((step, vel))
    if family == 'toms':
        for i in range(n):
            put('S' if i < third else 'U' if i < 2 * third else 'T', i, v[i])
        put('K', n - 1, 110)
    elif family == 'snare_roll':
        for i in range(n):
            put('S', i, v[i])
        put('S', n - 0.5, 120)
    elif family == 'congas':
        for i in range(n):
            put('M' if i % 2 == 0 else 'L', i, v[i])
        put('L', n - 1, 120)
    elif family == 'timbales':
        put('B', 0, 110)
        for i in range(n):
            put('U' if i % 2 == 0 else 'T', i, v[i])
    elif family == 'tabla':   # a tihai: tirakita dha three times, the third dha the bar line it lands on
        tokens = TIHAI.split()[-n:]
        for i, hits in enumerate(bol_steps(tokens)):
            for frac, letter, vel in hits:
                put(letter, i + frac, round(vel * (0.6 + 0.4 * i / max(n - 1, 1))))
    elif family == 'doumbek':
        for i in range(n):
            put('S', i, v[i])
            put('R', i + 0.5, 40)
        put('K', 0, 110)
        put('K', n - 1, 120)
    elif family == 'trap_roll':
        for i in range(n):
            put('H', i, 80)
            put('H', i + 0.5, 70)
        put('S', n - 2, 110)
        put('S', n - 1, 124)
    elif family == 'brush':
        for i in range(n):
            put('S', i, round(30 + 50 * i / max(n - 1, 1)))
        put('K', n - 1, 90)
    elif family == 'ride':
        put('Y', 0, 100)
        for i in range(n):
            put('S', i, v[i])
        put('K', n - 1, 110)
    elif family == 'stutter':   # a glitch: the snare ratchets faster to the bar line, blips climbing
        for i in range(n):
            d = 1 if i < n // 2 else 2 if i < n - 2 else 3
            for j in range(d):
                put('S', i + j / d, v[i])
            put('U', i + 0.5, 50 + 8 * i)
        put('K', n - 1, 110)
    else:
        raise ValueError(family)
    return f

# ---- grooves --------------------------------------------------------------------------------------
GROOVES = []
def G(name, kit, beats=4, steps=4, swing=12, fill='toms', bols=None, **lines):
    GROOVES.append(dict(name=name, kit=KIT_INDEX[kit], beats=beats, steps=steps, swing=swing, fill=fill, lines=lines, bols=bols))

# Western standards
G('rock', 'studio', K='x.......x.x.....', S='....x.......x...', H='x.x.x.x.x.x.x.x.')
G('pop eighths', 'studio', K='x.....x.x.......', S='....x.......x...', H='x.o.x.o.x.o.x.o.')
G('pop sixteenths', 'studio', fill='snare_roll', K='x.......x.x.....', S='....x.......x...', H='xoxoxoxoxoxoxoxo')
G('ballad', 'studio', K='x.........x.....', R='....x.......x...', H='x.o.x.o.x.o.x.o.')
G('funk', 'studio', swing=6, fill='snare_roll', K='x..x..x...x..x..', S='....x..o.o..x..o', H='xoxoxoxoxoxoxoxo')
G('disco', '909', fill='snare_roll', K='x...x...x...x...', C='....x.......x...', H='x...x...x...x...', O='..x...x...x...x.')
G('motown', 'studio', K='x.....x.x.......', S='....X.......X...', Z='....x.......x...', H='x.x.x.x.x.x.x.x.')
G('shuffle', 'studio', steps=3, swing=0, fill='ride', K='x.....x.....', S='...x.....x..', H='x.xx.xx.xx.x')
G('jazz swing', 'brushes', steps=3, swing=0, fill='ride', Y='x..x.xx..x.x', H='...x.....x..', K='g..g..g..g..', S='......o.....')
G('waltz', 'brushes', beats=3, fill='brush', K='x...........', S='....o...o...', H='x.x.x.x.x.x.')
G('march', 'studio', fill='snare_roll', S='x.xxx.x.x.xxx.x.', K='x.......x.......', Y='x.......x.......')
G('second line', 'studio', fill='snare_roll', K='x..x..x...x.x...', S='..x..xx.x.xx.x.x', H='x.x.x.x.x.x.x.x.')
G('country two-beat', 'studio', fill='snare_roll', K='x.......x.......', S='o.o.X.o.o.o.X.o.', H='x.x.x.x.x.x.x.x.')
G('folk', 'brushes', fill='brush', K='x.......x.......', R='....x.......x...', Z='x.x.x.x.x.x.x.x.')
G('six eight', 'studio', beats=3, steps=2, swing=0, K='x.....', S='...x..', H='xxxxxx')
G('habanera', 'studio', beats=2, K='x...x...', R='...x..x.', H='x.x.x.x.')
G('house', 'house', fill='snare_roll', K='x...x...x...x...', C='....x.......x...', O='..x...x...x...x.', Z='oooooooooooooooo')
G('trap', 'trap', swing=0, fill='trap_roll', K='x......x..x.....', S='........x.......', H='x.x.x.x.xrx.x.rr')
# Breaks and hip hop, re-played
G('amen-style break', 'boom bap', fill='snare_roll',
  K='x.x.......xx....|x.x.......x.....', S='....x..x.x..x..x|....x..x.x....x.', H='x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x.')
G('funky drummer-style', 'boom bap', swing=6, fill='snare_roll',
  K='x.x...x...x..x..', S='....x..o.o.ox..o', H='xxxxxxx.xxxxx.xx', O='.......x.....x..')
G('think-style break', 'boom bap', fill='snare_roll', K='x.....x...x.....', S='....x.......x...', H='xxxxxxxxxxxxxxxx')
G('boom bap', 'boom bap', swing=6, fill='snare_roll', K='x......x..x.....', S='....x.......x...', H='x.x.x.x.x.x.x.x.')
G('drum and bass', 'studio', fill='snare_roll', K='x.........x.....', S='....x.......x...', H='xxxxxxxxxxxxxxxx')
G('lo-fi', 'boom bap', swing=6, fill='brush', K='x.....x...x.....', S='....x.......x..o', H='x.xox.xox.xox.xo')
# Latin and Caribbean
G('son clave', 'world percussion', fill='congas', R='x..x..x...x.x...', K='...x.......x....', M='....o.....x...xx',
  L='..........xx....', B='x...x...x...x...')
G('rumba clave', 'world percussion', fill='congas', R='x..x...x..x.x...', K='...x.......x....', M='....o.....x...xx',
  L='..........xx....', Z='xoxoxoxoxoxoxoxo')
G('bossa nova', 'brushes', fill='brush', R='x..x..x...x..x..|..x..x..x..x....', K='x..xx..xx..xx..x|x..xx..xx..xx..x',
  H='x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x.')
G('samba', 'world percussion', fill='snare_roll', K='x.......X.......', Z='XxxxXxxxXxxxXxxx', B='x.x..x.x.x..x.x.',
  R='x..x..x...x..x..', S='..x...x...x...xx')
G('cumbia', 'world percussion', fill='congas', K='x.......x.......', Z='x.xxx.xxx.xxx.xx', M='..x...x...x...x.', L='x.......x.......')
G('baiao', 'world percussion', fill='timbales', K='x..x..x.x..x..x.', H='xoxxxoxxxoxxxoxx')
G('salsa', 'world percussion', fill='timbales', B='x.x.x.xxx.x.x.xx', R='..x.x...x..x..x.', M='....o.......o...',
  L='......xx......xx', K='...x.......x....')
G('one drop', 'studio', K='........x.......', R='........x.......', H='x.x.x.x.x.x.x.x.')
G('dembow', '808', fill='snare_roll', K='x...x...x...x...', S='...x..x....x..x.', H='x.x.x.x.x.x.x.x.')
G('soca', '909', fill='snare_roll', K='x...x...x...x...', S='....x.......x...', O='..x...x...x...x.', B='x.xx.xx.x.xx.xx.')
# Africa, the Middle East and India
G('afrobeat', 'studio', K='x.....x...x..x..', S='o.o.x.o.o.o.x.o.', H='x.x.x.x.x.x.x.x.', Z='xxxxxxxxxxxxxxxx',
  B='x.x..x.x..x.x..x')
G('12/8 bell', 'world percussion', steps=3, swing=0, fill='congas', B='x.x.xx.x.x.x', Z='xxxxxxxxxxxx', L='x.....x.....',
  M='...x.....x..', K='x.....x.....')
G('highlife', 'world percussion', fill='congas', B='x..x..x.x..x..x.', K='x.......x.......', M='..x...x...x...x.',
  Z='x.x.x.x.x.x.x.x.', R='x..x..x...x.x...')
G('maqsum', 'tabla and doumbek', fill='doumbek', K='x.......x.......', S='..x...x.....x...', R='o...o.o...o...o.')
G('baladi', 'tabla and doumbek', fill='doumbek', K='x.x.....x.......', S='......x.....x...', R='o...o...o...o.o.')
G('saidi', 'tabla and doumbek', fill='doumbek', K='x.....x.x.......', S='..x.........x...', R='o...o...o...o...')
# The tabla's thekas, a matra a step: the khali (the empty beat) is where the bayan rests, Dha becoming Ta
G('kehrwa', 'tabla and doumbek', steps=2, fill='tabla', bols='DHA ge na ti   na ka dhi na')
G('dadra', 'tabla and doumbek', beats=3, steps=2, swing=0, fill='tabla', bols='DHA dhi na   dha tu na')
G('teental', 'tabla and doumbek', fill='tabla', bols='DHA dhin dhin dha  dha dhin dhin dha  dha tin tin ta  ta dhin dhin dha')
# Glitch: clicks and cuts, cliqhop, IDM (on the cliqhop kit: K thud, S snap, R click, C zipper, H tick,
# O arpeggio, Y crackle, T bloop, U bip, L M pings, B bleep, Z dust)
G('cliqhop', 'cliqhop', swing=6, fill='stutter',
  K='x.....x...x.....|x.....x..x..x...', S='........X.......|........X.....3.', R='..x...x..x.x..x.|.x..x...x..x...x',
  H='x.xxx.x.x.xxx.x.|x.xxx.x.x.xx3.x.', B='...o.....o....o.|.o.....o...o....', L='o...............|........o.......')
G('clicks and cuts', 'cliqhop', fill='stutter', K='x...x...x...x...', C='....x.......x...', R='..x...x...x...x.',
  H='.xx..x.x.xx..xx.', L='x.....x.....x...', Z='.......o.......o', O='..........x.....')
G('idm', 'cliqhop', fill='stutter',
  K='x..x....x.x...x.|x....x.x..x....x', S='....x..3....x...|..x.....X...3.x.', H='xxx.xx.xxx.x6.xx|x.xx3.xxx.xxx.x6',
  U='.....o.....o....|...o.......o..o.', B='..o.......o.....|......o.......o.', Z='o.......o.......|o.......o.......')
G('microsound', 'cliqhop', fill='stutter', K='x...........o...', R='......x.....x..x', H='.x.x.x...x.x.x..', L='x.......x.....o.',
  M='....o.......o...', Z='...o.......o....', Y='x...............')
# Indonesia: the kendang, a gamelan's drummer, who leads its tempo, on the hand drums: the low head's dhang (L,
# the loud one the phrase ends on), the high head's tung (M), the slapped tak (R) and a deep dhe on the bar line (K)
G('kendang', 'world percussion', fill='congas', K='x...............|x...............', L='.......x.......X|.......x...x.x.X',
  M='...o.......o....|...o.......o....', R='..x...x...x...x.|..x...x...x..x..')

# Each accompaniment style's own drum part (rhythm style 274, its 0 to 14), a groove above by name
STYLE_PARTS = ['rock', 'pop eighths', 'waltz', 'country two-beat', 'jazz swing', 'shuffle', 'folk', 'pop eighths',
               'bossa nova', 'pop sixteenths', 'pop sixteenths', 'ballad', 'one drop', 'six eight', 'habanera']

# ---- building ---------------------------------------------------------------------------------------
def events_of(lines, beats, steps):
    """{letter: grid} -> sorted [(at in 24ths, voice, velocity)], and the bars"""
    unit = 24 // steps
    bar_steps = beats * steps
    out, bars = [], None
    for letter, grid in lines.items():
        parts = grid.split('|')
        bars = bars or len(parts)
        assert len(parts) == bars, f'{letter}: {len(parts)} bars, not {bars}'
        for b, part in enumerate(parts):
            assert len(part) == bar_steps, f'{letter}: bar {b + 1} has {len(part)} steps, not {bar_steps}: {part!r}'
            for i, c in enumerate(part):
                at = (b * bar_steps + i) * unit
                if c == '.':
                    continue
                if c == 'r':
                    out += [(at, LETTER[letter], 84), (at + unit // 2, LETTER[letter], 84)]
                elif c in '23456':   # a ratchet: that many hits in the step
                    d = int(c)
                    assert unit % d == 0, f'{letter}: {d} hits in a step of {unit} 24ths fall between them'
                    out += [(at + j * unit // d, LETTER[letter], 84) for j in range(d)]
                else:
                    out.append((at, LETTER[letter], VELOCITY[c]))
    return sorted(out), bars

def bol_events(bols, beats, steps):
    """a groove's bols -> sorted [(at in 24ths, voice, velocity)], and the bars"""
    unit = 24 // steps
    bar_steps = beats * steps
    out = []
    bars = bols.split('|')
    for b, bar in enumerate(bars):
        tokens = bar.split()
        assert len(tokens) == bar_steps, f'bar {b + 1} has {len(tokens)} bols, not {bar_steps}: {bar!r}'
        for i, hits in enumerate(bol_steps(tokens)):
            for frac, letter, vel in hits:
                at = (b * bar_steps + i + frac) * unit
                assert at == int(at), f'{tokens[i]!r} divides a step of {unit} 24ths unevenly'
                out.append((int(at), LETTER[letter], vel))
    return sorted(out), len(bars)

def groove_events(g):
    ev, bars = events_of(g['lines'], g['beats'], g['steps'])
    if g['bols']:
        more, bol_bars = bol_events(g['bols'], g['beats'], g['steps'])
        assert bars in (None, bol_bars), f"{g['name']}: the bols have {bol_bars} bars, the lines {bars}"
        ev, bars = sorted(ev + more), bol_bars
    return ev, bars

def fill_of(g):
    beats, steps = g['beats'], g['steps']
    unit = 24 // steps
    bar_steps = beats * steps
    fill_beats = max(beats // 2, 1)
    start = (beats - fill_beats) * steps          # the step the fill takes over at
    first = {k: v.split('|')[0][:start] + '.' * (bar_steps - start) for k, v in g['lines'].items()}
    ev, _ = events_of(first, beats, steps)
    if g['bols']:
        ev += [e for e in bol_events(g['bols'].split('|')[0], beats, steps)[0] if e[0] < start * unit]
    for letter, hits in fill_family(g['fill'], bar_steps - start).items():
        for step, vel in hits:
            at = round((start + step) * unit)
            if at < bar_steps * unit:
                ev.append((at, LETTER[letter], min(127, max(1, vel))))
    return sorted(ev)

def c_kit(k):
    vs = []
    for name in VOICES:
        v = k['voices'][name]
        f = lambda x: ('%.6g' % x) + ('' if any(c in '%.6g' % x for c in '.e') else '.0') + 'f'
        vs.append('    {%s, %s, %s, %s, %s, %s, %s, %s, %s, %d, %d, %s, %s, %s, %d, %s, %s, %s, %d, %d, %d, '
                  '%d, %s, %s, %s, %d, %s, %d, %d, %d, %d, %s, %s, %d},   // %s' % (
            f(v['tone_hz']), f(v['tone_bend']), f(v['bend_ms']), f(v['tone_ms']), f(v['tone_level']), f(v['noise_level']),
            f(v['metal_level']), f(v['metal_hz']), f(v['metal_ratio']), v['metal_count'], v['filter'], f(v['filter_hz']),
            f(v['filter_q']), f(v['noise_ms']), v['retrigs'], f(v['retrig_ms']), f(v['level']), f(v['pan']), v['choke'],
            v['follow'], v['sample'],
            v['wave'], f(v['fm_ratio']), f(v['fm_index']), f(v['fm_ms']), v['source'], f(v['dust']), v['ring'], v['scatter'],
            v['stutter'], v['stutter_blocks'], f(v['stutter_ratio']), f(v['stutter_fade']), v['decimate'], name.lower() + (' (%s)' % STROKES[v['sample']][0] if v['sample'] >= 0 else '')))
    return '  {"%s", %d, %d, %.3ff, {\n%s\n  }},' % (k['name'], k['bits'], k['hold'], k['drive'], '\n'.join(vs))

def main():
    lines = ['// generated by generator/drums.py - do not edit: change the kits and grooves there and run it',
             '#ifndef DRUM_PATTERNS_H', '#define DRUM_PATTERNS_H', '#include "drum_machine.h"', '#include "drum_samples.h"', '',
             'struct drum_event_t { uint16_t at; uint8_t voice; uint8_t velocity; };   // at in 24ths of a beat',
             'struct drum_groove_t {',
             '  const char *name;',
             '  uint8_t kit;            // its own kit, in drum_kits',
             '  uint8_t beats_per_bar;',
             '  uint8_t swing_unit;     // what the shuffle swings: 12 eighths, 6 sixteenths, 0 nothing (triplet grooves)',
             '  uint16_t length;        // 24ths of a beat',
             '  uint16_t count;',
             '  const drum_event_t *events;',
             '  uint16_t fill_count;    // its fill bar',
             '  const drum_event_t *fill;',
             '  uint8_t bol_steps;       // a theka written in bols: its bols to the beat as written (tabla laya stretches it), else 0',
             '};', '']
    lines.append('const uint8_t drum_gm_note[DRUM_VOICES] = {%s};' % ', '.join(map(str, GM_NOTE)))
    lines.append('const drum_kit_t drum_kits[] PROGMEM = {')
    lines += [c_kit(k) for k in KITS]
    lines += ['};', 'const uint8_t drum_kit_count = sizeof(drum_kits) / sizeof(drum_kit_t);', '']
    entries = []
    for i, g in enumerate(GROOVES):
        ev, bars = groove_events(g)
        fill = fill_of(g)
        lines.append('static const drum_event_t drum_groove_%d[] PROGMEM = {%s};   // %s' % (
            i, ', '.join('{%d, %d, %d}' % e for e in ev), g['name']))
        lines.append('static const drum_event_t drum_fill_%d[] PROGMEM = {%s};' % (i, ', '.join('{%d, %d, %d}' % e for e in fill)))
        entries.append('  {"%s", %d, %d, %d, %d, %d, drum_groove_%d, %d, drum_fill_%d, %d},' % (
            g['name'], g['kit'], g['beats'], g['swing'], bars * g['beats'] * 24, len(ev), i, len(fill), i,
            g['steps'] if g['bols'] else 0))
    lines += ['', '// drum groove (335) 1 is the first', 'const drum_groove_t drum_grooves[] PROGMEM = {'] + entries + [
        '};', 'const uint8_t drum_groove_count = sizeof(drum_grooves) / sizeof(drum_groove_t);', '']
    index = {g['name']: i for i, g in enumerate(GROOVES)}
    lines.append("// each accompaniment style's own drum part, by rhythm style (274)")
    lines.append('const uint8_t style_drum_groove[] = {%s};' % ', '.join(str(index[n]) for n in STYLE_PARTS))
    lines += ['', '#endif', '']
    with open(OUT, 'w') as f:
        f.write('\n'.join(lines))
    print(f'{len(KITS)} kits, {len(GROOVES)} grooves written to {os.path.relpath(OUT)}')
    print('grooves:', ', '.join(f'{i + 1} {g["name"]}' for i, g in enumerate(GROOVES)))
    print('kits:', ', '.join(f'{i + 1} {k["name"]}' for i, k in enumerate(KITS)))

if __name__ == '__main__':
    main()
