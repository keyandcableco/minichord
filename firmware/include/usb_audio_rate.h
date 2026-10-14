// The recording at two rates, for usb audio 2 (the speakerless USB configuration, address 244 in
// src/main.cpp): the part with nothing of the Teensy in it, so that tools/usb_audio_rate_test.cpp can run
// it on a computer. The Teensy side is the core's usb_audio.cpp and usb.c as scripts/usb_audio_choice.py
// builds them; with the speaker offered (0 and 1) none of this runs and the core's own code does.
//
// Why two rates: a Pixel 8a (likely any Pixel with the same audio processor, its AoC) hands USB audio to
// that processor, whose own rate is 48 kHz. Given 44.1 kHz from a full-speed device it takes it, but its
// capture dejitter ran dry about a hundred times a second ("USB RX Dejitter underrun" in its log),
// adaptive or asynchronous alike, and the sound came out choppy. On that Pixel the audio service asked
// for 48000 and took it as soon as the device offered it, so the recording offers 48000 beside 44100,
// and the host picks with the standard sampling frequency control.
//
// Why the device decides the packet sizes: the core's own sender counts 44 frames a packet with 45 every
// tenth, exactly 44100 a second by the host's clock, while the audio library makes its frames by the
// Teensy's crystal. The two clocks never quite agree, so sooner or later the core's two-block queue would
// either overflow and throw a whole 128-frame block away or run dry and pad a packet with silence: a click.
// The recording is asynchronous (its own clock), so the device may send as many frames as it has, and
// here the count follows how full the ring is, whatever the host's clock does.
//
// The ring: the audio library's update (the producer, in the audio interrupt) writes each 128-frame stereo
// block into it; the USB transmit (the consumer, in the USB interrupt, which can interrupt the producer
// halfway through a block) reads it. Each side moves only its own index, and the producer moves head only
// once the block's frames are written, so the consumer never sees a frame that's still being written. A
// frame is one 32-bit word, packed as the core's copy_from_buffers packs it: the USB left channel (the
// AudioOutputUSB's input 0) in the low half, sent first, the right (input 1) in the high half.
//
// 44100: the frames go through as they are, none changed, dropped or repeated, 44 or 45 to a packet (46
// when the ring is well over), as many 45s as keep the ring where it should be.
// 48000: a polyphase resampler steps 44100/48000 = 147/160 of an input frame for each frame it makes, so
// its 160 phases are exact and the pitch is the audio library's own; 47, 48 or 49 frames go to a packet,
// chosen by the ring the same way. 16 taps of a Kaiser-windowed sinc: a sine up to 15 kHz comes out
// about -82 dB THD+N, where a 4-point Hermite managed 26 dB at 10 kHz.
//
// Starting (the host chose the recording's streaming setting, or changed the rate) the ring is emptied and
// the first frame waits for three blocks to arrive; meanwhile, and after a run dry, packets of the nominal
// length are silence. If the producer ever finds the ring full (the host stopped asking for packets but
// didn't say so), what's in the ring no longer joins on to what comes next, so it starts again the same way.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

// Where the code goes on the Teensy (on a computer these are nothing). Its RAM1 holds the program's code in
// 32 KB blocks and the stack gets what the last block leaves, and the code had under 2 KB to go in its last
// block, so all but write_block() (in the audio interrupt) runs from flash, FLASHMEM: USB_AUDIO_RATE_SELDOM
// (not inlined) for what runs once or seldom, the coefficients, starting again, a new rate; USB_AUDIO_RATE_FLASH
// for packet() and what it's made of, every millisecond in the USB interrupt, which then runs from the
// processor's instruction cache, where hardly anything else in flash runs to push it out. The functions with
// either are static, a copy for each file that includes this (one does, on the Teensy): GCC won't put
// functions shared between files into a named section beside ones that aren't.
#ifndef USB_AUDIO_RATE_SELDOM
#define USB_AUDIO_RATE_SELDOM
#endif
#ifndef USB_AUDIO_RATE_FLASH
#define USB_AUDIO_RATE_FLASH
#endif

namespace usb_audio_rate {

constexpr uint32_t BLOCK = 128;                // frames in an audio library block
// The ring is kept two and a half blocks full on average (7 ms at 44.1 kHz): a block arrives every 2.9 ms,
// but anywhere in that time, as the audio interrupt's other work comes before it or not, so two blocks can
// come 5.8 ms apart, and a packet's frames have to be there all the same. Eight blocks of room leave
// another 11 ms for a host that stops asking for packets for a moment.
constexpr uint32_t RING = 1024;                // frames the ring holds, a power of two
constexpr uint32_t START = 3 * BLOCK;          // frames in the ring before the first is sent
constexpr int32_t TARGET = START - BLOCK / 2;  // the ring's average fill
constexpr uint32_t MAX_FRAMES = 49;            // the most a packet holds: 196 bytes, the endpoint's wMaxPacketSize
constexpr int TAPS = 16;                       // the resampler's taps, a power of two
constexpr uint32_t PHASES = 160;               // 48000 / 44100 = 160 / 147
constexpr uint32_t STEP = 147;
constexpr uint32_t STORED = PHASES / 2 + 1;    // phases 0 to 80 are kept; 81 to 159 are 79 to 1 backwards
// frames a packet in 1/65536ths, as each rate's nominal count per USB frame (1 ms)
constexpr int32_t NOMINAL_44 = 2890138;        // 44.1
constexpr int32_t NOMINAL_48 = 3145728;        // 48
// how the packet sizes follow the ring: its fill at each packet averaged over about 64 ms, which smooths
// away the sawtooth of a block arriving every 2.9 ms, then 1/512 of a frame a packet for each frame the
// average is from TARGET, and the sum of those (1/1048576 each) to learn the difference between the
// clocks: a loop that settles in a few seconds without overshooting, and holds the ring at TARGET.
// How far apart the clocks can be: the sum learns up to 0.1 frame a packet, 2270 ppm at 44.1 and 2080 at
// 48, and the ring's distance from TARGET makes up more; but at 44.1 a packet is never under 44 frames, so
// a host more than 2270 ppm fast (44.1 / 44) takes more than is made and the ring runs dry now and then.
// USB holds a host's 1 ms frames to 500 ppm; tools/usb_audio_rate_test.cpp runs 2000 either way, both rates.
constexpr int AVERAGE_SHIFT = 6;
constexpr int PROPORTIONAL_SHIFT = 9;
constexpr int INTEGRAL_SHIFT = 20;
constexpr int64_t INTEGRAL_LIMIT = (int64_t)6554 << INTEGRAL_SHIFT;   // 0.1 frame a packet

inline uint32_t pack(int16_t left, int16_t right)
{
	return (uint32_t)(uint16_t)left | ((uint32_t)(uint16_t)right << 16);
}

// Constant-initialized, so the firmware's copy is ready before the C++ constructors run (USB can start
// first): no constructors here, nor in Sender.
struct Ring {
	uint32_t *frame;     // RING frames, the owner's (the firmware's are in RAM2)
	uint32_t head;       // frames ever written: the producer's, moved only after the frames are there
	uint32_t tail;       // frames ever read: the consumer's
	uint32_t drops;      // blocks the producer found no room for: the producer's
};

// The producer: one block, left and right (either may be null, for silence). midway(i) runs after the i-th
// frame is written; the firmware's does nothing, the host test's lets the consumer interrupt there.
template <class Midway>
inline bool write_block(Ring &r, const int16_t *left, const int16_t *right, Midway &&midway)
{
	const uint32_t head = r.head;
	const uint32_t tail = __atomic_load_n(&r.tail, __ATOMIC_ACQUIRE);
	if (RING - (head - tail) < BLOCK) {
		__atomic_store_n(&r.drops, r.drops + 1, __ATOMIC_RELEASE);
		return false;
	}
	for (uint32_t i = 0; i < BLOCK; i++) {
		r.frame[(head + i) & (RING - 1)] = pack(left ? left[i] : 0, right ? right[i] : 0);
		midway(i);
	}
	__atomic_store_n(&r.head, head + BLOCK, __ATOMIC_RELEASE);
	return true;
}
inline bool write_block(Ring &r, const int16_t *left, const int16_t *right)
{
	return write_block(r, left, right, [](uint32_t) {});
}

// The resampler's coefficients, Q15: phase p's tap i weighs the input frame 1 + i - TAPS/2 - p/160 frames
// from where the output frame falls. Each phase sums to exactly 32768, so no phase is louder than another.
struct Table {
	int16_t c[STORED][TAPS];
};

USB_AUDIO_RATE_SELDOM static inline double bessel_i0(double x)
{
	double sum = 1, term = 1;
	for (int k = 1; k < 40; k++) {
		term *= (x / (2 * k)) * (x / (2 * k));
		sum += term;
		if (term < sum * 1e-17) break;
	}
	return sum;
}

// cutoff 0.87 of the input's Nyquist (19.2 kHz), Kaiser beta 9: flat to 10 kHz, 0.5 dB down at 15 kHz and
// 3.5 at 18; a sine up to 15 kHz about -82 dB THD+N, at 18 kHz -54 (tools/usb_audio_rate_test.cpp measures)
USB_AUDIO_RATE_SELDOM static inline void make_table(Table &t)
{
	const double cutoff = 0.87, beta = 9.0, pi = 3.14159265358979323846;
	const double i0_beta = bessel_i0(beta);
	for (uint32_t p = 0; p < STORED; p++) {
		double c[TAPS], sum = 0;
		for (int i = 0; i < TAPS; i++) {
			const double d = (1 + i - TAPS / 2) - (double)p / PHASES;
			const double x = cutoff * d;
			const double sinc = x == 0 ? 1 : sin(pi * x) / (pi * x);
			const double r = d / (TAPS / 2);
			const double window = r <= -1 || r >= 1 ? 0 : bessel_i0(beta * sqrt(1 - r * r)) / i0_beta;
			c[i] = sinc * window;
			sum += c[i];
		}
		int total = 0, largest = 0;
		for (int i = 0; i < TAPS; i++) {
			const double q = c[i] / sum * 32768;
			t.c[p][i] = (int16_t)(q < 0 ? q - 0.5 : q + 0.5);
			total += t.c[p][i];
			if (c[i] > c[largest]) largest = i;
		}
		t.c[p][largest] += 32768 - total;   // what the rounding left over, onto the largest tap
	}
}

// The consumer: everything in it is the USB interrupt's.
struct Sender {
	uint32_t rate;                 // 44100 or 48000, as the host set it
	uint32_t running;              // sending frames from the ring (0: waiting for it to fill)
	uint32_t drops_seen;
	int32_t average;               // the ring's fill, averaged, in 1/65536ths of a frame
	int64_t integral;
	uint32_t fraction;             // of a frame, carried from packet to packet
	uint32_t phase;                // the resampler's, in 160ths of an input frame
	uint32_t newest;               // where in history the newest input frame is
	uint32_t history[2 * TAPS];    // the last TAPS input frames, twice over so they're always in a row
	// counted for the host test (and anyone looking)
	uint32_t restarts, underruns, last_silent;
};

USB_AUDIO_RATE_FLASH static inline int16_t saturate(int32_t v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
}

// the frame the resampler makes now, from the last TAPS input frames
USB_AUDIO_RATE_FLASH static inline uint32_t interpolate(const Sender &s, const Table &t)
{
	const uint32_t *x = s.history + s.newest + 1;   // oldest first
	const bool stored = s.phase < STORED;          // or phase p is phase 160 - p backwards
	const int16_t *c = stored ? t.c[s.phase] : t.c[PHASES - s.phase] + TAPS - 1;
	const int step = stored ? 1 : -1;
	int32_t left = 0, right = 0;
	for (int i = 0; i < TAPS; i++, c += step) {
		left += *c * (int32_t)(int16_t)x[i];
		right += *c * (int32_t)(int16_t)(x[i] >> 16);
	}
	return pack(saturate((left + 16384) >> 15), saturate((right + 16384) >> 15));
}

USB_AUDIO_RATE_FLASH static inline void push(Sender &s, uint32_t frame)
{
	s.newest = (s.newest + 1) & (TAPS - 1);
	s.history[s.newest] = s.history[s.newest + TAPS] = frame;
}

// Empty the ring (the consumer's way: tail to head) and start again, the resampler from silence. Called
// when the host sets the recording's streaming setting, changes the rate, or the producer dropped a block.
USB_AUDIO_RATE_SELDOM static inline void restart(Sender &s, Ring &r)
{
	s.drops_seen = __atomic_load_n(&r.drops, __ATOMIC_ACQUIRE);
	__atomic_store_n(&r.tail, __atomic_load_n(&r.head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
	s.running = 0;
	s.phase = 0;
	s.newest = 0;
	memset(s.history, 0, sizeof(s.history));
	s.restarts++;
}

// The host configured the device: 44100 until it says otherwise, and nothing learned yet.
USB_AUDIO_RATE_SELDOM static inline void reset(Sender &s, Ring &r)
{
	s.rate = 44100;
	s.integral = 0;
	s.fraction = 0;
	restart(s, r);
}

// SET_CUR: 44100 and 48000 are taken (and only a change starts again, so setting the rate it already
// has costs nothing); anything else leaves the rate as it was, which GET_CUR then tells the host.
USB_AUDIO_RATE_SELDOM static inline bool set_rate(Sender &s, Ring &r, uint32_t hz)
{
	if (hz != 44100 && hz != 48000) return false;
	if (hz == s.rate) return true;
	s.rate = hz;
	s.integral = 0;      // what was learned was in the other rate's frames
	s.fraction = 0;
	restart(s, r);
	return true;
}

// frames for this packet: the rate's nominal count, corrected (when steer) by the ring's average fill
USB_AUDIO_RATE_FLASH static inline uint32_t count(Sender &s, bool steer)
{
	const bool high = s.rate == 48000;
	int32_t rate = high ? NOMINAL_48 : NOMINAL_44;
	if (steer) {
		const int32_t error = s.average - (TARGET << 16);
		s.integral += error;
		if (s.integral > INTEGRAL_LIMIT) s.integral = INTEGRAL_LIMIT;
		if (s.integral < -INTEGRAL_LIMIT) s.integral = -INTEGRAL_LIMIT;
		rate += error >> PROPORTIONAL_SHIFT;
	}
	rate += (int32_t)(s.integral >> INTEGRAL_SHIFT);
	s.fraction += (uint32_t)rate;
	uint32_t n = s.fraction >> 16;
	s.fraction &= 0xFFFF;
	const uint32_t lo = high ? 47 : 44, hi = high ? 49 : 46;
	return n < lo ? lo : n > hi ? hi : n;
}

// The next packet into out (MAX_FRAMES frames' room); returns its frames.
USB_AUDIO_RATE_FLASH static inline uint32_t packet(Sender &s, Ring &r, const Table &t, uint32_t *out)
{
	if (__atomic_load_n(&r.drops, __ATOMIC_ACQUIRE) != s.drops_seen) restart(s, r);
	const uint32_t head = __atomic_load_n(&r.head, __ATOMIC_ACQUIRE);
	uint32_t tail = r.tail;
	uint32_t fill = head - tail;
	if (!s.running) {
		if (fill < START) {
			const uint32_t n = count(s, false);
			memset(out, 0, n * 4);
			s.last_silent = n;
			return n;
		}
		// more than START when two blocks came between two packets (the third late and the fourth early,
		// the audio interrupt's lateness differing that much from one block to the next) or no packet was
		// asked for a while: none of it has been sent, so the oldest block or blocks go unheard
		if (fill > START) {
			tail = head - START;
			fill = START;
		}
		s.running = 1;
		s.average = (int32_t)(fill - BLOCK / 2) << 16;   // the sawtooth's middle, a block having just come
	}
	s.average += (((int32_t)fill << 16) - s.average) >> AVERAGE_SHIFT;
	const uint32_t n = count(s, true);
	uint32_t made;
	if (s.rate != 48000) {
		made = n < fill ? n : fill;
		for (uint32_t i = 0; i < made; i++) out[i] = r.frame[(tail + i) & (RING - 1)];
		tail += made;
	} else {
		// as many frames as the ring has input for: frame k of the packet takes an input frame after it
		// whenever the phase passes 160
		const uint32_t can = (fill * PHASES + PHASES - 1 - s.phase) / STEP;
		made = n < can ? n : can;
		for (uint32_t i = 0; i < made; i++) {
			out[i] = interpolate(s, t);
			s.phase += STEP;
			if (s.phase >= PHASES) {
				s.phase -= PHASES;
				push(s, r.frame[tail & (RING - 1)]);
				tail++;
			}
		}
	}
	__atomic_store_n(&r.tail, tail, __ATOMIC_RELEASE);
	s.last_silent = n - made;
	if (made < n) {   // ran dry: silence for the rest, and wait for the ring to fill again
		memset(out + made, 0, (n - made) * 4);
		s.running = 0;
		s.underruns++;
	}
	return n;
}

} // namespace usb_audio_rate
