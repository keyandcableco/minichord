// The recording at two rates (include/usb_audio_rate.h), run on a computer. To run it, from firmware/:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -Iinclude tools/usb_audio_rate_test.cpp -o /tmp/usb_audio_rate_test && /tmp/usb_audio_rate_test
//
// It puts the two interrupts against each other in time, as on the minichord: the audio library's update
// writing a 128-frame block into the ring every 2.9 ms by the Teensy's clock (late each time by anything
// up to 2.8 ms, nearly the whole 2.9, as the audio interrupt's other work comes before it or not), and the
// USB interrupt asking for a packet every 1 ms by the host's clock, which runs 500 ppm slow, true or 500
// ppm fast. A block takes 40 us to write here, and a packet falling inside that runs in the middle of it,
// as the USB interrupt (priority 128) interrupts the audio one (208). Each run is 10 minutes, at 44100, at
// 48000, and switching between them (SET_CUR while streaming, and the streaming setting off and on), with
// the audio library at 44117.647 Hz (what a Teensy 3 runs at, and what was asked for) and at 44100 (what
// the Teensy 4's I2S clock is set to: AUDIO_SAMPLE_RATE_EXACT 44100.0 in its core, the PLL4 fractions
// giving 44100 exactly). Then, a minute at each rate, the streaming setting turned on and off every few
// ms to every 400 (hundreds of starts, some of them finding four blocks waiting), and two minutes at each
// rate with the host 2000 ppm slow and fast, four times what USB allows.
//
// It checks, and prints:
//   44100: after the start every frame out is the next frame in, bit for bit, none dropped or repeated (the
//          input full-range noise, or sines with a pseudo-random lowest bit, so a slip can't line up); a
//          start that found more than three blocks begins with the newest three, the older ones unheard
//   48000: a 1 kHz sine on the left and a 10 kHz sine on the right come out clean (THD+N from a fitted sine,
//          100 ms windows), each channel only its own sine, the fitted phase never moving (a dropped or
//          repeated frame anywhere would move it by 0.14 rad at 1 kHz)
//   both:  packet sizes only 44/45/46 or 47/48/49, never over 196 bytes, silence only of nominal length
//          while starting, the ring never empty after it starts and never full, its fill bounded, and
//          the long-run rate equal to what the audio library makes
// and exits 1 if anything fails.
#include "usb_audio_rate.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

using namespace usb_audio_rate;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("    FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t splitmix(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

static Table table;

// ---------- what the audio library makes: frame n of the input ----------
enum Signal { NOISE, SINES };
struct Source {
	Signal kind;
	int f_left, f_right;               // Hz, at 44100 (the audio library's frequencies are by its 44100)
	double amplitude;
	std::vector<int16_t> left, right;   // a second of each sine: n mod 44100
	void prepare()
	{
		if (kind != SINES) return;
		left.resize(44100);
		right.resize(44100);
		for (int n = 0; n < 44100; n++) {
			left[n] = (int16_t)lrint(amplitude * sin(2 * M_PI * (double)((int64_t)f_left * n % 44100) / 44100));
			right[n] = (int16_t)lrint(amplitude * sin(2 * M_PI * (double)((int64_t)f_right * n % 44100) / 44100));
		}
	}
	void frame(uint64_t n, int16_t &l, int16_t &r) const
	{
		const uint64_t h = splitmix(n);
		if (kind == NOISE) {
			l = (int16_t)(h >> 16);
			r = (int16_t)(h >> 40);
		} else {   // the sine with a pseudo-random lowest bit, about 115 dB down
			l = (int16_t)((left[n % 44100] & ~1) | (int)((h >> 7) & 1));
			r = (int16_t)((right[n % 44100] & ~1) | (int)((h >> 9) & 1));
		}
	}
	uint32_t packed(uint64_t n) const
	{
		int16_t l, r;
		frame(n, l, r);
		return pack(l, r);
	}
};

// ---------- a sine fitted to a window of output ----------
struct Fit {
	double amplitude, phase, thdn_db, worst;   // phase in radians by the frame count since the start
};
static Fit fit(const int16_t *y, int n, int frequency, uint64_t first)
{
	const double w = 2 * M_PI * frequency / 48000.0;
	const double cw = cos(w), sw = sin(w);
	double m[3][4] = {};
	double s = 0, c = 1;
	for (int i = 0; i < n; i++) {
		const double x[3] = {s, c, 1};
		for (int a = 0; a < 3; a++) {
			m[a][3] += x[a] * y[i];
			for (int b = 0; b < 3; b++) m[a][b] += x[a] * x[b];
		}
		const double s2 = s * cw + c * sw;
		c = c * cw - s * sw;
		s = s2;
	}
	for (int i = 0; i < 3; i++)
		for (int k = i + 1; k < 3; k++) {
			const double f = m[k][i] / m[i][i];
			for (int j = i; j < 4; j++) m[k][j] -= f * m[i][j];
		}
	double v[3];
	for (int i = 2; i >= 0; i--) {
		double t = m[i][3];
		for (int j = i + 1; j < 3; j++) t -= m[i][j] * v[j];
		v[i] = t / m[i][i];
	}
	double residual = 0, worst = 0;
	s = 0, c = 1;
	for (int i = 0; i < n; i++) {
		const double e = y[i] - (v[0] * s + v[1] * c + v[2]);
		residual += e * e;
		worst = std::max(worst, fabs(e));
		const double s2 = s * cw + c * sw;
		c = c * cw - s * sw;
		s = s2;
	}
	Fit f;
	f.amplitude = hypot(v[0], v[1]);
	// v0 sin(wi) + v1 cos(wi) = A sin(wi + phi): from the start, phi less w times the frames before
	const double before = 2 * M_PI * (double)((uint64_t)frequency * first % 48000) / 48000;
	f.phase = remainder(atan2(v[1], v[0]) - before, 2 * M_PI);
	f.thdn_db = 10 * log10(residual / (n * f.amplitude * f.amplitude / 2));
	f.worst = worst;
	return f;
}

struct Channel {   // one channel's sine at 48000, window by window
	int frequency;
	std::vector<int16_t> window;
	uint64_t frames = 0;          // since the start
	bool have_phase = false;
	double phase0 = 0;
	// over the run
	int windows = 0;
	double worst_thdn = -999, sum_thdn = 0, worst_drift = 0, worst_error = 0, min_amp = 1e9, max_amp = 0;
	void start()
	{
		window.clear();
		frames = 0;
		have_phase = false;
	}
	void add(int16_t v)
	{
		window.push_back(v);
		if (window.size() < 4800) return;
		const uint64_t first = frames;
		frames += window.size();
		if (first > 0) {   // the first window has the resampler starting from silence
			const Fit f = fit(window.data(), (int)window.size(), frequency, first);
			if (!have_phase) {
				have_phase = true;
				phase0 = f.phase;
			}
			windows++;
			worst_thdn = std::max(worst_thdn, f.thdn_db);
			sum_thdn += f.thdn_db;
			worst_drift = std::max(worst_drift, fabs(remainder(f.phase - phase0, 2 * M_PI)));
			worst_error = std::max(worst_error, f.worst);
			min_amp = std::min(min_amp, f.amplitude);
			max_amp = std::max(max_amp, f.amplitude);
		}
		window.clear();
	}
};

// ---------- a run ----------
struct Control {
	double t;
	enum { ALT, RATE } kind;
	uint32_t value;
};
struct Run {
	std::string name;
	double producer_hz = 44117.647;
	double ppm = 0;               // the host's clock: + is fast, its millisecond shorter
	double seconds = 600;
	Source source = {NOISE, 0, 0, 0, {}, {}};
	std::vector<Control> controls;
	double jitter = 2.8e-3;       // the audio interrupt's lateness, 0 to this
	double write_time = 40e-6;    // the producer over one block
};
struct Outcome {
	uint64_t packets = 0, frames_44 = 0, frames_48 = 0, exact = 0, midway = 0;
	uint64_t sizes[2][MAX_FRAMES + 2] = {};
	uint32_t min_fill = RING, max_fill = 0, max_written = 0;
	double late_fill_sum = 0;
	uint64_t late_fill_n = 0, late_frames = 0, late_packets = 0;
	Channel left, right;
	uint32_t restarts = 0, underruns = 0, drops = 0;
	uint32_t trims = 0;               // starts that found more than START frames, the oldest going unheard
	uint64_t trimmed = 0;
};

static uint32_t ring_frames[RING];

static Outcome run(const Run &c)
{
	Outcome o;
	o.left.frequency = c.source.f_left;
	o.right.frequency = c.source.f_right;
	for (uint32_t &f : ring_frames) f = 0xDEADBEEF;   // a frame read before it's written shows
	Ring r = {ring_frames, 0, 0, 0};
	Sender s;
	memset(&s, 0, sizeof(s));                           // as the firmware's, in .bss
	reset(s, r);                                        // usb_audio_configure
	uint32_t restarts_seen = s.restarts;
	uint32_t alt = 0;
	const double producer_period = BLOCK / c.producer_hz;
	const double host_ms = 1e-3 / (1 + c.ppm * 1e-6);
	uint64_t next_packet = 0;
	size_t next_control = 0;
	uint64_t rng = 12345;
	auto random01 = [&]() { rng = splitmix(rng); return (rng >> 11) * (1.0 / 9007199254740992.0); };

	// the analysis of what's sent
	bool expecting = false, waiting_first = false;   // 44100: the next frame's number, once known
	uint64_t expect = 0;
	uint32_t out[MAX_FRAMES + 8];
	uint64_t writing = 0;                               // the block being written (its number)
	bool started_before = false;                        // the consumer has sent data since its last start
	auto started = [&]() {                              // the sender started again: the analysis too
		expecting = false;
		waiting_first = true;
		started_before = false;
		o.left.start();
		o.right.start();
	};

	auto usb_until = [&](double t) {   // every USB event before t, in order
		for (;;) {
			const double tp = next_packet * host_ms;
			const double tc = next_control < c.controls.size() ? c.controls[next_control].t : 1e300;
			if (std::min(tp, tc) >= t) return;
			if (tc <= tp) {
				const Control &k = c.controls[next_control++];
				if (k.kind == Control::ALT) {
					alt = k.value;
					restart(s, r);                       // usb_audio_rate_alt
				} else {
					const uint32_t was = s.rate;
					const bool ok = set_rate(s, r, k.value);
					CHECK(ok == (k.value == 44100 || k.value == 48000), "SET_CUR %u answered %d", k.value, ok);
					CHECK(s.rate == (ok ? k.value : was), "GET_CUR says %u after SET_CUR %u", s.rate, k.value);
				}
				if (s.restarts != restarts_seen) {
					restarts_seen = s.restarts;
					started();
				}
				continue;
			}
			const double now = next_packet * host_ms;
			next_packet++;
			if (!alt) continue;                          // no streaming, no packets asked for
			if (writing) o.midway++;
			const uint32_t rate = s.rate;
			const uint32_t before = r.head - r.tail;
			const bool was_running = s.running;
			const uint32_t n = packet(s, r, table, out);
			if (s.restarts != restarts_seen) {           // the producer had dropped a block
				restarts_seen = s.restarts;
				started();
			}
			// a start finding more than START frames (two blocks between two packets) sends the newest
			// START of them: the frames out begin that many later than the first block since the start
			uint32_t skip = 0;
			if (!was_running && s.running && before > START) {
				skip = before - START;
				CHECK(skip % BLOCK == 0, "a start skipping %u frames, not whole blocks", skip);
				o.trims++;
				o.trimmed += skip;
				if (expecting) expect += skip;
			}
			o.packets++;
			const int hi = rate == 48000;
			CHECK(n * 4 <= 196, "a packet of %u frames", n);
			o.sizes[hi][std::min<uint32_t>(n, MAX_FRAMES + 1)]++;
			if (hi) CHECK(n >= 47 && n <= 49, "a 48000 packet of %u", n);
			else CHECK(n >= 44 && n <= 46, "a 44100 packet of %u", n);
			const uint32_t data = n - s.last_silent;
			for (uint32_t i = data; i < n; i++) CHECK(out[i] == 0, "silence that isn't (%08x)", out[i]);
			if (s.last_silent && data == 0 && !started_before && !hi)   // starting: nominal length (44.1 a packet)
				CHECK(n == 44 || n == 45, "starting silence of %u at 44100", n);
			if (data) started_before = true;
			if (!hi) {
				o.frames_44 += data;
				for (uint32_t i = 0; i < data; i++) {
					if (!expecting) {
						CHECK(false, "a frame (%08x) before any block was published since the start", out[i]);
						continue;
					}
					if (out[i] != c.source.packed(expect)) {
						CHECK(false, "44100 frame %llu is %08x, should be %08x",
							(unsigned long long)expect, out[i], c.source.packed(expect));
						expecting = false;   // one report per slip
						break;
					}
					o.exact++;
					expect++;
				}
			} else {
				o.frames_48 += data;
				for (uint32_t i = 0; i < data; i++) {
					o.left.add((int16_t)out[i]);
					o.right.add((int16_t)(out[i] >> 16));
				}
			}
			if (s.running) {
				const uint32_t fill = r.head - r.tail;
				o.min_fill = std::min(o.min_fill, fill);
				o.max_fill = std::max(o.max_fill, fill);
			}
			if (now >= c.seconds / 2) {
				o.late_frames += n;
				o.late_packets++;
				o.late_fill_sum += before;   // as the packet found it
				o.late_fill_n++;
			}
		}
	};

	int16_t L[BLOCK], R[BLOCK];
	for (uint64_t k = 0;; k++) {
		const double start = k * producer_period + random01() * c.jitter;
		if (start >= c.seconds) break;
		usb_until(start);
		if (!alt) continue;                              // update() writes only while the host records
		for (uint32_t i = 0; i < BLOCK; i++) c.source.frame(k * BLOCK + i, L[i], R[i]);
		writing = k + 1;
		const bool ok = write_block(r, L, R, [&](uint32_t i) { usb_until(start + (i + 1) * c.write_time / BLOCK); });
		writing = 0;
		if (ok) {
			o.max_written = std::max(o.max_written, r.head - r.tail);
			if (waiting_first) {   // the first block since the start: where the frames out begin
				waiting_first = false;
				expecting = true;
				expect = k * BLOCK;
			}
		}
	}
	usb_until(c.seconds);
	o.restarts = s.restarts;
	o.underruns = s.underruns;
	o.drops = r.drops;
	return o;
}

// steady: one stream for most of the run, so its second half shows the long-run rate and the ring's average
static void report(const Run &c, const Outcome &o, bool has44, bool has48, int expected_restarts, bool steady = true)
{
	printf("  %s\n", c.name.c_str());
	printf("    %llu packets, %llu of them in the middle of a block's writing; restarts %u (expected %d), underruns %u, blocks dropped %u\n",
		(unsigned long long)o.packets, (unsigned long long)o.midway, o.restarts, expected_restarts, o.underruns, o.drops);
	printf("    starts that found more than %u frames: %u, %llu frames unheard\n", START, o.trims, (unsigned long long)o.trimmed);
	CHECK(o.underruns == 0, "the ring ran dry %u times", o.underruns);
	CHECK(o.drops == 0, "the producer found the ring full %u times", o.drops);
	CHECK((int)o.restarts == expected_restarts, "%u restarts", o.restarts);
	CHECK(o.midway > 100, "the consumer only %llu times in the middle of a block", (unsigned long long)o.midway);
	printf("    ring once started: min %u, max %u frames after a packet; max %u after a block (of %u)\n",
		o.min_fill, o.max_fill, o.max_written, RING);
	CHECK(o.min_fill > 0, "the ring emptied");
	CHECK(o.max_written < RING, "the ring filled");
	for (int hi = 0; hi < 2; hi++) {
		if (!(hi ? has48 : has44)) continue;
		printf("    %s packet sizes:", hi ? "48000" : "44100");
		for (uint32_t n = 0; n <= MAX_FRAMES + 1; n++)
			if (o.sizes[hi][n]) printf(" %u x%llu", n, (unsigned long long)o.sizes[hi][n]);
		printf("\n");
	}
	if (has44) {
		printf("    44100: %llu frames out, %llu checked equal to the input in order, bit for bit\n",
			(unsigned long long)o.frames_44, (unsigned long long)o.exact);
		CHECK(o.exact == o.frames_44 && o.exact > 0, "only %llu of %llu frames as they went in",
			(unsigned long long)o.exact, (unsigned long long)o.frames_44);
	}
	if (has48) {
		for (const Channel *ch : {&o.left, &o.right}) {
			const double expected = c.source.amplitude;
			printf("    48000 %s, %5d Hz: %d windows of 100 ms, THD+N worst %.1f dB (mean %.1f), largest error %.1f LSB,"
				" amplitude %.1f..%.1f (in %.0f), phase moved at most %.2g rad\n",
				ch == &o.left ? "left " : "right", ch->frequency, ch->windows, ch->worst_thdn,
				ch->windows ? ch->sum_thdn / ch->windows : 0, ch->worst_error, ch->min_amp, ch->max_amp, expected, ch->worst_drift);
			CHECK(ch->windows > 0, "no windows");
			CHECK(ch->worst_thdn < -75, "THD+N %.1f dB", ch->worst_thdn);
			CHECK(ch->worst_drift < 1e-3, "the phase moved %.3g rad: a frame slipped", ch->worst_drift);
			CHECK(fabs(ch->min_amp / expected - 1) < 0.01 && fabs(ch->max_amp / expected - 1) < 0.01, "amplitude off");
		}
	}
	if (steady && o.late_packets && has44 != has48) {
		const bool hi = has48 && !has44;
		const double made = c.producer_hz * (hi ? 160.0 / 147 : 1);   // frames a real second
		const double host_ms = 1e-3 / (1 + c.ppm * 1e-6);
		const double expected = made * host_ms, got = (double)o.late_frames / o.late_packets;
		printf("    second half: %.5f frames a packet (the audio library's rate makes %.5f), ring averaging %.1f (target %d)\n",
			got, expected, o.late_fill_sum / o.late_fill_n, TARGET);
		CHECK(fabs(got / expected - 1) < 20e-6, "the rate is %.1f ppm off", (got / expected - 1) * 1e6);
		CHECK(fabs(o.late_fill_sum / o.late_fill_n - TARGET) < 8, "the ring averages away from its target");
	}
}

// ---------- the small checks ----------
static void units()
{
	printf("units\n");
	// the channels packed as the core's copy_from_buffers: (right << 16) | (left & 0xFFFF), left first on the wire
	const int16_t pairs[][2] = {{1, 2}, {-1, 0}, {0, -1}, {-32768, 32767}, {32767, -32768}, {0x1234, -0x5678}};
	for (auto &p : pairs) {
		const uint32_t core = ((uint32_t)(int32_t)p[1] << 16) | ((uint32_t)(int32_t)p[0] & 0xFFFF);
		const uint32_t ours = pack(p[0], p[1]);
		uint8_t wire[4];
		memcpy(wire, &ours, 4);   // the Teensy is little-endian, like this computer
		CHECK(ours == core, "pack(%d, %d) %08x, the core makes %08x", p[0], p[1], ours, core);
		CHECK((int16_t)(wire[0] | wire[1] << 8) == p[0] && (int16_t)(wire[2] | wire[3] << 8) == p[1], "byte order");
	}
	// the table: every phase sums to 32768, the accumulators can't overflow, and the mirrored half matches
	int worst_sum = 0;
	for (uint32_t p = 0; p < STORED; p++) {
		int sum = 0, abs_sum = 0;
		for (int i = 0; i < TAPS; i++) {
			sum += table.c[p][i];
			abs_sum += abs(table.c[p][i]);
		}
		CHECK(sum == 32768, "phase %u sums to %d", p, sum);
		worst_sum = std::max(worst_sum, abs_sum);
	}
	CHECK(worst_sum <= 65535, "a phase's taps add to %d: the accumulator could overflow", worst_sum);
	CHECK(table.c[0][TAPS / 2 - 1] > 20000, "phase 0 isn't centred");
	for (int i = 0; i < TAPS; i++) CHECK(abs(table.c[PHASES / 2][i] - table.c[PHASES / 2][TAPS - 1 - i]) <= 2, "phase 80 isn't symmetric");
	printf("  table: %u phases of %d taps stored (%zu bytes), largest sum of |taps| %d (65535 at most)\n",
		STORED, TAPS, sizeof(Table), worst_sum);

	// SET_CUR and GET_CUR
	uint32_t frames[RING];
	Ring r = {frames, 0, 0, 0};
	Sender s;
	memset(&s, 0, sizeof(s));
	reset(s, r);
	CHECK(s.rate == 44100, "starts at %u", s.rate);
	uint32_t was = s.restarts;
	CHECK(set_rate(s, r, 44100) && s.restarts == was, "setting the rate it has started again");
	CHECK(set_rate(s, r, 48000) && s.rate == 48000 && s.restarts == was + 1, "48000 not taken");
	CHECK(!set_rate(s, r, 32000) && s.rate == 48000, "32000 changed the rate");
	CHECK(!set_rate(s, r, 0) && !set_rate(s, r, 96000) && s.rate == 48000, "0 or 96000 changed the rate");
	CHECK(set_rate(s, r, 44100) && s.rate == 44100, "back to 44100");

	// the producer finding the ring full: the drop counted, and the consumer's next packet starts again
	int16_t L[BLOCK], R[BLOCK];
	for (uint32_t i = 0; i < BLOCK; i++) L[i] = (int16_t)i, R[i] = (int16_t)-i;
	const int fit = RING / BLOCK;
	int written = 0;
	for (int b = 0; b <= fit; b++) written += write_block(r, L, R);
	CHECK(written == fit && r.drops == 1, "%d blocks written, %u dropped, of %d into %u frames", written, r.drops, fit + 1, RING);
	uint32_t out[MAX_FRAMES];
	was = s.restarts;
	uint32_t n = packet(s, r, table, out);
	CHECK(s.restarts == was + 1 && r.head == r.tail && s.last_silent == n, "a drop didn't start again");

	// running dry and starting again: the rest of the packet silence, then nothing lost after it
	reset(s, r);
	uint64_t next_in = 0, next_out = 0;
	auto write = [&]() {
		for (uint32_t i = 0; i < BLOCK; i++) L[i] = (int16_t)(next_in + i), R[i] = (int16_t)~(next_in + i);
		next_in += BLOCK;
		return write_block(r, L, R);
	};
	const int start = START / BLOCK;
	for (int b = 0; b < start; b++) write();
	bool dry = false, again = false;
	for (int k = 0; k < 40; k++) {
		if (k == 20)
			for (int b = 0; b < start; b++) write();
		n = packet(s, r, table, out);
		const uint32_t data = n - s.last_silent;
		for (uint32_t i = 0; i < data; i++, next_out++)
			CHECK(out[i] == pack((int16_t)next_out, (int16_t)~next_out), "after running dry, frame %llu is %08x", (unsigned long long)next_out, out[i]);
		for (uint32_t i = data; i < n; i++) CHECK(out[i] == 0, "not silence");
		if (data && s.last_silent) dry = true;
		if (k > 20 && data) again = true;
	}
	CHECK(dry && again && s.underruns == 2 && next_out == 2 * START, "running dry: %d %d %u %llu", dry, again, s.underruns, (unsigned long long)next_out);
	printf("  pack, SET_CUR/GET_CUR, a full ring, running dry: checked\n");
}

// what the resampler does to sines all the way up, for the record (not checked)
static void response()
{
	printf("48000, other frequencies (20 s each, for the record)\n");
	const int pairs[][2] = {{100, 5000}, {15000, 18000}, {19000, 20000}};
	for (auto &p : pairs) {
		Run c;
		c.seconds = 20;
		c.source = {SINES, p[0], p[1], 16000, {}, {}};
		c.source.prepare();
		c.controls = {{0, Control::ALT, 1}, {0.0003, Control::RATE, 48000}};
		const Outcome o = run(c);
		for (const Channel *ch : {&o.left, &o.right})
			printf("  %5d Hz: THD+N %.1f dB (worst of %d windows), gain %.2f dB\n", ch->frequency, ch->worst_thdn, ch->windows,
				20 * log10(ch->max_amp / c.source.amplitude));
	}
}

int main()
{
	make_table(table);
	units();
	response();
	for (double producer : {44117.647, 44100.0}) {
		printf("the audio library at %.3f Hz\n", producer);
		for (double ppm : {-500.0, 0.0, 500.0}) {
			char tag[64];
			snprintf(tag, sizeof(tag), "host %+.0f ppm", ppm);
			{   // 44100: noise, every frame checked
				Run c;
				c.name = std::string("44100, 10 min, ") + tag;
				c.producer_hz = producer;
				c.ppm = ppm;
				c.source = {NOISE, 0, 0, 0, {}, {}};
				c.controls = {{0, Control::ALT, 1}};
				report(c, run(c), true, false, 2);
			}
			{   // 48000, as Linux and Android ask for it: the streaming setting, then SET_CUR
				Run c;
				c.name = std::string("48000, 10 min, ") + tag;
				c.producer_hz = producer;
				c.ppm = ppm;
				c.source = {SINES, 1000, 10000, 16000, {}, {}};
				c.source.prepare();
				c.controls = {{0, Control::ALT, 1}, {0.0003, Control::RATE, 48000}};
				report(c, run(c), false, true, 3);
			}
			{   // switching: 48000 by SET_CUR while streaming, back to 44100, a rate refused, off and on again
				Run c;
				c.name = std::string("switching, 10 min, ") + tag;
				c.producer_hz = producer;
				c.ppm = ppm;
				c.source = {SINES, 1000, 10000, 16000, {}, {}};
				c.source.prepare();
				c.controls = {{0, Control::ALT, 1}, {120.00123, Control::RATE, 48000}, {200.5, Control::RATE, 32000},
					{360.00071, Control::RATE, 44100}, {480.2, Control::ALT, 0}, {480.25, Control::RATE, 48000},
					{480.3, Control::ALT, 1}};
				report(c, run(c), true, true, 7);
			}
		}
	}

	// the streaming setting on and off, over and over: on for 50, 11.3, 3.1 (not long enough to start),
	// 400 and 23.7 ms in turn, off between for 50, 7.1, 3.3, 20 and 13.9; at 48000 the rate set first, at
	// alt 0 (as Windows and macOS do), and kept through every start
	printf("the streaming setting turned on and off, the audio library at 44100.000 Hz\n");
	uint32_t trims = 0;
	for (uint32_t rate : {44100u, 48000u}) {
		for (double ppm : {-500.0, 500.0}) {
			const double on[] = {50, 11.3, 3.1, 400, 23.7}, off[] = {50, 7.1, 3.3, 20, 13.9};
			Run c;
			char name[96];
			snprintf(name, sizeof(name), "%u, 1 min, host %+.0f ppm, on and off", rate, ppm);
			c.name = name;
			c.producer_hz = 44100.0;
			c.ppm = ppm;
			c.seconds = 60;
			if (rate == 44100) c.source = {NOISE, 0, 0, 0, {}, {}};
			else {
				c.source = {SINES, 1000, 10000, 16000, {}, {}};
				c.source.prepare();
				c.controls.push_back({0, Control::RATE, 48000});
			}
			double t = 0.0001;
			for (int i = 0; t < c.seconds - 1; i++) {
				c.controls.push_back({t, Control::ALT, 1});
				t += on[i % 5] * 1e-3;
				c.controls.push_back({t, Control::ALT, 0});
				t += off[i % 5] * 1e-3;
			}
			const Outcome o = run(c);
			trims += o.trims;
			report(c, o, rate == 44100, rate == 48000, 1 + (int)c.controls.size(), false);
		}
	}
	CHECK(trims > 0, "no start found more than %u frames: that path wasn't run", START);

	// the host's clock far out, to show how far the packet sizes follow it (usb_audio_rate.h, INTEGRAL_LIMIT)
	printf("the host 2000 ppm out, the audio library at 44100.000 Hz\n");
	for (double ppm : {-2000.0, 2000.0}) {
		char tag[64];
		snprintf(tag, sizeof(tag), "host %+.0f ppm", ppm);
		{
			Run c;
			c.name = std::string("44100, 2 min, ") + tag;
			c.producer_hz = 44100.0;
			c.ppm = ppm;
			c.seconds = 120;
			c.source = {NOISE, 0, 0, 0, {}, {}};
			c.controls = {{0, Control::ALT, 1}};
			report(c, run(c), true, false, 2);
		}
		{
			Run c;
			c.name = std::string("48000, 2 min, ") + tag;
			c.producer_hz = 44100.0;
			c.ppm = ppm;
			c.seconds = 120;
			c.source = {SINES, 1000, 10000, 16000, {}, {}};
			c.source.prepare();
			c.controls = {{0, Control::ALT, 1}, {0.0003, Control::RATE, 48000}};
			report(c, run(c), false, true, 3);
		}
	}
	printf(failures ? "\n%d checks FAILED\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
