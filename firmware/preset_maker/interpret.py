"""Turns a description in words into minichord settings, by rules: no model, nothing online.

It knows a vocabulary of instrument words ("pad", "organ", "music box"), qualities ("dark",
"spacey", "detuned"), effects ("lots of reverb", "no delay"), settings ("pentatonic", "key of
E flat", "120 bpm", "jazzy") and control phrases ("the mod knob opens the filter", "double tap
starts the looper", "hover makes the chords sing"). Each word is tied to the chords or the harp
by the nearest of those words in its clause, so "warm pad chords with a plucky harp" puts the
pad on the chords and the pluck on the harp. Anything it doesn't know is reported, not guessed.

interpret(text, values, params) returns the changes in the units of parameters.json, for
preset_maker.apply_changes to check and store, with what was understood and what wasn't.
"""
import difflib, os, re
from dataclasses import dataclass, field

NOT_IN_PRESETS = {0, 1, 2, 3, 4, 5, 6, 7, 241, 242, 243, 244, 256, 382, 383, 510, 511}
HERE = os.path.dirname(os.path.abspath(__file__))
LOOKUP_H = os.path.join(os.path.dirname(HERE), "lib", "potentiometer", "src", "parameter_lookup.h")

# ---- what the words move ------------------------------------------------------------------------
# A role is one kind of setting, at its address in each section that has it.

ROLES = {
    "attack": {"chord": 137, "harp": 43}, "hold": {"chord": 138, "harp": 44},
    "decay": {"chord": 139, "harp": 45}, "sustain": {"chord": 140, "harp": 46},
    "release": {"chord": 141, "harp": 47},
    "cutoff": {"chord": 143, "harp": 49}, "resonance": {"chord": 145, "harp": 51},
    "filter_sens": {"chord": 155, "harp": 58},
    "fenv_attack": {"chord": 146, "harp": 52}, "fenv_decay": {"chord": 148, "harp": 54},
    "fenv_sustain": {"chord": 149, "harp": 55}, "fenv_release": {"chord": 150, "harp": 56},
    "reverb": {"chord": 184, "harp": 85},
    "crunch": {"chord": 185, "harp": 86}, "crunch_type": {"chord": 186, "harp": 87},
    "delay_time": {"chord": 176, "harp": 77}, "delay_filter": {"chord": 177, "harp": 78},
    "delay_feedback": {"chord": 179, "harp": 80}, "delay_mix": {"chord": 183, "harp": 84},
    "trem_wave": {"chord": 156, "harp": 59}, "trem_freq": {"chord": 157, "harp": 60},
    "trem_amount": {"chord": 159, "harp": 61},
    "vib_freq": {"chord": 161, "harp": 63}, "vib_amount": {"chord": 163, "harp": 64},
    "vib_depth": {"chord": 175, "harp": 76}, "vib_sustain": {"chord": 167, "harp": 68},
    "octave": {"chord": 198, "harp": 99}, "level": {"chord": 197, "harp": 97},
    "voice": {"chord": 265, "harp": 264},
    "wave": {"chord": 122, "harp": 42}, "wave2": {"chord": 125}, "wave3": {"chord": 128},
    "amp1": {"chord": 121, "harp": 41}, "amp2": {"chord": 124}, "amp3": {"chord": 127},
    "mult1": {"chord": 123}, "mult2": {"chord": 126}, "mult3": {"chord": 129},
    "noise": {"chord": 130},
    "lfo_wave": {"chord": 152}, "lfo_freq": {"chord": 153}, "lfo_amount": {"chord": 154},
    "glide": {"chord": 199}, "ensemble": {"chord": 259},
    "spread": {"harp": 257}, "spread_pattern": {"harp": 258},
    "string_model": {"harp": 217}, "string_decay": {"harp": 218}, "string_damping": {"harp": 219},
    "transient": {"harp": 101},
    "vowel": {"chord": 118}, "formant": {"chord": 119}, "vocoder": {"chord": 260},
    "touch": {"harp": 252}, "pressure": {"harp": 253}, "strum": {"harp": 263},
    "palm_mute": {"harp": 213}, "lift": {"harp": 216},
    "scale": {"harp": 36}, "chromatic": {"harp": 98},
    "barry": {"chord": 33}, "voice_leading": {"chord": 111}, "layout": {"chord": 39},
    "spacing": {"chord": 38},
    "reverb_size": {"global": 24}, "tempo": {"global": 187}, "key": {"global": 35},
    "temperament": {"global": 237}, "tuning": {"global": 109}, "transpose": {"global": 30},
    "color": {"global": 20}, "looper": {"global": 256},
    "ribbon": {"harp": 245}, "mpe": {"global": 110}, "midi_in": {"global": 8}, "knob_midi": {"global": 238},
    "knob_layer": {"global": 117}, "led": {"global": 32}, "hover_reach": {"global": 251},
    "alt_maj": {"chord": 202}, "alt_min": {"chord": 203}, "alt_7th": {"chord": 204}, "alt_maj7": {"chord": 205},
    "alt_min7": {"chord": 206}, "alt_majmin": {"chord": 207}, "alt_all": {"chord": 208},
}


def SET(role, v): return ("set", role, v)          # toward v; how far depends on "slightly" and "very"
def EXACT(role, v): return ("exact", role, v)      # to v, whatever the intensity
def SCALE(role, f): return ("scale", role, f)      # times f ("very" squares it, "slightly" roots it)
def MIN(role, v): return ("min", role, v)          # at least v
def MAX(role, v): return ("max", role, v)          # at most v
def IFZERO(role, v): return ("ifzero", role, v)    # v if it's off, so an effect has something to work with
def ADD(role, n): return ("add", role, n)


def synth_chord(a1, w1, m1, a2=0.0, w2=0, m2=2.0, a3=0.0, w3=0, m3=0.5, noise=0.0):
    """The chord section's own oscillators, in place of a sampled voice"""
    return (EXACT("voice", 0), EXACT("amp1", a1), EXACT("wave", w1), EXACT("mult1", m1),
            EXACT("amp2", a2), EXACT("wave2", w2), EXACT("mult2", m2),
            EXACT("amp3", a3), EXACT("wave3", w3), EXACT("mult3", m3), EXACT("noise", noise))


def synth_harp(wave, model=0):
    return (EXACT("voice", 0), EXACT("wave", wave), EXACT("string_model", model))


def env(attack, decay, sustain, release, hold=0):
    return (EXACT("attack", attack), EXACT("hold", hold), EXACT("decay", decay),
            EXACT("sustain", sustain), EXACT("release", release))


DELAY_READY = (IFZERO("delay_time", 350), IFZERO("delay_filter", 3000), IFZERO("delay_feedback", 0.4))
VIBRATO_READY = (MIN("vib_depth", 0.1), MIN("vib_sustain", 1.0), IFZERO("vib_freq", 5))


@dataclass
class Entry:
    words: tuple
    kind: str                      # sound, quality, effect or setting
    label: str
    moves: object = ()             # a tuple for every section, or {section: tuple}
    off: object = None             # what "no ..." does; effects default to their SETs at 0
    opposite: str = None           # a quality's opposite, for "less ..." and "not ..."
    home: str = "both"             # the section when the clause names none
    hue: int = None
    note: str = None


V = []   # the vocabulary


def add(words, kind, label=None, **kw):
    words = tuple(w.strip() for w in words.split(","))
    V.append(Entry(words, kind, label or words[0], **kw))


# Instruments and kinds of sound: whole settings, applied as they are
add("pad, pads, padlike, wash, washy", "sound", "pad", home="chord", hue=220, moves={
    "chord": env(700, 1000, 0.85, 2500) + (SET("ensemble", 40), SET("reverb", 0.55), SCALE("cutoff", 0.8)),
    "harp": env(400, 800, 0.8, 2500) + (EXACT("string_model", 0),)})
add("pluck, plucks, plucky, plucked, plucking, picked", "sound", "plucky", home="harp", hue=150,
    moves=env(1, 350, 0.0, 450))
add("stab, stabs, stabby", "sound", "stabs", home="chord", hue=10, moves=env(1, 180, 0.0, 200, hold=20))
add("drone, drones, droning", "sound", "drone", home="chord", hue=260, moves=env(1500, 1000, 1.0, 4000))
add("organ, organs, hammond, b3, drawbar, church organ, combo organ", "sound", "organ", home="chord", hue=30, moves={
    "chord": synth_chord(0.15, 0, 1.0, 0.1, 0, 2.0, 0.08, 0, 0.5) + env(5, 100, 1.0, 80) + (EXACT("cutoff", 3500),),
    "harp": synth_harp(0) + env(5, 100, 1.0, 80)})
add("electric piano, e piano, epiano, rhodes, wurlitzer, wurly, ep", "sound", "electric piano", home="chord", hue=35, moves={
    "chord": synth_chord(0.15, 0, 1.0, 0.05, 3, 2.0) + env(2, 1800, 0.25, 500)
             + (SET("trem_amount", 0.2), IFZERO("trem_freq", 4), EXACT("cutoff", 2500)),
    "harp": synth_harp(0) + env(1, 1500, 0.2, 500) + (EXACT("transient", 0.15),)})
add("piano, pianos, grand piano, acoustic piano, upright piano, keys", "sound", "piano", hue=40,
    moves=(EXACT("voice", 1),) + env(1, 2500, 0.3, 700) + (EXACT("cutoff", 4000), EXACT("string_model", 0)))
add("choir, choirs, voices, vocal, vocals, aahs, ahhs, oohs, singers", "sound", "choir", home="chord", hue=280,
    moves=(EXACT("voice", 3),) + env(250, 500, 0.9, 1200) + (EXACT("cutoff", 4000), EXACT("string_model", 0)))
add("string section, string quartet, string ensemble, orchestral strings, orchestra, orchestral, violin, violins, "
    "cello, cellos, viola, quartet", "sound", "string quartet", home="chord", hue=20,
    moves=(EXACT("voice", 4),) + env(200, 500, 0.9, 900) + (EXACT("cutoff", 4000), EXACT("string_model", 0)))
add("pizzicato, pizz, plucked strings", "sound", "pizzicato", home="harp", hue=15,
    moves=(EXACT("voice", 2),) + env(1, 600, 0.0, 400) + (EXACT("cutoff", 4000), EXACT("string_model", 0)))
add("guitar, guitars, acoustic guitar, nylon, folk, ukulele, uke, lute", "sound", "guitar", home="harp", hue=30, moves={
    "harp": synth_harp(0, 90) + env(1, 3000, 0.6, 900) + (EXACT("string_decay", 3.0), EXACT("string_damping", 40)),
    "chord": synth_chord(0.15, 9, 1.0) + env(1, 1200, 0.1, 500) + (EXACT("cutoff", 1800),)})
add("concert harp, celtic harp, real harp, lyre, zither, dulcimer, autoharp", "sound", "concert harp", home="harp",
    hue=50, moves=synth_harp(0, 75) + env(1, 4000, 0.6, 1500)
    + (EXACT("string_decay", 4.0), EXACT("string_damping", 25)))
add("banjo, mandolin, koto, shamisen, twang, twangy, sitar", "sound", "twangy strings", home="harp", hue=45,
    moves=synth_harp(0, 100) + env(1, 1500, 0.4, 600)
    + (EXACT("string_decay", 1.2), EXACT("string_damping", 70), SCALE("cutoff", 1.5)))
add("harpsichord, harpsichords, clavinet, clav", "sound", "harpsichord", home="harp", hue=45,
    moves={"harp": synth_harp(9, 60) + env(1, 900, 0.0, 300) + (EXACT("string_damping", 5), SCALE("cutoff", 1.5)),
           "chord": synth_chord(0.12, 9, 1.0, 0.06, 4, 2.0) + env(1, 900, 0.0, 300) + (SCALE("cutoff", 1.5),)})
add("bell, bells, chime, chimes, glass, glassy, glockenspiel, celesta, crystal, crystalline, bell like",
    "sound", "bells", home="harp", hue=190, moves={
        "harp": synth_harp(0) + env(1, 2000, 0.0, 2000) + (EXACT("transient", 0.15), SET("reverb", 0.4)),
        "chord": synth_chord(0.15, 0, 1.0, 0.06, 0, 2.0) + env(1, 2000, 0.0, 2000) + (SET("reverb", 0.4),)})
add("vibraphone, vibes, vibraphones", "sound", "vibraphone", home="harp", hue=200, moves={
    "harp": synth_harp(0) + env(1, 2000, 0.0, 1500) + (SET("trem_amount", 0.35), EXACT("trem_freq", 5)),
    "chord": synth_chord(0.15, 0, 1.0, 0.04, 0, 2.0) + env(1, 2000, 0.0, 1500)
             + (SET("trem_amount", 0.35), EXACT("trem_freq", 5))})
add("music box, musicbox", "sound", "music box", home="harp", hue=320,
    moves=synth_harp(0) + env(1, 1200, 0.0, 1200) + (EXACT("transient", 0.15), ADD("octave", 1)))
add("kalimba, mbira, thumb piano", "sound", "kalimba", home="harp", hue=35,
    moves=synth_harp(3, 30) + env(1, 700, 0.0, 700) + (EXACT("string_damping", 50), EXACT("transient", 0.2)))
add("marimba, marimbas, mallet, mallets", "sound", "marimba", home="harp", hue=30,
    moves=synth_harp(0) + env(1, 450, 0.0, 400) + (EXACT("transient", 0.25),))
add("xylophone, xylophones", "sound", "xylophone", home="harp", hue=55,
    moves=synth_harp(0) + env(1, 250, 0.0, 250) + (EXACT("transient", 0.25), ADD("octave", 1)))
add("flute, flutes, recorder, pan flute, panpipe, panpipes, ocarina, whistle", "sound", "flute", hue=160, moves={
    "chord": synth_chord(0.15, 0, 1.0, 0.03, 3, 2.0, noise=0.04) + env(90, 300, 0.85, 250)
             + (SET("vib_amount", 0.12),) + VIBRATO_READY,
    "harp": synth_harp(0) + env(60, 200, 0.8, 300) + (SET("vib_amount", 0.12), IFZERO("vib_freq", 5))})
add("brass, horn, horns, trumpet, trumpets, trombone, sax, saxophone", "sound", "brass", home="chord", hue=45, moves={
    "chord": synth_chord(0.15, 9, 1.0, 0.1, 9, 1.0) + env(60, 400, 0.8, 200)
             + (EXACT("cutoff", 900), EXACT("filter_sens", 1.2), EXACT("fenv_attack", 80),
                EXACT("fenv_decay", 400), EXACT("fenv_sustain", 0.5)),
    "harp": synth_harp(9) + env(30, 400, 0.8, 200)
            + (EXACT("cutoff", 400), EXACT("filter_sens", 2.0), EXACT("fenv_attack", 60),
               EXACT("fenv_decay", 400), EXACT("fenv_sustain", 0.5))})
add("accordion, accordions, harmonium, melodica, bandoneon, concertina, reed organ, reedy", "sound", "accordion",
    home="chord", hue=0, moves={
        "chord": synth_chord(0.12, 5, 1.0, 0.1, 5, 1.01) + env(30, 200, 1.0, 120) + (SET("trem_amount", 0.1), IFZERO("trem_freq", 6)),
        "harp": synth_harp(5) + env(20, 200, 1.0, 120)})
add("supersaw, super saw, trance, rave, hoover, edm", "sound", "supersaw", home="chord", hue=300, moves={
    "chord": synth_chord(0.1, 9, 1.0, 0.1, 9, 1.01, 0.1, 9, 0.5) + env(5, 400, 0.8, 400)
             + (EXACT("ensemble", 60), EXACT("cutoff", 2500)),
    "harp": synth_harp(9) + (MIN("vib_amount", 0.0),)})
add("lead, leads, synth lead, solo", "sound", "lead", home="harp", hue=350, moves={
    "chord": synth_chord(0.15, 9, 1.0) + env(5, 300, 0.8, 250) + (SET("glide", 120),),
    "harp": synth_harp(9) + env(5, 300, 0.8, 250)})
add("synth pluck, synth plucks, synthy pluck", "sound", "synth pluck", home="harp", hue=180, moves={
    "harp": synth_harp(9) + env(1, 300, 0.0, 300)
            + (EXACT("cutoff", 300), EXACT("filter_sens", 2.5), EXACT("fenv_attack", 1),
               EXACT("fenv_decay", 250), EXACT("fenv_sustain", 0)),
    "chord": synth_chord(0.15, 9, 1.0, 0.08, 9, 1.01) + env(1, 300, 0.0, 300)
             + (EXACT("cutoff", 400), EXACT("filter_sens", 2.0), EXACT("fenv_attack", 1),
                EXACT("fenv_decay", 250), EXACT("fenv_sustain", 0))})
add("chiptune, chip tune, 8 bit, 8bit, nes, game boy, gameboy, video game, arcade, retro game", "sound", "chiptune",
    hue=90, moves={
        "chord": synth_chord(0.12, 2, 1.0, 0.05, 5, 2.0) + env(1, 200, 0.6, 60)
                 + (EXACT("reverb", 0.05), EXACT("crunch", 0), EXACT("ensemble", 0), EXACT("cutoff", 5000),
                    EXACT("resonance", 0.7)),
        "harp": synth_harp(5) + env(1, 200, 0.6, 60)
                + (EXACT("reverb", 0.05), EXACT("crunch", 0), EXACT("cutoff", 2000), EXACT("resonance", 0.7))})
add("acid, squelch, squelchy, 303", "sound", "acid", hue=90, moves={
    "chord": (EXACT("resonance", 4.0), SCALE("cutoff", 0.6), EXACT("filter_sens", 2.0), EXACT("fenv_decay", 300),
              EXACT("fenv_sustain", 0.2)),
    "harp": (EXACT("resonance", 4.0), SCALE("cutoff", 0.6), EXACT("filter_sens", 3.0), EXACT("fenv_decay", 300),
             EXACT("fenv_sustain", 0.2))})
add("bass, bassy, bassline, sub, sub bass", "sound", "bass", home="chord", hue=240, moves=(ADD("octave", -1),))

# Qualities: they move the sound some way, by more with "very", by less with "slightly"
add("bright, brighter, brightest, brilliant, crisp, crisper, crispy, sparkly, sparkling, shiny, shinier, clear, "
    "clearer, open, opened", "quality", "brighter", opposite="darker", hue=55,
    moves=(SCALE("cutoff", 1.8), SCALE("resonance", 1.1)))
add("dark, darker, darkest, muffled, muffle, dull, duller, murky, mellow, mellower, round, rounder, subdued, "
    "closed, muted", "quality", "darker", opposite="brighter", hue=250, moves=(SCALE("cutoff", 0.5),))
add("warm, warmer, warmth, cozy, cosy", "quality", "warmer", opposite="colder", hue=25,
    moves={"chord": (SCALE("cutoff", 0.7), SCALE("resonance", 0.85), SET("ensemble", 25)),
           "harp": (SCALE("cutoff", 0.7), SCALE("resonance", 0.85))})
add("cold, colder, icy, sterile, clinical, glacial", "quality", "colder", opposite="warmer", hue=190,
    moves=(SCALE("cutoff", 1.4), SET("ensemble", 0)))
add("soft, softer, gentle, gentler, delicate, tender, quiet, quieter", "quality", "softer", opposite="punchier",
    hue=200, moves=(SCALE("level", 0.8), SCALE("attack", 1.5), MIN("attack", 15), SCALE("cutoff", 0.8)))
add("punchy, punchier, aggressive, hard, harder, harsh, harsher, biting, edgy, powerful, strong, stronger",
    "quality", "punchier", opposite="softer", hue=0,
    moves=(SET("attack", 1), SCALE("level", 1.15), SCALE("cutoff", 1.4), SCALE("resonance", 1.2)))
add("loud, louder, loudest, boost, boosted", "quality", "louder", opposite="quieter", moves=(SCALE("level", 1.3),))
add("hushed, quieter level", "quality", "quieter", opposite="louder", moves=(SCALE("level", 0.75),))
add("short, shorter, staccato, percussive, snappy, snappier, tight, tighter, clipped, choppy", "quality", "shorter",
    opposite="longer", moves=(SCALE("decay", 0.4), SET("sustain", 0.0), SCALE("release", 0.35),
                               SCALE("string_decay", 0.4)))
add("long, longer, sustained, sustaining, ringing, legato, lingering, endless, held", "quality", "longer",
    opposite="shorter", moves=(SET("sustain", 0.85), SCALE("release", 2.5), SCALE("decay", 2.0),
                                SCALE("string_decay", 2.0)))
add("slow attack, swell, swells, swelling, fade in, fades in, fading in, bowed, slow fade", "quality",
    "slow attack", opposite="fast attack", moves=(SET("attack", 800),))
add("fast attack, instant, immediate, quick attack, sharp attack", "quality", "fast attack",
    opposite="slow attack", moves=(SET("attack", 1),))
add("spacey, spacy, spacious, ambient, ethereal, dreamy, dreamlike, atmospheric, cinematic, cavernous, cathedral, "
    "vast, celestial, floaty, floating, heavenly, cosmic, galactic, space", "quality", "spacious",
    opposite="dry", hue=260,
    moves=(SET("reverb", 0.7), SET("reverb_size", 0.85), IFZERO("delay_time", 450), IFZERO("delay_filter", 2500),
           IFZERO("delay_feedback", 0.35), SET("delay_mix", 0.2)))
add("dry, drier, intimate, close up, upfront, in your face, dead", "quality", "dry", opposite="spacious",
    moves=(SET("reverb", 0.05), SET("reverb_size", 0.3), SET("delay_mix", 0.0)))
add("lofi, lo fi, dusty, vintage, tape, cassette, worn, nostalgic, old school, warped", "quality", "lo-fi", hue=35,
    moves={"chord": (SET("vib_amount", 0.06), EXACT("vib_freq", 0.6)) + VIBRATO_READY
                    + (SCALE("cutoff", 0.6), SET("crunch", 0.1), SET("noise", 0.02)),
           "harp": (SET("vib_amount", 0.06), EXACT("vib_freq", 0.6), SCALE("cutoff", 0.6), SET("crunch", 0.1))})
add("glitch, glitchy, random, chaotic, stuttering, stutter, sample and hold", "quality", "glitchy", home="chord",
    hue=300, moves={"chord": (EXACT("lfo_wave", 7), EXACT("lfo_freq", 8), SET("lfo_amount", 0.5), MIN("filter_sens", 1.0))})
add("wah, wobble, wobbles, wub, wubs, dubstep, filter sweep, sweeping, swirling, swirly", "quality",
    "filter wobble", home="chord", hue=120,
    moves={"chord": (EXACT("lfo_wave", 0), IFZERO("lfo_freq", 2), SET("lfo_amount", 0.6), MIN("filter_sens", 1.5))})
add("metallic, metal, clangy, clanging, steel, tinny", "quality", "metallic", hue=200,
    moves={"harp": (EXACT("string_damping", 0), SET("string_model", 70), SCALE("resonance", 1.4)),
           "chord": (SCALE("resonance", 1.4), EXACT("wave", 4))})
add("woody, wooden, organic, natural, earthy", "quality", "woody", hue=30,
    moves={"harp": (EXACT("string_damping", 70), SET("string_model", 60), SCALE("cutoff", 0.75)),
           "chord": (SCALE("cutoff", 0.75),)})
add("detuned, detune, out of tune, wonky, chorusy", "quality", "detuned", home="chord", hue=290,
    moves={"chord": (MIN("amp2", 0.1), EXACT("mult2", 1.01), SET("ensemble", 35)),
           "harp": (SET("vib_amount", 0.04), EXACT("vib_freq", 0.3))})
add("fat, fatter, thick, thicker, big, bigger, full, fuller, rich, richer, lush, lusher, huge, massive, layered",
    "quality", "fuller", opposite="thinner", home="chord", hue=270,
    moves={"chord": (MIN("amp2", 0.12), EXACT("mult2", 1.01), MIN("amp3", 0.08), EXACT("mult3", 0.5),
                     SET("ensemble", 45)),
           "harp": (SCALE("level", 1.1),)})
add("thin, thinner, small, smaller, tiny, weak, sparse", "quality", "thinner", opposite="fuller",
    moves={"chord": (EXACT("amp3", 0.0), SCALE("amp2", 0.3), SET("ensemble", 0)), "harp": (SCALE("level", 0.9),)})
add("pure, purer, sine, sine wave, sinewave", "quality", "pure", hue=180,
    moves={"chord": (EXACT("wave", 0), EXACT("wave2", 0), EXACT("wave3", 0), EXACT("crunch", 0), EXACT("noise", 0)),
           "harp": (EXACT("wave", 0), EXACT("crunch", 0))})
add("clean, cleaner", "quality", "clean", moves=(EXACT("crunch", 0), EXACT("noise", 0)))
add("saw, sawtooth, saws, buzzy, buzzing", "quality", "sawtooth", hue=350,
    moves={"chord": (EXACT("wave", 9), EXACT("wave2", 9)), "harp": (EXACT("wave", 9),)})
add("square, square wave, hollow, nasal", "quality", "square", hue=80,
    moves={"chord": (EXACT("wave", 11), EXACT("wave2", 11)), "harp": (EXACT("wave", 11),)})
add("triangle, triangle wave", "quality", "triangle", moves={"chord": (EXACT("wave", 3), EXACT("wave2", 3)),
                                                             "harp": (EXACT("wave", 3),)})
add("pulse, pulse wave, pwm", "quality", "pulse", moves={"chord": (EXACT("wave", 4), EXACT("wave2", 4)),
                                                         "harp": (EXACT("wave", 4),)})
add("airy, breathy, breath, whispery, windy", "quality", "breathy", home="chord", hue=170,
    moves={"chord": (SET("noise", 0.05), SCALE("cutoff", 1.2))})
add("noisy, hiss, hissy", "quality", "noisy", home="chord", moves={"chord": (SET("noise", 0.12),)})
add("sad, melancholy, melancholic, somber, sombre, moody, mournful, wistful, lonely", "quality", "melancholy",
    hue=230, moves=(SCALE("cutoff", 0.7), SET("reverb", 0.5), SCALE("attack", 1.5)))
add("happy, cheerful, upbeat, bouncy, playful, sunny, joyful, fun", "quality", "cheerful", hue=55,
    moves=(SCALE("cutoff", 1.3), SCALE("release", 0.7)))
add("eerie, creepy, haunting, haunted, spooky, ominous, mysterious, sinister, scary, dark ambient", "quality",
    "eerie", hue=280,
    moves={"chord": (SCALE("cutoff", 0.6), SET("reverb", 0.7), SET("reverb_size", 0.9), SET("vib_amount", 0.08),
                     EXACT("vib_freq", 0.4)) + VIBRATO_READY + (MIN("amp2", 0.1), EXACT("mult2", 1.01)),
           "harp": (SCALE("cutoff", 0.6), SET("reverb", 0.6), SET("vib_amount", 0.06), EXACT("vib_freq", 0.4))})
add("angry, heavy, brutal, gritty, grungy, nasty", "quality", "heavy", hue=0,
    moves=(SET("crunch", 0.4), EXACT("crunch_type", 1), SET("attack", 1), SCALE("cutoff", 1.3)))
add("calm, peaceful, relaxing, relaxed, chill, chilled, serene, soothing, meditative, zen, mellow out", "quality",
    "calm", hue=170, moves=(SCALE("cutoff", 0.8), SET("reverb", 0.5), SCALE("attack", 2.0), MIN("attack", 30)))
add("romantic, sweet, tender, lovely", "quality", "sweet", hue=330,
    moves=(SCALE("cutoff", 0.85), SET("reverb", 0.4)))
add("epic, majestic, heroic, grand, anthemic", "quality", "epic", hue=45,
    moves={"chord": (MIN("amp2", 0.12), EXACT("mult2", 1.01), MIN("amp3", 0.08), SET("ensemble", 45),
                     SET("reverb", 0.65), SET("reverb_size", 0.85)),
           "harp": (SET("reverb", 0.5),)})

# Effects: on at a typical amount, "lots of" for more, "a touch of" for less, "no" for off
add("reverb, reverbs, reverberation, reverby, verb, wet, wetter, hall, room, echoey room", "effect", "reverb",
    hue=240, moves=(SET("reverb", 0.6),))
add("delay, delays, echo, echoes, echoey, echoing, echoy, dub", "effect", "delay", hue=210,
    moves=DELAY_READY + (SET("delay_mix", 0.35),), off=(EXACT("delay_mix", 0),))
add("slapback, slap back", "effect", "slapback echo",
    moves=(EXACT("delay_time", 110), IFZERO("delay_filter", 3000), EXACT("delay_feedback", 0.1), SET("delay_mix", 0.3)),
    off=(EXACT("delay_mix", 0),))
add("distortion, distorted, crunch, crunchy, overdrive, overdriven, drive, driven, dirty, dirt, grit, saturated, "
    "saturation, grunge", "effect", "distortion", hue=10, moves=(SET("crunch", 0.5), EXACT("crunch_type", 1)),
    off=(EXACT("crunch", 0),))
add("fuzz, fuzzy, fuzzed", "effect", "fuzz", hue=5, moves=(SET("crunch", 0.6), EXACT("crunch_type", 2)),
    off=(EXACT("crunch", 0),))
add("tremolo, trem, pulsing, pulsating, throbbing", "effect", "tremolo", hue=320,
    moves=(SET("trem_amount", 0.5), IFZERO("trem_freq", 6)), off=(EXACT("trem_amount", 0),))
add("vibrato, wobbly, warble, warbly, wavering, seasick, wavy", "effect", "vibrato", hue=300,
    moves={"chord": (SET("vib_amount", 0.2),) + VIBRATO_READY,
           "harp": (SET("vib_amount", 0.2), IFZERO("vib_freq", 5), MIN("vib_depth", 0.1), MIN("vib_sustain", 1.0))},
    off={"chord": (EXACT("vib_amount", 0), EXACT("vib_depth", 0)), "harp": (EXACT("vib_amount", 0),)})
add("chorus, chorused, ensemble, shimmer, shimmery, shimmering, string machine, solina", "effect", "ensemble chorus",
    home="chord", hue=180, moves={"chord": (SET("ensemble", 50),)})
add("stereo, wide, wider, width, spread, spread out, panned, across the stereo field", "effect", "stereo spread",
    hue=170, moves={"harp": (SET("spread", 70),), "chord": (MIN("ensemble", 30),)},
    off={"harp": (EXACT("spread", 0),), "chord": (EXACT("ensemble", 0),)})
add("ping pong, ping pong strings, alternating sides", "setting", "strings ping-pong", home="harp",
    moves={"harp": (EXACT("spread_pattern", 1), MIN("spread", 60))})
add("glide, gliding, glides, portamento, slide, sliding, slidey", "effect", "glide", home="chord", hue=290,
    moves={"chord": (SET("glide", 300),)})
add("vocoder, robot, robotic, talk box, talkbox, talking chords, robot voice", "effect", "vocoder", home="chord",
    hue=100, moves={"chord": (SET("vocoder", 70),)},
    note="the vocoder needs a voice coming in over USB (usb audio set to 0 or 1 in minicontrol)")
add("sing, sings, singing, vowel, vowels, formant, formants, voice like, talking", "effect", "singing vowel",
    home="chord", hue=280, moves={"chord": (SET("formant", 70),)})
add("touch sensitive, touch sensitivity, velocity, velocity sensitive, dynamic, dynamics, expressive, responsive",
    "effect", "touch-sensitive harp", home="harp", moves={"harp": (SET("touch", 70), SET("strum", 60))})
add("pressure, aftertouch, pressure sensitive, squeeze, press harder", "effect", "touch pressure", home="harp",
    moves={"harp": (EXACT("pressure", 1), MIN("touch", 70))})
add("strum velocity, strum speed", "effect", "strum velocity", home="harp", moves={"harp": (SET("strum", 70),)})
add("palm mute, palm muting, mute with the palm", "effect", "palm mute", home="harp",
    moves={"harp": (EXACT("palm_mute", 5),)}, off={"harp": (EXACT("palm_mute", 0),)})
add("pluck on lift, lift to play, plays on lift, sounds on lift, on release", "effect", "pluck on lift", home="harp",
    moves={"harp": (EXACT("lift", 1),)}, off={"harp": (EXACT("lift", 0),)})
add("string model, physical model, physical modelling, physical modeling, karplus", "effect", "plucked string model",
    home="harp", moves={"harp": (SET("string_model", 80),)})

# Settings: exact choices
for words, label, value in (("pentatonic, major pentatonic", "pentatonic harp", 2),
                            ("minor pentatonic, blues, bluesy, blues scale", "minor pentatonic harp", 3),
                            ("major scale", "major scale harp", 1),
                            ("minor scale, natural minor", "minor scale harp", 5),
                            ("harmonic minor", "harmonic minor harp", 6),
                            ("diminished", "diminished harp", 4),
                            ("chord tones, follows the chord, follow the chord", "harp follows the chord", 0)):
    add(words, "setting", label, home="harp", moves={"harp": (EXACT("scale", value), EXACT("chromatic", 0))})
add("chromatic, chromatic harp", "setting", "chromatic harp", home="harp", moves={"harp": (EXACT("chromatic", 1),)},
    off={"harp": (EXACT("chromatic", 0),)})
add("jazz, jazzy, barry harris, sixth chords, 6th chords", "setting", "jazz chords (Barry Harris)", home="chord",
    hue=280, moves={"chord": (EXACT("barry", 1), MIN("voice_leading", 1))}, off={"chord": (EXACT("barry", 0),)})
add("voice leading, smooth voice leading, smooth chords, smooth chord changes", "setting", "voice leading",
    home="chord", moves={"chord": (EXACT("voice_leading", 1),)}, off={"chord": (EXACT("voice_leading", 0),)})
add("strict voice leading", "setting", "strict voice leading", home="chord",
    moves={"chord": (EXACT("voice_leading", 2),)}, off={"chord": (EXACT("voice_leading", 0),)})
add("suspended, sus, sus chords, sus4, sus2, extended chords, ninths, 9ths, ninth chords", "setting",
    "suspended and extended chords", home="chord", moves={"chord": (EXACT("layout", 1),)},
    off={"chord": (EXACT("layout", 0),)})
add("drop 2, drop two", "setting", "drop 2 voicing", home="chord", moves={"chord": (EXACT("spacing", 1),)})
add("drop 3, drop three", "setting", "drop 3 voicing", home="chord", moves={"chord": (EXACT("spacing", 2),)})
add("open voicing, open voicings, open chords, spread voicing, wide voicing", "setting", "open voicing",
    home="chord", moves={"chord": (EXACT("spacing", 3),)})
add("close voicing, closed voicing, close chords, tight voicing", "setting", "close voicing", home="chord",
    moves={"chord": (EXACT("spacing", 0),)})
for words, label, value in (("just intonation, just tuning, pure tuning", "just intonation", 2),
                            ("meantone, mean tone", "meantone tuning", 1), ("pythagorean", "Pythagorean tuning", 3),
                            ("werckmeister", "Werckmeister III tuning", 4), ("kirnberger", "Kirnberger III tuning", 5),
                            ("vallotti", "Vallotti tuning", 6), ("equal temperament, equal tuning", "equal temperament", 0),
                            ("quarter tones, quarter tone, quartertone, 24 edo", "quarter tones (24-EDO)", 11),
                            ("19 edo", "19-EDO", 10), ("31 edo", "31-EDO", 12)):
    add(words, "setting", label, home="global", moves=(EXACT("temperament", value),))
for words, label, n in (("octave up, up an octave, an octave up, octave higher, an octave higher, higher octave, "
                         "high pitched, higher pitched", "an octave up", 1),
                        ("two octaves up, up two octaves, two octaves higher", "two octaves up", 2),
                        ("octave down, down an octave, an octave down, octave lower, an octave lower, lower octave, "
                         "low pitched, lower pitched, deep, deeper", "an octave down", -1),
                        ("two octaves down, down two octaves, two octaves lower", "two octaves down", -2)):
    add(words, "setting", label, moves=(ADD("octave", n),))
for words, vowel in (("ah, ahh, aah", 0), ("eh", 25), ("ee, eee", 50), ("oh, ohh", 75), ("oo, ooh, ooo", 100)):
    add(words, "setting", f'"{words.split(",")[0]}" vowel', home="chord",
        moves={"chord": (EXACT("vowel", vowel), MIN("formant", 60))})

add("septimal chords, septimal, just chords, just sonorities, just intonation chords, septimal and just chords, "
    "xenharmonic chords, microtonal chords", "setting", "septimal and just alternate chords", home="chord",
    moves={"chord": (EXACT("alt_maj", 19), EXACT("alt_min", 20), EXACT("alt_7th", 22), EXACT("alt_maj7", 29),
                     EXACT("alt_min7", 24), EXACT("alt_majmin", 21), EXACT("alt_all", 27))},
    note="the alternate chords play in the alternate layout (chord layout 1); they're tuned for 19 and 31-EDO")

COLORS = {"red": 0, "orange": 30, "amber": 40, "gold": 50, "golden": 50, "yellow": 60, "lime": 90, "green": 120,
          "mint": 150, "teal": 175, "cyan": 185, "turquoise": 180, "aqua": 185, "blue": 225, "indigo": 250,
          "purple": 275, "violet": 280, "magenta": 300, "pink": 330, "rose": 340}

# ---- control phrases ----------------------------------------------------------------------------

CONTROLS = (   # (pattern, control), tried in order; a match is taken out of the clause
    (r"\bdouble ?tap(?:s)?(?: control| slot| pair| setting)? (?:number )?(?:1|2|3|one|two|three)\b|"
     r"\b(?:first|second|third|1st|2nd|3rd) double ?tap(?: control| slot| pair| setting)?\b", "tap_n"),
    (r"\balternate hover\b|\bsecond hover\b|\bhover alternate\b", "alt_hover"),
    (r"\b(?:alternate|alt|second function)s? (?:pots|knobs|potentiometers|dials|functions) (?:for|on|of) (?:the )?"
     r"chords? and (?:the )?harp\b|\bchord and harp (?:knob|pot)s?(?:'| )?alternates?\b", "two_knobs"),
    (r"\bdouble ?tap(?:s|ping|ped)?\b(?: (?:on )?(?:the )?modifier(?: button)?)?|\btap(?:ping)? twice\b|\btwo taps\b",
     "tap"),
    (r"\bhover(?:ing|s)?\b|\bhands? (?:held )?(?:over|above)(?: the (?:harp|plate|strings))?\b|"
     r"\bwav(?:e|ing) (?:a |my |your )?hands?\b|\bproximity\b", "hover"),
    (r"\b(?:(?:holding|hold|with|plus) )?(?:the )?modifier(?: held| button)?(?: and| plus| with)?(?: the)? "
     r"mod(?:ulation)? (?:knob|wheel|dial|potentiometer|pot)\b|"
     r"\b(?:shift(?:ed)?|alternate|alt|second function of(?: the)?) mod(?:ulation)? (?:knob|wheel|dial|potentiometer|pot)\b|"
     r"\bmod(?:ulation)? (?:knob|wheel|dial|potentiometer|pot) (?:alternate|alt|second|shifted)(?: function)?\b|"
     r"\bmod(?:ulation)? (?:knob|wheel|dial|potentiometer|pot) (?:with|holding|plus) (?:the )?modifier(?: held| button)?\b|"
     r"\b(?:alternate|alt|second function)(?: function)? (?:on|of|for) (?:the )?mod(?:ulation)? "
     r"(?:knob|wheel|dial|potentiometer|pot)\b",
     "mod_alt"),
    (r"\bmod(?:ulation)? (?:knob|wheel|dial|potentiometer|pot)\b|\bthird knob\b|\bright knob\b", "mod"),
    (r"\bchords? (?:knob|dial|pot)\b|\bleft knob\b|\bfirst knob\b", "chord_knob"),
    (r"\bharp (?:knob|dial|pot)\b|\bmiddle knob\b|\bsecond knob\b", "harp_knob"),
    (r"\b(?:the )?knob\b", "mod"),
)
KNOB_ADDRESSES = {"mod": (14, 15), "mod_alt": (16, 17), "chord_knob": (10, 11), "harp_knob": (12, 13)}
CONTROL_NAMES = {"mod": "mod knob", "mod_alt": "modifier + mod knob", "chord_knob": "modifier + chord knob",
                 "harp_knob": "modifier + harp knob", "hover": "hover", "tap": "double tap"}
TAP_PAIRS = ((200, 201), (209, 210), (211, 212))

# What a control can be given, by the words for it
TARGETS = {
    "cutoff": "overall filter, main filter, filter, filters, cutoff, cut off, brightness, brighter, darker, tone, low pass, lowpass, opens, closes",
    "resonance": "resonance, reso, squelch, peak",
    "reverb": "reverb, verb, wet, wetness, ambience, space, room",
    "reverb_size": "room size, reverb size, hall size, size",
    "delay_mix": "delay, echo, echoes",
    "delay_time": "delay time, echo time, delay length",
    "vib_amount": "vibrato, warble, wobbly",
    "trem_amount": "tremolo, pulsing",
    "crunch": "crunch, distortion, drive, overdrive, fuzz, grit, dirt",
    "glide": "glide, portamento, slide",
    "vowel": "vowel, vowels, mouth",
    "formant": "formant, formants, sing, singing, sings",
    "vocoder": "vocoder, robot, talk, talking",
    "attack": "attack, fade in",
    "release": "release, tail, ring out",
    "sustain": "sustain",
    "decay": "decay",
    "octave": "octave, octaves",
    "wave": "waveform, wave, waveshape, shape, oscillator",
    "string_model": "string model, pluckiness",
    "spread": "spread, stereo, width, panning",
    "ensemble": "ensemble, chorus, shimmer",
    "tempo": "tempo, bpm, speed",
    "level": "volume, level, loudness, mute, mutes, silence, silences",
    "lfo_amount": "wah, sweep, wobble, wub",
    "noise": "noise, breath, air",
    "transpose": "transpose, transposes, key",
    "looper": "looper, loop, loops, record, recording, recorder",
    "chromatic": "chromatic",
    "lift": "pluck on lift, lift",
    "scale": "scale, pentatonic, harp mode, scalar harp mode, scalar mode, harp scale, scale mode",
    "ribbon": "ribbon, ribbon mode, harp ribbon",
    "mpe": "mpe, mpe mode, mpe output, mde, mde mode",
    "midi_in": "midi in, midi in plays, midi input",
    "knob_midi": "knobs send midi, knob midi, midi knobs",
    "knob_layer": "knob layer, knob layers, layer, layers",
    "layout": "chord layout, alternate chord layout, alternate chords, alternate layout, alt layout, sus chords",
    "barry": "barry harris, jazz, jazzy, sixth chords",
    "voice_leading": "voice leading",
    "touch": "touch, velocity",
    "palm_mute": "palm mute",
}
# Where a knob sweeps each role, (low, high) in parameters.json units; its centre is stored in the preset
SWEEP = {"cutoff": {"chord": (150, 4000), "harp": (80, 2000)}, "resonance": (0.7, 4.5), "reverb": (0, 0.9),
         "reverb_size": (0.2, 1.0), "delay_mix": (0, 0.6), "delay_time": (50, 600), "vib_amount": (0, 0.5),
         "trem_amount": (0, 0.8), "crunch": (0, 0.7), "glide": (0, 800), "vowel": (0, 100), "formant": (0, 100),
         "vocoder": (0, 100), "attack": (1, 2000), "release": (100, 4000), "sustain": (0, 1), "decay": (50, 3000),
         "spread": (0, 100), "ensemble": (0, 100), "tempo": (60, 180), "level": (0.2, 1.8),
         "lfo_amount": (0, 1), "noise": (0, 0.3), "string_model": (0, 100), "transpose": (0, 12), "touch": (0, 100)}
# What a double tap or a hand over the plate takes each role to
TAP = {"resonance": 4.0, "reverb": 0.9, "reverb_size": 1.0, "delay_mix": 0.45, "delay_time": 500, "vib_amount": 0.3,
       "trem_amount": 0.6, "crunch": 0.6, "glide": 400, "vowel": 100, "formant": 90, "vocoder": 80, "attack": 1000,
       "release": 3500, "sustain": 1.0, "decay": 2000, "spread": 90, "ensemble": 80, "level": 1.6, "lfo_amount": 0.7,
       "noise": 0.15, "string_model": 100, "wave": 9, "looper": 7, "chromatic": 1, "lift": 1, "scale": 2, "barry": 1,
       "voice_leading": 1, "touch": 80, "palm_mute": 5, "transpose": 7, "tempo": 140, "ribbon": 1, "mpe": 1,
       "midi_in": 1, "knob_midi": 1, "knob_layer": 1, "layout": 1}
# Words for several settings at once, for a double tap with slots to fill
MULTI_TARGETS = {"midi options": ("mpe", "midi_in", "knob_midi"), "recent midi options": ("mpe", "midi_in", "knob_midi"),
                 "midi settings": ("mpe", "midi_in", "knob_midi"), "midi stuff": ("mpe", "midi_in", "knob_midi")}
# Settings that are on or off ("make sure ribbon mode is on"), and what on is
ON_OFF = {"ribbon": 245, "ribbon mode": 245, "harp ribbon": 245, "mpe": 110, "mpe mode": 110, "mpe output": 110,
          "mde": 110, "mde mode": 110, "midi in": 8, "midi in plays": 8, "midi input": 8, "knobs send midi": 238,
          "knob midi": 238, "knob layer": 117, "pluck on lift": 216, "chromatic mode": 98, "chromatic": 98,
          "scalar harp mode": 36, "scalar mode": 36, "harp mode": 36, "scale mode": 36, "single port mode": 108,
          "single port": 108, "note off on lift": 215, "harp note off on lift": 215, "barry harris mode": 33,
          "barry harris": 33, "voice leading": 111, "chord layout": 39, "alternate chord layout": 39,
          "alternate layout": 39, "alt layout": 39, "alternate chords": 39, "retrigger chords": 21,
          "palm mute": 213, "touch pressure": 253, "pressure": 253, "looper": None}
ON_VALUE = {213: 5}
# What has to be on for a control's setting to be heard
PREPARE = {"delay_mix": DELAY_READY, "delay_time": (MIN("delay_mix", 0.3), IFZERO("delay_filter", 3000),
                                                     IFZERO("delay_feedback", 0.4)),
           "vib_amount": VIBRATO_READY, "trem_amount": (IFZERO("trem_freq", 6),),
           "lfo_amount": (IFZERO("lfo_freq", 2), MIN("filter_sens", 1.0)), "vowel": (MIN("formant", 70),)}

# ---- words --------------------------------------------------------------------------------------

SECTION_WORDS = {"chord": {"chord", "chords", "buttons", "left hand", "chord section", "chord sound", "accompaniment"},
                 "harp": {"harp", "harps", "strings", "string", "strip", "plate", "right hand", "harp section",
                          "touch strip", "strum", "strums", "strumming", "melody"}}
STRONG = {"very", "really", "super", "extremely", "so", "lots", "lot", "loads", "tons", "heavy", "heavily", "huge",
          "massive", "much", "way", "intense", "intensely", "deeply", "plenty", "maximum", "max", "full"}
WEAK = {"slightly", "bit", "little", "subtle", "subtly", "touch", "hint", "lightly", "light", "somewhat",
        "mildly", "faint", "faintly", "tad", "gently", "kind", "kinda", "sort", "sorta"}
NEGATE = {"no", "not", "without", "zero", "remove", "removed", "off", "kill", "lose", "drop", "stop", "never",
          "isnt", "dont", "doesnt", "less"}
MORE = {"more", "extra", "increase", "increased", "add", "boost", "bigger"}
ACTION_VERBS = {"turns", "turn", "adds", "add", "makes", "make", "starts", "start", "opens", "open", "sweeps",
                "sweep", "controls", "control", "changes", "change", "brings", "bring", "gives", "give", "does",
                "do", "kicks", "switches", "switch", "toggles", "toggle", "sets", "set", "mutes", "mute"}
STOPWORDS = set("""a an the and or with of to in on for it its it's is are be been being am that this these those
i i'd i'm id im me my we our us you your want wants wanted would like likes please make makes making sound sounds
sounding preset patch some something kind sort so very really just also too as well should could can have has
had give gives use using used put then when while from by at into onto over under but all each every both one
two three four five plus set let lets let's get gets keep more much lot lots tons little bit slightly quite
pretty fairly rather somewhat extra super extremely way instead now again back still maybe thing things part
side section feel feeling vibe vibes type style ish mode button buttons knob turn turns turning goes go going
them they their there here what which who how if than out about around through across field between behind own
only same different new nice good great cool beautiful sounding sort of kinda sorta tone tones get got let's
yeah yes okay ok um uh hmm think thinking thought guess sure probably perhaps possibly
mean means anyway kind course whatever there's theres it'd isnt also all any some most my mine maybe recent
would'nt wouldnt want wanted wish hope going gonna wanna let lets please thanks thank sounds sound right left
know see try trying tried able unless otherwise case build built separate profile profiles based
temperament alternate setting settings options option actually basically something like really much bit amount level levels stuff start
starts starting play plays playing played sounds both everything whole overall little touch hint lot add adds
adding added need needs want hear heard should be will its whose whole than up down""".split())

_NUMBER_WORDS = {"one": "1", "two": "2", "three": "3", "four": "4", "five": "5", "six": "6", "seven": "7",
                 "eight": "8", "nine": "9", "ten": "10", "eleven": "11", "twelve": "12"}


def selectors():
    """Addresses a knob sweeps across their whole list rather than around their value"""
    found = set()
    try:
        with open(LOOKUP_H) as f:
            for m in re.finditer(r"\{\s*(\d+),\s*1,", f.read()):
                found.add(int(m.group(1)))
    except OSError:
        pass
    return found


def normalise(text):
    t = text.lower().replace("’", "'").replace("&", " and ").replace("+", " plus ")
    t = re.sub(r"(\d)\s*-\s*bit", r"\1 bit", t)
    t = re.sub(r"\b([a-g])\s*#", r"\1 sharp", t)
    t = re.sub(r"(?<=[a-z])-(?=[a-z])", " ", t)          # lo-fi, double-tap, touch-sensitive
    t = re.sub(r"\b(isn|don|doesn|won)'t\b", r"\1t", t)
    t = t.replace("'s ", " ").replace("'", "")
    return t


def tokens(text):
    return re.findall(r"[a-z0-9#=.]+", text)


@dataclass
class Result:
    changes: list = field(default_factory=list)     # [{"address", "value", "reason"}]
    understood: list = field(default_factory=list)  # what it did, line by line
    unknown: list = field(default_factory=list)     # words it didn't know
    notes: list = field(default_factory=list)
    base: str = None                                # a shared preset named as the starting point
    heard_as: list = field(default_factory=list)    # words taken as near ones it knows


class Interpreter:
    def __init__(self, params, preset_names=(), profile=False):
        self.params = params
        self.profile = profile
        self.selectors = selectors()
        self.phrases = {}
        for e in V:
            for w in e.words:
                self.phrases[tuple(tokens(normalise(w)))] = e
        self.by_label = {e.label: e for e in V}
        self.targets = {}
        for role, words in TARGETS.items():
            for w in words.split(","):
                self.targets[tuple(tokens(w.strip()))] = role
        self.section_phrases = {tuple(w.split()): s for s, ws in SECTION_WORDS.items() for w in ws}
        self.multi = {tuple(tokens(k)): v for k, v in MULTI_TARGETS.items()}
        self.on_off = {tuple(tokens(k)): v for k, v in ON_OFF.items() if v is not None}
        self.names = {}
        for a, p in params.items():
            if a in NOT_IN_PRESETS or p["group"] == "hidden" or 220 <= a <= 235 or p.get("follows_target") is not None \
                    or a in (10, 12, 14, 16, 200, 209, 211, 249):
                continue
            name = " ".join(tokens(normalise(p["name"])))
            group = " ".join(tokens(normalise(p["group"])))
            variants = [name, f"{group} {name}"]
            if p["section"] != "global":
                variants += [f"{p['section']} {name}", f"{p['section']}s {name}", f"{p['section']} {group} {name}"]
            for v in variants:
                self.names.setdefault(tuple(v.split()), set()).add(a)
        self.preset_names = {tuple(tokens(normalise(n))): n for n in preset_names}
        known = set()
        for d in (self.phrases, self.targets, self.section_phrases):
            for k in d:
                known.update(k)
        known.update(COLORS)
        known.update({"hover", "hovering", "double", "tap", "knob", "knobs", "modifier", "mod", "modulation",
                      "proximity", "twice", "bpm", "hertz", "transpose", "transposed", "minor", "major",
                      "sharp", "flat", "key", "semitones", "octaves"})
        known.update(STRONG | WEAK | NEGATE | MORE | ACTION_VERBS)
        for d in (self.multi, self.on_off, self.names):
            for k in d:
                known.update(k)
        known.update({"led", "leds", "attenuate", "attenuation", "distance", "reach", "type", "value", "slot",
                      "control", "edo", "semitone", "potentiometer", "potentiometers", "pots", "pot", "alternate"})
        self.known = known
        self.longest = max(len(k) for d in (self.phrases, self.targets, self.section_phrases, self.multi,
                                            self.on_off, self.names) for k in d)

    # ---- state ----

    def _addr(self, role, section):
        where = ROLES[role]
        if "global" in where:
            return where["global"]
        return where.get(section)

    def _get(self, a):
        return self.state[a]

    def _put(self, a, v, why):
        p = self.params[a]
        lo, hi = p["min_value"], p["max_value"]
        v = max(lo, min(hi, v))
        if p["data_type"] != "float":
            v = int(round(v))
        else:
            v = round(v, 2)
        self.state[a] = v
        self.why[a] = why
        self.touched.add(a)

    def _put_raw(self, a, v, why):
        self.state[a] = v
        self.why[a] = why
        self.touched.add(a)

    def _exact_kind(self, a):
        p = self.params[a]
        return p["data_type"] != "float" and (p["max_value"] - p["min_value"]) <= 30

    def _move(self, move, section, k, why):
        kind, role, v = move
        a = self._addr(role, section)
        if a is None or a not in self.state:
            return
        cur = self._get(a)
        if self.profile and kind in ("scale", "min", "max", "ifzero", "add"):
            if not why.startswith("for the"):
                self.skipped.add(why)
            return
        if kind == "exact" or (kind == "set" and (self._exact_kind(a) or self.profile)):
            new = v
        elif kind == "set":
            new = cur + (v - cur) * k if k <= 1 else v + (v - cur) * (k - 1) * 0.5
        elif kind == "scale":
            new = cur * (v ** k) if cur else (self.params[a]["max_value"] * 0.05 if v > 1 else 0)
        elif kind == "min":
            new = max(cur, v)
        elif kind == "max":
            new = min(cur, v)
        elif kind == "ifzero":
            new = v if cur == 0 else cur
        elif kind == "add":
            new = cur + v
        else:
            return
        self._put(a, new, why)

    def _moves_for(self, moves, section):
        if isinstance(moves, dict):
            return moves.get(section, ())
        return moves

    def _sections(self, section):
        return ("chord", "harp") if section in ("both", "global", None) else (section,)

    def _apply(self, entry, section, k, why, moves=None):
        moves = entry.moves if moves is None else moves
        done_global = set()
        for s in self._sections(section):
            for m in self._moves_for(moves, s):
                if "global" in ROLES[m[1]]:
                    if m[1] in done_global:
                        continue
                    done_global.add(m[1])
                self._move(m, s, k, why)

    # ---- one descriptor ----

    def describe(self, entry, section, strength, negate, more, less):
        key = (entry.label, section, negate, less)
        if key in self.applied:
            return
        self.applied.add(key)
        if isinstance(entry.moves, dict) and not any(entry.moves.get(s) for s in self._sections(section)):
            if entry.home in entry.moves:
                self.result.notes.append(f"{entry.label} is for the {'chords' if entry.home == 'chord' else entry.home}; "
                                         f"put it there")
                section = entry.home
        where = {"chord": "chords", "harp": "harp", "both": "both", "global": ""}.get(section, "")
        prefix = f"{where}: " if where and entry.home != "global" else ""
        if entry.kind == "quality":
            if negate or less:
                opp = self.by_label.get(entry.opposite)
                if opp is None:
                    self.result.notes.append(f'"less {entry.label}" has no opposite here; left alone')
                    return
                self._apply(opp, section, 0.5, f"less {entry.label}")
                self.result.understood.append(f"{prefix}a little {opp.label} (less {entry.label})")
                return
            k = {1: 1.0, 2: 1.6, 0: 0.5}[strength]
            self._apply(entry, section, k, entry.label)
            word = {1: "", 2: "very ", 0: "slightly "}[strength]
            self.result.understood.append(f"{prefix}{word}{entry.label}")
        elif entry.kind == "effect":
            if negate and not less:
                off = entry.off
                if off is None:
                    off = {s: tuple(EXACT(m[1], 0) for m in self._moves_for(entry.moves, s) if m[0] == "set")
                           for s in ("chord", "harp")}
                self._apply(entry, section, 1, f"no {entry.label}", moves=off)
                self.result.understood.append(f"{prefix}no {entry.label}")
                return
            if less:
                halves = {s: tuple(SCALE(m[1], 0.5) for m in self._moves_for(entry.moves, s) if m[0] == "set")
                          for s in ("chord", "harp")}
                self._apply(entry, section, 1, f"less {entry.label}", moves=halves)
                self.result.understood.append(f"{prefix}less {entry.label}")
                return
            k = {1: 1.0, 2: 1.5, 0: 0.5}[strength]
            moves = {}
            for s in ("chord", "harp"):
                out = []
                for m in self._moves_for(entry.moves, s):
                    if m[0] == "set":
                        a = self._addr(m[1], s)
                        cur = self._get(a) if a in self.state else 0
                        target = m[2] * k
                        out.append(EXACT(m[1], max(target, cur * 1.5) if more and cur >= target * 0.5 else target))
                    else:
                        out.append(m)
                moves[s] = tuple(out)
            self._apply(entry, section, 1, entry.label, moves=moves)
            word = "more " if more else {1: "", 2: "lots of ", 0: "a touch of "}[strength]
            self.result.understood.append(f"{prefix}{word}{entry.label}")
            if entry.note:
                self.result.notes.append(entry.note)
        else:   # sound, setting
            if negate:
                if entry.off is not None:
                    self._apply(entry, section, 1, f"no {entry.label}", moves=entry.off)
                    self.result.understood.append(f"{prefix}no {entry.label}")
                else:
                    self.result.notes.append(f'"not {entry.label}": nothing to undo there; left alone')
                return
            self._apply(entry, section, 1, entry.label)
            self.result.understood.append(f"{prefix}{entry.label}")
            if entry.note:
                self.result.notes.append(entry.note)
        if entry.hue is not None and self.hue is None:
            self.hue = entry.hue

    # ---- one control ----

    def assign(self, control, role, section, words):
        index = None
        if control.startswith("tap:"):
            index, control = int(control[4:]), "tap"
        names = CONTROL_NAMES[control]
        where = ROLES[role]
        if "global" in where:
            section = "global"
        elif section is None or section not in where:
            if control == "harp_knob" and "harp" in where:
                section = "harp"
            elif "chord" in where:
                section = "chord"
            else:
                section = next(iter(where))
        a = self._addr(role, section)
        p = self.params[a]
        label = self.setting_name(a)
        for m in PREPARE.get(role, ()):
            self._move(m, section, 1, f"for the {names}")
        if control in KNOB_ADDRESSES:
            ca, ra = KNOB_ADDRESSES[control]
            if role == "looper" or p["controls"] != "all":
                self.result.notes.append(f"a knob can't move {label}; try the double tap")
                return
            self._put(ca, a, f"{names} → {label}")
            if a in self.selectors or role not in SWEEP:
                self._put(ra, 100, names)
                choices = "on and off" if p["max_value"] - p["min_value"] == 1 else "through all its choices"
                self.result.understood.append(f"{names} → {label}, {choices}")
            elif self.profile:
                self._put(ra, 100, names)
                self.result.understood.append(f"{names} → {label}, from 0 to twice each bank's own setting")
            else:
                sweep = SWEEP[role]
                lo, hi = sweep[section] if isinstance(sweep, dict) else sweep
                centre = (lo + hi) / 2
                self._put(a, centre, f"centre of the {names} sweep")
                self._put(ra, round((hi - lo) / (hi + lo) * 100) if hi + lo else 100, names)
                self.result.understood.append(f"{names} → {label}, about {lo:g} to {hi:g}")
            return
        value = self._tap_value(role, a, words)
        if control == "hover":
            if role == "looper" or p["controls"] != "all":
                self.result.notes.append(f"hover can't move {label}; try the double tap")
                return
            self._put(249, a, f"hover → {label}")
            self._put_raw(250, value, "hover value")
            self.last_value = (250, a)
            self.result.understood.append(f"hover → {label}, toward {value:g} with a hand 2 cm over the plate")
            return
        # double tap: three slots, all flipped by the one double tap
        if index is None:
            index = self.next_tap
        if index >= len(TAP_PAIRS):
            self.result.notes.append(f"the double tap has only three slots; {label} was left out")
            return
        if self.taps_used == 0 and index == 0 and not self.profile and not ({"also", "too"} & set(words)):
            for c, v in TAP_PAIRS[1:]:
                if self.state.get(c):
                    self._put(c, 0, "double tap reset")
        ca, va = TAP_PAIRS[index]
        self.next_tap = index + 1
        self.taps_used += 1
        self._put(ca, a, f"double tap → {label}")
        self._put_raw(va, value, "double tap value")
        self.last_value = (va, a)
        slot = "" if index == 0 else f" (slot {index + 1})"
        if role == "looper":
            self.result.understood.append(f"double tap{slot} → the looper: record, play, stop, then a new recording")
        else:
            self.result.understood.append(f"double tap{slot} → {label} to {value:g}, and back on the next double tap")
        if index > 0 and not self.tap_note:
            self.tap_note = True
            self.result.notes.append("one double tap flips all three of its slots together")

    def setting_name(self, a):
        p = self.params[a]
        name, group = p["name"].strip(), p["group"]
        plain = {"Effects", "General", "Notes", "Oscillator", "Envelope", "Settings", "Potentiometer", "Device",
                 "Buttons", "Voicing", "Key and tuning", "MIDI", "Double tap", "hidden", "Rythm"}
        if group not in plain and group.lower().rstrip("s") not in name.lower():
            name = f"{group.lower()} {name}"
        if p["section"] == "global" or p["section"] in name.lower():
            return name
        return f"{p['section']} {name}"

    def _tap_value(self, role, a, words):
        p = self.params[a]
        cur = self._get(a)
        down = {"dark", "darker", "close", "closes", "muffle", "muffles", "muffled", "down", "lower", "less", "off",
                "mute", "mutes", "silence", "silences", "kill", "kills", "cut", "cuts"} & set(words)
        if role == "cutoff":
            lo, hi = SWEEP["cutoff"]["harp" if a == 49 else "chord"]
            if down:
                return lo * 2
            if {"bright", "brighter", "open", "opens", "up"} & set(words):
                return hi
            return lo * 2 if cur > (lo + hi) / 2 else hi
        if role == "level":
            return 0 if down else 1.6
        if role == "octave":
            return max(p["min_value"], cur - 1) if down else min(p["max_value"], cur + 1)
        if role in TAP:
            return 0 if down and role not in ("looper",) else TAP[role]
        return p["max_value"]

    # ---- a whole description ----

    def run(self, text, values, first=True):
        from preset_maker import to_human
        self.result = Result()
        self.state = {a: to_human(p, values[a]) for a, p in self.params.items()}
        start = dict(self.state)
        self.why = {}
        self.hue = None
        self.color = None
        self.taps_used = 0
        self.next_tap = 0
        self.tap_note = False
        self.applied = set()
        self.touched = set()
        self.skipped = set()
        self.pending = []
        self.void_next = False
        self.last_value = None
        for clause in self._clauses(normalise(text)):
            self._clause(clause)
        color = self.color if self.color is not None else (None if self.profile else self.hue)
        if color is not None and self.result.understood and (first or self.color is not None):
            self._put(20, color, "bank color")
        if self.profile:
            for a in sorted(self.touched):
                self.result.changes.append({"address": a, "value": self.state[a], "reason": self.why.get(a, "")})
            if self.skipped:
                self.result.notes.append("left out, since a profile sets the same value in every bank and these "
                                         "depend on each bank's own sound: " + ", ".join(sorted(self.skipped)))
        else:
            for a in sorted(self.state):
                if self.state[a] != start[a]:
                    self.result.changes.append({"address": a, "value": self.state[a], "reason": self.why.get(a, "")})
        return self.result

    def _clauses(self, text):
        parts = re.split(r"(?<!\d)\.|\.(?!\d)|[,;:!?\n]+|\bbut\b|\bwhile\b|\bwhereas\b|\bthen\b|\bexcept\b", text)
        out = []
        for part in parts:
            toks = tokens(part)
            if not toks:
                continue
            # "and" splits only between two parts that each name a section or a control
            pieces, cur = [], []
            sections = {w for ws in SECTION_WORDS.values() for w in ws}
            for j, t in enumerate(toks):
                nxt = toks[j + 1:j + 3]
                joined = (cur and cur[-1] == "on" and nxt[:1] == ["off"]) or \
                         (cur and cur[-1] in sections and (nxt[:1] and nxt[0] in sections or len(nxt) >= 2 and
                                                           nxt[0] == "the" and nxt[1] in sections))
                if t in ("and", "plus") and cur and not joined:
                    pieces.append(cur)
                    cur = []
                else:
                    cur.append(t)
            pieces.append(cur)
            merged = [pieces[0]]
            for piece in pieces[1:]:
                if self._anchored(piece) and self._anchored(merged[-1]):
                    merged.append(piece)
                else:
                    merged[-1] = merged[-1] + ["and"] + piece
            out.extend(m for m in merged if m)
        return out

    def _anchored(self, toks):
        s = " ".join(toks)
        if any(re.search(p, s) for p, _ in CONTROLS):
            return True
        return any(self._find(toks, i, self.section_phrases) for i in range(len(toks)))

    def _find(self, toks, i, table):
        for n in range(min(self.longest, len(toks) - i), 0, -1):
            key = tuple(toks[i:i + n])
            if key in table:
                return key, n
        return None

    def _clause(self, toks):
        r = self.result
        used = [False] * len(toks)
        s = " ".join(toks)
        offsets, pos = [], 0
        for t in toks:
            offsets.append(pos)
            pos += len(t) + 1

        def take_span(m):
            idx = [i for i, o in enumerate(offsets) if m.start() <= o < m.end()]
            for i in idx:
                used[i] = True
            return idx

        # numbers and keys
        for m in re.finditer(r"\b(\d{2,3}) ?(?:bpm|beats per minute)\b", s):
            self._put(187, int(m.group(1)), "tempo")
            r.understood.append(f"rhythm tempo {int(m.group(1))} bpm")
            take_span(m)
        for m in re.finditer(r"\b(?:a ?= ?)?(4[34]\d)(?:\.0)? ?(?:hz|hertz)\b|\btuned? to (4[34]\d)\b", s):
            hz = int(m.group(1) or m.group(2))
            if 432 <= hz <= 446:
                self._put(109, hz * 10, "tuning")
                r.understood.append(f"tuned to A = {hz} Hz")
                take_span(m)
        for m in re.finditer(r"\b(?:key of|in the key of|in)\s+([a-g])(?:\s+(sharp|flat))?(?:\s+(major|minor))\b|"
                             r"\b(?:key of|in the key of)\s+([a-g])(?:\s+(sharp|flat))?\b", s):
            root = m.group(1) or m.group(4)
            acc = m.group(2) or m.group(5)
            key = self._key(root, acc, m.group(3) == "minor")
            if key is not None:
                self._put(35, key, "key")
                r.understood.append(f"chords spelled for the key of {root.upper()}"
                                    f"{' ' + acc if acc else ''}{' minor' if m.group(3) == 'minor' else ''}")
                take_span(m)
        for m in re.finditer(r"\btranspose[ds]? (?:up )?(?:by )?(\d+|" + "|".join(_NUMBER_WORDS) + r")(?: semitones?)?", s):
            n = int(_NUMBER_WORDS.get(m.group(1), m.group(1)))
            self._put(30, n, "transpose")
            r.understood.append(f"transposed up {n} semitones")
            take_span(m)
        self._exact_settings(s, take_span)
        self._values(s, take_span)
        # a shared preset to start from
        for i in range(len(toks)):
            for key, name in self.preset_names.items():
                if tuple(toks[i:i + len(key)]) == key and not any(used[i:i + len(key)]):
                    cue = {"like", "from", "on", "preset", "base", "based", "using"} & set(toks[max(0, i - 3):i])
                    if len(key) >= 2 or cue:
                        r.base = name
                        for j in range(i, i + len(key)):
                            used[j] = True
        # controls
        found = []
        saw_alt_hover = False
        for pattern, control in CONTROLS:
            for m in re.finditer(pattern, s):
                idx = [i for i, o in enumerate(offsets) if m.start() <= o < m.end()]
                if not idx or any(used[i] for i in idx):
                    continue
                take_span(m)
                if control == "alt_hover":
                    note = "there's only one hover, so an alternate hover can't be set"
                    if note not in r.notes:
                        r.notes.append(note)
                    saw_alt_hover = True
                elif control == "two_knobs":
                    found += [(idx[0], "chord_knob"), (idx[0] + 0.5, "harp_knob")]
                elif control == "tap_n":
                    words = m.group(0).split()
                    n = next(({"1": 1, "one": 1, "first": 1, "1st": 1, "2": 2, "two": 2, "second": 2, "2nd": 2,
                               "3": 3, "three": 3, "third": 3, "3rd": 3}[w] for w in words
                              if w in ("1", "2", "3", "one", "two", "three", "first", "second", "third",
                                       "1st", "2nd", "3rd")), 1)
                    found.append((idx[0], f"tap:{n - 1}"))
                else:
                    found.append((idx[0], control))
        found.sort()
        controls = [c for _, c in found]
        rest = list(toks)
        while rest and rest[0] in ("and", "also", "then", "so", "maybe"):
            rest = rest[1:]
        follows = bool(rest) and (rest[0] in ACTION_VERBS or rest[0] == "to")
        carried = False
        if saw_alt_hover or (self.void_next and not controls and (follows or self._roles(list(toks), list(used)))):
            self.void_next = saw_alt_hover   # what the hover that doesn't exist would have done is dropped
            return
        self.void_next = False
        if not controls and follows and self.pending:
            controls, carried = self.pending, True
        if not controls:
            self._switches(toks, used)
            self._named_numbers(toks, used)
        # sections
        anchors = []
        i = 0
        while i < len(toks):
            hit = None if used[i] or self._covered(toks, i) else self._find(toks, i, self.section_phrases)
            if hit and not self._find(toks, i, self.phrases if hit[1] == 1 else {}) and \
                    not (controls and (self._find(toks, i, self.targets) or self._find(toks, i, self.multi))):
                key, n = hit
                anchors.append((i, self.section_phrases[key]))
                for j in range(i, i + n):
                    used[j] = True
                i += n
            else:
                i += 1
        if controls:
            self._assignments(toks, used, controls, anchors, carried)
            self._values(s, take_span)
            self._leftovers(toks, used, again=True)
            return
        self.pending = []
        # descriptors
        i = 0
        while i < len(toks):
            if used[i]:
                i += 1
                continue
            if toks[i] in COLORS:
                self.color = COLORS[toks[i]]
                r.understood.append(f"bank color {toks[i]}")
                used[i] = True
                i += 1
                continue
            hit = self._find(toks, i, self.phrases)
            if not hit:
                i += 1
                continue
            key, n = hit
            entry = self.phrases[key]
            window = [toks[j] for j in range(max(0, i - 3), i) if not used[j]]
            strength = 2 if STRONG & set(window) else (0 if WEAK & set(window) else 1)
            negate = bool(NEGATE & set(window)) or (i > 0 and toks[i - 1] == "no")
            less = "less" in window or "fewer" in window
            more = bool(MORE & set(window)) or key[-1].endswith("er") and entry.kind == "effect"
            for j in range(max(0, i - 3), i):
                if toks[j] in STRONG | WEAK | NEGATE | MORE:
                    used[j] = True
            section = self._section_for(i, anchors, entry)
            self.describe(entry, section, strength, negate, more, less)
            for j in range(i, i + n):
                used[j] = True
            i += n
        self._leftovers(toks, used)

    def _covered(self, toks, i):
        """Whether a vocabulary phrase starting before i runs over it ("septimal chords")"""
        for j in range(max(0, i - 3), i):
            hit = self._find(toks, j, self.phrases)
            if hit and j + hit[1] > i:
                return True
        return False

    def _roles(self, toks, used):
        roles = []
        i = 0
        while i < len(toks):
            hit = None
            if not used[i]:
                hit = self._find(toks, i, self.multi)
                if hit:
                    roles += [(i, r) for r in self.multi[hit[0]]]
                else:
                    hit = self._find(toks, i, self.targets)
                    if hit:
                        roles.append((i, self.targets[hit[0]]))
            if hit:
                for j in range(i, i + hit[1]):
                    used[j] = True
                i += hit[1]
            else:
                i += 1
        seen, out = set(), []
        for i, role in roles:
            if role not in seen:
                seen.add(role)
                out.append((i, role))
        return out

    def _assignments(self, toks, used, controls, anchors, carried=False):
        roles = self._roles(toks, used)
        if not roles:
            if not carried:
                self.pending = controls       # the next clause may say ("..., to turn pluck on lift on and off")
            self._leftovers(toks, used)
            return
        if len(controls) == 1 and controls[0].startswith("tap"):
            c = controls[0]
            start = int(c[4:]) if ":" in c else None
            pairs = [((f"tap:{start + k}" if start is not None else "tap"), r) for k, r in enumerate(roles)]
            self.pending = []
        elif len(controls) == 1:
            pairs = [(controls[0], roles[0])]
            self.pending = []
        else:
            pairs = list(zip(controls, roles))
            self.pending = controls[len(roles):]
        for control, (i, role) in pairs:
            section = None
            if anchors:
                section = min(anchors, key=lambda a: abs(a[0] - i))[1]
            self.assign(control, role, section, toks)
        for i, t in enumerate(toks):
            if t in ACTION_VERBS or t in STRONG | WEAK | NEGATE | MORE or t in ("also", "too", "on", "off"):
                used[i] = True
        self._leftovers(toks, used)

    def _exact_settings(self, s, take_span):
        r = self.result
        fill = r"(?:\s+(?:is|set|to|be|at|of|should|as|make|sure|it|now))*"
        for m in re.finditer(r"\b(?:attenuat\w*|dim\w*)\s+(?:all\s+)?(?:of\s+)?(?:the\s+)?(?:my\s+)?leds?\s+(?:to|at|by)\s+"
                             r"(\d*\.?\d+)|\bleds?\s+(attenuation|brightness)?" + fill + r"\s*(?:to|at|of)?\s*(\d*\.?\d+)"
                             r"|\bled\s+(attenuation|brightness)" + fill + r"\s+(\d*\.?\d+)", s):
            v = float(m.group(1) or m.group(3) or m.group(5))
            if v > 1:
                v /= 100
            if "brightness" in m.group(0):
                v = 1 - v
            self._put(32, v, "LED attenuation")
            r.understood.append(f"LED attenuation {self.state[32]:g} (brightness {1 - self.state[32]:.0%})")
            take_span(m)
        for m in re.finditer(r"\bhover(?:ing)?\s+(?:distance|reach|height|range)\b" + fill + r"\s+(\d+)"
                             r"(?:\s*(?:cm|centimet\w*))?", s):
            self._put(251, int(m.group(1)), "hover reach")
            r.understood.append(f"hover reach {self.state[251]} cm")
            if int(m.group(1)) != self.state[251]:
                r.notes.append(f"hover reach goes from 3 to 10 cm; set to {self.state[251]}")
            take_span(m)
        for m in re.finditer(r"\b(?:(chords?|harp)\s+)?(?:crunch|distortion)(?:\s+type)?" + fill +
                             r"\s+(?:type\s+)?(\d)(?![\d.])", s):
            n = int(m.group(2))
            if n > 2:
                r.notes.append(f"crunch has types 0, 1 and 2; took {n} as the third, 2")
                n = 2
            where = {"chord": (186,), "chords": (186,), "harp": (87,)}.get(m.group(1), (186, 87))
            for a in where:
                self._put(a, n, "crunch type")
            r.understood.append(f"{'chord' if where == (186,) else 'harp' if where == (87,) else 'chord and harp'} "
                                f"crunch type {n}")
            take_span(m)
    def _values(self, s, take_span):
        """"the value is 100", "hover value 0.8": in the units of the setting the hover or double tap moves"""
        r = self.result
        fill = r"(?:\s+(?:is|set|to|be|at|of|should|as|make|sure|it|now))*"
        for m in re.finditer(r"(?:(?:^|\band\s+)(?:also\s+|then\s+)?(?:the\s+)?|\b(hover|double ?tap)\s+)value\b" + fill +
                             r"\s+(\d*\.?\d+)", s):
            if m.group(1) and m.group(1).startswith("hover"):
                where = (250, self.state.get(249))
            else:
                where = self.last_value
            if not where or not where[1]:
                continue
            va, ta = where
            t = self.params[ta]
            n = float(m.group(2))
            if t["data_type"] == "float" and n > t["max_value"] and n / 100 <= t["max_value"]:
                n /= 100
            n = max(t["min_value"], min(t["max_value"], n))
            self._put_raw(va, n, "value")
            r.understood.append(f"{'hover' if va == 250 else 'double tap'} value {n:g} ({self.setting_name(ta)})")
            take_span(m)


    def _switches(self, toks, used):
        """Settings named as on or off: "make sure ribbon mode is on", "turn off MPE" """
        i = 0
        while i < len(toks):
            hit = None if used[i] else self._find(toks, i, self.on_off)
            if not hit:
                i += 1
                continue
            key, n = hit
            after, before = toks[i + n:i + n + 4], toks[max(0, i - 3):i]
            if {"off", "disabled"} & set(after) or "disable" in before or ("off" in before and "turn" in before):
                on = False
            elif {"on", "enabled"} & set(after) or "enable" in before or "on" in before:
                on = True
            else:
                i += 1
                continue
            a = self.on_off[key]
            self._put(a, ON_VALUE.get(a, 1) if on else 0, self.setting_name(a))
            extra = " (1, major)" if a == 36 and on else ""
            self.result.understood.append(f"{self.setting_name(a)} {'on' if on else 'off'}{extra}")
            for j in range(max(0, i - 3), min(len(toks), i + n + 4)):
                if j >= i and j < i + n or toks[j] in ("on", "off", "enable", "disable", "enabled", "disabled",
                                                       "turn", "switch"):
                    used[j] = True
            i += n

    def _named_numbers(self, toks, used):
        """Any setting by its name and a number: "ribbon glide 30", "set the chord attack to 200" """
        fillers = {"is", "set", "to", "be", "at", "of", "should", "as", "make", "sure", "="}
        i = 0
        while i < len(toks):
            hit = None if used[i] else self._find(toks, i, self.names)
            if not hit:
                i += 1
                continue
            key, n = hit
            k = i + n
            while k < len(toks) and toks[k] in fillers and k < i + n + 3:
                k += 1
            if k >= len(toks) or not re.fullmatch(r"\d*\.?\d+", toks[k]) or \
                    (k + 1 < len(toks) and toks[k + 1] in ("edo", "bpm", "hz", "cm", "hertz")):
                i += 1
                continue
            addrs = sorted(self.names[key])
            if len(addrs) > 1:   # a plain name ("chord attack") means the main one, not a filter's or vibrato's
                plain = [a for a in addrs if self.params[a]["group"] in
                         ("Envelope", "Oscillator", "Effects", "Notes", "General", "Buttons", "Voicing")]
                if plain and len({self.params[a]["section"] for a in plain}) == len(plain):
                    addrs = plain
            if len(addrs) > 1 and not (len(addrs) == 2 and
                                       {self.params[a]["section"] for a in addrs} == {"chord", "harp"} and
                                       self.params[addrs[0]]["name"] == self.params[addrs[1]]["name"]):
                self.result.notes.append(f'"{" ".join(key)}" could be: ' +
                                         ", ".join(self.setting_name(a) for a in addrs) + "; say which")
                i += n
                continue
            for a in addrs:
                p = self.params[a]
                v = float(toks[k])
                if p["data_type"] == "float" and v > p["max_value"] and v / 100 <= p["max_value"]:
                    v /= 100
                self._put(a, v, self.setting_name(a))
                self.result.understood.append(f"{self.setting_name(a)} {self.state[a]:g}")
                if not (p["min_value"] <= v <= p["max_value"]):
                    self.result.notes.append(f"{self.setting_name(a)} goes from {p['min_value']} to "
                                             f"{p['max_value']}; set to {self.state[a]:g}")
            for j in range(i, k + 1):
                used[j] = True
            i = k + 1

    def _section_for(self, i, anchors, entry):
        if entry.home == "global":
            return "global"
        near = [a for a in anchors if abs(a[0] - i) <= 4]
        if near:
            return min(near, key=lambda a: (abs(a[0] - i), -(a[0] > i)))[1]
        return entry.home

    def _key(self, root, accidental, minor):
        semis = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}[root]
        semis += {"sharp": 1, "flat": -1}.get(accidental, 0)
        names = {"c": 0, "g": 1, "d": 2, "a": 3, "e": 4, "b": 5, "f": 6, "bb": 7, "eb": 8, "ab": 9, "db": 10,
                 "gb": 11, "f#": 12, "c#": 13, "g#": 14, "d#": 15, "a#": 16}
        if minor:
            semis += 3
            by_semis = {0: "c", 1: "db", 2: "d", 3: "eb", 4: "e", 5: "f", 6: "gb", 7: "g", 8: "ab", 9: "a",
                        10: "bb", 11: "b"}
            return names[by_semis[semis % 12]]
        name = root + {"sharp": "#", "flat": "b"}.get(accidental, "")
        if name in names:
            return names[name]
        by_semis = {0: "c", 1: "db", 2: "d", 3: "eb", 4: "e", 5: "f", 6: "gb", 7: "g", 8: "ab", 9: "a", 10: "bb", 11: "b"}
        return names[by_semis[semis % 12]]

    def _leftovers(self, toks, used, again=False):
        if again:   # a second look, after values were read: take back what they used
            self.result.unknown = [w for w in self.result.unknown if w not in
                                   {t for i, t in enumerate(toks) if used[i]}]
            return
        for i, t in enumerate(toks):
            if used[i] or t in STOPWORDS or t in STRONG | WEAK | NEGATE | MORE or t.isdigit() or len(t) < 3:
                continue
            if t not in self.result.unknown:
                self.result.unknown.append(t)

    def fix_spelling(self, text):
        """Words close to ones it knows (a typo, or a mishearing) are taken as those, and reported"""
        out = []
        for t in re.findall(r"[A-Za-z][A-Za-z']*|[^A-Za-z]+", text):
            w = t.lower()
            if w.isalpha() and len(w) >= 5 and w not in self.known and w not in STOPWORDS:
                close = difflib.get_close_matches(w, [k for k in self.known if len(k) >= 4], n=1, cutoff=0.84)
                if close:
                    self.result_heard.append((w, close[0]))
                    out.append(close[0])
                    continue
            out.append(t)
        return "".join(out)


def interpret(text, values, params, preset_names=(), first=True, profile=False):
    it = Interpreter(params, preset_names, profile)
    it.result_heard = []
    fixed = it.fix_spelling(text)
    result = it.run(fixed, values, first)
    result.heard_as = it.result_heard
    return result


def print_vocabulary():
    kinds = {"sound": "Instruments and kinds of sound", "quality": "Qualities (with very, slightly, more, less, not)",
             "effect": "Effects (with lots of, a touch of, more, less, no)", "setting": "Settings"}
    for kind, title in kinds.items():
        print(f"{title}:")
        for e in V:
            if e.kind == kind:
                print(f"  {e.label}: {', '.join(e.words)}")
        print()
    print("Colours: " + ", ".join(COLORS))
    print("Also: 120 bpm, tuned to 432 hz, key of E flat, in A minor, transpose up 2")
    print("\nControls: the mod knob, the modifier with the mod knob, the chord knob, the harp knob, hover, double tap.")
    print("  e.g. \"the mod knob opens the filter\", \"double tap starts the looper\", \"hover makes the chords sing\"")
    print("  what they can take: " + "; ".join(f"{r}: {w}" for r, w in TARGETS.items()))
    print("\nName the chords or the harp to say which one a word is for (\"plucky harp\", \"dark chords\");")
    print("otherwise it goes where it usually belongs, or to both.")


def export_data(params, version):
    """Everything describe.js needs, from this file and the firmware: the one source for both"""
    def moves(m):
        if isinstance(m, dict):
            return {k: [list(x) for x in v] for k, v in m.items()}
        return [list(x) for x in m] if m is not None else None
    return {
        "firmware_version": version,
        "params": {str(a): {k: p.get(k) for k in ("name", "group", "section", "data_type", "min_value", "max_value",
                                                 "default_value", "controls", "follows_target",
                                                 "introduction_version")}
                   for a, p in params.items()},
        "selectors": sorted(selectors()),
        "vocabulary": [{"words": list(e.words), "kind": e.kind, "label": e.label, "moves": moves(e.moves),
                        "off": moves(e.off), "opposite": e.opposite, "home": e.home, "hue": e.hue, "note": e.note}
                       for e in V],
        "roles": ROLES, "controls": [list(c) for c in CONTROLS], "knob_addresses": KNOB_ADDRESSES,
        "control_names": CONTROL_NAMES, "tap_pairs": [list(p) for p in TAP_PAIRS], "targets": TARGETS,
        "multi_targets": {k: list(v) for k, v in MULTI_TARGETS.items()}, "on_off": ON_OFF,
        "on_value": {str(k): v for k, v in ON_VALUE.items()}, "sweep": SWEEP, "tap": TAP,
        "prepare": {k: [list(x) for x in v] for k, v in PREPARE.items()},
        "section_words": {k: sorted(v) for k, v in SECTION_WORDS.items()},
        "strong": sorted(STRONG), "weak": sorted(WEAK), "negate": sorted(NEGATE), "more": sorted(MORE),
        "action_verbs": sorted(ACTION_VERBS), "stopwords": sorted(STOPWORDS), "number_words": _NUMBER_WORDS,
        "colors": COLORS, "not_in_presets": sorted(NOT_IN_PRESETS),
    }
