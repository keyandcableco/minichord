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
import difflib, json, os, re, unicodedata
from dataclasses import dataclass, field

NOT_IN_PRESETS = {0, 1, 2, 3, 4, 5, 6, 7, 241, 242, 243, 244, 256, 382, 383, 384, 510, 511}
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
    "note1": {"chord": 131}, "note2": {"chord": 132}, "note3": {"chord": 133}, "note4": {"chord": 134},
    "voice1": {"chord": 270}, "voice2": {"chord": 271}, "voice3": {"chord": 272}, "voice4": {"chord": 273},
    "ribbon_span": {"harp": 246}, "ribbon_snap": {"harp": 247}, "ribbon_glide": {"harp": 248},
    "vib_attack": {"chord": 164, "harp": 65},
    "transient_wave": {"harp": 100}, "transient_attack": {"harp": 102}, "transient_decay": {"harp": 104},
    "out_freq": {"chord": 192, "harp": 88}, "out_res": {"chord": 193, "harp": 89}, "out_lp": {"chord": 194, "harp": 90},
    "out_bp": {"chord": 195, "harp": 91}, "out_hp": {"chord": 196, "harp": 92},
    "out_lfo_wave": {"harp": 93}, "out_lfo_freq": {"harp": 94}, "out_lfo_amount": {"harp": 95}, "out_lfo_sens": {"harp": 96},
    "harp_shuffle": {"harp": 40}, "inversion": {"chord": 37}, "roll": {"chord": 135}, "loose": {"chord": 136},
    "retrigger": {"chord": 21}, "held_strings": {"harp": 22}, "cantus": {"chord": 115}, "brothers": {"chord": 390},
    "pan": {"global": 29}, "reverb_hidamp": {"global": 25}, "reverb_lowpass": {"global": 27},
    "vocoder_carrier": {"global": 261}, "vocoder_consonants": {"global": 262}, "formant_size": {"chord": 239},
    "rhythm_length": {"global": 188}, "swing": {"global": 190}, "rhythm_note": {"global": 191},
    "rhythm_style": {"global": 274}, "rhythm_bass": {"global": 275}, "rhythm_chords": {"global": 276},
    "rhythm_follow": {"global": 277}, "rhythm_change": {"global": 278}, "rhythm_artic": {"global": 279},
    "rhythm_accents": {"global": 280}, "midi_clock": {"global": 266},
    # firmware 51-58
    "hammer_on": {"harp": 302}, "phone_beat": {"global": 311}, "kick_stab": {"global": 312},
    "kick_note": {"global": 313}, "style_b": {"global": 314}, "rhythm_fill": {"global": 315},
    "rhythm_break": {"global": 315}, "rhythm_ending": {"global": 315}, "fill_combo": {"global": 316},
    "rhythm_intro": {"global": 317}, "rhythm_end_on": {"global": 318}, "palm_break": {"global": 319},
    "fingered_glide": {"chord": 320}, "tap_hold": {"global": 321}, "revert_panic": {"global": 322},
    "revert": {"global": 323}, "panic": {"global": 323}, "press_again": {"chord": 324},
    # firmware 59-60: the DJ box and the harp's song; the drums
    "dj_filter": {"global": 327}, "dj_resonance": {"global": 328}, "phone_echo": {"global": 329},
    "echo_time": {"global": 330}, "echo_feedback": {"global": 331}, "tape_stop": {"global": 332}, "grab": {"global": 332},
    "phone_strum": {"global": 333}, "drums": {"global": 334}, "drum_groove": {"global": 335}, "drum_groove_b": {"global": 336},
    "drum_kit": {"global": 337}, "drum_level": {"global": 338}, "drum_humanize": {"global": 339}, "drum_midi": {"global": 340},
    "count_in": {"global": 341}, "drums_only": {"global": 342}, "harp_drums": {"global": 343},
    "loop_quantize": {"global": 283}, "loop_countin": {"global": 284}, "loop_click": {"global": 285},
    "click_level": {"global": 286}, "chord_memory": {"global": 287}, "looper_pc": {"global": 256},
    "loop_length": {"global": 288},
    **{f"rhythm{i}": {"global": 220 + i} for i in range(16)},
    "lfo_wave": {"chord": 152}, "lfo_freq": {"chord": 153}, "lfo_amount": {"chord": 154},
    "glide": {"chord": 199}, "ensemble": {"chord": 259},
    "spread": {"harp": 257}, "spread_pattern": {"harp": 258},
    "string_model": {"harp": 217}, "string_decay": {"harp": 218}, "string_damping": {"harp": 219},
    "transient": {"harp": 101},
    "vowel": {"chord": 118}, "formant": {"chord": 119}, "vocoder": {"global": 260},
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
    # firmware 49 and 63-68: koto press, the microtonal settings, the drone
    "koto": {"harp": 289}, "custom_scale": {"harp": 236}, "custom_down": {"harp": 352},
    "koto_steps": {"harp": 347}, "koto_snap": {"harp": 348}, "modifier_step": {"global": 349},
    "ribbon_step_snap": {"harp": 350}, "melodic_direction": {"harp": 351}, "maqam": {"harp": 356},
    "maqam_jins": {"harp": 357}, "purity": {"global": 358}, "mts": {"global": 371}, "drone": {"global": 372},
    "drone_level": {"global": 373}, "drone_pace": {"global": 374}, "drone_register": {"global": 375},
    "sharp_fn": {"global": 31},
    # firmware 69: hammer-on time, tabla laya, the drums' and the drone's reverb, speed ramp
    "hammer_time": {"harp": 376}, "tabla_laya": {"global": 377}, "drum_reverb": {"global": 378},
    "drone_reverb": {"global": 379}, "speed_ramp": {"global": 380},
    # firmware 74: the tanpura's jawari
    "drone_jawari": {"global": 381},
    # firmware 78: the reed piano's bark
    "reed_bark": {"chord": 387, "harp": 386},
    # firmware 79: the tonewheel organ
    "organ_drawbars": {"global": 388}, "organ_perc": {"global": 389},
    # the pitch bend envelope's start and decay, chord shuffling, the generator scale (firmware 38)
    "bend": {"chord": 170, "harp": 71}, "bend_decay": {"chord": 173, "harp": 74}, "chord_shuffle": {"chord": 120},
    "generator": {"harp": 267}, "generator_notes": {"harp": 268}, "generator_mode": {"harp": 269},
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
    base: str = None               # a shared preset to start from ("Twin Peaks" is Ben's Twin Green)
    chords: str = None             # a song: descriptions, in these same words, of its chords and harp
    harp: str = None
    both: str = None


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
# A pipe organ is still the synth's; the tonewheel organ (voice source 13, firmware 79) is a model of the Hammond kind
add("church organ, pipe organ, cathedral organ", "sound", "church organ", home="chord", hue=30, moves={
    "chord": synth_chord(0.15, 0, 1.0, 0.1, 0, 2.0, 0.08, 0, 0.5) + env(5, 100, 1.0, 80) + (EXACT("cutoff", 3500),),
    "harp": synth_harp(0) + env(5, 100, 1.0, 80)})
ORGAN_NOTE = ("the organ is a model of a tonewheel organ (voice 13): organ drawbars (388) picks the registration, organ "
              "percussion (389) the struck harmonic; its swell is the level (on hover or a knob) and its Leslie the tremolo "
              "and vibrato. Needs firmware 79")
ORGAN = (EXACT("voice", 13),) + env(5, 100, 1.0, 80) + (EXACT("cutoff", 5000), EXACT("string_model", 0))
add("organ, organs, hammond, hammond organ, b3, b-3, drawbar, drawbar organ, drawbars, tonewheel, tonewheel organ, "
    "combo organ", "sound", "organ", home="chord", hue=30, moves=ORGAN, note=ORGAN_NOTE)
for words, label, reg, perc in (
        ("jazz organ, blues organ, jimmy smith, organ trio, soul jazz organ", "jazz organ", 0, 2),
        ("rock organ, deep purple, hard rock organ", "rock organ", 1, 0),
        ("ballad organ, soft jazz organ, mellow organ", "ballad organ", 2, 0),
        ("flute organ, soft organ, gentle organ, pure organ", "flute organ", 3, 0),
        ("gospel organ, soul organ, church hammond, sunday organ", "gospel organ", 4, 0),
        ("shrill organ, hollow organ, screaming organ", "shrill organ", 5, 0),
        ("reedy organ, clarinet organ", "reedy organ", 6, 0),
        ("full organ, all drawbars, every drawbar, drawbars all out", "full organ", 7, 0)):
    add(words, "sound", label, home="chord", hue=30,
        moves=ORGAN + (EXACT("organ_drawbars", reg), EXACT("organ_perc", perc)), note=ORGAN_NOTE)
add("organ percussion, percussive organ, percussion on the organ, organ with percussion, organ click",
    "setting", "the organ's percussion (the third, fast)", home="global",
    moves=(EXACT("organ_perc", 2),), off=(EXACT("organ_perc", 0),), note=ORGAN_NOTE)
# The sampled electric piano, vibraphone, sitar and bass (voice source 6-9, firmware 73), as the piano is
add("electric piano, e piano, epiano, rhodes, ep, dx7, dx 7, fm piano", "sound", "electric piano", home="chord", hue=35,
    moves=(EXACT("voice", 9),) + env(2, 2500, 0.3, 600) + (EXACT("cutoff", 4000), EXACT("string_model", 0)),
    note="the sampled electric piano needs firmware 73")
# The reed piano (voice source 10, firmware 78): a model of the Wurlitzer, not samples; it barks when dug into
REED_NOTE = ("the reed piano is a model of the Wurlitzer 200A (voice 10), not samples: it barks as it is struck harder "
             "(harp reed bark 386 with touch velocity, chord reed bark 387); needs firmware 78")
add("wurlitzer, wurlitzers, wurly, wurlie, wurli, reed piano, reed electric piano, 200a, wurlitzer 200a",
    "sound", "Wurlitzer (reed piano)", home="chord", hue=30,
    moves=(EXACT("voice", 10),) + env(2, 2500, 0.3, 450) + (EXACT("cutoff", 5000), EXACT("string_model", 0)),
    note=REED_NOTE)
# "bark" alone stays the filter's (an envelope filter's quack, below); with the instrument named it is the reed's
add("barky wurlitzer, barking wurlitzer, wurlitzer barking, barky wurly, gritty wurlitzer, growling wurlitzer, "
    "dirty wurlitzer, wurlitzer dug into, wurlitzer played hard", "sound", "Wurlitzer, barking (struck hard)",
    home="chord", hue=30,
    moves=(EXACT("voice", 10), SET("reed_bark", 90)) + env(2, 2500, 0.3, 450) + (EXACT("cutoff", 5000), EXACT("string_model", 0)),
    off=(EXACT("reed_bark", 60),), note=REED_NOTE)
add("growl, growly, growling, dig in, dug in, digging in, played hard, struck hard, more bark, wurlitzer bark",
    "setting", "the reed piano barking (struck hard)", home="chord",
    moves=(SET("reed_bark", 90),), off=(EXACT("reed_bark", 60),), note=REED_NOTE)
add("mellow wurlitzer, soft wurlitzer, round wurlitzer, gentle wurlitzer, sweet wurlitzer, mellow wurly",
    "sound", "Wurlitzer, round (struck softly)", home="chord", hue=30,
    moves=(EXACT("voice", 10), SET("reed_bark", 25)) + env(2, 2500, 0.3, 450) + (EXACT("cutoff", 5000), EXACT("string_model", 0)),
    off=(EXACT("reed_bark", 60),), note=REED_NOTE)
add("no bark, without bark, less bark, struck softly, played softly", "setting", "the reed piano round (struck softly)",
    home="chord", moves=(SET("reed_bark", 25),), off=(EXACT("reed_bark", 60),), note=REED_NOTE)
add("piano, pianos, grand piano, acoustic piano, upright piano, keys", "sound", "piano", hue=40,
    moves=(EXACT("voice", 1),) + env(1, 2500, 0.3, 700) + (EXACT("cutoff", 4000), EXACT("string_model", 0)))
add("choir, choirs, voices, vocal, vocals, aahs, ahhs, oohs, singers", "sound", "choir", home="chord", hue=280,
    moves=(EXACT("voice", 3),) + env(250, 500, 0.9, 1200) + (EXACT("cutoff", 4000), EXACT("string_model", 0)))
add("string section, string quartet, string ensemble, orchestral strings, orchestra, orchestral, violin, violins, "
    "cello, cellos, viola, quartet, bowed strings, bowed string, strings section, real strings, sampled strings, "
    "violin strings, cello strings, string orchestra, chamber strings, legato strings", "sound", "string quartet", home="chord", hue=20,
    moves=(EXACT("voice", 4),) + env(200, 500, 0.9, 900) + (EXACT("cutoff", 4000), EXACT("string_model", 0)))
# The section's own oscillators as they're set, in place of a sampled voice; one at zero gets a level to be heard
add("synth, synths, synthy, synthesizer, synthesizers, synthesiser, synthesisers, oscillator, oscillators, own synth, "
    "its own synth, their own synth, no samples, not sampled", "sound", "synth", hue=200, moves={
        "chord": (EXACT("voice", 0), IFZERO("amp1", 0.15)), "harp": (EXACT("voice", 0), IFZERO("amp1", 0.15))})
add("pizzicato, pizz, plucked strings, pizzicato strings, pizz strings, pizzicato string, plucked string section",
    "sound", "pizzicato", home="harp", hue=15,
    moves=(EXACT("voice", 2),) + env(1, 600, 0.0, 400) + (EXACT("cutoff", 4000), EXACT("string_model", 0)))
add("guitar, guitars, acoustic guitar, nylon, folk, ukulele, uke, lute", "sound", "guitar", home="harp", hue=30, moves={
    "harp": synth_harp(0, 90) + env(1, 3000, 0.6, 900) + (EXACT("string_decay", 3.0), EXACT("string_damping", 40)),
    "chord": synth_chord(0.15, 9, 1.0) + env(1, 1200, 0.1, 500) + (EXACT("cutoff", 1800),)})
add("concert harp, celtic harp, real harp, lyre, zither", "sound", "concert harp", home="harp",
    hue=50, moves=synth_harp(0, 75) + env(1, 4000, 0.6, 1500)
    + (EXACT("string_decay", 4.0), EXACT("string_damping", 25)))
add("sitar, sitars, sitaar", "sound", "sitar", home="harp", hue=40,
    moves=(EXACT("voice", 8),) + env(1, 3500, 0.35, 1200) + (EXACT("cutoff", 5000), EXACT("string_model", 0)),
    note="the sampled sitar needs firmware 73")
add("upright bass, upright basses, double bass, double basses, acoustic bass, stand-up bass, standup bass, "
    "contrabass, bass fiddle, doghouse bass, string bass", "sound", "upright bass", home="chord", hue=25, moves={
    "chord": (EXACT("voice1", 15),),
    "harp": (EXACT("voice", 14),) + env(1, 1500, 0.0, 300) + (EXACT("cutoff", 4000), EXACT("string_model", 0))},
    note="the sampled upright bass (VSCO 2's double bass, plucked) needs firmware 80; on the chords it plays the bass "
         "note under the others")
add("bass guitar, bass guitars, electric bass, fingered bass, finger bass, slap bass", "sound", "bass guitar", home="chord", hue=25, moves={
    "chord": (EXACT("voice1", 8),),
    "harp": (EXACT("voice", 7),) + env(1, 2000, 0.4, 300) + (EXACT("cutoff", 4000), EXACT("string_model", 0))},
    note="the sampled bass needs firmware 73; on the chords it plays the bass note under the others")
add("banjo, mandolin, koto, shamisen, twang, twangy", "sound", "twangy strings", home="harp", hue=45,
    moves=synth_harp(0, 100) + env(1, 1500, 0.4, 600)
    + (EXACT("string_decay", 1.2), EXACT("string_damping", 70), SCALE("cutoff", 1.5)))
add("harpsichord, harpsichords, clavinet, clav", "sound", "harpsichord", home="harp", hue=45,
    moves={"harp": synth_harp(9, 60) + env(1, 900, 0.0, 300) + (EXACT("string_damping", 5), SCALE("cutoff", 1.5)),
           "chord": synth_chord(0.12, 9, 1.0, 0.06, 4, 2.0) + env(1, 900, 0.0, 300) + (SCALE("cutoff", 1.5),)})
add("bell, bells, chime, chimes, glass, glassy, glockenspiel, celesta, crystal, crystalline, bell like",
    "sound", "bells", home="harp", hue=190, moves={
        "harp": synth_harp(0) + env(1, 2000, 0.0, 2000) + (EXACT("transient", 0.15), SET("reverb", 0.4)),
        "chord": synth_chord(0.15, 0, 1.0, 0.06, 0, 2.0) + env(1, 2000, 0.0, 2000) + (SET("reverb", 0.4),)})
add("vibraphone, vibes, vibraphones", "sound", "vibraphone", home="harp", hue=200,
    moves=(EXACT("voice", 6),) + env(1, 3000, 0.25, 1500) + (EXACT("cutoff", 4000), EXACT("string_model", 0)),
    note="the sampled vibraphone needs firmware 73 (its motor's tremolo is in the recording)")
add("music box, musicbox", "sound", "music box", home="harp", hue=320,
    moves=synth_harp(0) + env(1, 1200, 0.0, 1200) + (EXACT("transient", 0.15), ADD("octave", 1)))
# Sampled from firmware 79 (voice source 11-12): the kalimba, a Tanzanian mbira, and the marimba, both VCSL (CC0)
add("kalimba, kalimbas, mbira, mbiras, thumb piano, sansa, likembe", "sound", "kalimba", home="harp", hue=35,
    moves=(EXACT("voice", 11),) + env(1, 1800, 0.0, 900) + (EXACT("cutoff", 5000), EXACT("string_model", 0)),
    note="the sampled kalimba needs firmware 79")
add("marimba, marimbas, mallet, mallets", "sound", "marimba", home="harp", hue=30,
    moves=(EXACT("voice", 12),) + env(1, 1500, 0.0, 700) + (EXACT("cutoff", 5000), EXACT("string_model", 0)),
    note="the sampled marimba needs firmware 79")
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
# The sampled accordion (voice source 15, firmware 80), FluidR3's, its musette reeds beating
add("accordion, accordions, squeezebox, squeeze box, musette, piano accordion, button accordion, bandoneon, concertina",
    "sound", "accordion", home="chord", hue=0,
    moves=(EXACT("voice", 15),) + env(30, 200, 1.0, 120) + (EXACT("cutoff", 5000), EXACT("string_model", 0)),
    note="the sampled accordion needs firmware 80 (its musette beating is in the recording)")
add("harmonium, melodica, reed organ, reedy", "sound", "reed organ",
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

def rhythm(steps, length=16, swing=None):
    """A rhythm-mode pattern: each step a sum of voices (1, 2, 4, 8 the chord's four from the bottom;
    16, 32, 64 three more, higher), two steps a beat"""
    steps = (list(steps) * 16)[:16]
    moves = tuple(EXACT(f"rhythm{i}", v) for i, v in enumerate(steps)) + (EXACT("rhythm_length", length),
                                                                         EXACT("rhythm_style", 0))
    return moves + ((EXACT("swing", swing),) if swing else ())


# One voice at a time: the chords play their lowest voice alone, as a mono synth does
MONO = (EXACT("note1", 0.6), EXACT("note2", 0), EXACT("note3", 0), EXACT("note4", 0))
POLY = (EXACT("note1", 0.5), EXACT("note2", 0.5), EXACT("note3", 0.5), EXACT("note4", 0.5))
add("monophonic, mono, mono synth, one note at a time, single note", "setting", "monophonic chords", home="chord",
    moves={"chord": MONO}, off={"chord": POLY},
    note="monophonic: the chords play their lowest voice alone; the harp stays polyphonic")
add("polyphonic, poly, full chords", "setting", "polyphonic chords", home="chord", moves={"chord": POLY})
add("bark, barky, barking, quack, quacky, envelope filter, auto wah, autowah, funky filter", "quality", "filter bark",
    hue=280, moves=(SET("resonance", 3.0), SET("filter_sens", 2.5), EXACT("fenv_attack", 1), EXACT("fenv_decay", 160),
                    EXACT("fenv_sustain", 0.1), SCALE("cutoff", 0.5)))

# Synths everyone knows, and songs: the nearest the minichord comes, as a starting point
ARP_BASS = synth_chord(0.15, 9, 1.0, 0.1, 2, 0.5) + MONO + env(1, 300, 0.6, 80) + (
    EXACT("octave", 1), EXACT("glide", 60), EXACT("cutoff", 250), EXACT("resonance", 3.0), EXACT("filter_sens", 2.5),
    EXACT("fenv_attack", 1), EXACT("fenv_decay", 180), EXACT("fenv_sustain", 0.15), EXACT("fenv_release", 100),
    EXACT("reverb", 0.05), EXACT("ensemble", 0), EXACT("crunch", 0.1))
CLAV_HARP = synth_harp(9, 60) + env(1, 500, 0.0, 120) + (
    EXACT("string_damping", 5), EXACT("cutoff", 900), EXACT("filter_sens", 2.0), EXACT("fenv_attack", 1),
    EXACT("fenv_decay", 150), EXACT("fenv_sustain", 0.0), EXACT("reverb", 0.1))
OBX_BRASS = synth_chord(0.1, 9, 1.0, 0.1, 9, 1.01, 0.08, 9, 0.5) + env(3, 600, 0.75, 250) + (
    EXACT("cutoff", 3200), EXACT("resonance", 1.0), EXACT("filter_sens", 0.8), EXACT("fenv_attack", 1),
    EXACT("fenv_decay", 300), EXACT("fenv_sustain", 0.6), EXACT("ensemble", 45), SET("reverb", 0.35), EXACT("crunch", 0))
CS80_PAD = synth_chord(0.12, 9, 1.0, 0.1, 9, 1.01, 0.06, 3, 0.5) + env(600, 1500, 0.9, 3500) + (
    EXACT("cutoff", 900), EXACT("resonance", 1.4), SET("vib_amount", 0.05), EXACT("vib_freq", 5)) + VIBRATO_READY + (
    EXACT("ensemble", 50), SET("reverb", 0.8), SET("reverb_size", 0.95))
MELLOTRON_FLUTE = synth_chord(0.15, 0, 1.0, 0.03, 3, 2.0, noise=0.04) + env(60, 300, 0.85, 300) + (
    SET("vib_amount", 0.05), EXACT("vib_freq", 0.6)) + VIBRATO_READY + (EXACT("cutoff", 1800),)
add("herbie hancock, chameleon, chameleon bass, head hunters, headhunters", "sound", "Herbie Hancock's Chameleon",
    hue=280, moves={"chord": ARP_BASS, "harp": CLAV_HARP},
    note="Chameleon: the chords are the ARP bass, one voice, an octave down with a quick filter bark; "
         "the harp is a clavinet-like pluck. Play the bass line on the chord buttons")
add("arp 2600, arp odyssey, odyssey, arp bass", "sound", "ARP bass", home="chord", hue=280,
    moves={"chord": ARP_BASS})
add("minimoog, mini moog, moog, moog bass, model d", "sound", "Minimoog", home="chord", hue=30, moves={
    "chord": synth_chord(0.12, 9, 1.0, 0.1, 9, 1.01, 0.08, 2, 0.5) + MONO + env(2, 400, 0.7, 120) + (
        EXACT("octave", 1), EXACT("glide", 40), EXACT("cutoff", 600), EXACT("resonance", 1.8), EXACT("filter_sens", 1.5),
        EXACT("fenv_attack", 2), EXACT("fenv_decay", 350), EXACT("fenv_sustain", 0.4), EXACT("ensemble", 0))})
add("ob xa, obxa, ob x, oberheim, jupiter 8, jupiter", "sound", "OB-Xa brass", home="chord", hue=0,
    moves={"chord": OBX_BRASS})
add("van halen, jump by van halen, van halen jump", "sound", "Van Halen's Jump", hue=0, moves={
        "chord": OBX_BRASS, "harp": synth_harp(9) + env(1, 400, 0.5, 300) + (EXACT("cutoff", 1500),)},
    note="Jump: big detuned brass chords; play the riff on the chord buttons")
add("africa, toto", "sound", "Toto's Africa", hue=30, moves={
    "harp": synth_harp(0, 20) + env(1, 900, 0.0, 900) + (EXACT("transient", 0.3), EXACT("string_damping", 50),
                                                         SET("reverb", 0.45)),
    "chord": synth_chord(0.12, 9, 1.0, 0.08, 3, 2.0) + env(120, 800, 0.8, 900) + (
        EXACT("cutoff", 900), EXACT("resonance", 1.2), EXACT("ensemble", 40), SET("reverb", 0.5))},
    note="Africa: the harp is the kalimba and marimba riff, the chords a soft brass pad")
add("superstition, stevie wonder", "sound", "Stevie Wonder's Superstition", hue=40, moves={
    "harp": CLAV_HARP + (SET("touch", 60),),
    "chord": synth_chord(0.12, 4, 1.0, 0.06, 9, 2.0) + env(1, 500, 0.2, 100) + (
        EXACT("cutoff", 1500), EXACT("filter_sens", 1.5), EXACT("fenv_attack", 1), EXACT("fenv_decay", 200),
        EXACT("fenv_sustain", 0.1), EXACT("reverb", 0.1))},
    note="Superstition: clavinet on both; tap the harp hard and soft for the funk")
add("final countdown, the final countdown", "sound", "Europe's The Final Countdown", hue=50, moves={
    "chord": synth_chord(0.12, 9, 1.0, 0.1, 9, 1.01, 0.06, 11, 0.5) + env(15, 500, 0.85, 600) + (
        EXACT("cutoff", 2400), EXACT("ensemble", 50), SET("reverb", 0.6), SET("reverb_size", 0.8)),
    "harp": synth_harp(9) + env(5, 300, 0.7, 400) + (EXACT("cutoff", 1600),)})
add("take on me, a ha", "sound", "a-ha's Take On Me", hue=200, moves={
    "harp": synth_harp(11) + env(1, 250, 0.3, 200) + (EXACT("cutoff", 2000), EXACT("resonance", 1.2),
                                                      EXACT("transient", 0)),
    "chord": synth_chord(0.1, 9, 1.0, 0.08, 11, 2.0) + env(5, 400, 0.7, 300) + (EXACT("cutoff", 3000),
                                                                               EXACT("ensemble", 30))},
    note="Take On Me: the harp is the bright riff synth; strum or tap it in the scale")
add("axel f, beverly hills cop, harold faltermeyer", "sound", "Axel F", hue=300, moves={
    "harp": synth_harp(11) + env(1, 200, 0.8, 120) + (EXACT("cutoff", 1500), EXACT("resonance", 1.5)),
    "chord": synth_chord(0.12, 11, 1.0, 0.06, 9, 0.5) + env(2, 300, 0.7, 150) + (EXACT("glide", 120),
                                                                               EXACT("cutoff", 1800))})
add("cs 80, cs80, yamaha cs 80", "sound", "CS-80 brass", home="chord", hue=220, moves={"chord": CS80_PAD})
add("blade runner, vangelis", "sound", "Vangelis's Blade Runner", hue=220,
    moves={"chord": CS80_PAD + (IFZERO("delay_time", 450), IFZERO("delay_filter", 2500), IFZERO("delay_feedback", 0.4),
                                SET("delay_mix", 0.2)),
           "harp": synth_harp(0) + env(1, 2500, 0.0, 2500) + (SET("reverb", 0.7),)},
    note="Blade Runner: slow, swelling brass with vibrato and a huge room; hold the chords")
add("strawberry fields, strawberry fields forever, mellotron, mellotron flute", "sound",
    "the Beatles' Strawberry Fields (Mellotron flute)", hue=330, moves={
        "chord": MELLOTRON_FLUTE,
        "harp": synth_harp(0) + env(40, 200, 0.8, 300) + (SET("vib_amount", 0.05), EXACT("vib_freq", 0.6))})
add("clint eastwood, gorillaz, omnichord, classic omnichord, om 84, om84, om 27, om27, om 36, om36", "sound",
    "classic Omnichord (Gorillaz's Clint Eastwood)", hue=120, moves={
        "harp": synth_harp(0) + env(1, 1500, 0.0, 1500) + (EXACT("transient", 0.1), SET("reverb", 0.35)),
        "chord": synth_chord(0.12, 3, 1.0, 0.06, 0, 2.0) + env(5, 300, 0.8, 250) + (EXACT("cutoff", 1400),
                                                                                    SET("reverb", 0.3))},
    note="Clint Eastwood's rhythm is the minichord's rhythm mode, turned on at the instrument")
add("baba o riley, baba oriley, baba o reilly, baba oreilly, teenage wasteland, the who", "sound",
    "the Who's Baba O'Riley", hue=60, moves={
        "chord": synth_chord(0.15, 0, 1.0, 0.1, 0, 2.0, 0.08, 0, 0.5) + env(5, 100, 1.0, 80) + (EXACT("cutoff", 3500),)
                 + rhythm([1, 4, 2, 8, 4, 16, 8, 4]),
        "harp": synth_harp(0) + env(1, 300, 0.0, 200) + (EXACT("transient", 0.2), EXACT("trem_wave", 2),
                                                         EXACT("trem_freq", 8), SET("trem_amount", 0.6))},
    note="Baba O'Riley: organ chords, and a harp chopped into repeats like the organ's marimba repeat; in rhythm "
         "mode the chords play the arpeggio")
add("sweet dreams, eurythmics", "sound", "Eurythmics' Sweet Dreams", hue=250, moves={
    "harp": synth_harp(9) + env(1, 220, 0.0, 150) + (ADD("octave", -1), EXACT("cutoff", 400), EXACT("resonance", 2.5),
                                                     EXACT("filter_sens", 2.5), EXACT("fenv_attack", 1),
                                                     EXACT("fenv_decay", 150), EXACT("fenv_sustain", 0)),
    "chord": synth_chord(0.1, 9, 1.0, 0.08, 9, 1.01) + env(300, 800, 0.8, 1500) + (EXACT("cutoff", 700),
                                                                                  SET("reverb", 0.5))},
    note="Sweet Dreams: the harp is the bass sequence, an octave down; the chords a dark pad")
add("stranger things", "sound", "the Stranger Things theme", hue=0, moves={
    "harp": synth_harp(9) + env(1, 350, 0.1, 400) + (EXACT("cutoff", 500), EXACT("resonance", 2.0),
                                                     EXACT("filter_sens", 2.0), EXACT("fenv_decay", 300),
                                                     EXACT("fenv_sustain", 0.1)) + DELAY_READY + (SET("delay_mix", 0.25),
                                                                                                 SET("reverb", 0.5)),
    "chord": synth_chord(0.1, 9, 1.0, 0.1, 9, 1.01, 0.06, 9, 0.5) + env(400, 1000, 0.8, 2500) + (
        EXACT("cutoff", 600), SET("reverb", 0.6), EXACT("ensemble", 30)) + rhythm([1, 2, 4, 8, 16, 8, 4, 2])},
    note="Stranger Things: the harp is the arpeggio; strum it slowly up and down, or turn on rhythm mode, which "
         "plays the chords as a rising arpeggio")
add("juno, juno 60, juno 106, juno60, juno106", "sound", "Juno", home="chord", hue=180, moves={
    "chord": synth_chord(0.12, 4, 1.0, 0.08, 9, 1.0) + (EXACT("ensemble", 70), EXACT("cutoff", 1500))})
add("prophet, prophet 5, prophet5", "sound", "Prophet-5", home="chord", hue=20, moves={
    "chord": synth_chord(0.12, 9, 1.0, 0.1, 4, 1.01) + env(80, 800, 0.8, 1200) + (EXACT("cutoff", 1200),
                                                                                  EXACT("resonance", 1.3))})
add("theremin, theremins", "sound", "theremin", hue=160, moves={
    "chord": synth_chord(0.15, 0, 1.0) + MONO + env(150, 300, 1.0, 400) + (EXACT("glide", 250), SET("vib_amount", 0.12),
                                                                         EXACT("vib_freq", 5)) + VIBRATO_READY,
    "harp": synth_harp(0) + env(100, 300, 1.0, 400) + (SET("vib_amount", 0.12), EXACT("vib_freq", 5),
                                                       EXACT("ribbon", 1), EXACT("ribbon_snap", 0), EXACT("ribbon_glide", 150))},
    note="theremin: the chords play one voice that glides, and the harp is a fretless ribbon: slide a finger along it")
add("harmonica, harmonicas, blues harp, mouth organ", "sound", "harmonica", hue=20, moves={
    "chord": synth_chord(0.12, 5, 1.0, 0.08, 4, 1.0) + env(20, 200, 0.9, 100) + (SET("vib_amount", 0.06),
                                                                              EXACT("vib_freq", 5)) + VIBRATO_READY,
    "harp": synth_harp(5) + env(15, 200, 0.9, 100) + (SET("vib_amount", 0.06), EXACT("vib_freq", 5))})
# The instruments of the folk, synth and ambient bank sets (minicontrol presets/, 2026-10-12), each the
# nearest a sentence comes to the preset: the gesture it was built on comes with the sound (pluck on
# lift, the koto press as a noter or a pressed rim, pressure as a volume pedal or a bow)
MAJOR_PENTATONIC, MIXOLYDIAN, KURD = 661, 1717, 1451   # custom scale bits: 0 2 4 7 9; 0 2 4 5 7 9 10; 0 1 3 5 7 8 10
add("autoharp, auto harp, autoharps, chromaharp", "sound", "autoharp", hue=35,
    moves={"harp": synth_harp(0, 100) + env(1, 3500, 0.6, 600) + (
               EXACT("string_decay", 3.5), EXACT("string_damping", 45), EXACT("scale", 0), EXACT("chromatic", 0),
               EXACT("lift", 1), EXACT("ribbon", 0), EXACT("touch", 60), EXACT("strum", 60), EXACT("palm_mute", 7)),
           "chord": synth_chord(0.14, 3, 1.0, 0.06, 9, 2.0, 0.05, 0, 0.5) + env(1, 1400, 0.3, 350) + (
               EXACT("retrigger", 1), EXACT("cutoff", 1600))},
    note="autoharp: every string plays the chord held, as its chord bars leave only those, and sounds as the "
         "finger lifts; the Folk & Americana set's bank 1")
add("dulcimer, mountain dulcimer, appalachian dulcimer, lap dulcimer, noter", "sound", "mountain dulcimer",
    hue=90,
    moves={"harp": synth_harp(0, 100) + env(1, 3000, 0.6, 600) + (
               EXACT("string_decay", 3.2), EXACT("string_damping", 35), EXACT("scale", 10),
               EXACT("custom_scale", MIXOLYDIAN), EXACT("chromatic", 0), EXACT("lift", 1), EXACT("koto", 2),
               EXACT("koto_snap", 70), EXACT("ribbon", 0), EXACT("palm_mute", 6)),
           "chord": synth_chord(0.14, 3, 1.0, 0.07, 0, 0.5, 0.05, 9, 1.0) + env(2, 2500, 0.8, 800) + (
               EXACT("cutoff", 1400), EXACT("note2", 0))},
    note="dulcimer: the melody string in the mixolydian mode of the DAD tuning, plucked on lift; press a ringing "
         "string to slide it up a fret, the noter (koto press, firmware 64); the chords its drone strings")
add("bluegrass, bluegrass banjo, five string banjo, 5 string banjo, banjo rolls", "sound", "bluegrass banjo",
    hue=50,
    moves={"harp": synth_harp(0, 100) + env(1, 1500, 0.3, 250) + (
               EXACT("string_decay", 1.1), EXACT("string_damping", 60), EXACT("transient", 0.22), ADD("octave", 1),
               EXACT("hammer_on", 1), EXACT("hammer_time", 60), EXACT("lift", 0), EXACT("strum", 75),
               EXACT("touch", 55)),
           "chord": synth_chord(0.14, 9, 1.0, 0.08, 3, 2.0, 0.04, 0, 1.0) + env(1, 900, 0.2, 200) + (
               EXACT("retrigger", 1), EXACT("cutoff", 1800), EXACT("voice1", 15), EXACT("rhythm_style", 3),
               EXACT("tempo", 136))},
    note="bluegrass: a popping banjo an octave up with hammer-ons quick enough for its rolls, a flat-picked guitar "
         "and the sampled upright bass (firmware 80) in boom-chick")
add("pedal steel, pedal steel guitar, steel guitar, lap steel, slide guitar, bottleneck", "sound", "pedal steel",
    hue=195,
    moves={"harp": synth_harp(3) + env(15, 1200, 0.85, 500) + (
               EXACT("cutoff", 1200), EXACT("ribbon", 1), EXACT("ribbon_snap", 60), EXACT("ribbon_glide", 110),
               EXACT("ribbon_span", 12), SET("vib_amount", 0.03), EXACT("vib_freq", 4.8), EXACT("vib_attack", 500),
               EXACT("touch", 60), EXACT("pressure", 1), EXACT("lift", 0)),
           "chord": synth_chord(0.12, 3, 1.0, 0.07, 0, 2.0, 0.04, 9, 1.0) + env(40, 2500, 0.8, 900) + (
               EXACT("retrigger", 0), EXACT("cutoff", 1800), EXACT("glide", 220), EXACT("fingered_glide", 1),
               SET("vib_amount", 0.012), EXACT("vib_freq", 5)) + VIBRATO_READY},
    note="pedal steel: the harp is the bar on a ribbon, louder as the finger presses (the volume pedal); roll from "
         "one chord onto the next and the chord slides there, the pedals. Needs firmware 54")
add("honky tonk piano, honky-tonk piano, honkytonk piano, honky tonk, honkytonk, honky-tonk, tack piano, saloon piano, barroom piano, barrelhouse piano, barrelhouse", "sound", "honky-tonk piano",
    hue=10,
    moves={"chord": (EXACT("voice", 1),) + env(1, 3500, 0.9, 300) + (
               EXACT("cutoff", 3000), EXACT("string_model", 0), EXACT("ensemble", 45), EXACT("voice1", 15)),
           "harp": (EXACT("voice", 1),) + env(1, 3500, 0.9, 500) + (
               EXACT("cutoff", 4000), EXACT("string_model", 0), EXACT("touch", 80))},
    note="honky-tonk: the sampled piano a little out of tune with itself (the ensemble), the sampled upright bass "
         "under it (firmware 80); say boogie for the left hand")
add("washboard, washboard band, jug band, skiffle, spoons", "sound", "washboard band", home="global", hue=40,
    moves=(EXACT("harp_drums", 1), EXACT("drum_kit", 5), IFZERO("drum_level", 65)),
    note="washboard band: the harp's twelve strings are the brushes kit's pads (a suitcase kick at the bottom, "
         "the washboard's snare and knocks, the spoons); the chords stay as they are. Needs firmware 60")
add("cajun, zydeco, cajun accordion, zydeco accordion", "sound", "Cajun accordion", hue=330,
    moves={"chord": (EXACT("voice", 15),) + env(12, 300, 0.95, 120) + (
               EXACT("retrigger", 1), EXACT("cutoff", 4000), EXACT("string_model", 0)),
           "harp": (EXACT("voice", 4),) + env(50, 1500, 0.9, 350) + (
               EXACT("cutoff", 4000), EXACT("string_model", 0), EXACT("touch", 60), EXACT("pressure", 1),
               EXACT("hammer_on", 1), EXACT("hammer_time", 80), EXACT("lift", 0))},
    note="Cajun: the sampled accordion on the chords, its reeds beating against each other (firmware 80), and the sampled strings as "
         "a fiddle on the harp, swelling as the finger presses and slurring as one lands beside a held note")
add("synthwave, synth wave, retrowave, outrun, eighties pad, 80s pad", "sound", "synthwave pad",
    hue=290,
    moves={"chord": synth_chord(0.10, 9, 1.0, 0.09, 9, 1.0, 0.06, 4, 0.5) + env(300, 1500, 0.85, 1800) + (
               EXACT("cutoff", 500), EXACT("resonance", 1.4), EXACT("filter_sens", 1.0), EXACT("fenv_attack", 600),
               EXACT("fenv_decay", 2000), EXACT("fenv_sustain", 0.5), EXACT("ensemble", 70), SET("reverb", 0.45)),
           "harp": synth_harp(11) + env(1, 900, 0.0, 700) + (
               EXACT("cutoff", 1300), EXACT("filter_sens", 1.0), EXACT("fenv_attack", 1), EXACT("fenv_decay", 400),
               EXACT("fenv_sustain", 0.3)) + DELAY_READY + (SET("delay_mix", 0.25), EXACT("spread", 70),
                                                            EXACT("spread_pattern", 1))},
    note="synthwave: a chorused saw pad that opens as its filter envelope swells, a square pluck ping-ponging in "
         "an echo; say hover opens the filter for the hand")
add("synth brass, synthbrass, brass stabs, eighties brass, 80s brass, stab brass", "sound", "synth brass",
    home="chord", hue=45,
    moves={"chord": synth_chord(0.11, 9, 1.0, 0.10, 9, 1.0, 0.05, 4, 0.5) + env(8, 500, 0.7, 180) + (
               EXACT("retrigger", 1), EXACT("cutoff", 900), EXACT("resonance", 1.3), EXACT("filter_sens", 1.5),
               EXACT("fenv_attack", 1), EXACT("fenv_decay", 320), EXACT("fenv_sustain", 0.45), EXACT("ensemble", 35),
               EXACT("bend", 0.94), EXACT("bend_decay", 60), MIN("vib_depth", 1.0)),
           "harp": synth_harp(9) + env(3, 800, 0.8, 150) + (
               EXACT("cutoff", 1200), EXACT("filter_sens", 1.2), EXACT("fenv_attack", 1), EXACT("fenv_decay", 300),
               EXACT("fenv_sustain", 0.5), SET("vib_amount", 0.02), EXACT("vib_freq", 5.5), EXACT("vib_attack", 350))},
    note="synth brass: a bite on every press and a scoop up to the pitch (the pitch bend envelope starting a little "
         "under); for the stabs on a song's kicks say the kick stabs the chords")
add("italo, italo disco, italodisco, eurodisco, euro disco, hi nrg, hi-nrg", "sound", "Italo disco",
    hue=320,
    moves={"chord": synth_chord(0.11, 9, 1.0, 0.09, 4, 1.0, 0.06, 9, 2.0) + env(2, 400, 0.5, 200) + (
               EXACT("retrigger", 1), EXACT("cutoff", 1500), EXACT("filter_sens", 1.0), EXACT("fenv_attack", 1),
               EXACT("fenv_decay", 300), EXACT("fenv_sustain", 0.5), EXACT("ensemble", 50), EXACT("rhythm_style", 9),
               EXACT("tempo", 124), EXACT("drums", 1), EXACT("drum_groove", 6), EXACT("drum_kit", 2)),
           "harp": synth_harp(3) + env(1, 700, 0.0, 400) + (EXACT("transient", 0.2), ADD("octave", 1)) + DELAY_READY
                   + (SET("delay_mix", 0.22), EXACT("spread", 60), EXACT("spread_pattern", 1))},
    note="Italo disco: a bright stab arpeggiating in sixteenths (rhythm mode) over a 909 four-on-the-floor at 124, a "
         "bell pluck echoing on the harp. The drums need firmware 60")
add("berlin school, tangerine dream, klaus schulze, berlin sequence, sequencer line", "sound", "Berlin school sequence",
    hue=140,
    moves={"chord": synth_chord(0.14, 9, 1.0, 0.0, 0, 2.0, 0.06, 11, 2.0) + env(1, 250, 0.3, 120) + (
               EXACT("retrigger", 1), EXACT("cutoff", 600), EXACT("resonance", 2.0), EXACT("filter_sens", 1.8),
               EXACT("fenv_attack", 1), EXACT("fenv_decay", 200), EXACT("fenv_sustain", 0.3), EXACT("chord_shuffle", 1))
               + rhythm([1, 2, 4, 8, 1, 2, 16, 8, 1, 2, 4, 8, 1, 32, 4, 2]) + (
               EXACT("tempo", 122), EXACT("rhythm_note", 110), IFZERO("delay_time", 246), IFZERO("delay_filter", 2500),
               IFZERO("delay_feedback", 0.3), SET("delay_mix", 0.2)),
           "harp": synth_harp(9) + env(5, 1200, 0.8, 300) + (
               EXACT("cutoff", 1000), EXACT("resonance", 1.4), EXACT("filter_sens", 1.0), EXACT("fenv_attack", 1),
               EXACT("fenv_decay", 600), EXACT("fenv_sustain", 0.5), EXACT("hammer_on", 1), EXACT("hammer_time", 90),
               EXACT("lift", 0)) + DELAY_READY + (SET("delay_mix", 0.3),)},
    note="Berlin school: tap hold and the chord buttons run a two-bar resonant sequence through the chord's notes and "
         "the extras an octave up, into an echo; a lead with hammer-on slides over it")
add("trance gate, trance gated, gated pad, chopped pad, gated synth, gate the pad, gate the chords", "sound",
    "trance gate", home="chord", hue=180,
    moves={"chord": synth_chord(0.10, 9, 1.0, 0.09, 9, 1.0, 0.05, 9, 2.0) + env(5, 1500, 0.9, 150) + (
               EXACT("cutoff", 2500), EXACT("ensemble", 85), EXACT("trem_wave", 2), EXACT("trem_freq", 9.0),
               SET("trem_amount", 0.9), SET("speed_ramp", 15))},
    note="trance gate: a square-wave tremolo chopping a supersaw into sixteenths (9 Hz is sixteenths at 135 bpm); "
         "the speed ramp (firmware 69) slews a change of rate, so a double tap to 18 Hz doubles it smoothly")
add("power ballad, eighties ballad, 80s ballad, gated reverb, gated drums, gated snare, big snare", "sound",
    "power ballad", hue=350,
    moves={"chord": synth_chord(0.09, 9, 1.0, 0.08, 0, 2.0, 0.06, 4, 0.5) + env(150, 2000, 0.85, 1500) + (
               EXACT("cutoff", 1200), EXACT("ensemble", 55), SET("reverb", 0.5), EXACT("rhythm_style", 11),
               EXACT("tempo", 72), EXACT("drums", 1), EXACT("drum_groove", 4), EXACT("drum_kit", 1),
               SET("drum_reverb", 70)),
           "harp": (EXACT("voice", 9),) + env(1, 4000, 1.0, 600) + (
               EXACT("cutoff", 4000), EXACT("string_model", 0), SET("reverb", 0.5), EXACT("touch", 80))},
    note="power ballad: a wide pad, the sampled electric piano in a hall (firmware 73), and a slow 808 ballad with "
         "the drums deep in the reverb, the gated room of the eighties (firmware 69)")
add("glass harmonica, glass harp, glass armonica, armonica, wine glasses, wineglasses, singing glass, singing glasses",
    "sound", "glass harmonica", hue=185,
    moves={"harp": synth_harp(0, 30) + env(120, 5000, 0.6, 2500) + (
               EXACT("string_decay", 7.0), EXACT("string_damping", 10), SET("reverb", 0.55), ADD("octave", 1),
               EXACT("lift", 1), EXACT("koto", 1), EXACT("koto_snap", 0), EXACT("ribbon", 0), EXACT("palm_mute", 6)),
           "chord": synth_chord(0.09, 0, 1.0, 0.07, 0, 2.0, 0.04, 3, 0.5) + env(900, 2000, 0.95, 2500) + (
               EXACT("cutoff", 2500), SET("trem_amount", 0.15), EXACT("trem_freq", 0.3), SET("reverb", 0.6),
               EXACT("ensemble", 15), SET("purity", 80))},
    note="glass harmonica: glasses that sing as the fingers lift, bending up a semitone when a ringing one is pressed "
         "harder (koto press); a bowed glass pad on the chords, tuned pure")
add("singing bowl, singing bowls, tibetan bowl, tibetan bowls, sound bowl, sound bowls, temple bowls, crystal bowls, "
    "sound bath", "sound", "singing bowls", hue=40,
    moves={"harp": synth_harp(0, 15) + env(1, 5000, 0.0, 4000) + (
               EXACT("string_decay", 8.0), EXACT("string_damping", 8), EXACT("transient", 0.1),
               SET("trem_amount", 0.15), EXACT("trem_freq", 3.0), SET("reverb", 0.5), EXACT("spread", 80),
               EXACT("spread_pattern", 1), EXACT("lift", 1), EXACT("koto", 2), EXACT("koto_snap", 0),
               EXACT("ribbon", 0), EXACT("scale", 10), EXACT("custom_scale", MAJOR_PENTATONIC), EXACT("chromatic", 0),
               EXACT("palm_mute", 6)),
           "chord": synth_chord(0.10, 0, 1.0, 0.06, 0, 2.0, 0.03, 3, 0.5) + env(5, 5000, 0.0, 5000) + (
               EXACT("retrigger", 1), EXACT("cutoff", 1500), EXACT("note2", 0), SET("trem_amount", 0.25),
               EXACT("trem_freq", 2.2), SET("reverb", 0.6), EXACT("drone", 4), EXACT("drone_level", 70),
               EXACT("drone_pace", 20), EXACT("drone_register", 0), SET("drone_reverb", 50),
               EXACT("drone_jawari", 20))},
    note="singing bowls: small bowls struck as the fingers lift and bent by pressing the rim, big bowls humming for "
         "seconds on the chord buttons, and the drone (firmware 68) as the great bowl's hum, low and smooth")
add("orwell, orwell 9, orwell nine, orwell scale, xenharmonic, xenharmonic scale", "sound", "Orwell[9] in 31 tones",
    hue=95,
    moves={"harp": synth_harp(0, 60) + env(1, 3500, 0.5, 2000) + (
               EXACT("string_decay", 4.5), EXACT("string_damping", 30), EXACT("temperament", 12), EXACT("scale", 12),
               EXACT("generator", 7), EXACT("generator_notes", 9), EXACT("generator_mode", 4), EXACT("chromatic", 0),
               EXACT("lift", 1), EXACT("ribbon", 0)) + DELAY_READY + (SET("delay_mix", 0.25),),
           "chord": synth_chord(0.09, 3, 1.0, 0.07, 0, 2.0, 0.05, 9, 0.5) + env(700, 2000, 0.9, 2200) + (
               EXACT("cutoff", 1500), EXACT("ensemble", 40), SET("reverb", 0.55), EXACT("temperament", 12))},
    note="Orwell[9]: nine notes of 31-tone equal temperament stacked from a small third (7 steps, 271 cents), the "
         "generator scale (firmware 38); the mod knob on generator mode turns it through its modes")
add("mohajira, neutral thirds, neutral third scale, neutral scale", "setting", "Mohajira (neutral thirds) in 31 tones",
    home="harp", moves=(EXACT("temperament", 12), EXACT("scale", 12), EXACT("generator", 9), EXACT("generator_notes", 7),
                        EXACT("generator_mode", 0), EXACT("chromatic", 0)),
    note="Mohajira: seven notes stacked from a neutral third (9 steps of 31), neither major nor minor")
add("whale, whales, whale song, whalesong, humpback, humpback whale", "sound", "whale song", hue=220,
    moves={"harp": synth_harp(3) + env(200, 1500, 0.9, 900) + (
               EXACT("cutoff", 800), EXACT("resonance", 1.4), EXACT("filter_sens", 0.6), EXACT("fenv_attack", 400),
               EXACT("fenv_decay", 1200), EXACT("fenv_sustain", 0.6), EXACT("ribbon", 1), EXACT("ribbon_snap", 0),
               EXACT("ribbon_glide", 250), EXACT("ribbon_span", 12), ADD("octave", -1), EXACT("touch", 60),
               EXACT("pressure", 1), EXACT("lift", 0), IFZERO("delay_time", 600), IFZERO("delay_filter", 2000),
               IFZERO("delay_feedback", 0.5), SET("delay_mix", 0.4), SET("reverb", 0.6), EXACT("spread", 70),
               EXACT("spread_pattern", 1)),
           "chord": synth_chord(0.14, 0, 0.5, 0.06, 3, 1.0, 0.03, 0, 2.0, noise=0.02) + env(1500, 2000, 0.9, 3000) + (
               EXACT("cutoff", 500), EXACT("note2", 0), EXACT("lfo_wave", 0), EXACT("lfo_freq", 0.08),
               EXACT("lfo_amount", 0.2), SET("reverb", 0.65), ADD("octave", -1), EXACT("glide", 800),
               EXACT("fingered_glide", 1))},
    note="whale: a low voice on the ribbon with the slowest glide, swelling under pressure into a long echo; the "
         "chords an ocean, a drone pad that slowly swells and slides from chord to chord")
add("handpan, hand pan, handpans, hang drum, pantam", "sound", "handpan", hue=120,
    moves={"harp": synth_harp(0, 40) + env(1, 4000, 0.0, 2500) + (
               EXACT("string_decay", 5.0), EXACT("string_damping", 20), EXACT("transient", 0.12),
               SET("trem_amount", 0.08), EXACT("trem_freq", 2.5), SET("reverb", 0.5), EXACT("spread", 50),
               EXACT("spread_pattern", 1), EXACT("touch", 85), EXACT("strum", 50), EXACT("palm_mute", 4),
               EXACT("scale", 10), EXACT("custom_scale", KURD), EXACT("chromatic", 0), EXACT("lift", 0)),
           "chord": synth_chord(0.12, 0, 1.0, 0.07, 0, 2.0, 0.04, 3, 0.5) + env(2, 3500, 0.0, 2500) + (
               EXACT("retrigger", 1), EXACT("cutoff", 1500), EXACT("note2", 0), SET("trem_amount", 0.12),
               EXACT("trem_freq", 2.8), SET("reverb", 0.5), ADD("octave", -1))},
    note="handpan: the strings as tone fields in the Kurd scale, ringing long with a slow shimmer, as loud as they're "
         "struck, damped by the flat hand; the chord buttons the ding and the low note under it")
add("steel drum, steel drums, steel pan, steelpan, steel band", "sound", "steel drum", home="harp", hue=50, moves={
    "harp": synth_harp(3) + env(1, 700, 0.0, 600) + (EXACT("transient", 0.25), EXACT("cutoff", 1500)),
    "chord": synth_chord(0.12, 3, 1.0, 0.05, 0, 2.0) + env(1, 700, 0.0, 600)})
add("toy piano, toy pianos, kids piano", "sound", "toy piano", home="harp", hue=330, moves={
    "harp": synth_harp(0) + env(1, 500, 0.0, 400) + (EXACT("transient", 0.3), ADD("octave", 1)),
    "chord": synth_chord(0.12, 0, 1.0, 0.06, 3, 2.0) + env(1, 500, 0.0, 400) + (ADD("octave", 1),)})
add("calliope, carousel, circus organ, fairground organ, merry go round", "sound", "calliope", hue=50, moves={
    "chord": synth_chord(0.15, 0, 1.0, 0.1, 0, 2.0, 0.06, 3, 0.5) + env(5, 100, 1.0, 80) + (
        EXACT("cutoff", 4000), SET("vib_amount", 0.1), EXACT("vib_freq", 6)) + VIBRATO_READY,
    "harp": synth_harp(0) + env(5, 100, 1.0, 80) + (SET("vib_amount", 0.1), EXACT("vib_freq", 6), ADD("octave", 1))})
add("stylophone", "sound", "Stylophone", hue=90, moves={
    "chord": synth_chord(0.12, 11, 1.0) + MONO + env(1, 100, 1.0, 30) + (EXACT("reverb", 0.05), EXACT("cutoff", 3000)),
    "harp": synth_harp(11) + env(1, 100, 1.0, 30) + (EXACT("reverb", 0.05),)})
add("synth strings, string synth, strings synth", "sound", "synth strings", home="chord", hue=200, moves={
    "chord": synth_chord(0.1, 9, 1.0, 0.08, 9, 1.01) + env(250, 800, 0.85, 1200) + (EXACT("ensemble", 70),
                                                                                    EXACT("cutoff", 1600)),
    "harp": synth_harp(9) + env(150, 600, 0.8, 1000)})
add("orchestra hit, orchestra hits, orch hit, orchestral hit", "sound", "orchestra hit", home="chord", hue=10, moves={
    "chord": (EXACT("voice", 4),) + env(1, 300, 0.0, 300) + (EXACT("cutoff", 4000), SET("crunch", 0.1), SET("reverb", 0.4)),
    "harp": (EXACT("voice", 4),) + env(1, 300, 0.0, 300) + (EXACT("cutoff", 4000),)})
RHYTHM_NOTE = "rhythm patterns play in rhythm mode, which is turned on at the instrument"
# Written accompaniment parts that follow the held chord (firmware 42, rhythm style 274); 0 is the 16-step
# pattern (220-235), which the two that have no style of their own still write
RHYTHM_STYLE_WORDS = (
        ("alberti bass, alberti, classical left hand, mozart left hand", "Alberti bass", (EXACT("rhythm_style", 1),)),
        ("waltz, oom pah pah, three four, 3 4 time, three four time, 3 4", "waltz", (EXACT("rhythm_style", 2),)),
        ("boom chick, two beat, country rhythm, country beat, johnny cash beat, oom pah, march, two step",
         "boom-chick", (EXACT("rhythm_style", 3),)),
        ("walking bass, swing bass, jazz rhythm, jazz walking bass, walking bass line", "walking bass",
         (EXACT("rhythm_style", 4),)),
        ("boogie, boogie woogie, rock and roll, rock n roll, rock and roll rhythm, chuck berry rhythm, "
         "shuffle rhythm guitar", "boogie", (EXACT("rhythm_style", 5),)),
        ("travis picking, fingerpicking, finger picking, fingerpicked, fingerstyle, finger style", "Travis picking",
         (EXACT("rhythm_style", 6),)),
        ("strummed guitar, strummed guitar rhythm, campfire, campfire strum, down up strum, strumming pattern",
         "strummed guitar", (EXACT("rhythm_style", 7),)),
        ("bossa, bossa nova, samba", "bossa nova", (EXACT("rhythm_style", 8),)),
        ("arpeggio, arpeggios, arpeggiated, arpeggiator, arp up, rolling arpeggio, arpeggio up", "arpeggio up",
         (EXACT("rhythm_style", 9),)),
        ("arpeggio up and down, arp up and down, up and down arpeggio, up down arpeggio", "arpeggio up and down",
         (EXACT("rhythm_style", 10),)),
        ("ballad, ballad rhythm, pop piano, piano pulse", "ballad", (EXACT("rhythm_style", 11),)),
        ("offbeat, offbeats, reggae, skank, ska", "reggae offbeat", (EXACT("rhythm_style", 12),)),
        ("six eight, 6 8, 6 8 time, six eight time, compound time, 6 8 arpeggio", "6/8 arpeggio",
         (EXACT("rhythm_style", 13),)),
        ("habanera, tango, carmen rhythm", "habanera", (EXACT("rhythm_style", 14),)),
        ("arpeggio down, arp down, falling arpeggio", "arpeggio down", rhythm([8, 4, 2, 1])),
        ("straight eighths, eighths, pulsing chords, driving eighths, rock eighths", "straight eighths", rhythm([15])))
for words, label, moves in RHYTHM_STYLE_WORDS:
    add(words, "setting", f"{label} rhythm", home="global", moves=moves, off=(EXACT("rhythm_style", 0),),
        note=RHYTHM_NOTE)
# Rhythm style B (firmware 53, 314): the style a fill switches to and back from, "a verse in Travis picking and a
# chorus strummed". It must count the bar as style A does
SECTIONS_NOTE = ("fills switch between the two styles: the double tap on rhythm fill, or the modifier held with a tap "
                 "of hold; style B must have style A's beats to the bar")
for words, label, moves in RHYTHM_STYLE_WORDS:
    style = next((m[2] for m in moves if m[1] == "rhythm_style"), None)
    if not style:
        continue
    names = [w.strip() for w in words.split(",")][:3]
    phrases = [f"{form.format(n)}" for n in names for form in
               ("style b {}", "b section {}", "b style {}", "{} for the chorus", "{} in the chorus", "chorus in {}",
                "fills switch to {}", "fill into {}", "{} as style b", "second style {}")]
    add(", ".join(phrases), "setting", f"style B {label}", home="global", moves=(EXACT("style_b", style),),
        note=SECTIONS_NOTE)
# How the style plays (firmware 42): its bass and chord parts, whether it follows the hands, when a chord
# change lands, how long and how accented its notes are
for words, label, role, value in (
        ("no bass, without bass, without the bass, no bass line, no bass part, bass rests", "no bass in the rhythm",
         "rhythm_bass", 2),
        ("held bass, pedal bass, drone bass, pedal tone, bass held", "a held bass", "rhythm_bass", 1),
        ("held chords over the bass, chords held over the bass, bass under held chords, bass line under held chords, "
         "hold the chords, pad over the bass, walking bass under a pad", "chords held over the bass", "rhythm_chords", 1),
        ("just the bass, bass line alone, bass alone, no chords, without chords, only the bass", "the bass alone",
         "rhythm_chords", 2),
        ("only while i hold a chord, only while i hold, only while holding, only when i hold a chord, "
         "stops when i let go, stop when i let go",
         "rhythm only while a chord is held", "rhythm_follow", 1),
        ("sync start, starts when i press, start when i press, starts with the chord, omnichord style, "
         "omnichord sync", "rhythm starts with the press", "rhythm_follow", 2),
        ("change chords on the beat, chord changes on the beat, on the beat", "chord changes on the beat",
         "rhythm_change", 1),
        ("on the bar, on the downbeat, quantized to the bar, chord changes on the bar, change chords on the bar",
         "chord changes on the bar", "rhythm_change", 2),
        ("staccato rhythm, detached rhythm, short rhythm notes, detached", "short rhythm notes", "rhythm_artic", 50),
        ("legato rhythm, smooth rhythm, connected rhythm, connected", "long rhythm notes", "rhythm_artic", 150),
        ("even rhythm, flat dynamics, no accents, unaccented, even dynamics", "even accents", "rhythm_accents", 0),
        ("accented, accents, accented rhythm, punchy rhythm", "strong accents", "rhythm_accents", 100)):
    add(words, "setting", label, home="global", moves=(EXACT(role, value),), note=RHYTHM_NOTE)
add("swing, swung, swinging, shuffle feel, shuffled", "setting", "swing", home="global",
    moves=(EXACT("swing", 1.25),), off=(EXACT("swing", 1.0),), note=RHYTHM_NOTE)
add("straight time, no swing", "setting", "straight time", home="global", moves=(EXACT("swing", 1.0),))
add("slow vibrato, gentle vibrato, lazy vibrato", "effect", "slow vibrato", hue=300,
    moves={"chord": (SET("vib_amount", 0.2), EXACT("vib_freq", 3.5)) + VIBRATO_READY,
           "harp": (SET("vib_amount", 0.2), EXACT("vib_freq", 3.5), MIN("vib_depth", 0.1), MIN("vib_sustain", 1.0))},
    off={"chord": (EXACT("vib_amount", 0), EXACT("vib_depth", 0)), "harp": (EXACT("vib_amount", 0),)})
add("fast vibrato, quick vibrato, nervous vibrato", "effect", "fast vibrato", hue=300,
    moves={"chord": (SET("vib_amount", 0.2), EXACT("vib_freq", 7.5)) + VIBRATO_READY,
           "harp": (SET("vib_amount", 0.2), EXACT("vib_freq", 7.5), MIN("vib_depth", 0.1), MIN("vib_sustain", 1.0))},
    off={"chord": (EXACT("vib_amount", 0), EXACT("vib_depth", 0)), "harp": (EXACT("vib_amount", 0),)})
add("delayed vibrato, vibrato that grows, growing vibrato, vibrato fades in", "effect", "delayed vibrato", hue=300,
    moves={"chord": (SET("vib_amount", 0.2), EXACT("vib_attack", 800)) + VIBRATO_READY,
           "harp": (SET("vib_amount", 0.2), IFZERO("vib_freq", 5), EXACT("vib_attack", 800))},
    off={"chord": (EXACT("vib_attack", 1),), "harp": (EXACT("vib_attack", 1),)})
add("key click, clicky, hammer, hammered, percussive click", "effect", "key click", home="harp",
    moves={"harp": (SET("transient", 0.3), EXACT("transient_wave", 5), EXACT("transient_attack", 0),
                    EXACT("transient_decay", 20))}, off={"harp": (EXACT("transient", 0),)})
add("telephone, phone, old radio, am radio, radio, megaphone, transistor radio", "quality", "telephone", hue=60,
    moves=(EXACT("out_freq", 1500), EXACT("out_res", 3.0), EXACT("out_lp", 0), EXACT("out_bp", 1.0), EXACT("out_hp", 0)))
add("fretless, fretless harp, ribbon harp, slide harp, pitch slide, glissando, gliss", "setting", "fretless ribbon harp",
    home="harp", moves={"harp": (EXACT("ribbon", 1), EXACT("ribbon_snap", 0), SET("ribbon_glide", 60))},
    off={"harp": (EXACT("ribbon", 0),)}, note="a ribbon harp: slide a finger along the strip")
add("in tune slides, snapped ribbon, ribbon that snaps", "setting", "ribbon that snaps to the notes", home="harp",
    moves={"harp": (EXACT("ribbon", 1), EXACT("ribbon_snap", 70))})
for words, label, value in (("harp in octaves, octave strings, octaves on the harp, doubled octaves", "harp in octaves", 4),
                            ("harp in sixths, sixths", "harp in sixths", 3), ("harp in fourths, fourths", "harp in fourths", 2),
                            ("harp in seconds, clusters, cluster", "harp in seconds", 1)):
    add(words, "setting", label, home="harp", moves={"harp": (EXACT("harp_shuffle", value),)},
        off={"harp": (EXACT("harp_shuffle", 0),)})
for words, label, value in (("root position", "root position", 0), ("first inversion", "first inversion", 1),
                            ("second inversion", "second inversion", 2), ("third inversion", "third inversion", 3)):
    add(words, "setting", f"chords in {label}", home="chord", moves={"chord": (EXACT("inversion", value),)})
add("rolled chords, rolled, strummed chords, broken chords, spread chords", "setting", "rolled chords", home="chord",
    moves={"chord": (SET("roll", 40),)}, off={"chord": (EXACT("roll", 0),)})
add("humanized, humanised, loose, sloppy, human feel", "setting", "loose timing", home="chord",
    moves={"chord": (SET("loose", 15),)}, off={"chord": (EXACT("loose", 0),)})
add("retrigger chords, retrigger, restrike", "setting", "chords retrigger", home="chord",
    moves={"chord": (EXACT("retrigger", 1),)}, off={"chord": (EXACT("retrigger", 0),)})
add("held strings follow the chords, strings follow the chords, held notes change", "setting",
    "held strings follow the chords", home="harp", moves={"harp": (EXACT("held_strings", 1),)},
    off={"harp": (EXACT("held_strings", 0),)})
add("cantus, harp sets the melody, harp leads the chord, melody voice", "setting", "the harp sets a chord voice",
    home="chord", moves={"chord": (EXACT("cantus", 5),)}, off={"chord": (EXACT("cantus", 0),)})
add("brothers, brother chords, barry harris brothers, barry harris block chords, sixth diminished, sixth diminished scale, "
    "6th diminished, harp chooses the chord, harp picks the chord", "setting",
    "the harp chooses the chord or its brother (Barry Harris)", home="both", hue=280,
    moves={"chord": (EXACT("brothers", 1),), "harp": (EXACT("scale", 8), EXACT("chromatic", 0))},
    off={"chord": (EXACT("brothers", 0),)})
add("chords left harp right, harp right chords left, separated, split stereo, wide separation", "setting",
    "chords and harp apart", home="global", moves=(EXACT("pan", 0.2),))
add("centred, centered, mono mix, both in the middle", "setting", "chords and harp in the middle", home="global",
    moves=(EXACT("pan", 1.0),))
add("dark reverb, warm reverb, damped reverb", "setting", "dark reverb", home="global",
    moves=(EXACT("reverb_hidamp", 0.7), EXACT("reverb_lowpass", 0.6), MIN("reverb", 0.3)))
add("bright reverb, shimmering reverb, plate reverb", "setting", "bright reverb", home="global",
    moves=(EXACT("reverb_hidamp", 0.0), EXACT("reverb_lowpass", 0.0), MIN("reverb", 0.3)))
add("harp vocoder, vocoded harp, vocoder on the harp", "setting", "vocoder on the harp", home="harp",
    moves=(EXACT("vocoder_carrier", 1), SET("vocoder", 70)),
    note="the vocoder needs a voice coming in over USB (usb audio set to 0 or 1 in minicontrol)")
add("clear words, intelligible, consonants", "setting", "clearer vocoder words", home="global",
    moves=(EXACT("vocoder_consonants", 70),))
add("child voice, small voice, little voice, chipmunk", "setting", "small singing voice", home="chord",
    moves={"chord": (EXACT("formant_size", 20), MIN("formant", 60))})
add("giant voice, deep voice, bass voice, big voice", "setting", "large singing voice", home="chord",
    moves={"chord": (EXACT("formant_size", 85), MIN("formant", 60))})
add("dim the leds, dim leds, dimmed leds, dimmer leds, darker leds, dim the lights, dim lights, dim led, leds dim",
    "setting", "dimmer LEDs", home="global", moves=(EXACT("led", 0.6),))
add("bright leds, brighter leds, full brightness, leds bright, leds at full", "setting", "LEDs at full brightness",
    home="global", moves=(EXACT("led", 0.0),))
add("leds off, no leds, turn off the leds, lights off, turn off the lights, dark leds", "setting", "LEDs off",
    home="global", moves=(EXACT("led", 1.0),))
# MIDI clock out (firmware 43, 266): rhythm mode's tempo sent as clock. Clock in isn't a setting: in rhythm
# mode the minichord always follows a clock coming in, so asking it to follow one turns its own off
CLOCK_NOTE = "MIDI clock goes out in rhythm mode, which is turned on at the instrument"
for words, label, value, note in (
        ("send clock, send midi clock, clock out, midi clock out, send the tempo, tempo out, sync out, "
         "sync my drum machine, sync the drum machine, drive my drum machine, drive the drum machine, sync the daw, "
         "sync my daw, drive my sequencer, drive the sequencer, be the master clock, master clock, "
         "minichord is the master, the minichord is the master", "MIDI clock out on both ports", 3, CLOCK_NOTE),
        ("clock on port 1, clock out on port 1, clock on the chord port", "MIDI clock out on port 1", 1, CLOCK_NOTE),
        ("clock on port 2, clock out on port 2, clock on the harp port", "MIDI clock out on port 2", 2, CLOCK_NOTE),
        ("no clock, no midi clock, stop sending clock, dont send clock, dont send midi clock, no clock out",
         "no MIDI clock out", 0, None),
        ("follow my daw, follow the daw, sync to the computer, sync to my daw, sync to the daw, slave to the clock, "
         "follow the clock, follow midi clock, follow an external clock, external clock", "no MIDI clock out", 0,
         "in rhythm mode the minichord follows a clock coming in by itself; its own clock out is set off so it "
         "doesn't send one too")):
    add(words, "setting", label, home="global", moves=(EXACT("midi_clock", value),), note=note)
# Firmware 51-58: the harp's hammer-on, the phone follower's settings, rhythm sections, fingered glide, the double
# tap held, revert and panic, the held chord pressed again
add("hammer on, hammer ons, hammer-ons, hammering on, hammer-on, pull offs, pull-offs, hammer ons and pull offs, "
    "legato harp, harp legato, slide between strings, slide between the strings, fretted slide, finger slides",
    "setting", "hammer-on", home="harp", moves=(EXACT("hammer_on", 1), EXACT("lift", 0), EXACT("ribbon", 0)),
    off=(EXACT("hammer_on", 0),),
    note="a finger landing beside a held string takes it over, and slides on; it plucks on touch, without the ribbon")
PHONE_NOTE = "the phone's sound is what plays into the minichord over USB (USB audio 0 or 1)"
add("follow my phone, follow the phone, follow the music, lock to the phone, lock to my phone, lock to the music, "
    "sync to my phone, sync to the phone, sync to the music, phone beat, beat from my phone, beat from the phone, "
    "tempo from my phone, tempo from the phone, follow the song, follow the beat of the music, play along with my phone",
    "setting", "rhythm follows the phone's beat", home="global", moves=(EXACT("phone_beat", 1),), off=(EXACT("phone_beat", 0),),
    note=PHONE_NOTE + "; it follows in rhythm mode, turned on at the instrument")
add("stab on every kick, stabs on every kick, stab on the kick, stabs on the kick, chords on every kick, chord on every kick, "
    "chords on the kick, hit on every kick, hits on the kick, restrike on the kick, kick stab, kick stabs",
    "setting", "the held chord struck on every kick", home="global", moves=(EXACT("kick_stab", 1),),
    off=(EXACT("kick_stab", 0),), note=PHONE_NOTE)
add("kick to midi, kicks to midi, send the kick as midi, send kicks as midi, kick out as midi, midi kick, kick note, "
    "kick drum to midi, trigger my drum machine from the kick, kick triggers my drum machine",
    "setting", "each kick sent as MIDI note 36", home="global", moves=(EXACT("kick_note", 36),),
    off=(EXACT("kick_note", 0),), note=PHONE_NOTE)
add("fills on modifier and hold, modifier and hold fills, modifier hold fill, fill on modifier and hold, fill button",
    "setting", "a fill on the modifier and hold", home="global", moves=(EXACT("fill_combo", 1),),
    off=(EXACT("fill_combo", 0),), note=RHYTHM_NOTE)
add("rhythm intro, an intro, with an intro, intro fill, pickup bar, a pickup bar, start with a fill, count in with a fill",
    "setting", "a rhythm intro", home="global", moves=(EXACT("rhythm_intro", 1),), off=(EXACT("rhythm_intro", 0),),
    note=RHYTHM_NOTE)
add("rhythm ending, a rhythm ending, proper ending, a proper ending, end on a chord, end on the chord, end with a fill, "
    "ending fill, outro, an outro, end the rhythm properly",
    "setting", "a rhythm ending on the long press", home="global", moves=(EXACT("rhythm_end_on", 1),),
    off=(EXACT("rhythm_end_on", 0),), note=RHYTHM_NOTE)
add("palm break, palm stops the band, palm stops the rhythm, palm mute stops the rhythm, stop with the palm, "
    "band stops with the palm, stop hits, palm breaks",
    "setting", "the palm stops the rhythm to the bar line", home="global",
    moves=(EXACT("palm_break", 1), MIN("palm_mute", 5)), off=(EXACT("palm_break", 0),), note=RHYTHM_NOTE)
add("fingered glide, fingered portamento, legato glide, legato portamento, glide only when legato, glide when i roll, "
    "glide only when i roll, glide only on overlapping chords, portamento only when legato",
    "setting", "fingered glide", home="chord", moves=(EXACT("fingered_glide", 1), IFZERO("glide", 300)),
    off=(EXACT("fingered_glide", 0),),
    note="the chords glide only when the fingers roll from one chord to the next; a chord struck afresh lands")
add("double tap hold, double tap and hold, momentary double tap, momentary double taps, tap and hold, tap then hold, "
    "momentary modifier, hold for the effect",
    "setting", "the double tap held, momentary", home="global", moves=(EXACT("tap_hold", 1),),
    off=(EXACT("tap_hold", 0),), note="tap the modifier, then press and hold: the double tap's settings while held")
add("revert and panic, panic button, a panic button, modifier panic, modifier and down reverts, revert gesture, "
    "revert button, undo button",
    "setting", "modifier with down reverts, with up panics", home="global", moves=(EXACT("revert_panic", 1),),
    off=(EXACT("revert_panic", 0),))
for words, label, value in (
        ("climb the inversions, climbing inversions, inversion climb, press again to climb, press again for the next "
         "inversion, repress climbs, re press climbs, walk up the inversions", "pressing the held chord climbs its inversions", 1),
        ("climb and come back down, inversions up and down, ping pong inversions, climb the inversions and back",
         "pressing the held chord climbs its inversions and back down", 2),
        ("press again for an octave, octave on repress, press again octave up, repress for an octave",
         "pressing the held chord goes up an octave and back", 3)):
    add(words, "setting", label, home="chord", moves=(EXACT("press_again", value),), off=(EXACT("press_again", 0),),
        note="with hold on: the chord ringing, pressed again")
# Firmware 59: the DJ box on the song, and the harp that plays it
DJ_NOTE = "the DJ box works on the song as it plays through the minichord (USB audio 1, play along)"
for words, label, moves in (
        ("strum along with my phone, strum along with the music, harp strums on the beat, harp strums with the song, "
         "strum on the beat, strum on every beat", "the harp strums the held chord on the music's beat", (EXACT("phone_strum", 1),)),
        ("strum eighths with the music, strum down and up with the music, harp strums eighths, strum in eighths with my phone",
         "the harp strums down and up with the music", (EXACT("phone_strum", 2),)),
        ("harp plays the song, the song on the harp, harp plays my phone, the harp plays the phone",
         "the harp plays the song, once a second of it is grabbed", ()),
        ("muffle the song, muffled song, song underwater, song through a low pass", "the song muffled (DJ filter)",
         (EXACT("dj_filter", 40),)),
        ("thin the song, thin out the song, song through a high pass, tinny song", "the song thinned (DJ filter)",
         (EXACT("dj_filter", 160),)),
        ("echo on the song, song echo, dub the song, dub echo on the song", "an echo on the song, in time",
         (EXACT("phone_echo", 40),))):
    add(words, "setting", label, home="global", moves=moves, note=DJ_NOTE)
for words, voice in (("harp plays the song, the song on the harp, harp plays my phone, the harp plays the phone", "harp"),):
    pass
add("song voice, the song as an instrument, sampled song, song sample", "sound", "the song as the voice (grab a second first)",
    home="harp", moves=(EXACT("voice", 5),), note="grab a second of the song first: the double tap on phone deck at 2, or the Lab's page")
# Firmware 60: the drums. A groove by name ("bossa nova drums", "an amen break"), a kit ("on the 808"), and how they play
DRUM_NOTE = "the drums play in rhythm mode, which is turned on at the instrument"
GROOVE_WORDS = [
    ("rock beat, rock drums, rock groove, straight rock beat", 1), ("pop beat, pop drums, eighth note beat", 2),
    ("sixteenth note beat, pop sixteenths, busy pop beat", 3), ("ballad drums, ballad beat, slow ballad beat", 4),
    ("funk beat, funk drums, funky beat, funk groove", 5), ("disco beat, disco drums, four on the floor disco", 6),
    ("motown beat, motown drums, sixties soul beat", 7), ("shuffle beat, shuffle drums, blues shuffle", 8),
    ("jazz drums, swing drums, swing beat, ride cymbal swing, jazz ride", 9), ("waltz drums, waltz beat", 10),
    ("march beat, marching drums, snare march, military march", 11), ("second line, new orleans second line, second line beat", 12),
    ("country beat, train beat, country drums, two beat drums", 13), ("folk drums, folk beat, brushed folk beat", 14),
    ("six eight drums, 6 8 drums, six eight beat", 15), ("habanera drums, tango drums, tango beat", 16),
    ("house beat, house drums, four on the floor, four to the floor", 17), ("trap beat, trap drums, trap hats, 808 trap", 18),
    ("amen break, amen breakbeat, the amen, amen style break, jungle break, breakbeat", 19),
    ("funky drummer, funky drummer break, funky drummer beat", 20), ("think break, think breakbeat", 21),
    ("boom bap, boom bap beat, boom bap drums, 90s hip hop beat, hip hop beat", 22),
    ("drum and bass, drum n bass, dnb, d and b, drum and bass beat", 23), ("lo fi beat, lo fi drums, lofi beat, lofi hip hop", 24),
    ("son clave, clave beat, clave, son montuno drums", 25), ("rumba clave, rumba, rumba beat", 26),
    ("bossa nova drums, bossa drums, bossa nova beat, bossa beat", 27), ("samba drums, samba beat, batucada", 28),
    ("cumbia, cumbia beat, cumbia drums", 29), ("baiao, baiao beat, forro", 30), ("salsa, salsa beat, salsa drums", 31),
    ("one drop, reggae drums, reggae beat, one drop beat", 32), ("dembow, reggaeton, reggaeton beat, dembow beat", 33),
    ("soca, soca beat, calypso drums", 34), ("afrobeat, afrobeat drums, fela beat, tony allen beat", 35),
    ("12 8 bell, bell pattern, agbekor, west african bell, african bell pattern", 36), ("highlife, highlife beat", 37),
    ("maqsum, maqsoum, belly dance beat, arabic beat", 38), ("baladi, baladi beat", 39), ("saidi, saidi beat", 40),
    ("kehrwa, keharwa, kaherva", 41), ("dadra, dadra taal", 42), ("teental, teentaal, tintal, tabla beat", 43),
    ("cliqhop, cliqhop beat, glitch hop, glitch hop beat", 44), ("clicks and cuts, clicks n cuts, microhouse, microhouse beat", 45),
    ("idm, idm beat, braindance, broken glitch beat", 46), ("microsound, microsound beat, sparse clicks", 47)]
GROOVE_NAMES = ["rock", "pop eighths", "pop sixteenths", "ballad", "funk", "disco", "motown", "shuffle", "jazz swing", "waltz",
                "march", "second line", "country two-beat", "folk", "six eight", "habanera", "house", "trap",
                "amen-style break", "funky drummer-style", "think-style break", "boom bap", "drum and bass", "lo-fi",
                "son clave", "rumba clave", "bossa nova", "samba", "cumbia", "baiao", "salsa", "one drop", "dembow", "soca",
                "afrobeat", "12/8 bell", "highlife", "maqsum", "baladi", "saidi", "kehrwa", "dadra", "teental",
                "cliqhop", "clicks and cuts", "idm", "microsound"]
KIT_NAMES = ["808", "909", "CR-78", "studio", "brushes", "boom bap", "world percussion", "tabla and doumbek", "trap", "house",
             "glitch", "cliqhop"]
for words, n in GROOVE_WORDS:
    add(words, "setting", f"drums: the {GROOVE_NAMES[n - 1]} groove", home="global", moves=(EXACT("drums", 1), EXACT("drum_groove", n)),
        off=(EXACT("drum_groove", 0),), note=DRUM_NOTE)
KIT_WORDS = [("808 kit, 808 drums, on the 808, tr 808, eight oh eight", 1), ("909 kit, 909 drums, on the 909, tr 909", 2),
             ("cr78, cr 78, cr78 kit, vintage drum machine, old drum machine, rhythm box", 3),
             ("studio kit, acoustic drums, real drums, live drums, drum kit", 4), ("brushes, brushed drums, brush kit, jazz brushes", 5),
             ("boom bap kit, dusty drums, lo fi drums, crunchy drums, sp 1200 drums", 6),
             ("world percussion, congas, bongos, hand drums, latin percussion, percussion kit", 7),
             ("tabla, tablas, doumbek, darbuka, tabla and doumbek", 8), ("trap kit, 808 bass kick, trap drums kit", 9),
             ("house kit, house drums kit, club drums", 10), ("glitch drums, bitcrushed drums, broken drums, glitchy drums, glitch kit", 11),
             ("cliqhop kit, cliqhop drums, blips and bleeps, bleeps and blips, blippy drums, clicky drums, supercollider drums, "
              "pure data drums, computer music drums", 12)]
for words, n in KIT_WORDS:
    add(words, "setting", f"drums: the {KIT_NAMES[n - 1]} kit", home="global", moves=(EXACT("drums", 1), EXACT("drum_kit", n)),
        off=(EXACT("drum_kit", 0),), note=DRUM_NOTE)
for words, label, moves, off in (
        ("drums, add drums, with drums, drum machine, a drummer, drum beat, rhythm with drums", "drums",
         (EXACT("drums", 1),), (EXACT("drums", 0),)),
        ("drums only, just drums, only drums, just the drums, drums alone", "drums only",
         (EXACT("drums", 1), EXACT("drums_only", 1)), (EXACT("drums_only", 0),)),
        ("drum count in, sticks count in, count in with sticks, count the band in, rhythm count in, count off with sticks",
         "a count-in of sticks before the rhythm",
         (EXACT("count_in", 1),), (EXACT("count_in", 0),)),
        ("loose drums, humanize the drums, humanized drums, sloppy drums, human drummer, drunk drummer, behind the beat",
         "loose drums (humanize)", (EXACT("drums", 1), EXACT("drum_humanize", 50)), (EXACT("drum_humanize", 0),)),
        ("tight drums, drums on the grid, quantized drums, robot drums", "drums on the grid", (EXACT("drum_humanize", 0),), None),
        ("drums to midi, drums out over midi, send the drums to my daw, drums on channel 10, midi drums, record the drums",
         "the drums out on MIDI channel 10", (EXACT("drum_midi", 1),), (EXACT("drum_midi", 0),)),
        ("louder drums, drums up, more drums, turn up the drums", "louder drums", (SET("drum_level", 100),), None),
        ("quieter drums, drums down, softer drums, turn down the drums, less drums", "quieter drums", (SET("drum_level", 35),), None)):
    add(words, "setting", label, home="global", moves=moves, off=off, note=DRUM_NOTE)
# The harp as drum pads needs nothing else: not rhythm mode, not the drums on. While it is on the strings
# play no notes, and the ribbon, the zipper plate's positions, hammer-on and palm mute rest until it's off.
add("harp drums, finger drumming, finger drums, drum pads, harp as drum pads, strings as drum pads, tap the drums on the harp, "
    "harp drum pads, drum pad harp, play drums on the harp, drums on the harp, harp plays the drums",
    "setting", "the harp plays the drums", home="global",
    moves=(EXACT("harp_drums", 1), IFZERO("drum_level", 70)), off=(EXACT("harp_drums", 0),),
    note="the harp's strings are the kit's pads at any time, rhythm mode or not; while on they play no notes, "
         "and the ribbon, the zipper plate, hammer-on and palm mute wait until it's off")
# The looper in time (firmware 46): quantize, count-in, click and its level
LOOPER_NOTE = "the looper is worked live: the double tap, minicontrol's looper buttons, or control changes"
for words, label, value in (
        ("quantize the loop, quantized looper, quantized loop, loop in time, tighten the loop, quantize, quantized, "
         "quantize to eighths", "looper quantized to eighths", 2),
        ("quantize to quarters, quantized to quarters, quantize to the beat", "looper quantized to quarter notes", 1),
        ("quantize to sixteenths, quantized to sixteenths", "looper quantized to sixteenths", 3),
        ("quantize to triplets, quantized to triplets, triplet loop, loop in triplets", "looper quantized to triplets", 4),
        ("loop as played, no quantize, loose loop, unquantized, not quantized", "looper as played", 0)):
    add(words, "setting", label, home="global", moves=(EXACT("loop_quantize", value),), note=LOOPER_NOTE)
for words, label, value in (
        ("count me in, count in, countin, a bar of count in, one bar count in, one bar of count in", "one bar's count-in", 1),
        ("two bar count in, two bars count in, count in two bars, two bars of count in", "two bars' count-in", 2),
        ("no count in, record straight away, record at once, no countin", "no count-in", 0)):
    add(words, "setting", label, home="global", moves=(EXACT("loop_countin", value),), note=LOOPER_NOTE)
# The looper's phrase in bars (firmware 48): the recording closes by itself on the bar line
for value, names in ((1, ("one", "1")), (2, ("two", "2")), (3, ("three", "3")), (4, ("four", "4")),
                     (6, ("six", "6")), (8, ("eight", "8")), (12, ("twelve", "12")), (16, ("sixteen", "16"))):
    bar = "bar" if value == 1 else "bars"
    words = ", ".join(w for n in names for w in (
        f"{n} bar loop", f"{n} bar loops", f"{n} bar phrase", f"loop of {n} {bar}", f"loops of {n} {bar}",
        f"{n} {bar} long loop", f"loop {n} {bar}", f"loops {n} {bar} long", f"loop {n} {bar} long"))
    if value == 1:
        words += ", a bar long loop, loop of a bar, loops of a bar, loop a bar long, loops a bar long"
    add(words, "setting", f"loops of {value} {bar}, closing by themselves on the bar line", home="global",
        moves=(EXACT("loop_length", value),), note=LOOPER_NOTE)
add("loop as long as I play, free length loop, loop length off, no loop length, any length loop",
    "setting", "loops as long as they are played", home="global", moves=(EXACT("loop_length", 0),), note=LOOPER_NOTE)
add("click, metronome, click track", "effect", "looper click", home="global",
    moves=(EXACT("loop_click", 2), SET("click_level", 50)), off=(EXACT("loop_click", 0),),
    note="the click sounds on the speaker and headphones only, never in what goes out over USB")
for words, label, value in (
        ("click only for the count in, click for the count in, metronome for the count in", "click for the count-in", 1),
        ("click all the time, metronome all the time, always click, click throughout, click while it loops, "
         "metronome while it loops, click while it plays", "click all the time", 3),
        ("no click, no metronome, click off, metronome off", "no click", 0)):
    add(words, "setting", label, home="global", moves=(EXACT("loop_click", value),))
for words, label, value in (("quiet click, soft click, quieter click, gentle click", "a quiet click", 25),
                            ("loud click, louder click, loud metronome", "a loud click", 80)):
    add(words, "setting", label, home="global", moves=(MIN("loop_click", 2), EXACT("click_level", value)))
add("chord memory, chord memories, omnichord memory", "setting", "chord memory (played live)", home="global",
    note="chord memory records a progression in time with rhythm mode and plays it back by itself, so both hands are "
         "free for the strings. It's played live, not kept in a preset: say \"double tap works the chord memory\", "
         "or use minicontrol's chord memory buttons")
add("twin peaks, laura palmer, badalamenti, angelo badalamenti, david lynch", "sound",
    "Twin Peaks (Ben's Twin Green preset)", base="Twin Green", hue=120)

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
add("short, shorter, staccato, percussive, snappy, snappier, tight, tighter, clipped, choppy, short tail, "
    "short release, shorter tail", "quality", "shorter",
    opposite="longer", moves=(SCALE("decay", 0.4), SET("sustain", 0.0), SCALE("release", 0.35),
                               SCALE("string_decay", 0.4), SET("rhythm_artic", 50)))
add("long, longer, sustained, sustaining, ringing, legato, lingering, endless, held, long tail, long release, "
    "longer tail, long decay", "quality", "longer",
    opposite="shorter", moves=(SET("sustain", 0.85), SCALE("release", 2.5), SCALE("decay", 2.0),
                                SCALE("string_decay", 2.0), SET("rhythm_artic", 150)))
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
    hue=300, moves={"chord": (EXACT("lfo_wave", 7), EXACT("lfo_freq", 8), SET("lfo_amount", 0.5), MIN("filter_sens", 1.0)),
                    "harp": (EXACT("out_lfo_wave", 7), EXACT("out_lfo_freq", 8), SET("out_lfo_amount", 0.5),
                             MIN("out_lfo_sens", 1.0))})
add("wah, wobble, wobbles, wub, wubs, dubstep, filter wobble, filter sweep, sweeping, swirling, swirly", "quality",
    "filter wobble", home="chord", hue=120,
    moves={"chord": (EXACT("lfo_wave", 0), IFZERO("lfo_freq", 2), SET("lfo_amount", 0.6), MIN("filter_sens", 1.5)),
           "harp": (EXACT("out_lfo_wave", 0), IFZERO("out_lfo_freq", 2), SET("out_lfo_amount", 0.6),
                    MIN("out_lfo_sens", 1.5))})
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
    moves={"chord": (SET("glide", 300),),
           "harp": (EXACT("ribbon", 1), SET("ribbon_glide", 120), EXACT("ribbon_snap", 30))},
    off={"chord": (EXACT("glide", 0),), "harp": (EXACT("ribbon_glide", 0),)},
    note="a gliding harp plays as a ribbon: slide a finger along the strip")
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
                            ("meantone, mean tone, meantone tuning, meantone temperament", "meantone tuning", 1),
                            ("pythagorean, pythagorean tuning", "Pythagorean tuning", 3),
                            ("werckmeister, werckmeister tuning, werckmeister temperament", "Werckmeister III tuning", 4),
                            ("kirnberger, kirnberger tuning", "Kirnberger III tuning", 5),
                            ("vallotti, vallotti tuning", "Vallotti tuning", 6), ("equal temperament, equal tuning", "equal temperament", 0),
                            ("quarter tones, quarter tone, quartertone, 24 edo", "quarter tones (24-EDO)", 11),
                            ("19 edo", "19-EDO", 10), ("31 edo", "31-EDO", 12)):
    add(words, "setting", label, home="global", moves=(EXACT("temperament", value),))
add("own temperament, my own temperament, my own tuning, custom temperament, custom tuning, scala tuning, scala file, "
    "a scala scale", "setting", "your own temperament", home="global", moves=(EXACT("temperament", 13),),
    note="your own temperament is twelve offsets in cents: set them in minicontrol's Own temperament, read a Scala "
         "file there, or send a MIDI Tuning Standard message from the computer. Needs firmware 67")

# ---- microtonal playing (firmware 63-68) ----
MICRO_NOTE = "needs firmware 63-68"
# The maqams: scalar harp mode 14 from the key, quarter tones if the tuning is still equal; the three whose
# way down differs (Rast, Hijaz, Nahawand) with melodic direction. Their other upper jins by name too.
for words, label, m, jins in (
        ("maqam rast, rast", "maqam Rast", 0, 0), ("maqam bayati, bayati", "maqam Bayati", 1, 0),
        ("maqam saba, saba", "maqam Saba", 2, 0), ("maqam hijaz, hijaz, hijaz scale", "maqam Hijaz", 3, 0),
        ("maqam sikah, sikah, segah, maqam segah", "maqam Sikah", 4, 0),
        ("maqam nahawand, nahawand", "maqam Nahawand", 5, 0), ("maqam kurd, kurd", "maqam Kurd", 6, 0),
        ("maqam ajam, ajam", "maqam Ajam", 7, 0),
        ("maqam suznak, suznak", "maqam Suznak (Rast with Hijaz on G)", 0, 1),
        ("maqam husseini, husseini, husayni", "maqam Husseini (Bayati with B half-flat)", 1, 1),
        ("saba zamzam, maqam saba zamzam", "maqam Saba Zamzam", 2, 1),
        ("hijazkar, maqam hijazkar", "maqam Hijazkar", 3, 1), ("maqam huzam, huzam", "maqam Huzam", 4, 1),
        ("maqam, arabic scale, arabic maqam, middle eastern scale, arabic music, quarter tone scale",
         "a maqam (Rast)", 0, 0)):
    add(words, "setting", label, home="harp",
        moves=(EXACT("scale", 14), EXACT("maqam", m), EXACT("maqam_jins", jins), EXACT("chromatic", 0),
               IFZERO("temperament", 11)) + ((EXACT("melodic_direction", 1),) if m in (0, 3, 5) and not jins else ()),
        note="the harp plays the maqam from the key (scalar harp mode 14), in quarter tones (24-EDO) unless "
             "another division is chosen; put maqam upper jins on the double tap to modulate. " + MICRO_NOTE)
# Ragas: the custom scale from the key, its way down where the avaroha differs, and a tanpura on Sa
# tuned for the raga (Pa, or Ma where Pa is left out, or Ni). Degrees as masks: 1 b2 2 b3 3 4 #4 5 b6 6 b7 7.
RAGA_NOTE = ("the harp plays the raga from the key (scalar harp mode 10), its way down with melodic direction, over "
             "a tanpura on Sa: set Sa with the chord key signature and the transpose. " + MICRO_NOTE)
for words, label, up, down, tanpura in (
        ("raag yaman, raga yaman, yaman, yaman kalyan, kalyan", "raga Yaman", 2645, 2773, 1),
        ("raag bhupali, raga bhupali, bhupali, bhoop, mohanam, raga mohanam", "raga Bhupali", 661, 0, 1),
        ("raag bilawal, raga bilawal, bilawal, shankarabharanam, raga shankarabharanam", "raga Bilawal", 2741, 0, 1),
        ("raag khamaj, raga khamaj, khamaj", "raga Khamaj", 2737, 1717, 1),
        ("raag kafi, raga kafi, kafi, kharaharapriya, raga kharaharapriya", "raga Kafi", 1709, 0, 1),
        ("raag bhairav, raga bhairav, bhairav, mayamalavagowla, raga mayamalavagowla", "raga Bhairav", 2483, 0, 1),
        ("raag bhairavi, raga bhairavi, bhairavi, hanumatodi, raga hanumatodi", "raga Bhairavi", 1451, 0, 1),
        ("raag todi, raga todi, todi, miyan ki todi", "raga Todi", 2507, 0, 1),
        ("raag malkauns, raga malkauns, malkauns, hindolam, raga hindolam", "raga Malkauns", 1321, 0, 2),
        ("raag marwa, raga marwa, marwa", "raga Marwa", 2643, 0, 3),
        ("raag desh, raga desh, desh", "raga Desh", 2213, 1717, 1),
        ("raag bihag, raga bihag, bihag", "raga Bihag", 2225, 2741, 1),
        ("raag durga, raga durga, durga", "raga Durga", 677, 0, 1),
        ("raag hamsadhwani, raga hamsadhwani, hamsadhwani", "raga Hamsadhwani", 2197, 0, 1)):
    add(words, "setting", label, home="harp",
        moves=(EXACT("scale", 10), EXACT("custom_scale", up), EXACT("custom_down", down), EXACT("chromatic", 0),
               EXACT("melodic_direction", 1 if down else 0), IFZERO("drone", tanpura)),
        note=RAGA_NOTE)
add("melodic minor, jazz minor, melodic minor scale", "setting", "melodic minor, natural minor on the way down",
    home="harp", moves=(EXACT("scale", 10), EXACT("custom_scale", 2733), EXACT("custom_down", 1453),
                        EXACT("chromatic", 0), EXACT("melodic_direction", 1)),
    note="up with a raised sixth and seventh, down the natural minor, from the key (scalar harp mode 10). " + MICRO_NOTE)
add("melodic direction, ascending and descending, aroha and avaroha, aroha avaroha, arohana avarohana, "
    "a different way down, way up and way down, descending form, descending forms, the way down", "setting",
    "a scale's way down as it falls (melodic direction)", home="harp", moves=(EXACT("melodic_direction", 1),),
    off=(EXACT("melodic_direction", 0),),
    note="in scalar harp modes 10, 11, 14 and 15: a falling line plays the scale's down form. " + MICRO_NOTE)
add("upper jins, other jins, second jins, swap the jins, change the jins, jins swap", "setting",
    "the maqam's other upper jins", home="harp", moves=(EXACT("maqam_jins", 1),), off=(EXACT("maqam_jins", 0),),
    note="put it on the double tap to modulate as you play. " + MICRO_NOTE)
for words, label, value, tuning in (
        ("quarter tone modifier, modifier quarter tone, modifier a quarter tone, half sharp modifier, half sharps, "
         "half flats", "the modifier moves a quarter tone", 1, 11),
        ("modifier diesis, diesis modifier, modifier a diesis", "the modifier moves a diesis of 31", 1, 12),
        ("modifier one step, modifier by one step, microtonal modifier", "the modifier moves one step of the tuning", 1, None),
        ("semitone modifier, modifier a semitone, modifier by a semitone, modifier a half step", "the modifier moves a semitone", 2, None)):
    add(words, "setting", label, home="global",
        moves=(EXACT("modifier_step", value),) + ((IFZERO("temperament", tuning),) if tuning else ()),
        note="in 19, 24 and 31 (in 12 there is only the semitone). " + MICRO_NOTE)
add("koto press, press to bend, bend by pressing, press the string to bend, press bends, ato oshi, oshi, "
    "press a ringing string", "setting", "koto press: press a ringing string to bend it", home="harp",
    moves=(EXACT("koto", 2), EXACT("lift", 1), EXACT("ribbon", 0)), off=(EXACT("koto", 0),),
    note="plucking on lift, a finger on a ringing string bends it as hard as it presses. Needs firmware 49")
add("koto press in steps, koto bends in steps, koto in steps, bends in steps, bend in steps, stepped bends, fretted bends, bends in quarter tones, shruti bends, "
    "gamaka in steps, meend in steps, bends that snap", "setting", "koto press bends in steps of the tuning, snapping",
    home="harp", moves=(EXACT("koto_steps", 1), SET("koto_snap", 60), IFZERO("koto", 2), EXACT("lift", 1),
                        EXACT("ribbon", 0)), off=(EXACT("koto_steps", 0), EXACT("koto_snap", 0)),
    note="koto press counted in steps of 19, 24 or 31 and held on each. " + MICRO_NOTE)
add("ribbon step snap, frets between the notes, microtonal frets, snap to the microtones, find the microtones, "
    "light frets, ribbon microtones", "setting", "the ribbon held lightly on the steps between its strings",
    home="harp", moves=(EXACT("ribbon", 1), SET("ribbon_step_snap", 50), IFZERO("ribbon_snap", 40)),
    off=(EXACT("ribbon_step_snap", 0),),
    note="where two strings are more than a step apart, the slide rests on each step between. " + MICRO_NOTE)
add("purity, pure chords, beatless chords, still chords, sweeten the chords, sweet tuning, adaptive just intonation, "
    "adaptive tuning, barbershop tuning, barbershop, just chords, pure thirds", "setting",
    "chords drawn toward just intervals (purity)", home="global", moves=(SET("purity", 100),),
    off=(EXACT("purity", 0),),
    note="every note toward the just interval it makes with the chord's root; put purity on a knob or hover to "
         "sweep it. " + MICRO_NOTE)
add("mts, mts output, midi tuning standard, microtonal midi, tuned midi, send the tuning, midi in tune", "setting",
    "MTS output: the tuning over plain MIDI", home="global", moves=(EXACT("mts", 1),), off=(EXACT("mts", 0),),
    note="each note's key retuned with an MTS message before it starts, for synths that read MTS; MPE output, if "
         "on, carries the pitch instead. " + MICRO_NOTE)
DRONE_NOTE = "a drone on the key's Sa (the chord key signature's root and the transpose). " + MICRO_NOTE
for words, label, value in (
        ("tanpura, tanpura drone, a tanpura, with a tanpura, tambura, tamboura, sa drone, drone on sa, indian drone, "
         "tanpura on pa, pa tanpura", "a tanpura (Pa, Sa, Sa, low Sa)", 1),
        ("tanpura with ma, ma tanpura, tanpura on ma, tanpura tuned to ma", "a tanpura with Ma", 2),
        ("tanpura with ni, ni tanpura, tanpura on ni, tanpura tuned to ni", "a tanpura with Ni", 3),
        ("all sa tanpura, tanpura all sa, tanpura on sa only", "a tanpura, all Sa", 4),
        ("shruti box, sruti box, surpeti, harmonium drone, reed drone, drone box", "a shruti box (Sa and Pa)", 5)):
    add(words, "setting", label, home="global", moves=(EXACT("drone", value),), off=(EXACT("drone", 0),),
        note=DRONE_NOTE)
for words, label, moves in (
        ("louder tanpura, louder drone, drone up, tanpura up, more drone", "a louder drone", (SET("drone_level", 85),)),
        ("quieter tanpura, quieter drone, softer drone, drone down, tanpura down, less drone", "a quieter drone",
         (SET("drone_level", 25),)),
        ("slow tanpura, slower tanpura, lazy tanpura", "a slower tanpura", (SET("drone_pace", 20),)),
        ("fast tanpura, faster tanpura, quick tanpura", "a quicker tanpura", (SET("drone_pace", 7),)),
        ("low tanpura, male tanpura, deep tanpura", "the drone low (Sa from E2)", (EXACT("drone_register", 0),)),
        ("high tanpura, female tanpura", "the drone high (Sa from D3)", (EXACT("drone_register", 2),))):
    add(words, "setting", label, home="global", moves=moves + (IFZERO("drone", 1),), note=DRONE_NOTE)

# ---- firmware 69: quick hammer-ons, tabla laya, the drums' and the drone's reverb, speed ramp ----
FW69_NOTE = "needs firmware 69"
add("grace notes, grace note, gracenotes, cuts, pipe cuts, piping cuts, gracings, quick hammer ons, quick hammer-ons, "
    "fast hammer ons, fast hammer-ons, trills, trill, fast trills, quick trills, ornaments, ornamented, ornamentation",
    "setting", "quick hammer-ons, for grace notes and trills", home="harp",
    moves=(EXACT("hammer_on", 1), EXACT("hammer_time", 50), EXACT("lift", 0), EXACT("ribbon", 0)),
    off=(EXACT("hammer_time", 180),),
    note="hammer-on takes a held string over 50 ms after its pluck (hammer-on time), so a grace note or a trill "
         "follows at once; strum quickly, or a slow strum moves a held string instead of plucking the next. " + FW69_NOTE)
add("slow hammer ons, slow hammer-ons, careful hammer ons, late hammer ons", "setting",
    "hammer-ons only on a string that has rung a while", home="harp",
    moves=(EXACT("hammer_on", 1), EXACT("hammer_time", 300), EXACT("lift", 0), EXACT("ribbon", 0)),
    off=(EXACT("hammer_time", 180),), note="hammer-on time 300 ms: even a slow strum plucks every string. " + FW69_NOTE)
TABLA_NOTE = ("the tabla's thekas (kehrwa, dadra, teental) counted against the tempo: a bol a beat, the tempo counts "
              "matras. " + FW69_NOTE)
for words, label, value, tempo in (
        ("a bol a beat, one bol a beat, bol a beat, a bol per beat, one bol per beat, a matra a beat, one matra a beat, "
         "matra a beat, a beat a matra, tempo in matras, count matras, counted in matras", "the thekas a bol a beat (tabla laya)", 1, None),
        ("vilambit, vilambit laya, vilambit teental, slow teental, slow theka, slow laya", "vilambit: a slow theka, a bol a beat", 1, 48),
        ("madhya laya, madhya, medium teental, medium theka, medium laya", "madhya laya: a medium theka, a bol a beat", 1, 120),
        ("drut, drut laya, drut teental, fast teental, fast theka, fast laya", "drut: a fast theka, a bol a beat", 1, 240),
        ("dugun, double laya, two bols a beat, two bols per beat", "dugun: the thekas two bols a beat", 2, None),
        ("chaugun, four bols a beat, four bols per beat", "chaugun: the thekas four bols a beat", 3, None)):
    add(words, "setting", label, home="global",
        moves=(EXACT("tabla_laya", value),) + ((EXACT("tempo", tempo), EXACT("drums", 1), IFZERO("drum_groove", 43)) if tempo else ()),
        off=(EXACT("tabla_laya", 0),), note=TABLA_NOTE)
REVERB_SEND_NOTE = "into the reverb the harp and the chords share (reverb size sets its room); as loud dry as before. " + FW69_NOTE
add("drum reverb, reverb on the drums, drums in the reverb, wet drums, roomy drums, drums in the room, ambient drums, "
    "drums with reverb, reverb drums, reverberant drums", "setting", "the drums in the reverb", home="global",
    moves=(SET("drum_reverb", 50),), off=(EXACT("drum_reverb", 0),), note="the drums, harp drums too, " + REVERB_SEND_NOTE)
add("dry drums, drums dry, no reverb on the drums", "setting", "the drums dry", home="global",
    moves=(EXACT("drum_reverb", 0),), note=FW69_NOTE)
add("drone reverb, reverb on the drone, reverb on the tanpura, tanpura reverb, tanpura in the reverb, drone in the reverb, "
    "wet tanpura, wet drone, tanpura in the room, drone in the room, reverberant tanpura", "setting",
    "the drone in the reverb", home="global", moves=(SET("drone_reverb", 50), IFZERO("drone", 1)),
    off=(EXACT("drone_reverb", 0),), note="the drone " + REVERB_SEND_NOTE)
add("dry drone, dry tanpura, no reverb on the drone, no reverb on the tanpura", "setting", "the drone dry", home="global",
    moves=(EXACT("drone_reverb", 0),), note=FW69_NOTE)

# ---- firmware 74: the tanpura's jawari ----
JAWARI_NOTE = ("drone jawari (381): how closely each tanpura string lies along its curved bridge, as the cotton thread "
               "under it is slid to set: 0 closed, a plain string; 100 open, lying right on the bridge; 60 the usual. "
               "Needs firmware 74")
add("jawari, jivari, juari, open jawari, more jawari, buzzing tanpura, buzzy tanpura, bright tanpura, tanpura buzz, "
    "more buzz on the tanpura, the tanpura buzzing, let the tanpura buzz, tanpura shimmer, blooming tanpura",
    "setting", "the jawari open: the tanpura buzzing and blooming", home="global",
    moves=(SET("drone_jawari", 90), IFZERO("drone", 1)), off=(EXACT("drone_jawari", 60),), note=JAWARI_NOTE)
add("closed jawari, less jawari, no jawari, smooth tanpura, plain tanpura, dull tanpura, tanpura without buzz, "
    "no buzz on the tanpura, tanpura not buzzing", "setting", "the jawari closed: a plain tanpura", home="global",
    moves=(SET("drone_jawari", 10), IFZERO("drone", 1)), off=(EXACT("drone_jawari", 60),), note=JAWARI_NOTE)
SPEED_RAMP_NOTE = ("the tremolo's and the vibrato's rates wind up to a new rate and coast down, as a rotating speaker's rotors "
                   "do; put the tremolo speed on the double tap or a knob to switch. " + FW69_NOTE)
add("speed ramp, ramped tremolo, tremolo ramp, tremolo spins up, tremolo winds up, spin up and down, spins up and down, "
    "winds up and down, rotor ramp, gradual speed change", "setting", "the tremolo and vibrato wind up and down (speed ramp)",
    home="global", moves=(SET("speed_ramp", 20),), off=(EXACT("speed_ramp", 0),), note=SPEED_RAMP_NOTE)
add("leslie, leslies, leslie speaker, leslie cabinet, rotary speaker, rotary, rotating speaker, rotating speakers, "
    "spinning speaker, rotor speaker, rotary cabinet, chorale and tremolo", "effect", "a rotating speaker",
    home="chord", hue=310,
    moves={"chord": (SET("trem_amount", 0.3), EXACT("trem_freq", 0.8), EXACT("trem_wave", 0), EXACT("speed_ramp", 12))},
    off={"chord": (EXACT("trem_amount", 0), EXACT("speed_ramp", 0))},
    note="the chords' tremolo turning slowly (chorale, 0.8 Hz); the double tap on the tremolo speed spins it up to "
         "fast (6.7 Hz) and back over about a second. " + FW69_NOTE)
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
    (r"\balternate hover\b|\bsecond hover\b|\bhover alternate\b|"
     r"\b(?:holding|hold|with) (?:the )?modifier(?: held| button)?,? hover(?:ing|s)?\b|"
     r"\bhover(?:ing|s)? (?:with|holding) (?:the )?modifier(?: held| button)?\b", "alt_hover"),
    # the sound coming in over USB, moving a setting: "the kick ducks the chords", "the hats open the filter"
    (r"\b(?:the )?(?:phone(?:s)?|music|backing track|track|kick drum|kicks?|bass drum|hi ?hats?|hats|cymbals)(?: sound| audio)?\b"
     r"(?= (?:ducks?|pumps?|opens?|closes?|moves?|drives?|swells?|mutes?|quiets?|brightens?|darkens?|works?|runs?|"
     r"shapes?|makes?|wobbles?|sweeps?|controls?|push(?:es)?|turns?|raises?|lowers?)\b)", "phone"),
    (r"\b(?:alternate|alt|second function)s? (?:pots|knobs|potentiometers|dials|functions) (?:for|on|of) (?:the )?"
     r"chords? and (?:the )?harp\b|\bchord and harp (?:knob|pot)s?(?:'| )?alternates?\b", "two_knobs"),
    (r"(?<!momentary )\bdouble ?tap(?:s|ping|ped)?\b(?! (?:and )?hold\b)(?: (?:on )?(?:the )?modifier(?: button)?)?|"
     r"\btap(?:ping)? twice\b|\btwo taps\b", "tap"),
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
                 "harp_knob": "modifier + harp knob", "hover": "hover", "tap": "double tap",
                 "alt_hover": "alternate hover", "phone": "the phone's sound"}
TAP_PAIRS = ((200, 201), (209, 210), (211, 212))
PHONE_SLOTS = ((303, 304, 305, 306), (307, 308, 309, 310))   # control, value, band, release
PHONE_BANDS = ({"kick", "kicks", "drum", "bass", "low", "bassline"}, {"hats", "hat", "hi", "cymbals", "treble", "high"})

# What a control can be given, by the words for it
TARGETS = {
    "cutoff": "overall filter, main filter, filter, filters, cutoff, cut off, brightness, brighter, darker, tone, low pass, lowpass, opens, closes",
    "resonance": "resonance, reso, squelch, peak",
    "reverb": "reverb mix, reverb level, reverb amount, reverb, verb, wet, wetness, ambience, space, room",
    "reverb_size": "room size, reverb size, hall size, size",
    "delay_mix": "delay mix, echo mix, delay level, delay, echo, echoes",
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
    "level": "volume, level, loudness, mute, mutes, silence, silences, duck, ducks, ducking, pump, pumps, pumping, swell, the swell, swell pedal, expression pedal, organ swell",
    "lfo_amount": "wah, sweep, wobble, wub",
    "noise": "noise, breath, air",
    "transpose": "transpose, transposes, transposition",
    "key": "key, key signature, key sig, chord key signature",
    "looper": "looper, loop, loops, record, recording, recorder",
    "looper_pc": "computer looper, looper on the computer, the computers looper, daw looper, lab looper, labs looper",
    "chord_memory": "chord memory, chord memories, omnichord memory, progression memory, record chords, records chords, "
                    "record a progression, records the chords, record the chords",
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
    "brothers": "brothers, brother chords",
    "voice_leading": "voice leading",
    "touch": "touch, velocity",
    "palm_mute": "palm mute",
    "rhythm_fill": "fill, fills, a fill, plays a fill, drum fill, rhythm fill",
    "rhythm_break": "break, breaks, a break, stop the band, stops the band",
    "rhythm_ending": "ending, the ending, ends the song, end the song, outro",
    "revert": "revert, reverts, undo, undoes, put the preset back, puts the preset back",
    "panic": "panic, panics, all notes off, stop everything, stops everything",
    "dj_filter": "song filter, the songs filter, dj filter, filter on the song, filters the song, sweeps the song",
    "phone_echo": "echo throw, dub throw, throws the echo, throw the echo, echoes the song, song echo",
    "tape_stop": "tape stop, tape stops, stops the tape, stop the tape",
    "grab": "grab, grabs, grabs the song, grab the song, samples the song, sample the song",
    "drum_kit": "drum kit, drum kits, kits",
    "drum_level": "drum level, drums level, drum volume",
    "purity": "purity, pureness, how pure, just intonation amount",
    "drone_level": "drone level, tanpura level, drone volume, tanpura volume",
    "drone": "tanpura, shruti box",
    "maqam_jins": "jins, upper jins, other jins",
    "sharp_fn": "sharp function, flats, flat button, sharps to flats, sharp to flat",
    "trem_freq": "tremolo speed, tremolo rate, tremolo frequency, rotor speed, leslie speed, speaker speed, rotary speed, "
                 "leslie, rotary, rotating speaker, speeds up the leslie, speed up the leslie, spins up the leslie, "
                 "spin up the leslie, speeds up the rotary, speeds up the speaker, switches the leslie, switch the leslie, "
                 "leslie fast and slow, fast leslie",
    "speed_ramp": "speed ramp, ramp",
    "tabla_laya": "laya, tabla laya, dugun",
    "drum_reverb": "drum reverb, drums reverb, reverb on the drums",
    "drone_reverb": "drone reverb, tanpura reverb, reverb on the drone, reverb on the tanpura",
    "hammer_time": "hammer-on time, hammer on time",
    "drone_jawari": "jawari, jivari, drone jawari, tanpura jawari, the jawari, tanpura buzz, drone buzz",
    "reed_bark": "bark, reed bark, wurlitzer bark, the bark, growl, how hard it's struck",
    "organ_drawbars": "drawbars, organ drawbars, registration, the registration, organ registration",
    "organ_perc": "organ percussion",
}
# Where a knob sweeps each role, (low, high) in parameters.json units; its centre is stored in the preset
SWEEP = {"trem_freq": (0.5, 8), "speed_ramp": (0, 40), "drum_reverb": (0, 100), "drone_reverb": (0, 100), "drone_jawari": (0, 100), "reed_bark": (0, 100), "hammer_time": (30, 300), "purity": (0, 100), "drone_level": (0, 100), "dj_filter": (0, 200), "phone_echo": (0, 100), "drum_level": (0, 100), "drum_kit": (0, 11),
         "cutoff": {"chord": (150, 4000), "harp": (80, 2000)}, "resonance": (0.7, 4.5), "reverb": (0, 0.9),
         "reverb_size": (0.2, 1.0), "delay_mix": (0, 0.6), "delay_time": (50, 600), "vib_amount": (0, 0.5),
         "trem_amount": (0, 0.8), "crunch": (0, 0.7), "glide": (0, 800), "vowel": (0, 100), "formant": (0, 100),
         "vocoder": (0, 100), "attack": (1, 2000), "release": (100, 4000), "sustain": (0, 1), "decay": (50, 3000),
         "spread": (0, 100), "ensemble": (0, 100), "tempo": (60, 180), "level": (0.2, 1.8),
         "lfo_amount": (0, 1), "noise": (0, 0.3), "string_model": (0, 100), "transpose": (0, 12), "touch": (0, 100)}
# What a double tap or a hand over the plate takes each role to
TAP = {"resonance": 4.0, "reverb": 0.9, "reverb_size": 1.0, "delay_mix": 0.45, "delay_time": 500, "vib_amount": 0.3,
       "trem_amount": 0.6, "crunch": 0.6, "glide": 400, "vowel": 100, "formant": 90, "vocoder": 80, "attack": 1000,
       "release": 3500, "sustain": 1.0, "decay": 2000, "spread": 90, "ensemble": 80, "level": 1.6, "lfo_amount": 0.7,
       "noise": 0.15, "string_model": 100, "wave": 9, "looper": 6, "looper_pc": 7, "chord_memory": 5, "chromatic": 1, "lift": 1, "scale": 2, "barry": 1, "brothers": 1,
       "voice_leading": 1, "touch": 80, "palm_mute": 5, "transpose": 7, "tempo": 140, "ribbon": 1, "mpe": 1,
       "midi_in": 1, "knob_midi": 1, "knob_layer": 1, "layout": 1, "key": 1,
       "rhythm_fill": 1, "rhythm_break": 2, "rhythm_ending": 3, "revert": 1, "panic": 2,
       "dj_filter": 40, "phone_echo": 100, "tape_stop": 1, "grab": 2, "drum_level": 100,
       "purity": 100, "drone_level": 100, "drone": 1, "maqam_jins": 1, "sharp_fn": 1,
       "trem_freq": 6.7, "speed_ramp": 20, "tabla_laya": 2, "drum_reverb": 60, "drone_reverb": 60, "hammer_time": 50,
       "drone_jawari": 100, "reed_bark": 100}
# the double tap's actions, which take a step rather than toggle a value
TAP_ACTIONS = {"looper", "looper_pc", "chord_memory", "rhythm_fill", "rhythm_break", "rhythm_ending", "revert", "panic",
               "tape_stop", "grab"}
# Words between an effect and its amount ("reverb at 40%"), and waveforms by name ("chord waveform 1 sawtooth")
AMOUNT_FILLERS = {"to", "at", "of", "is", "set", "=", "around", "about"}
WAVE_NAMES = {"sine": 0, "sawtooth": 9, "saw": 9, "square": 11, "triangle": 3, "pulse": 4}
# Each chord note's own instrument (firmware 41, 270-273): the note voice's number is the chord voice's plus
# one, so 0 can mean "as the chord voice". Positions count as the note levels do, first the bass.
NOTE_INSTRUMENTS = {"pizzicato strings": 3, "plucked strings": 3, "pizz strings": 3, "pizzicato": 3, "pizz": 3,
                    "string quartet": 5, "bowed strings": 5, "strings": 5, "violins": 5, "violin": 5, "cellos": 5,
                    "cello": 5, "choir": 4, "voices": 4, "vocals": 4, "vocal": 4, "aahs": 4, "piano": 2, "pianos": 2,
                    "oscillators": 1, "synth voice": 1, "own synth": 1, "vibraphone": 7, "vibes": 7,
                    "bass guitar": 8, "electric bass": 8, "fingered bass": 8, "sitar": 9, "electric piano": 10,
                    "e piano": 10, "epiano": 10, "rhodes": 10,
                    "wurlitzer": 11, "wurly": 11, "reed piano": 11, "kalimba": 12, "mbira": 12, "marimba": 13,
                    "organ": 14, "hammond": 14, "upright bass": 15, "double bass": 15,
                    "acoustic bass": 15, "string bass": 15, "accordion": 16, "squeezebox": 16}
NOTE_POSITIONS = {"bass note": [0], "bottom note": [0], "lowest note": [0], "low note": [0], "first note": [0],
                  "bass": [0], "bottom": [0], "second note": [1], "tenor": [1], "third note": [2], "alto": [2],
                  "fourth note": [3], "top note": [3], "highest note": [3], "high note": [3], "soprano": [3],
                  "top": [3], "middle notes": [1, 2], "middle two": [1, 2], "inner voices": [1, 2], "middle": [1, 2],
                  "above": [1, 2, 3], "upper notes": [1, 2, 3], "upper voices": [1, 2, 3]}
NOTE_VOICE_NAMES = {1: "its own synth", 2: "piano", 3: "pizzicato", 4: "choir", 5: "string quartet", 6: "song sample",
                    7: "vibraphone", 8: "bass guitar", 9: "sitar", 10: "electric piano", 11: "reed piano", 12: "kalimba",
                    13: "marimba", 14: "organ", 15: "upright bass", 16: "accordion"}
# What chord voice and harp voice play, in place of the oscillators; the notes' own voices count one higher
VOICE_NAMES = {1: "piano", 2: "pizzicato", 3: "choir", 4: "string quartet", 5: "song sample", 6: "vibraphone",
               7: "bass guitar", 8: "sitar", 9: "electric piano", 10: "reed piano", 11: "kalimba",
               12: "marimba", 13: "organ", 14: "upright bass", 15: "accordion"}
# The settings only the section's own oscillators hear; a sampled voice doesn't. A waveform names the synth itself
WAVE_ROLES = ("wave", "wave2", "wave3")
OSC_ROLES = WAVE_ROLES + ("amp1", "amp2", "amp3", "mult1", "mult2", "mult3", "noise")


def _join(names):
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]
NOTE_NAMES = ("bass note", "second note", "third note", "top note")
EVERY_NOTE = [5, 2, 4, 3]   # "each note a different instrument": cello-ish quartet, piano, choir, pizzicato on top
NOTE_VOICES_NOTE = ("each chord note's instrument: the first is the bass and the fourth the top in the stock voicing; "
                    "chord spacing without voice leading can reorder them. Needs firmware 41")


def _alternation(words):
    return "|".join(re.escape(w) for w in sorted(words, key=len, reverse=True))


NOTE_PATTERNS = (   # (regex, which group is the instrument, which the position)
    (r"\b(" + _alternation(NOTE_INSTRUMENTS) + r")\s+(?:on|in|at|for|as|playing)\s+(?:the\s+)?("
     + _alternation(NOTE_POSITIONS) + r")\b", 1, 2),
    (r"\b(" + _alternation(NOTE_INSTRUMENTS) + r")\s+(" + _alternation(NOTE_POSITIONS) + r")\b", 1, 2),
    (r"\b(?:the\s+)?(" + _alternation(NOTE_POSITIONS) + r")\s+(?:is\s+|on\s+|in\s+|as\s+|played\s+by\s+|gets\s+)?"
     r"(?:the\s+)?(" + _alternation(NOTE_INSTRUMENTS) + r")\b", 2, 1),
)
NOTE_SPLIT = (r"\b(" + _alternation(NOTE_INSTRUMENTS) + r")\s+and\s+(" + _alternation(NOTE_INSTRUMENTS)
              + r")\s+(?:on\s+|in\s+|for\s+)?(?:the\s+)?chords?\b")
NOTE_EVERY = (r"\b(?:each|every)\s+(?:chord\s+)?note\s+(?:is\s+)?(?:a\s+|on\s+a\s+)?different\s+instrument\b|"
              r"\bdifferent\s+instruments?\s+(?:on|for)\s+(?:each|every)\s+note\b|\bmixed\s+instruments\b")

# A control named with one of these and no setting is cleared: "turn off hover", "unassign the mod knob"
CLEAR_WORDS = {"off", "unassign", "unassigned", "disable", "disabled", "nothing", "none", "remove", "clear",
               "cleared", "free", "no"}
# Words for several settings at once, for a double tap with slots to fill
MULTI_TARGETS = {"midi options": ("mpe", "midi_in", "knob_midi"), "recent midi options": ("mpe", "midi_in", "knob_midi"),
                 "midi settings": ("mpe", "midi_in", "knob_midi"), "midi stuff": ("mpe", "midi_in", "knob_midi")}
# Settings that are on or off ("make sure ribbon mode is on"), and what on is
ON_OFF = {"ribbon": 245, "ribbon mode": 245, "harp ribbon": 245, "mpe": 110, "mpe mode": 110, "mpe output": 110,
          "mde": 110, "mde mode": 110, "midi in": 8, "midi in plays": 8, "midi input": 8, "knobs send midi": 238,
          "knob midi": 238, "knob layer": 117, "pluck on lift": 216, "chromatic mode": 98, "chromatic": 98,
          "scalar harp mode": 36, "scalar mode": 36, "harp mode": 36, "scale mode": 36, "single port mode": 108,
          "single port": 108, "note off on lift": 215, "harp note off on lift": 215, "barry harris mode": 33,
          "barry harris": 33, "brothers": 390, "brother chords": 390, "voice leading": 111, "chord layout": 39, "alternate chord layout": 39,
          "alternate layout": 39, "alt layout": 39, "alternate chords": 39, "retrigger chords": 21,
          "palm mute": 213, "touch pressure": 253, "pressure": 253, "looper": None,
          "midi clock out": 266, "clock out": 266, "midi clock": 266,
          "phone beat": 311, "kick stab": 312, "kick stabs": 312,
          "kick note": 313, "rhythm intro": 317, "rhythm ending": 318, "palm break": 319,
          "double tap hold": 321, "tap and hold": 321, "revert and panic": 322, "panic button": 322,
          "melodic direction": 351, "mts output": 371, "mts": 371, "tanpura": 372, "koto press": 289,
          "tabla laya": 377, "drum reverb": 378, "drone reverb": 379, "speed ramp": 380}
ON_VALUE = {213: 5, 266: 3, 313: 36, 372: 1, 289: 2, 377: 1, 378: 50, 379: 50, 380: 20}
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
ACTION_VERBS = {"works", "work", "runs", "run", "operates", "operate", "drives", "steps", "step", "triggers",
                "trigger", "fires", "assign", "assigns", "assigned", "map", "maps", "mapped", "route", "routes", "routed", "link",
                "links", "linked", "point", "points", "turns", "turn", "adds", "add", "makes", "make", "starts", "start", "opens", "open", "sweeps",
                "sweep", "controls", "control", "changes", "change", "brings", "bring", "gives", "give", "does",
                "do", "kicks", "switches", "switch", "toggles", "toggle", "sets", "set", "mutes", "mute",
                "moves", "move", "closes", "close", "swells", "swell", "shapes", "shape", "brightens", "darkens",
                "raises", "lowers", "pushes", "quiets"}
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
know see try trying tried able unless otherwise case bank banks color led leds light lights assign assigns
rhythm rhythms tuning tunings intonation theme themes song songs tune tunes track soundtrack music intro riff
mix level amount loop looper loops looping
assigned map maps mapped route routes routed link links linked point points build built separate profile profiles based
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
    t = "".join(c for c in unicodedata.normalize("NFKD", text) if not unicodedata.combining(c))
    t = t.lower().replace("’", "'").replace("colour", "color").replace("rythm", "rhythm")
    t = re.sub(r"\b((?:[a-z]\.){2,})", lambda m: m.group(1).replace(".", ""), t)   # r.e.m. is rem
    t = re.sub(r"\b(mr|mrs|ms|dr|st|jr|vs)\.", r"\1", t)                         # mr. blue sky.replace("&", " and ").replace("+", " plus ")
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
    problems: list = field(default_factory=list)    # words a song's recipe uses that aren't known


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
            m = re.fullmatch(r"waveform (\d)", name)
            if m and p["section"] == "chord":
                variants += [f"oscillator {m.group(1)}", f"osc {m.group(1)}", f"chord oscillator {m.group(1)}"]
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
        if role == "voice" and section == "chord":
            self._whole_chord()

    # ---- the sampled voices ----

    def _whole_chord(self):
        """An instrument for the chords is every note's: a note given its own one goes back to the chord voice,
        unless this description gave it"""
        addrs = [ROLES[f"voice{n + 1}"]["chord"] for n in range(4)]
        notes = [n for n, a in enumerate(addrs) if self.state.get(a) and a not in self.touched]
        voice = self.state[ROLES["voice"]["chord"]] + 1    # in the notes' own count
        had = {}
        for n in notes:
            if self.state[addrs[n]] != voice:
                had.setdefault(NOTE_VOICE_NAMES[self.state[addrs[n]]], []).append(NOTE_NAMES[n])
            self._put(addrs[n], 0, "the chord voice")
        if had:
            self.later.append("every chord note on the chord voice now (it had " + _join(
                [f"{name} on {'all four notes' if len(where) == 4 else 'the ' + _join(where)}"
                 for name, where in had.items()]) + ")")

    def _samples(self, section):
        """The sampled instruments a section plays in place of its oscillators: none when any of its notes hears them"""
        voice = self.state.get(ROLES["voice"][section], 0)
        if section == "harp":
            return [VOICE_NAMES[voice]] if voice else []
        heard = []
        for n in range(4):
            own = self.state.get(ROLES[f"voice{n + 1}"]["chord"], 0)
            if own == 1 or (own == 0 and voice == 0):
                return []
            heard.append(NOTE_VOICE_NAMES[own] if own else VOICE_NAMES[voice])
        return list(dict.fromkeys(heard))

    def _oscillators(self, entry, section):
        """A word for the oscillators, said of a section playing a sample: a waveform names the synth, so the section
        goes back to it, unless this description chose its instrument. Another word reaches a sample only in part,
        which is said once the description is read, of the instrument it ends on"""
        if self.profile:
            return
        for s in self._sections(section):
            roles = {m[1] for m in self._moves_for(entry.moves, s)}
            if not roles & set(OSC_ROLES):
                continue
            samples = self._samples(s)
            if samples and roles <= set(WAVE_ROLES) and ROLES["voice"][s] not in self.touched:
                self._move(EXACT("voice", 0), s, 1, entry.label)
                self.later.append(f"{entry.label}: the {'chords on their' if s == 'chord' else 'harp on its'} own "
                                  f"synth again, in place of the {_join(samples)}, since a waveform is the synth's")
            else:
                self.unheard.setdefault(s, {})[entry.label] = roles <= set(OSC_ROLES)

    def _say_unheard(self):
        for s, words in self.unheard.items():
            samples = self._samples(s)
            if not samples:
                continue
            part, whose = ("chords", "their") if s == "chord" else ("harp", "its")
            for how, labels in (("not", [w for w, all_osc in words.items() if all_osc]),
                                ("only partly", [w for w, all_osc in words.items() if not all_osc])):
                if labels:   # the synth offered, unless the sample was asked for too
                    self.result.notes.append(
                        f"{_join(labels)}: {how} heard, since the {part} play{'s' if s == 'harp' else ''} the "
                        f"{_join(samples)} and not {whose} oscillators" +
                        ("" if ROLES["voice"][s] in self.touched else f"; say \"synth {part}\" for all of it"))

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

    def describe(self, entry, section, strength, negate, more, less, amount=None):
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
            self._oscillators(entry, section)
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
            if amount is not None:   # "reverb at 40%": exactly that much
                moves = {}
                for s in ("chord", "harp"):
                    out = []
                    for m in self._moves_for(entry.moves, s):
                        if m[0] == "set":
                            a = self._addr(m[1], s)
                            if a is None or a not in self.state:
                                continue
                            p, v = self.params[a], amount
                            if p["data_type"] == "float" and v > p["max_value"] and v / 100 <= p["max_value"]:
                                v /= 100
                            out.append(EXACT(m[1], v))
                        else:
                            out.append(m)
                    moves[s] = tuple(out)
                self._apply(entry, section, 1, entry.label, moves=moves)
                self.result.understood.append(f"{prefix}{entry.label} at {amount:g}")
                if entry.note:
                    self.result.notes.append(entry.note)
                if entry.hue is not None and self.hue is None:
                    self.hue = entry.hue
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
        elif entry.kind == "song":
            self._song(entry, section, negate, prefix)
            return
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
            if entry.base:
                if self.profile:
                    self.result.notes.append(f"a profile can't start from a preset, so {entry.label} was left out")
                else:
                    self.result.base = entry.base
        if entry.hue is not None and self.hue is None:
            self.hue = entry.hue

    def _song(self, entry, section, negate, prefix):
        """A song is its recipe: descriptions of its chords and harp in these same words, each read with
        its section held. Named for one section ("an Africa harp"), only that part is played."""
        r = self.result
        if negate:
            r.notes.append(f'"not {entry.label}": nothing to undo there; left alone')
            return
        if entry.hue is not None and self.hue is None:
            self.hue = entry.hue
        marks = (len(r.understood), len(r.notes), list(r.unknown))
        outer = (self.applied, self.forced, self.pending, self.void_next, self.last_value)
        self.applied, self.pending, self.void_next = set(), [], False
        used = []
        for part, forced in (("both", None), ("chords", "chord"), ("harp", "harp")):
            text = getattr(entry, part)
            if not text or (forced and section in ("chord", "harp") and forced != section):
                continue
            self.forced = forced
            used.append(f"{part if part != 'both' else ''}{': ' if part != 'both' else ''}{text}")
            for clause in self._clauses(normalise(text)):
                self._clause(clause)
        r.problems += [(entry.label, w) for w in r.unknown if w not in marks[2]]
        del r.understood[marks[0]:]
        del r.notes[marks[1]:]
        r.unknown = marks[2]
        self.applied, self.forced, self.pending, self.void_next, self.last_value = outer
        r.understood.append(f"{prefix}{entry.label} ({'; '.join(used)})")
        if entry.note:
            r.notes.append(entry.note)
        if entry.base:
            if self.profile:
                r.notes.append(f"a profile can't start from a preset, so {entry.label} was left out")
            else:
                r.base = entry.base

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
        if control in ("hover", "alt_hover"):
            if role == "looper" or p["controls"] != "all":
                self.result.notes.append(f"hover can't move {label}; try the double tap")
                return
            ca, va = (249, 250) if control == "hover" else (325, 326)
            self._put(ca, a, f"{names} → {label}")
            self._put_raw(va, value, f"{names} value")
            self.last_value = (va, a)
            held = "" if control == "hover" else ", with the modifier held"
            self.result.understood.append(f"{names} → {label}, toward {value:g} with a hand 2 cm over the plate{held}")
            return
        if control == "phone":
            if p["controls"] != "all":
                self.result.notes.append(f"the phone's sound can't move {label}; try the double tap")
                return
            if self.next_phone >= len(PHONE_SLOTS):
                self.result.notes.append(f"the phone's sound has two followers; {label} was left out")
                return
            ca, va, ba, ra = PHONE_SLOTS[self.next_phone]
            self.next_phone += 1
            ws = set(words)
            band = 1 if PHONE_BANDS[0] & ws else 2 if PHONE_BANDS[1] & ws else 0
            release = 40 if band == 2 else 250 if role == "level" else 120
            self._put(ca, a, f"the phone's sound → {label}")
            self._put_raw(va, value, "phone follow value")
            self._put(ba, band, "phone follow band")
            self._put(ra, release, "phone follow release")
            self.last_value = (va, a)
            what = ("the whole sound", "the kick (bass)", "the hats (treble)")[band]
            self.result.understood.append(f"the phone's sound, {what} → {label}, toward {value:g} at its loudest, "
                                          f"back over {release} ms")
            if PHONE_NOTE not in self.result.notes:
                self.result.notes.append(PHONE_NOTE)
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
        elif role == "looper_pc":
            self.result.understood.append(f"double tap{slot} → a looper on the computer (control change 90 over USB); "
                                          "the minichord's own looper is left alone")
        elif role == "chord_memory":
            self.result.understood.append(f"double tap{slot} → the chord memory: record a progression in time, then it "
                                          "plays back by itself (it turns rhythm mode on)")
        elif role in ("rhythm_fill", "rhythm_break", "rhythm_ending"):
            what = {"rhythm_fill": "a fill into the next bar line (and style B, if set)",
                    "rhythm_break": "a break: the rhythm rests to the bar line",
                    "rhythm_ending": "the ending: a fill, the last chord, and rhythm mode off"}[role]
            self.result.understood.append(f"double tap{slot} → {what}")
        elif role in ("revert", "panic"):
            self.result.understood.append(f"double tap{slot} → " + ("the preset back as it was saved" if role == "revert"
                                          else "the panic: every voice stopped, All Notes Off"))
        elif role in ("tape_stop", "grab"):
            self.result.understood.append(f"double tap{slot} → " + ("the tape stop, and the song back in the next time"
                                          if role == "tape_stop" else "a second of the song grabbed onto the harp"))
        else:
            self.result.understood.append(f"double tap{slot} → {label} to {value:g}, and back on the next double tap")
        if index > 0 and not self.tap_note:
            self.tap_note = True
            self.result.notes.append("one double tap flips all three of its slots together")

    def _clear(self, control):
        """A control set to do nothing"""
        if control.startswith("tap"):
            pairs = [TAP_PAIRS[int(control[4:])]] if ":" in control else TAP_PAIRS
            for c, _ in pairs:
                self._put(c, 0, "double tap cleared")
            slot = f" slot {int(control[4:]) + 1}" if ":" in control else ""
            self.result.understood.append(f"double tap{slot} does nothing")
        elif control in ("hover", "alt_hover"):
            self._put(249 if control == "hover" else 325, 0, f"{CONTROL_NAMES[control]} cleared")
            self.result.understood.append(f"{CONTROL_NAMES[control]} does nothing")
        elif control == "phone":
            for slot in PHONE_SLOTS:
                self._put(slot[0], 0, "phone follower cleared")
            self.result.understood.append("the phone's sound moves nothing")
        else:
            self._put(KNOB_ADDRESSES[control][0], 0, f"{CONTROL_NAMES[control]} cleared")
            self.result.understood.append(f"{CONTROL_NAMES[control]} does nothing")

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
                "mute", "mutes", "silence", "silences", "kill", "kills", "cut", "cuts", "duck", "ducks", "ducking",
                "pump", "pumps", "pumping"} & set(words)
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
            return 0 if down and role not in TAP_ACTIONS else TAP[role]
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
        self.next_phone = 0
        self.tap_note = False
        self.applied = set()
        self.touched = set()
        self.skipped = set()
        self.pending = []
        self.void_next = False
        self.last_value = None
        self.forced = None
        self.unheard = {}
        self.later = []   # said once the whole description is read, so a song's recipe keeps them too
        for clause in self._clauses(normalise(text)):
            self._clause(clause)
        self._say_unheard()
        if not self.profile:
            self.later += conflict_notes(self.params, self.selectors, start, self.state, self.setting_name)
        self.result.notes = list(dict.fromkeys(self.result.notes + self.later))
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
        parts = re.split(r"(?<!\d)\.|\.(?!\d)|[,;:!?\n]+|\bbut\b|(?<!only )(?<!click )(?<!metronome )\bwhile\b|\bwhereas\b|\bthen\b|\bexcept\b", text)
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
        return any(self._find(toks, i, self.section_phrases) and not self._covered(toks, i) for i in range(len(toks)))

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
        for m in re.finditer(r"\b(?:(?:tempo|speed)\s+(?:of\s+|at\s+|to\s+|is\s+)?|at\s+)?(\d{2,3}) ?(?:bpm|beats per minute)\b"
                             r"|\btempo\s+(?:of\s+|at\s+|to\s+|is\s+)?(\d{2,3})\b", s):
            m_bpm = m.group(1) or m.group(2)
            self._put(187, int(m_bpm), "tempo")
            r.understood.append(f"rhythm tempo {int(m_bpm)} bpm")
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
                if control == "two_knobs":
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
            self._note_voices(s, take_span)
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
            if toks[i] in COLORS and not self._find(toks, i, self.phrases):
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
            amount = None
            if entry.kind == "effect":
                k = i + n
                while k < len(toks) and toks[k] in AMOUNT_FILLERS and k < i + n + 3:
                    k += 1
                if k < len(toks) and re.fullmatch(r"\d*\.?\d+", toks[k]) and \
                        not (k + 1 < len(toks) and toks[k + 1] in ("edo", "bpm", "hz", "cm", "hertz")):
                    amount = float(toks[k])
                    for j in range(i + n, k + 1):
                        used[j] = True
                    if k + 1 < len(toks) and toks[k + 1] in ("percent", "pc"):
                        used[k + 1] = True
            section = self._section_for(i, anchors, entry)
            self.describe(entry, section, strength, negate, more, less, amount)
            for j in range(i, i + n):
                used[j] = True
            i += n
        self._leftovers(toks, used)

    def _covered(self, toks, i):
        """Whether a vocabulary phrase starting before i runs over it ("septimal chords")"""
        for j in range(max(0, i - self.longest + 1), i):
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
        if not roles and CLEAR_WORDS & set(toks):
            for control in controls:
                self._clear(control)
            for i, t in enumerate(toks):
                if t in CLEAR_WORDS or t in ACTION_VERBS or t in ("also", "too", "on"):
                    used[i] = True
            self.pending = []
            self._leftovers(toks, used)
            return
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


    def _note_voices(self, s, take_span):
        """An instrument for some of the chord's notes: "choir on top", "piano bass", "pizzicato strings and choir
        on the chords" (the bass and top one, the middle two the other), "each note a different instrument" """
        r = self.result
        sets = []
        for m in re.finditer(NOTE_EVERY, s):
            sets.append((list(range(4)), None, m))
        for m in re.finditer(NOTE_SPLIT, s):
            sets.append(([0, 3], NOTE_INSTRUMENTS[m.group(1)], m))
            sets.append(([1, 2], NOTE_INSTRUMENTS[m.group(2)], m))
        for pattern, gi, gp in NOTE_PATTERNS:
            for m in re.finditer(pattern, s):
                sets.append((NOTE_POSITIONS[m.group(gp)], NOTE_INSTRUMENTS[m.group(gi)], m))
        done = []
        for notes, voice, m in sets:
            if any(m.start() < e and s0 < m.end() for s0, e in done if (s0, e) != (m.start(), m.end())):
                continue   # a shorter reading inside a longer one already taken
            done.append((m.start(), m.end()))
            take_span(m)
            for n in notes:
                v = EVERY_NOTE[n] if voice is None else voice
                self._put(270 + n, v, f"{NOTE_NAMES[n]} voice")
            if voice is None:
                r.understood.append("chords: each note a different instrument (" + ", ".join(
                    f"{NOTE_NAMES[n]} {NOTE_VOICE_NAMES[EVERY_NOTE[n]]}" for n in range(4)) + ")")
            else:
                r.understood.append(f"chords: {NOTE_VOICE_NAMES[voice]} on the " +
                                    " and ".join(NOTE_NAMES[n] for n in notes))
        if done:
            self._move(MIN("cutoff", 3000), "chord", 1, "for the sampled notes")
            if NOTE_VOICES_NOTE not in r.notes:
                r.notes.append(NOTE_VOICES_NOTE)

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
            wave = None
            if k < len(toks) and toks[k] in WAVE_NAMES and \
                    all("waveform" in self.params[a]["name"] for a in self.names[key]):
                wave = WAVE_NAMES[toks[k]]
            elif k >= len(toks) or not re.fullmatch(r"\d*\.?\d+", toks[k]) or \
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
                v = float(toks[k]) if wave is None else wave
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
        if self.forced:
            return self.forced
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
        for t in re.findall(r"[A-Za-z0-9][A-Za-z0-9']*|[^A-Za-z0-9]+", text):
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
    songs = [e for e in V if e.kind == "song"]
    print(f"Songs ({len(songs)}; say the title, or the title and artist):")
    for e in songs:
        print(f"  {e.label}")
    print()
    print("Colours: " + ", ".join(COLORS))
    print("Also: 120 bpm, tuned to 432 hz, key of E flat, in A minor, transpose up 2")
    print("\nControls: the mod knob, the modifier with the mod knob, the chord knob, the harp knob, hover, double tap.")
    print("  e.g. \"the mod knob opens the filter\", \"double tap starts the looper\", \"hover makes the chords sing\"")
    print("  what they can take: " + "; ".join(f"{r}: {w}" for r, w in TARGETS.items()))
    print("\nName the chords or the harp to say which one a word is for (\"plucky harp\", \"dark chords\");")
    print("otherwise it goes where it usually belongs, or to both.")



def conflict_notes(params, sel, start, state, name):
    """The CONFLICTS rows the change from start to state brought about, as notes"""
    def target(a):
        if isinstance(a, dict):
            t = int(state.get(a["at"], 0) or 0)
            return t if t in params else None
        return a

    def holds(term):
        a, op, b = term
        a = target(a)
        if a is None:
            return False
        if op in ("selector", "!selector"):
            return (a in sel) == (op == "selector")
        if op in ("sweepable", "!sweepable"):
            return (params[a].get("controls") == "all") == (op == "sweepable")
        x = state.get(a, 0)
        if isinstance(b, dict):
            b = state.get(b["value"], 0) if "value" in b else [state.get(v, 0) for v in b["values"]]
        if op == "==": return x == b
        if op == "!=": return x != b
        if op == ">": return x > b
        if op == "<": return x < b
        if op == "in": return x in b
        return x not in b

    notes = []
    for row in CONFLICTS:
        about = [target(a) for a in row["about"]]
        if not any(a is not None and state.get(a) != start.get(a) for a in about):
            continue
        if not all(holds(t) for t in row["when"]):
            continue
        note = row["note"]
        at = next((t[0] for t in row["when"] if isinstance(t[0], dict)), None) or \
             next((a for a in row["about"] if isinstance(a, dict)), None)
        if "{target}" in note:
            t = target(at) if at is not None else int(state.get(row["when"][0][0], 0) or 0)
            if t not in params:
                continue
            note = note.replace("{target}", name(t))
        notes.append(note)
    return notes

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
        "selectors": sorted(selectors()), "conflicts": CONFLICTS,
        "vocabulary": [{"words": list(e.words), "kind": e.kind, "label": e.label, "moves": moves(e.moves),
                        "off": moves(e.off), "opposite": e.opposite, "home": e.home, "hue": e.hue, "note": e.note,
                        "base": e.base, "chords": e.chords, "harp": e.harp, "both": e.both}
                       for e in V],
        "roles": ROLES, "controls": [list(c) for c in CONTROLS], "knob_addresses": KNOB_ADDRESSES,
        "control_names": CONTROL_NAMES, "tap_pairs": [list(p) for p in TAP_PAIRS], "targets": TARGETS,
        "phone_slots": [list(p) for p in PHONE_SLOTS], "phone_bands": [sorted(b) for b in PHONE_BANDS],
        "phone_note": PHONE_NOTE, "tap_actions": sorted(TAP_ACTIONS),
        "multi_targets": {k: list(v) for k, v in MULTI_TARGETS.items()}, "on_off": ON_OFF,
        "on_value": {str(k): v for k, v in ON_VALUE.items()}, "sweep": SWEEP, "tap": TAP,
        "prepare": {k: [list(x) for x in v] for k, v in PREPARE.items()},
        "section_words": {k: sorted(v) for k, v in SECTION_WORDS.items()},
        "strong": sorted(STRONG), "weak": sorted(WEAK), "negate": sorted(NEGATE), "more": sorted(MORE),
        "action_verbs": sorted(ACTION_VERBS), "stopwords": sorted(STOPWORDS), "number_words": _NUMBER_WORDS,
        "colors": COLORS, "not_in_presets": sorted(NOT_IN_PRESETS), "clear_words": sorted(CLEAR_WORDS),
        "amount_fillers": sorted(AMOUNT_FILLERS), "wave_names": WAVE_NAMES,
        "note_instruments": NOTE_INSTRUMENTS, "note_positions": NOTE_POSITIONS,
        "note_voice_names": {str(k): v for k, v in NOTE_VOICE_NAMES.items()}, "note_names": list(NOTE_NAMES),
        "voice_names": {str(k): v for k, v in VOICE_NAMES.items()}, "wave_roles": list(WAVE_ROLES),
        "osc_roles": list(OSC_ROLES),
        "every_note": EVERY_NOTE, "note_voices_note": NOTE_VOICES_NOTE,
        "note_patterns": [list(p) for p in NOTE_PATTERNS], "note_split": NOTE_SPLIT, "note_every": NOTE_EVERY,
    }


# ---- settings that undo one another ----
# Combinations where one setting is silent, or only half works, because of another (from the firmware's
# own code: the 2026-10-11 audit). Checked on the finished preset: a note when the description changed
# one of a row's "about" settings and every "when" term holds. A term is [A, op, B]: A an address, or
# {"at": k} the address setting k holds (a knob's or hover's target; false when unassigned); B a value,
# a list, {"value": a} the value at a, or {"values": [a, ...]} theirs. Values as the description reads
# them (floats as 0-1). "{target}" in a note is the {"at"} target's name.
SAMPLE_VOICES = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]   # 10 and 13, the reed piano and the organ, are models but take a sample's place
KNOBS = [(10, 11, "the chord knob's alternate"), (12, 13, "the harp knob's alternate"),
         (14, 15, "the mod knob"), (16, 17, "the mod knob's alternate")]
CONFLICTS = [
    *({"about": [a], "when": [[a, "!=", 0], [264, "!=", 13], [265, "!=", 13], [270, "!=", 14], [271, "!=", 14],
                               [272, "!=", 14], [273, "!=", 14]],
       "note": f"organ {what} only shapes the organ, and nothing plays it (harp or chord voice 13, or a note voice 14)"}
      for a, what in ((388, "drawbars"), (389, "percussion"))),
    {"about": [386], "when": [[386, "!=", 60], [264, "!=", 10]],
     "note": "harp reed bark only shapes the reed piano, and the harp isn't playing it (harp voice 10)"},
    {"about": [387], "when": [[387, "!=", 60], [265, "!=", 10], [270, "!=", 11], [271, "!=", 11], [272, "!=", 11], [273, "!=", 11]],
     "note": "chord reed bark only shapes the reed piano, and no chord note plays it (chord voice 10, or a note voice 11)"},
    {"about": [376], "when": [[376, "!=", 180], [302, "==", 0]],
     "note": "hammer-on time shapes hammer-on, which is off (0)"},
    {"about": [377], "when": [[377, ">", 0], [335, "not in", [41, 42, 43]]],
     "note": "tabla laya only moves the tabla's thekas (drum grooves 41-43: kehrwa, dadra, teental), and no theka is chosen"},
    {"about": [378], "when": [[378, ">", 0], [334, "==", 0], [343, "==", 0]],
     "note": "drum reverb with no drums: neither drums nor harp drums is on"},
    {"about": [379], "when": [[379, ">", 0], [372, "==", 0]],
     "note": "drone reverb with the drone off (0)"},
    {"about": [381], "when": [[381, "!=", 60], [372, "==", 0]],
     "note": "drone jawari shapes the tanpura, and the drone is off (0)"},
    {"about": [381], "when": [[381, "!=", 60], [372, "==", 5]],
     "note": "drone jawari shapes the tanpura's strings; the shruti box (drone 5) has reeds, not strings"},
    {"about": [380], "when": [[380, ">", 0], [159, "==", 0], [163, "==", 0], [61, "==", 0], [64, "==", 0]],
     "note": "speed ramp moves the tremolo's and the vibrato's rates, and there is no tremolo or vibrato to hear it in"},
    {"about": [351, 36], "when": [[351, "==", 1], [36, "not in", [10, 11, 14, 15]]],
     "note": "melodic direction plays a way down only in scalar harp modes 10, 11, 14 and 15 (the custom scale, "
             "the maqams)"},
    {"about": [356, 357], "when": [[36, "not in", [14, 15]]],
     "note": "the maqam is heard in scalar harp modes 14 and 15 only"},
    {"about": [347, 348], "when": [[289, "==", 0]],
     "note": "koto press steps and koto press snap shape koto press, which is off (0)"},
    {"about": [350], "when": [[350, ">", 0], [245, "==", 0]],
     "note": "ribbon step snap is for the ribbon (harp ribbon), which is off"},
    {"about": [371, 110], "when": [[371, "==", 1], [110, "==", 1]],
     "note": "with MPE output on the bends carry each note's pitch, and MTS output sends nothing"},
    {"about": [349], "when": [[349, "!=", 0], [237, "not in", [10, 11, 12]]],
     "note": "modifier step only matters in 19, 24 and 31: in twelve the modifier has only the semitone"},
    {"about": [342, 274], "when": [[342, "==", 1], [274, "==", 0]],
     "note": "drums only on rhythm style 0: a chord press makes no sound; choose a rhythm style (1-14) and "
             "set rhythm bass or rhythm chords to 1 to play over the drums"},
    {"about": [342, 274, 275, 276], "when": [[342, "==", 1], [274, "!=", 0], [275, "!=", 1], [276, "!=", 1]],
     "note": "drums only: a chord press sounds through the voices the rhythm leaves to hold it, so set rhythm "
             "bass or rhythm chords to 1 to hear the chords over the drums"},
    {"about": [253, 252], "when": [[253, ">", 0], [252, "==", 0]],
     "note": "touch pressure reaches the sound only as far as touch velocity does: at touch velocity 0 only "
             "its MIDI goes out"},
    {"about": [163, 175], "when": [[163, ">", 0], [175, "==", 0]],
     "note": "the chords' vibrato is silent while chord vibrato intensity is 0"},
    {"about": [170, 171, 172, 173, 174, 175], "when": [[170, ">", 0], [175, "==", 0]],
     "note": "the chords' pitch bend envelope is silent while chord vibrato intensity is 0"},
    {"about": [163, 161], "when": [[163, ">", 0], [175, ">", 0], [161, "==", 0]],
     "note": "chord vibrato frequency is 0, so the vibrato holds still"},
    {"about": [163, 167], "when": [[163, ">", 0], [175, ">", 0], [167, "==", 0]],
     "note": "chord vibrato sustain is 0, so the vibrato dies away after its attack, hold and decay"},
    {"about": [265, 199], "when": [[265, "in", SAMPLE_VOICES], [199, ">", 0]],
     "note": "glide doesn't reach a sampled chord voice: a sample jumps to each pitch"},
    {"about": [265, 320], "when": [[265, "in", SAMPLE_VOICES], [320, "==", 1]],
     "note": "fingered glide doesn't reach a sampled chord voice: a sample jumps to each pitch"},
    {"about": [265, 163, 170, 175], "when": [[265, "in", SAMPLE_VOICES], [175, ">", 0]],
     "note": "vibrato and pitch bend don't reach a sampled chord voice, only the synth's oscillators"},
    {"about": [264, 64, 71], "when": [[264, "in", SAMPLE_VOICES], [64, ">", 0]],
     "note": "the harp's vibrato doesn't reach a sampled harp voice, only its own synth"},
    {"about": [264, 71], "when": [[264, "in", SAMPLE_VOICES], [71, ">", 0]],
     "note": "the harp's pitch bend doesn't reach a sampled harp voice, only its own synth"},
    {"about": [38, 111], "when": [[38, ">", 0], [111, ">", 0]],
     "note": "voice leading replaces chord spacing: the spacing isn't heard while voice leading is on"},
    {"about": [38, 120], "when": [[38, ">", 0], [111, "==", 0], [120, "==", 0]],
     "note": "with chord shuffling at 0 the chords sit too low for chord spacing to drop a note: it isn't heard"},
    {"about": [390, 39], "when": [[390, "==", 1], [39, "==", 1]],
     "note": "brothers walks the standard chords: under the alternate layout's sus and ninth chords it isn't heard"},
    {"about": [390, 115], "when": [[390, "==", 1], [115, ">", 0]],
     "note": "brothers takes over from the cantus under the standard chords; the cantus still steers the augmented, the alternate layout's chords and notes outside the scale"},
    {"about": [390, 36, 98], "when": [[390, "==", 1], [98, "==", 0], [36, "not in", [8, 9]]],
     "note": "brothers' brother chords come from notes between the chord's own: harp mode 8 or 9 plays them, other harp modes only some or none"},
    {"about": [39, 31, 33], "when": [[39, "==", 1]],
     "note": "the alternate chord layout turns the sharp button off (and sharp function and Barry Harris mode with it)"},
    {"about": [36, 98], "when": [[36, "!=", 0], [98, "==", 1]],
     "note": "chromatic harp comes first: scalar harp mode isn't heard while chromatic mode is on"},
    {"about": [40, 36, 98], "when": [[40, "!=", 0], [98, "==", 1]],
     "note": "harp shuffling reorders the chord's notes only: it isn't heard in chromatic mode"},
    {"about": [40, 36, 98], "when": [[40, "!=", 0], [98, "==", 0], [36, "!=", 0]],
     "note": "harp shuffling reorders the chord's notes only: it isn't heard in scalar harp mode"},
    {"about": [257, 258, 245], "when": [[257, ">", 0], [258, "==", 0], [245, "==", 1]],
     "note": "string spread by pitch drifts on the ribbon, whose lowest and highest strings move as it plays: "
             "spread pattern 1 holds"},
    {"about": [259, 29], "when": [[259, ">", 0], [29, "==", 0]],
     "note": "at pan 0 the chords are hard right, so the chord ensemble can't spread them"},
    {"about": [108, 106, 107], "when": [[108, "==", 1], [106, "==", {"value": 107}]],
     "note": "in single port mode give the chords and the harp different MIDI channels: on one channel notes "
             "sent in never reach the chords, and a chord's note-off can end a harp note"},
    {"about": [110], "when": [[110, "==", 1]],
     "note": "MPE sends on its own channels (the chords 2-5, the harp 2-13 on its port, or 6-16 sharing one), "
             "not the chord and harp channels"},
    {"about": [340, 110], "when": [[340, "!=", 0], [110, "==", 1]],
     "note": "drum MIDI goes out on channel 10, which MPE gives to a harp string: an MPE synth would play the "
             "drums as that string"},
    {"about": [188, 189, 191], "when": [[274, "!=", 0]],
     "note": "cycle length, measure update and note pushed duration shape rhythm style 0 only"},
    {"about": [117, 10], "when": [[117, "==", 1], [10, "==", 0]],
     "note": "on knob layer 1 the chord knob plays its alternate only, and none is assigned: the knob does nothing"},
    {"about": [117, 12], "when": [[117, "==", 1], [12, "==", 0]],
     "note": "on knob layer 1 the harp knob plays its alternate only, and none is assigned: the knob does nothing"},
    {"about": [117, 16], "when": [[117, "==", 1], [16, "==", 0], [14, "!=", 0]],
     "note": "on knob layer 1 the mod knob plays its alternate only, and none is assigned: the knob does nothing"},
]
for _k, _r, _name in KNOBS:
    CONFLICTS += [
        {"about": [_k, {"at": _k}], "when": [[{"at": _k}, "!sweepable", None]],
         "note": f"{_name} can't move {{target}}: that setting is for the double tap, or for nothing live"},
        {"about": [_k, _r, {"at": _k}], "when": [[{"at": _k}, "sweepable", None], [{"at": _k}, "!selector", None],
                                                 [{"at": _k}, "==", 0]],
         "note": f"{_name} sweeps {{target}} around the preset's own value, which is 0, so it does nothing: "
                 f"give {{target}} a value first"},
        {"about": [_k, _r], "when": [[{"at": _k}, "sweepable", None], [{"at": _k}, "!selector", None],
                                     [_r, "==", 0]],
         "note": f"{_name}'s range is 0, so it doesn't move {{target}}"},
        {"about": [_k], "when": [[_k, "in", list(range(202, 209))]],
         "note": f"{_name} can't move a chord layout slot: only the double tap can"},
    ]
for _h, _name in ((249, "hover"), (325, "the alternate hover")):
    CONFLICTS += [
        {"about": [_h, {"at": _h}], "when": [[{"at": _h}, "!sweepable", None]],
         "note": f"{_name} can't move {{target}}: that setting is for the double tap, or for nothing live"},
        {"about": [_h, 10, 12, 14, 16], "when": [[{"at": _h}, "sweepable", None],
                                                  [_h, "in", {"values": [10, 12, 14, 16]}]],
         "note": f"{_name} and a knob both move {{target}}: hover works from the preset's value, so the knob's "
                 f"setting is lost each time a hand passes"},
        {"about": [_h], "when": [[_h, "in", list(range(202, 209))]],
         "note": f"{_name} can't move a chord layout slot: only the double tap can"},
        {"about": [_h], "when": [[_h, "==", 109]],
         "note": f"{_name}'s values stop at 4095, so on master tuning it always lands on 432 Hz"},
    ]
for _t in (200, 209, 211):
    CONFLICTS += [
        {"about": [_t, 10, 12, 14, 16], "when": [[_t, "!=", 0], [_t, "in", {"values": [10, 12, 14, 16]}]],
         "note": "the double tap and a knob both set {target}: the knob puts its own position back at once"},
        {"about": [_t], "when": [[_t, "==", 109]],
         "note": "double tap values stop at 4095, so on master tuning it always lands on 432 Hz"},
    ]


# ---- songs ----
# songs.json: one line a song, its chords and harp described in these same words, so the list grows
# without code. A title of one word ("Jump") needs its artist, or "alone": true, so it can't fire on
# an ordinary word.
SONGS_JSON = os.path.join(HERE, "songs.json")


def load_songs():
    try:
        with open(SONGS_JSON) as f:
            songs = json.load(f)
    except FileNotFoundError:
        return
    for song in songs:
        title, by = song["song"].replace(".", ""), song.get("by", "").replace(".", "")
        artists = [by] + ([by[4:]] if by.lower().startswith("the ") else []) if by else []
        words = [title] if (len(tokens(normalise(title))) >= 2 or song.get("alone")) and not song.get("no_title") else []
        for b in artists:
            words += [f"{title} by {b}", f"{b} {title}"]
        words += song.get("words", [])
        note = song.get("note")
        recipe = " ".join(song.get(k) or "" for k in ("chords", "harp", "both")).lower()
        if "vocoder" in recipe or "talk box" in recipe:   # a recipe's own notes aren't shown, so say it here
            note = (note + "; " if note else "") + ("this one talks: the vocoder needs a voice coming in over USB "
                                                    "(usb audio set to 0 or 1 in minicontrol)")
        V.append(Entry(tuple(words), "song", f"{title} ({by})" if by else title, hue=song.get("hue"),
                       note=note, base=song.get("base"), chords=song.get("chords"),
                       harp=song.get("harp"), both=song.get("both")))


load_songs()
