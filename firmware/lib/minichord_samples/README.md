# minichord_samples

The sampled instruments the harp and chord voices can play (harp voice, 264; chord voice, 265): piano, pizzicato strings, choir and a string quartet, as `AudioSynthWavetable::instrument_data`.

`src/minichord_samples.cpp` is generated; don't edit it. To rebuild it, run `generator/samples.py` with a folder of one-note-per-file recordings laid out as the Minichord Lab's `samples/` is (`piano/`, `quartet/{pizz,cello,viola,violin}/`, `choir/musyng/aah/`, each note named by its MIDI number), plus `quartet/pizz-fluid/`: FluidR3_GM's `pizzicato_strings-mp3` from the same project, named the same way (Musyng Kite's pizzicato is two sections a semitone apart from 48 to 62, so the middle of the range comes from FluidR3). It needs ffmpeg and numpy:

    python3 generator/samples.py ~/minichord-lab/samples

It prints each instrument's size. The sample data sits in flash beside the program, below the presets' storage at the top of flash (1 MB until firmware 61, 512 kB until 70, 128 kB since). `pio run` reports what is left as "free for files": take 128 kB (131072 bytes) from it for what the next sample can use. Firmware 70 left about 675 kB.

See `LICENSE` for where the recordings come from and their terms (CC BY 3.0 and CC BY-SA 3.0).
