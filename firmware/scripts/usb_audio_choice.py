# USB audio, a choice: the minichord's USB audio setting (address 244, src/main.cpp) decides whether
# the phone or computer it's plugged into is offered a USB speaker. The Teensy core's MIDI16_AUDIO_SERIAL
# type declares one in a fixed table, so this builds a copy of the core's usb_desc.c with both
# arrangements in it:
#   - the core's own descriptors, unchanged, now in RAM: what the host is told unless asked otherwise
#   - the same without the speaker: the AudioControl interface keeps its USB-streaming output (the
#     minichord's sound, recorded over USB) and loses the speaker's input terminal, volume feature
#     unit and output terminal, and the speaker's streaming interface goes
# and usb_desc_pick(speaker), which main.cpp's startup_middle_hook calls before USB starts, copies the
# speakerless one in when it's wanted, and has the speakerless minichord connect at full speed (12 Mbit/s,
# not 480): a Pixel phone's audio processor (its AoC, which takes over USB audio) won't record 44.1 kHz from
# a high-speed device ("Alert: 44100Hz, >=HS" in its log, and the recording silent), and full speed is
# where a USB audio 1.0 device like this one usually is, so the phone can play the minichord's own sound.
# And a copy of the core's usb.c that sends a configuration descriptor as long as its own wTotalLength says
# (the core sends the table's whole length, which the speakerless one is shorter than). The core's own
# files are left as they are (they're shared by every project on the machine): the copies are made in the
# build directory and compiled in their place. A core whose files don't read as expected stops the build
# rather than being half patched.
#
# Without the speaker the recording also offers two rates, 44100 and 48000, and the host chooses with the
# standard sampling frequency control: a Pixel 8a's audio runs at 48 kHz, and given only 44.1 its capture
# ran dry about a hundred times a second ("USB RX Dejitter underrun" in its log), the sound choppy. And the
# recording is sent its own way: the core's sender counts out 44100 frames a second by the host's clock
# while the audio library makes them by the Teensy's crystal, and the two clocks never quite agree, so
# sooner or later its two-block queue would throw a block away or pad a packet with silence, a click. So
# the speakerless tables list both rates and a sampling frequency control, usb.c's copy answers SET_CUR and
# GET_CUR for the recording's endpoint and passes on the host starting and stopping it, and a fourth copy,
# of usb_audio.cpp, sends from a ring by include/usb_audio_rate.h (the ring, the packet sizes and the
# resampler, which tools/usb_audio_rate_test.cpp runs on a computer). With the speaker offered all of it
# stands aside: the core's code runs as it was, and the descriptors are the core's byte for byte.
import os, re
Import("env")

CORE = os.path.join(env.PioPlatform().get_package_dir("framework-arduinoteensy"), "cores", "teensy4")
BUILD = env.subst("$BUILD_DIR")
RATE_H = os.path.join(env.subst("$PROJECT_INCLUDE_DIR"), "usb_audio_rate.h")
# what the speaker takes: a byte of the AC header's interface list, its input terminal (12), feature
# unit (10) and output terminal (9), and its streaming interface (9+9+7+11+9+7) with the sync endpoint (9)
SPEAKER_BYTES = "(1 + 12+10+9 + 9+9+7+11+9+7+9)"
# and what the second rate adds: its tSamFreq in the recording's format descriptor
TWO_RATES_BYTES = "3"
# the recording's packets without the speaker: up to 49 stereo 16-bit frames, as 48000 needs (47 to 49 a
# packet, usb_audio_rate.h); the core's 180 is 45
TX_NOSPEAKER_SIZE = 196


def fail(path, why):
    raise SystemExit(f"usb_audio_choice.py: {why} in {path}; the Teensy core has changed, so check the patch")


def write(name, text):
    path = os.path.join(BUILD, name)
    os.makedirs(BUILD, exist_ok=True)
    if not os.path.exists(path) or open(path).read() != text:
        open(path, "w").write(text)
    return path


def without_speaker(block, path):
    """one speed's audio function, the speaker taken out"""
    def sub(pattern, repl, what):
        nonlocal block
        block, n = re.subn(pattern, repl, block, count=1)
        if n != 1:
            fail(path, f"no {what}")
    # the interface association: control and the recording's streaming, two interfaces, not three
    sub(r"\n(\s*)3,(\s*// bInterfaceCount)", r"\n\g<1>2,\g<2>", "bInterfaceCount 3")
    # the class-specific header: one streaming interface in the collection, so a byte shorter, and the
    # descriptors that follow it (header 9, input terminal 12, output terminal 9) total 30
    sub(r"\n(\s*)10,(\s*// bLength\n\s*0x24,\s*// bDescriptorType, 0x24 = CS_INTERFACE\n\s*0x01,\s*// bDescriptorSubtype, 1 = HEADER)",
        r"\n\g<1>9,\g<2>", "AC header length 10")
    sub(r"LSB\(62\), MSB\(62\)", "LSB(30), MSB(30)", "AC wTotalLength 62")
    sub(r"\n(\s*)2,(\s*// bInCollection)", r"\n\g<1>1,\g<2>", "bInCollection 2")
    sub(r"\n\s*AUDIO_INTERFACE\+2,\s*// baInterfaceNr\(2\)[^\n]*", "", "baInterfaceNr(2)")
    # the recording asynchronous, as it is: the minichord's clock sets its rate, not the host's (on a
    # Pixel 8a this alone didn't stop 44.1's underruns; 48 kHz did)
    sub(r"0x09,(\s*// bmAttributes = isochronous, )adaptive", r"0x05,\g<1>asynchronous (usb_audio_choice.py)",
        "the recording's adaptive endpoint")
    # the speaker's terminals and volume: from the second input terminal to the first streaming interface
    first_it = block.find("// Input Terminal Descriptor")
    second_it = block.find("// Input Terminal Descriptor", first_it + 1)
    if first_it < 0 or second_it < 0:
        fail(path, "second input terminal")
    line_start = block.rfind("\n", 0, second_it) + 1
    rest = block[line_start:]
    as_at = rest.find("// Standard AS Interface Descriptor")
    if as_at < 0 or "0x31" not in rest[:as_at]:
        fail(path, "speaker terminals before the first streaming interface")
    block = block[:line_start] + rest[rest.rfind("\n", 0, as_at) + 1:]
    # the speaker's streaming interface, to the end of the function
    first_as = block.find("// Standard AS Interface Descriptor")
    second_as = block.find("// Standard AS Interface Descriptor", first_as + 1)
    if second_as < 0 or "AUDIO_INTERFACE+2" not in block[second_as:] or "AUDIO_SYNC_ENDPOINT" not in block[second_as:]:
        fail(path, "speaker streaming interface")
    block = block[:block.rfind("\n", 0, second_as) + 1]
    if "AUDIO_INTERFACE+2" in block or "AUDIO_RX_ENDPOINT" in block:
        fail(path, "speaker left over after the cut")
    # the recording at two rates: 44100 and 48000 in its format descriptor (three bytes longer), the
    # sampling frequency control on its endpoint (SET_CUR and GET_CUR, usb.c's copy), and room in its
    # packets for 49 frames
    sub(r"\n(\s*)11,(\s*// bLength\n\s*0x24,\s*// bDescriptorType = CS_INTERFACE\n\s*2,\s*// bDescriptorSubtype = FORMAT_TYPE\n)",
        r"\n\g<1>14,\g<2>", "the format descriptor's length 11")
    sub(r"\n(\s*)1,(\s*)// bSamFreqType = 1 frequency\n(\s*)LSB\(44100\), MSB\(44100\), 0,(\s*)// tSamFreq\n",
        r"\n\g<1>2,\g<2>// bSamFreqType = 2 frequencies (usb_audio_choice.py)\n"
        r"\g<3>LSB(44100), MSB(44100), 0,\g<4>// tSamFreq[1]\n"
        r"\g<3>LSB(48000), MSB(48000), 0,\g<4>// tSamFreq[2]\n", "the one frequency, 44100")
    sub(r"LSB\(AUDIO_TX_SIZE\), MSB\(AUDIO_TX_SIZE\),(\s*)// wMaxPacketSize",
        r"LSB(AUDIO_TX_NOSPEAKER_SIZE), MSB(AUDIO_TX_NOSPEAKER_SIZE),\g<1>// wMaxPacketSize, 49 frames (usb_audio_choice.py)",
        "the recording's wMaxPacketSize")
    sub(r"(0x25,\s*// bDescriptorType, 0x25 = CS_ENDPOINT\n\s*1,\s*// bDescriptorSubtype, 1 = EP_GENERAL\n\s*)0x00,(\s*// bmAttributes)",
        r"\g<1>0x01,\g<2>, bit 0 = Sampling Frequency control (usb_audio_choice.py)", "the class-specific endpoint's bmAttributes")
    return block


def patch_usb_desc():
    path = os.path.join(CORE, "usb_desc.c")
    src = open(path).read()
    for speed in ("480", "12"):
        head = f"PROGMEM const uint8_t usb_config_descriptor_{speed}[CONFIG_DESC_SIZE] = {{\n"
        start = src.find(head)
        end = src.find("\n};\n", start)
        if start < 0 or end < 0:
            fail(path, f"usb_config_descriptor_{speed}")
        table = src[start + len(head):end + 4]
        # the speakerless table: its own length and one interface fewer, and its audio function cut
        m = re.search(r"#ifdef AUDIO_INTERFACE\n\t// configuration for [^\n]*\n", table)
        stop = table.find("\n#endif", m.end()) if m else -1
        if not m or stop < 0:
            fail(path, f"the audio function in usb_config_descriptor_{speed}")
        bare = table[:m.end()] + without_speaker(table[m.end():stop], path) + table[stop:]
        for old, new, what in (
            ("LSB(CONFIG_DESC_SIZE),                 // wTotalLength\n        MSB(CONFIG_DESC_SIZE),",
             "LSB(USB_NOSPEAKER_DESC_SIZE),          // wTotalLength\n        MSB(USB_NOSPEAKER_DESC_SIZE),", "wTotalLength"),
            ("NUM_INTERFACE,                          // bNumInterfaces",
             "NUM_INTERFACE-1,                        // bNumInterfaces", "bNumInterfaces")):
            if bare.count(old) != 1:
                fail(path, f"{what} in usb_config_descriptor_{speed}")
            bare = bare.replace(old, new)
        both = (f"// usb_audio_choice.py: in RAM, so usb_desc_pick can put the speakerless one in its place\n"
                f"uint8_t usb_config_descriptor_{speed}[CONFIG_DESC_SIZE] = {{\n" + table +
                f"\n// usb_audio_choice.py: the speakerless one, in flash, its size what its bytes make (which the assert\n"
                f"// holds to the length it's given)\n"
                f"static PROGMEM const uint8_t usb_config_descriptor_{speed}_nospeaker[] = {{\n" + bare +
                f"_Static_assert(sizeof(usb_config_descriptor_{speed}_nospeaker) == USB_NOSPEAKER_DESC_SIZE, \"the speakerless table's length\");\n")
        src = src[:start] + both + src[end + 4:]
    first = src.find("// usb_audio_choice.py: in RAM")
    src = (src[:first] + "#include <string.h>\n#define USB_NOSPEAKER_DESC_SIZE (CONFIG_DESC_SIZE - " + SPEAKER_BYTES +
           " + " + TWO_RATES_BYTES + ")\n" + f"#define AUDIO_TX_NOSPEAKER_SIZE {TX_NOSPEAKER_SIZE}\n\n" + src[first:] +
           "\n\n// usb_audio_choice.py: the host offered a USB speaker or not, chosen before USB starts\n"
           "// and the speakerless one at full speed (usb.c's copy reads this as USB starts): a Pixel records 44.1 kHz\n"
           "// only from a full-speed device\n"
           "uint8_t usb_full_speed_only = 0;\n"
           "// and its recording at two rates, sent its own way (usb.c's and usb_audio.cpp's copies read this), the\n"
           "// resampler's coefficients worked out now, before USB starts, where they hold nothing up\n"
           "uint8_t usb_audio_speakerless = 0;\n"
           "void usb_audio_rate_prepare(void);\n"
           "// one speakerless table over the core's: the empty asm hides which table it is, or GCC copies from a\n"
           "// copy of its bytes it makes in .rodata (RAM1 on a Teensy 4, 283 bytes each) rather than from flash\n"
           "static void usb_desc_nospeaker(uint8_t *to, const uint8_t *from)\n{\n"
           "\t__asm__ (\"\" : \"+r\" (from));\n"
           "\tmemcpy(to, from, USB_NOSPEAKER_DESC_SIZE);\n"
           "\tmemset(to + USB_NOSPEAKER_DESC_SIZE, 0, CONFIG_DESC_SIZE - USB_NOSPEAKER_DESC_SIZE);\n}\n"
           "void usb_desc_pick(int speaker)\n{\n\tif (speaker) return;\n\tusb_full_speed_only = 1;\n"
           "\tusb_audio_speakerless = 1;\n\tusb_audio_rate_prepare();\n"
           "\tusb_desc_nospeaker(usb_config_descriptor_480, usb_config_descriptor_480_nospeaker);\n"
           "\tusb_desc_nospeaker(usb_config_descriptor_12, usb_config_descriptor_12_nospeaker);\n}\n")
    return write("usb_desc_choice.c", src)


def patch_usb():
    path = os.path.join(CORE, "usb.c")
    src = open(path).read()
    for old, new, what in (
        ("extern const uint8_t usb_config_descriptor_480[];\nextern const uint8_t usb_config_descriptor_12[];",
         "extern uint8_t usb_config_descriptor_480[];   // usb_audio_choice.py: in RAM\nextern uint8_t usb_config_descriptor_12[];\n"
         "#ifdef AUDIO_INTERFACE\n"
         "// usb_audio_choice.py: the speakerless recording at two rates (usb_desc_choice.c, usb_audio_choice.cpp)\n"
         "extern uint8_t usb_audio_speakerless;\n"
         "void usb_audio_rate_alt(uint8_t alt);\n"
         "void usb_audio_rate_set(uint32_t hz);\n"
         "uint32_t usb_audio_rate_get(void);\n"
         "#endif", "the descriptors' declarations"),
        ("\t\tif (setup.wIndex == AUDIO_INTERFACE+1) {\n\t\t\tusb_audio_transmit_setting = setup.wValue;\n",
         "\t\tif (setup.wIndex == AUDIO_INTERFACE+1) {\n\t\t\tusb_audio_transmit_setting = setup.wValue;\n"
         "\t\t\t// usb_audio_choice.py: the speakerless recording starts again from an empty ring\n"
         "\t\t\tif (usb_audio_speakerless) usb_audio_rate_alt(setup.wValue);\n",
         "the recording's SET_INTERFACE"),
        ("\t  case 0x81A2: // GET_CUR (wValue=0, wIndex=interface, wLength=len)\n\t\tif (setup.wLength >= 3) {\n",
         "\t  case 0x0122: // SET_CUR to an endpoint (usb_audio_choice.py): the speakerless recording's sampling\n"
         "\t\t// frequency, 3 bytes, taken once they've come (endpoint0_complete). The status stage goes with the\n"
         "\t\t// data, so a rate it hasn't got can't be refused with a stall: the rate stays as it was, and GET_CUR\n"
         "\t\t// (which Linux asks next, to check) says so\n"
         "\t\tif (usb_audio_speakerless && setup.wValue == 0x0100 && setup.wIndex == (AUDIO_TX_ENDPOINT | 0x80)\n"
         "\t\t  && setup.wLength == 3) {\n"
         "\t\t\tendpoint0_setupdata.bothwords = setupdata;\n"
         "\t\t\tendpoint0_receive(endpoint0_buffer, 3, 1);\n"
         "\t\t\treturn;\n"
         "\t\t}\n"
         "\t\tbreak;\n"
         "\t  case 0x81A2: // GET_CUR (wValue=0, wIndex=interface, wLength=len)\n"
         "\t\t// usb_audio_choice.py: the speakerless recording's rate as it is now (the core's answer is always 44100)\n"
         "\t\tif (usb_audio_speakerless && setup.wValue == 0x0100 && setup.wIndex == (AUDIO_TX_ENDPOINT | 0x80)\n"
         "\t\t  && setup.wLength >= 3) {\n"
         "\t\t\tuint32_t hz = usb_audio_rate_get();\n"
         "\t\t\tendpoint0_buffer[0] = hz;\n"
         "\t\t\tendpoint0_buffer[1] = hz >> 8;\n"
         "\t\t\tendpoint0_buffer[2] = hz >> 16;\n"
         "\t\t\tendpoint0_transmit(endpoint0_buffer, 3, 0);\n"
         "\t\t\treturn;\n"
         "\t\t}\n"
         "\t\tif (setup.wLength >= 3) {\n",
         "GET_CUR"),
        ("\tif (setup.word1 == 0x02010121 || setup.word1 == 0x01000121 /* TODO: check setup.word2 */) {\n"
         "\t\tusb_audio_set_feature(&endpoint0_setupdata, endpoint0_buffer);\n\t}\n",
         "\tif (setup.word1 == 0x02010121 || setup.word1 == 0x01000121 /* TODO: check setup.word2 */) {\n"
         "\t\tusb_audio_set_feature(&endpoint0_setupdata, endpoint0_buffer);\n\t}\n"
         "\t// usb_audio_choice.py: the speakerless recording's sampling frequency, SET_CUR's 3 bytes (endpoint0_setup)\n"
         "\tif (setup.wRequestAndType == 0x0122 && usb_audio_speakerless) {\n"
         "\t\tusb_audio_rate_set(endpoint0_buffer[0] | (endpoint0_buffer[1] << 8) | ((uint32_t)endpoint0_buffer[2] << 16));\n"
         "\t}\n",
         "the audio feature's completion"),
        # The interrupt takes a waiting SETUP before it looks at what's complete, and each SETUP forgets the
        # last transfer's completion. So if the interrupt came late (interrupts held for a flash write, say)
        # and the host's next request was already in, a SET_CUR's 3 bytes, though they had come, were never
        # taken: the GET_CUR that Linux sends straight after got the old rate, and the stream ran at it, the
        # Pixel underrunning again. Without the speaker, a completion that's there runs before the SETUP.
        ("\t\t\tendpoint0_notify_mask = 0;\n\t\t\tendpoint0_setup(s.bothwords);\n",
         "#ifdef AUDIO_INTERFACE\n"
         "\t\t\t// usb_audio_choice.py: the last transfer's completion first, if it's there (a SET_CUR's rate,\n"
         "\t\t\t// whose GET_CUR may be this SETUP), where the core would let this SETUP forget it\n"
         "\t\t\tif (usb_audio_speakerless && endpoint0_notify_mask && (USB1_ENDPTCOMPLETE & endpoint0_notify_mask)) {\n"
         "\t\t\t\tUSB1_ENDPTCOMPLETE = endpoint0_notify_mask;\n"
         "\t\t\t\tendpoint0_notify_mask = 0;\n"
         "\t\t\t\tendpoint0_complete();\n"
         "\t\t\t}\n"
         "#endif\n"
         "\t\t\tendpoint0_notify_mask = 0;\n\t\t\tendpoint0_setup(s.bothwords);\n",
         "the interrupt's SETUP loop"),
        ("\t\t\t\t\tdatalen = list->length;\n",
         "\t\t\t\t\tdatalen = list->length;\n"
         "\t\t\t\t\t// usb_audio_choice.py: a configuration as long as it says it is\n"
         "\t\t\t\t\tif ((setup.wValue >> 8) == 2 || (setup.wValue >> 8) == 7) datalen = list->addr[2] | (list->addr[3] << 8);\n",
         "the descriptor length"),
        ("\t//USB1_PORTSC1 |= USB_PORTSC1_PFSC; // force 12 Mbit/sec\n",
         "\t// usb_audio_choice.py: the speakerless minichord at full speed, as usb_desc_pick chose\n"
         "\textern uint8_t usb_full_speed_only;\n"
         "\tif (usb_full_speed_only) USB1_PORTSC1 |= USB_PORTSC1_PFSC; // force 12 Mbit/sec\n",
         "the full-speed switch")):
        if src.count(old) != 1:
            fail(path, what)
        src = src.replace(old, new)
    return write("usb_choice.c", src)


def patch_usb_midi():
    # USB MIDI's half-full packet is flushed by usb_midi_flush_output(), from the SOF interrupt and from
    # usbMIDI.send_now() in the program, with nothing between the two: the interrupt's flush landing inside the
    # program's prepared the dTD the hardware had just taken a second time and linked it to itself, after which
    # the controller's descriptor engine froze (every endpoint silent, frames still counted; only a bus or chip
    # reset brings it back). The firmware no longer calls send_now(), and here the flush holds interrupts, so a
    # send_now() added later can't bring it back. And the three statics that usb_midi_write_packed() shares with
    # the interrupt are volatile: as compiled, tx_head was read before tx_noautoflush was set, a four-instruction
    # window in which the interrupt's flush left write_packed() with a stale head and its bytes in the wrong buffer.
    path = os.path.join(CORE, "usb_midi.c")
    src = open(path).read()
    for old, new, what in (
        ("static uint8_t tx_noautoflush=0;", "static volatile uint8_t tx_noautoflush=0;   // usb_audio_choice.py: shared with the SOF interrupt", "tx_noautoflush"),
        ("static uint8_t tx_head=0;\nstatic uint16_t tx_available=0;", "static volatile uint8_t tx_head=0;   // usb_audio_choice.py: shared with the SOF interrupt\nstatic volatile uint16_t tx_available=0;", "tx_head/tx_available"),
        ("void usb_midi_flush_output(void)\n{\n\t//printf(\"usb_midi_flush_output\\n\");\n\tif (tx_noautoflush == 0 && tx_available > 0) {\n",
         "void usb_midi_flush_output(void)\n{\n"
         "\t// usb_audio_choice.py: with interrupts held: called from the program (send_now) and from the SOF interrupt\n"
         "\tuint32_t primask;\n\t__asm__ volatile(\"mrs %0, primask\" : \"=r\"(primask));\n\t__disable_irq();\n"
         "\tif (tx_noautoflush == 0 && tx_available > 0) {\n", "usb_midi_flush_output's head"),
        ("\t\ttx_available = 0;\n\t\tusb_stop_sof_interrupts(MIDI_INTERFACE);\n\t}\n}\n",
         "\t\ttx_available = 0;\n\t\tusb_stop_sof_interrupts(MIDI_INTERFACE);\n\t}\n\tif (!primask) __enable_irq();\n}\n", "usb_midi_flush_output's end")):
        if src.count(old) != 1:
            fail(path, what)
        src = src.replace(old, new)
    return write("usb_midi_choice.c", src)


# The speakerless recording sent its own way, by include/usb_audio_rate.h: update() writes each block into
# its ring, the USB interrupt's tx_event sends from it, at 44100 or 48000 as the host set it, packets of up to
# 196 bytes (the endpoint's room and the transmit buffer both). The glue goes in after the core's counters,
# before anything that uses it; with the speaker offered every one of these steps aside.
RATE_GLUE = r'''
// usb_audio_choice.py: the recording at two rates, 44100 and 48000, without the speaker (usb audio 2) only.
// The ring between update() and the USB interrupt, the packet sizes and the resampler are in the project's
// include/usb_audio_rate.h (tools/usb_audio_rate_test.cpp runs them on a computer); this is what ties them
// to the core. With the speaker offered (0 and 1) none of it runs and the core's own code does, as it was.
#define USB_AUDIO_RATE_SELDOM __attribute__((noinline)) FLASHMEM   // out of RAM1 (usb_audio_rate.h says why)
#define USB_AUDIO_RATE_FLASH FLASHMEM
#include "@RATE_H@"
#define AUDIO_TX_NOSPEAKER_SIZE @TX_SIZE@
#define USB_AUDIO_TX_BUFFER ((((AUDIO_TX_NOSPEAKER_SIZE > AUDIO_TX_SIZE) ? AUDIO_TX_NOSPEAKER_SIZE : AUDIO_TX_SIZE) + 31) & ~31)
static_assert(usb_audio_rate::MAX_FRAMES * 4 <= AUDIO_TX_NOSPEAKER_SIZE, "the recording's packets fit its wMaxPacketSize");
extern "C" uint8_t usb_audio_speakerless;   // usb_desc_choice.c: usb_desc_pick(0) set it, before USB started
// The ring's frames and the resampler's coefficients in RAM2 (with the audio library's blocks): RAM1 is the
// stack's and has little to spare. The indexes and the sender's state stay in RAM1, set to zero before
// anything runs (no constructors: USB can start before the C++ ones run).
DMAMEM static uint32_t usb_audio_rate_frames[usb_audio_rate::RING] __attribute__ ((aligned(32)));
DMAMEM static usb_audio_rate::Table usb_audio_rate_table __attribute__ ((aligned(32)));
static usb_audio_rate::Ring usb_audio_rate_ring = { usb_audio_rate_frames, 0, 0, 0 };
static usb_audio_rate::Sender usb_audio_rate_sender;

// usb_desc_pick(0), before USB starts: the resampler's coefficients (sines and Bessel sums, too slow for an
// interrupt), and the sender as a newly connected host finds it
extern "C" FLASHMEM void usb_audio_rate_prepare(void)
{
	usb_audio_rate::make_table(usb_audio_rate_table);
	usb_audio_rate::reset(usb_audio_rate_sender, usb_audio_rate_ring);
}
// The packet already handed to the controller goes out first when the host next asks for one; whatever was
// left in it (a stream stopped, the other rate) becomes silence. The host doesn't ask while it sets the
// streaming setting, and hardly ever while it changes the rate; if it did, that one packet is part silence.
static FLASHMEM void usb_audio_rate_silence_queued(void)
{
	memset(usb_audio_transmit_buffer, 0, AUDIO_TX_NOSPEAKER_SIZE);
	arm_dcache_flush_delete(usb_audio_transmit_buffer, AUDIO_TX_NOSPEAKER_SIZE);
}
// usb.c's copy, in the USB interrupt: the host set the recording's streaming setting (SET_INTERFACE, the
// core having set usb_audio_transmit_setting), set its rate (SET_CUR) or asks for it (GET_CUR): seldom,
// so from flash like the rest of what isn't every millisecond
extern "C" FLASHMEM void usb_audio_rate_alt(uint8_t)
{
	usb_audio_rate::restart(usb_audio_rate_sender, usb_audio_rate_ring);
	usb_audio_rate_silence_queued();
}
extern "C" FLASHMEM void usb_audio_rate_set(uint32_t hz)
{
	const uint32_t restarts = usb_audio_rate_sender.restarts;
	usb_audio_rate::set_rate(usb_audio_rate_sender, usb_audio_rate_ring, hz);
	if (usb_audio_rate_sender.restarts != restarts) usb_audio_rate_silence_queued();
}
extern "C" uint32_t usb_audio_rate_get(void)
{
	return usb_audio_rate_sender.rate;
}
// tx_event, in the USB interrupt: the next packet, in bytes (not inlined, or it would be in RAM1 with tx_event)
static FLASHMEM __attribute__((noinline)) unsigned int usb_audio_rate_transmit(void)
{
	return usb_audio_rate::packet(usb_audio_rate_sender, usb_audio_rate_ring, usb_audio_rate_table,
		(uint32_t *)usb_audio_transmit_buffer) * 4;
}

'''


def patch_usb_audio():
    path = os.path.join(CORE, "usb_audio.cpp")
    src = open(path).read()
    glue = RATE_GLUE.replace("@RATE_H@", RATE_H).replace("@TX_SIZE@", str(TX_NOSPEAKER_SIZE))
    for old, new, what in (
        ("volatile uint32_t usb_audio_overrun_count;\n", "volatile uint32_t usb_audio_overrun_count;\n" + glue, "the counters"),
        ("\tmemset(&tx_transfer, 0, sizeof(tx_transfer));\n\tusb_config_tx_iso(AUDIO_TX_ENDPOINT, AUDIO_TX_SIZE, 1, tx_event);\n\ttx_event(NULL);\n",
         "\tmemset(&tx_transfer, 0, sizeof(tx_transfer));\n"
         "\t// usb_audio_choice.py: without the speaker, the recording's sender as new, its packets up to 196 bytes\n"
         "\tif (usb_audio_speakerless) usb_audio_rate::reset(usb_audio_rate_sender, usb_audio_rate_ring);\n"
         "\tusb_config_tx_iso(AUDIO_TX_ENDPOINT, usb_audio_speakerless ? AUDIO_TX_NOSPEAKER_SIZE : AUDIO_TX_SIZE, 1, tx_event);\n"
         "\ttx_event(NULL);\n", "usb_audio_configure's transmit endpoint"),
        ("/*DMAMEM*/ uint16_t usb_audio_transmit_buffer[AUDIO_TX_SIZE/2] __attribute__ ((used, aligned(32)));\n",
         "// usb_audio_choice.py: room for the speakerless recording's 196-byte packets, in whole cache lines\n"
         "/*DMAMEM*/ uint16_t usb_audio_transmit_buffer[USB_AUDIO_TX_BUFFER/2] __attribute__ ((used, aligned(32)));\n",
         "the transmit buffer"),
        ("static void tx_event(transfer_t *t)\n{\n\tint len = usb_audio_transmit_callback();\n",
         "static void tx_event(transfer_t *t)\n{\n"
         "\t// usb_audio_choice.py: without the speaker, the recording's own sender\n"
         "\tint len = usb_audio_speakerless ? usb_audio_rate_transmit() : usb_audio_transmit_callback();\n",
         "tx_event"),
        ("void AudioOutputUSB::update(void)\n{\n\taudio_block_t *left, *right;\n\n\t// TODO: we shouldn't be writing to these......\n",
         "void AudioOutputUSB::update(void)\n{\n\taudio_block_t *left, *right;\n\n"
         "\t// usb_audio_choice.py: without the speaker, each block into the recording's ring while the host\n"
         "\t// records (its streaming setting 1); the core's two-block queue below isn't used\n"
         "\tif (usb_audio_speakerless) {\n"
         "\t\tleft = receiveReadOnly(0); // input 0 = left channel\n"
         "\t\tright = receiveReadOnly(1); // input 1 = right channel\n"
         "\t\tif (usb_audio_transmit_setting) {\n"
         "\t\t\tusb_audio_rate::write_block(usb_audio_rate_ring, left ? left->data : NULL, right ? right->data : NULL);\n"
         "\t\t}\n"
         "\t\tif (left) release(left);\n"
         "\t\tif (right) release(right);\n"
         "\t\treturn;\n"
         "\t}\n"
         "\t// TODO: we shouldn't be writing to these......\n",
         "AudioOutputUSB::update")):
        if src.count(old) != 1:
            fail(path, what)
        src = src.replace(old, new)
    return write("usb_audio_choice.cpp", src)


DESC, USB, MIDI, AUDIO = patch_usb_desc(), patch_usb(), patch_usb_midi(), patch_usb_audio()
env.AddBuildMiddleware(lambda env, node: env.Object(os.path.join("$BUILD_DIR", "usb_desc_choice.o"), DESC), "*cores/teensy4/usb_desc.c")
env.AddBuildMiddleware(lambda env, node: env.Object(os.path.join("$BUILD_DIR", "usb_choice.o"), USB), "*cores/teensy4/usb.c")
env.AddBuildMiddleware(lambda env, node: env.Object(os.path.join("$BUILD_DIR", "usb_midi_choice.o"), MIDI), "*cores/teensy4/usb_midi.c")
env.AddBuildMiddleware(lambda env, node: env.Object(os.path.join("$BUILD_DIR", "usb_audio_choice.o"), AUDIO), "*cores/teensy4/usb_audio.cpp")
