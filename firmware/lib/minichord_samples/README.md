# minichord_samples

The sampled instruments the harp and chord voices can play (harp voice, 264; chord voice, 265): piano, pizzicato strings, choir and a string quartet (1-4), and since firmware 73 a vibraphone, a fingered bass, a sitar and an electric piano (6-9; 5 is the song, grabbed from the phone deck), as `AudioSynthWavetable::instrument_data`.

`src/minichord_samples.cpp` is generated; don't edit it. To rebuild it, run `generator/samples.py` with a folder of one-note-per-file recordings laid out as the Minichord Lab's `samples/` is (`piano/`, `quartet/{pizz,cello,viola,violin}/`, `choir/musyng/aah/`, each note named by its MIDI number), plus `quartet/pizz-fluid/`: FluidR3_GM's `pizzicato_strings-mp3` from the same project, named the same way (Musyng Kite's pizzicato is two sections a semitone apart from 48 to 62, so the middle of the range comes from FluidR3), and `vibraphone/fluid/`, `bass/fluid/`, `sitar/musyng/` and `epiano/fluid/` (the Lab's `tools/fetch-instrument-samples.sh` fetches them). It needs ffmpeg and numpy:

    python3 generator/samples.py ~/minichord-lab/samples

It prints each instrument's size. The sample data sits in flash beside the program, below the presets' storage at the top of flash (1 MB until firmware 61, 512 kB from 61 to 71, when each preset was a file of its own and on the Teensy 4.0 a file takes a 32 kB block; 256 kB since 72, the twelve presets in one file). `pio run` reports what is left as "free for files": take 256 kB (262144 bytes) from it for what the next sample can use, about 547 kB at firmware 72 and 174 kB at firmware 73.

See `LICENSE` for where the recordings come from and their terms (CC BY 3.0 and CC BY-SA 3.0).
