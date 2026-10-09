"""Speech in for preset_maker: record from a microphone until Enter, then Whisper, run locally
(faster-whisper), turns it into text. Nothing leaves the computer until the text goes to Claude.

The minichord is a USB audio input too, and a desktop often makes it the default one, which would
record the instrument instead of the player: an input with "minichord" in its name, or a monitor of
an output, is never picked unless asked for by name.
"""
import os, shutil, subprocess, sys, threading

RATE = 16000   # what Whisper takes
# Words Whisper would otherwise mishear, given as a hint of the vocabulary to expect
VOCABULARY = ("Minichord preset. Chord buttons, the harp, chord knob, harp knob, mod knob, modifier, "
              "double tap, hover, looper, vocoder, formant, reverb, delay, crunch, tremolo, vibrato, "
              "resonance, low pass filter, envelope, attack, decay, sustain, release, sawtooth, square, "
              "triangle, sine, pulse, string model, palm mute, strum velocity, arpeggio, Omnichord.")


def pulse_sources():
    """(name, description) of each PulseAudio/PipeWire input, or [] where there's no pactl"""
    if not shutil.which("pactl"):
        return []
    try:
        out = subprocess.run(["pactl", "list", "short", "sources"], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    return [line.split("\t")[1] for line in out.splitlines() if "\t" in line]


def pulse_default():
    try:
        return subprocess.run(["pactl", "get-default-source"], capture_output=True, text=True, timeout=5).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return ""


def usable(name):
    n = name.lower()
    return "minichord" not in n and not n.endswith(".monitor")


def list_mics():
    import sounddevice as sd
    sources = pulse_sources()
    if sources:
        print("Sound server inputs (--mic takes any part of the name):")
        default = pulse_default()
        for s in sources:
            notes = []
            if s == default:
                notes.append("default")
            if not usable(s):
                notes.append("not a microphone" if s.endswith(".monitor") else "the minichord itself")
            print(f"  {s}" + (f"   ({', '.join(notes)})" if notes else ""))
        print()
    print("Audio devices (--mic takes the number or any part of the name):")
    for i, d in enumerate(sd.query_devices()):
        if d["max_input_channels"] > 0:
            print(f"  {i}: {d['name']}" + ("   (the minichord itself)" if "minichord" in d["name"].lower() else ""))


class Microphone:
    def __init__(self, choice=None):
        import sounddevice as sd
        self.sd = sd
        self.device, self.name = self._pick(choice)

    def _pick(self, choice):
        sd = self.sd
        sources = pulse_sources()
        has_pulse = any(d["name"] == "pulse" for d in sd.query_devices())
        if sources and has_pulse:
            # through the sound server, naming the source, so the desktop's default doesn't matter
            if choice is not None:
                match = [s for s in sources if choice.lower() in s.lower()]
                if match:
                    os.environ["PULSE_SOURCE"] = match[0]
                    return "pulse", match[0]
            else:
                default = pulse_default()
                pick = default if usable(default) else next((s for s in sources if usable(s)), None)
                if pick is None:
                    raise RuntimeError("no microphone found besides the minichord; plug one in, or see --list-mics")
                os.environ["PULSE_SOURCE"] = pick
                return "pulse", pick
        devices = sd.query_devices()
        if choice is not None:
            if choice.isdigit() and int(choice) < len(devices):
                return int(choice), devices[int(choice)]["name"]
            for i, d in enumerate(devices):
                if d["max_input_channels"] > 0 and choice.lower() in d["name"].lower():
                    return i, d["name"]
            raise RuntimeError(f"no input called {choice!r}; see --list-mics")
        i = sd.default.device[0]
        if i is not None and i >= 0 and usable(devices[i]["name"]):
            return i, devices[i]["name"]
        for i, d in enumerate(devices):
            if d["max_input_channels"] > 0 and usable(d["name"]):
                return i, d["name"]
        raise RuntimeError("no microphone found besides the minichord; see --list-mics")

    def record(self):
        """Records from Enter to Enter; returns mono float32 at 16 kHz"""
        import numpy as np
        sd = self.sd
        chunks = []
        rate = RATE
        try:
            sd.check_input_settings(device=self.device, channels=1, samplerate=RATE)
        except Exception:
            rate = int(sd.query_devices(self.device)["default_samplerate"])

        def take(data, frames, t, status):
            chunks.append(data[:, 0].copy())

        with sd.InputStream(device=self.device, channels=1, samplerate=rate, dtype="float32", callback=take):
            input("  Recording... press Enter when you're done. ")
        if not chunks:
            return np.zeros(0, dtype=np.float32)
        audio = np.concatenate(chunks)
        if rate != RATE:
            t_in = np.arange(len(audio)) / rate
            t_out = np.arange(int(len(audio) * RATE / rate)) / RATE
            audio = np.interp(t_out, t_in, audio).astype(np.float32)
        return audio


class Transcriber:
    def __init__(self, model="base.en"):
        self.model_name = model
        self.model = None

    def __call__(self, audio):
        if self.model is None:
            from faster_whisper import WhisperModel
            print(f"  Loading the speech model ({self.model_name}; the first time, it downloads)...", flush=True)
            self.model = WhisperModel(self.model_name, device="cpu", compute_type="int8")
        language = "en" if self.model_name.endswith(".en") else None
        segments, _ = self.model.transcribe(audio, language=language, initial_prompt=VOCABULARY, vad_filter=True)
        return " ".join(s.text.strip() for s in segments).strip()


class VoiceInput:
    """Asks for each request by voice, with typing always open as well."""

    def __init__(self, mic=None, model="base.en"):
        self.mic = Microphone(mic)
        self.transcribe = Transcriber(model)
        print(f"Microphone: {self.mic.name}")

    def listen(self):
        while True:
            audio = self.mic.record()
            if len(audio) < RATE // 4:
                print("  That was too short to hear anything.")
                return ""
            print("  Listening back...", flush=True)
            text = self.transcribe(audio)
            if not text:
                print("  I didn't catch any words.")
                return ""
            print(f'  Heard: "{text}"')
            answer = input("  Enter to use it, 'r' to say it again, or type a correction: ").strip()
            if answer.lower() == "r":
                continue
            return answer or text

    def ask(self, prompt, first=False):
        """None means finish. Enter speaks, anything typed is used as it is."""
        while True:
            hint = "Enter to speak, or type it" + ("" if first else "; 'done' to finish, 'undo' for the last version")
            typed = input(f"{prompt} ({hint}) > ").strip()
            if typed.lower() in ("done", "quit", "exit"):
                return None
            if typed:
                return typed
            text = self.listen()
            if text:
                return text


class TextInput:
    def ask(self, prompt, first=False):
        hint = "" if first else " (enter to finish, 'undo' for the last version)"
        try:
            text = input(f"{prompt}{hint} > ").strip()
        except EOFError:
            return None
        return text or None
