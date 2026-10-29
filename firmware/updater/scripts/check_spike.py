#!/usr/bin/env python3
"""The boot-switch spike's checks without a minichord (build.sh runs them after both builds).

  1. The flash map in include/boot_switch.h agrees with the firmware's own (the presets in main.cpp, the
     kit's header in flash_room.py) and with embed_updater.py's.
  2. Every byte of the updater lands in its slot, and the blob in the app is exactly the updater's build.
  3. The boot page that points the switch at the updater, decoded, against firmware 116's sector 1 and the
     app's own: only the entry, the code signing pointer and the boot data's length differ.
  4. The app's flip() and everything it calls run from RAM (ITCM), its buffer is in RAM (DTCM), the only
     flash addresses it names are sector 1's, and the handlers that could still run are in ITCM.
  5. The app's image stays below 0x601E0000, so the Teensy's bootloader never erases the kit's or the
     presets' block when it's flashed, and its own boot pointer names a program outside the slot.
  6. The pure parts of boot_switch.h (the write guard, framing, numbers, CRC) on this computer.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
FIRMWARE = os.path.dirname(PROJECT)
BIN = os.path.expanduser("~/.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin")
BUILD = os.path.join(PROJECT, ".pio", "build")
FLASH = 0x60000000
results = []


def check(ok, what):
    results.append((bool(ok), what))
    print(("PASS  " if ok else "FAIL  ") + what)
    return ok


def tool(name, *args):
    return subprocess.run([os.path.join(BIN, "arm-none-eabi-" + name), *args], capture_output=True, text=True,
                          check=True).stdout


def read_hex(path):
    data, base = {}, 0
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        raw = bytes.fromhex(line[1:])
        count, offset, kind = raw[0], (raw[1] << 8) | raw[2], raw[3]
        if kind == 0:
            for i in range(count):
                data[base + offset + i] = raw[4 + i]
        elif kind == 4:
            base = ((raw[4] << 8) | raw[5]) << 16
        elif kind == 1:
            break
    return data


def region(data, start, length):
    return bytes(data.get(start + i, 0xFF) for i in range(length))


NM_LINE = re.compile(r"^([0-9a-f]{8})(?: ([0-9a-f]{8}))? (\w) (.*)$")


def symbols(elf):
    """name: (address, size), demangled (names can hold spaces); size 0 where nm gives none"""
    out = {}
    for line in tool("nm", "-S", "-C", elf).splitlines():
        m = NM_LINE.match(line)
        if m:
            out.setdefault(m.group(4), (int(m.group(1), 16), int(m.group(2) or "0", 16)))
    return out


# ---- 1. the flash map -------------------------------------------------------------------------------------
header = open(os.path.join(PROJECT, "include", "boot_switch.h")).read()


def const(name, text=header):
    m = re.search(r"constexpr uint32_t %s = (0x[0-9A-Fa-f]+|\d+);" % name, text)
    return int(m.group(1), 0) if m else None


SLOT, SLOT_SIZE, SWITCH, KIT_HEADER, PRESETS = (const(n) for n in ("SLOT", "SLOT_SIZE", "SWITCH", "KIT_HEADER", "PRESETS"))
SAVED = SLOT + SLOT_SIZE
main_cpp = open(os.path.join(FIRMWARE, "src", "main.cpp")).read()
program_size = int(re.search(r"program_flash_size = (0x[0-9A-Fa-f]+);", main_cpp).group(1), 16)
store = int(re.search(r"preset_store_size = (\d+) \* 1024;", main_cpp).group(1)) * 1024
check(FLASH + program_size - store == PRESETS, "presets at 0x%08X, as main.cpp's preset store" % PRESETS)
room = open(os.path.join(FIRMWARE, "scripts", "flash_room.py")).read()
check(int(re.search(r"KIT_HEADER = (0x[0-9A-Fa-f]+)", room).group(1), 16) == KIT_HEADER,
      "kit header at 0x%08X, as flash_room.py's" % KIT_HEADER)
check(KIT_HEADER + 4096 == SLOT and SAVED + 4096 == PRESETS,
      "slot 0x%08X-0x%08X and saved copy 0x%08X sit between them" % (SLOT, SAVED - 1, SAVED))
embed = open(os.path.join(HERE, "embed_updater.py")).read()
check(all(int(re.search(r"^%s = (0x[0-9A-Fa-f]+)" % n, embed, re.M).group(1), 16) == v
          for n, v in (("SLOT", SLOT), ("SLOT_SIZE", SLOT_SIZE), ("SWITCH", SWITCH))),
      "embed_updater.py has the same slot and switch")

# ---- 2. the updater in its slot, and in the app -----------------------------------------------------------
up_elf = os.path.join(BUILD, "updater", "firmware.elf")
app_elf = os.path.join(BUILD, "switch_test", "firmware.elf")
up = read_hex(os.path.join(BUILD, "updater", "firmware.hex"))
check(all(SLOT <= a < SLOT + SLOT_SIZE for a in up),
      "updater hex: all %d bytes inside the slot (0x%08X-0x%08X)" % (len(up), min(up), max(up)))
loaded = []   # (name, size, VMA, LMA) of every section that puts bytes in the image
for line in tool("objdump", "-h", "-w", up_elf).splitlines():
    p = line.split()
    if len(p) >= 7 and p[0].isdigit() and "LOAD" in line and int(p[2], 16):
        loaded.append((p[1], int(p[2], 16), int(p[3], 16), int(p[4], 16)))
bad = [s for s in loaded if not (SLOT <= s[3] and s[3] + s[1] <= SAVED)]
check(not bad, "updater ELF: every loaded section's load address in the slot (%s)" %
      ", ".join("%s@0x%08X+%d" % (s[0], s[3], s[1]) for s in loaded))
blob_h = open(os.path.join(BUILD, "switch_test", "generated", "updater_blob.h")).read()


def blob(name):
    body = re.search(r"%s\[(\d+)\][^=]*= \{(.*?)\};" % name, blob_h, re.S)
    raw = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", body.group(2)))
    assert len(raw) == int(body.group(1))
    return raw


image = blob("updater_image")
check(image == region(up, SLOT, len(image)) and len(image) <= SLOT_SIZE,
      "the app's updater_image is the updater's build, %d of %d bytes (%d to spare)" % (len(image), SLOT_SIZE, SLOT_SIZE - len(image)))

# ---- 3. the boot page -------------------------------------------------------------------------------------
FIELDS = ("header", "entry", "reserved", "dcd", "boot_data", "self", "csf", "reserved2", "bd.start", "bd.length", "bd.plugin")


def decode(raw):
    return dict(zip(FIELDS, struct.unpack("<11I", raw[:44])))


page = blob("updater_boot_page")
real = read_hex(os.path.join(FIRMWARE, "firmware.hex"))   # firmware 116, advanced's
app = read_hex(os.path.join(BUILD, "switch_test", "firmware.hex"))
redirect, fw116, own = decode(page), decode(region(real, SWITCH, 64)), decode(region(app, SWITCH, 64))
reset = symbols(up_elf)["ResetHandler"][0]
print("      field        redirect     fw 116       switch test")
for f in FIELDS:
    print("      %-12s 0x%08X   0x%08X   0x%08X%s" % (f, redirect[f], fw116[f], own[f], "" if redirect[f] == fw116[f] else "   <- differs"))
check({f for f in FIELDS if redirect[f] != fw116[f]} == {"entry", "csf", "bd.length"},
      "the redirect differs from fw 116's sector 1 only in entry, csf and the boot data's length")
check(redirect["entry"] == reset | 1 and SLOT <= redirect["entry"] < SAVED,
      "its entry 0x%08X is the updater's ResetHandler with the Thumb bit" % redirect["entry"])
check(fw116["entry"] & 1 and own["entry"] & 1, "fw 116's and the app's entries carry the Thumb bit too")
check(redirect["bd.length"] >= redirect["entry"] - FLASH, "the boot data's image covers the entry")
check(page[44:] == b"\xff" * (256 - 44), "the rest of the page is FF")
check(region(real, FLASH, 4096) == region(app, FLASH, 4096), "sector 0 (FlexSPI config) is the same in fw 116 and the app")

# ---- 4. flip() from RAM -----------------------------------------------------------------------------------
BRANCH = re.compile(r"\tb[a-z]*(?:\.[nw])?\s+([0-9a-f]+) <")   # every direct branch and call, to an address
INDIRECT = re.compile(r"\tblx\s+r\d+|\tbx\s+r(?!14\b)\d+|\tldr(?:\.w)?\s+pc,|\tmov\s+pc,")   # through a register (bx lr returns)
WORD = re.compile(r"\.word\s+0x([0-9a-f]{8})")


def from_ram(elf, start_name, buffer_name):
    syms = symbols(elf)
    functions = sorted((a & ~1, s, n) for n, (a, s) in syms.items() if s and (a < 0x00080000 or a >= 0x60000000))

    def containing(addr):   # the function whose bytes hold addr: (start, size, name)
        for start, size, name in functions:
            if start <= addr < start + size:
                return start, size, name
        return addr, 0, "? at 0x%08X" % addr

    first = containing(next(a for n, (a, s) in syms.items() if n.startswith("boot_switch::flip(")) & ~1)
    todo, seen, flash_words, indirect = [first], {}, set(), []
    while todo:
        start, size, name = todo.pop()
        if start in seen:
            continue
        seen[start] = name
        if start >= 0x00080000 or not size:
            continue
        text = tool("objdump", "-d", "-C", "--start-address=0x%x" % start, "--stop-address=0x%x" % (start + size), elf)
        for target in BRANCH.findall(text):
            t = int(target, 16)
            if not start <= t < start + size:   # out of this function: into another one
                todo.append(containing(t))
        indirect += [name for _ in INDIRECT.findall(text)]
        flash_words |= {int(w, 16) for w in WORD.findall(text) if 0x60000000 <= int(w, 16) < 0x60200000}
    outside = {hex(a): n for a, n in seen.items() if a >= 0x00080000 or n.startswith("?")}
    check(not outside, "%s: flip() and what it calls all in ITCM: %s%s" % (
        start_name, ", ".join(sorted(set(seen.values()))), "; NOT: %s" % outside if outside else ""))
    check(not indirect, "%s: no call through a register on the way%s" % (start_name, " (%s)" % ", ".join(indirect) if indirect else ""))
    # inside sector 1, or its end as a loop's bound
    check(all(SWITCH <= w <= SWITCH + 4096 for w in flash_words), "%s: the only flash addresses flip() and its callees name are sector 1's: %s" % (
        start_name, ", ".join("0x%08X" % w for w in sorted(flash_words)) or "none"))
    # what can still run once flash_wait turns interrupts back on: the fault handler (every vector starts
    # as it), SysTick's and PendSV's (exceptions the NVIC doesn't switch off), and USB's
    handlers = {n: syms[n][0] for n in ("unused_interrupt_vector", "systick_isr", "pendablesrvreq_isr", "usb_isr")}
    check(all(a < 0x00080000 for a in handlers.values()), "%s: the handlers that could still run are in ITCM (%s)" % (
        start_name, ", ".join("%s 0x%X" % kv for kv in handlers.items())))
    buf = syms[buffer_name][0]
    check(0x20000000 <= buf < 0x20080000, "%s: flip()'s buffer %s at 0x%08X, in DTCM" % (start_name, buffer_name, buf))


from_ram(app_elf, "switch test", "buffer")
# The updater writes sector 1 as any flash (write_switch): its code is all in its slot (check 2), so nothing
# it runs is ever erased under it, and it has no flip() to check
check(not any(n.startswith("boot_switch::flip(") for n in symbols(up_elf)), "the updater has no flip(): BACK writes and checks sector 1 from the slot")

# ---- 5. the app's image -----------------------------------------------------------------------------------
check(max(app) < KIT_HEADER, "the app's hex ends at 0x%08X, below the 64 kB block of the kit's header and the presets" % max(app))
check(own["entry"] and not SLOT <= own["entry"] < SAVED, "the app's own boot pointer names the app (0x%08X)" % own["entry"])

# ---- 6. the header's pure parts on this computer ----------------------------------------------------------
test = r'''
#include "boot_switch.h"
#include <cstdio>
using namespace boot_switch;
int fails = 0;
#define T(c) do { if (!(c)) { std::printf("  failed: %s\n", #c); fails++; } } while (0)
int main() {
  T(!may_write(KIT_HEADER, SECTOR));            // the kit's header sector
  T(!may_write(SLOT - 1, 1));
  T(!may_write(SLOT - PAGE, 2 * PAGE));          // straddling into the slot from below
  T(may_write(SLOT, PAGE));
  T(may_write(SLOT, SLOT_SIZE + SECTOR));        // the slot and the saved copy, the whole of it
  T(!may_write(SLOT, SLOT_SIZE + SECTOR + 1));
  T(may_write(SAVED, SECTOR));
  T(!may_write(SAVED + SECTOR - PAGE, 2 * PAGE)); // straddling into the presets
  T(!may_write(PRESETS, PAGE));
  T(!may_write(SWITCH, PAGE));                   // sector 1 only through flip()
  T(!may_write(FLASH, SECTOR));
  T(!may_write(SLOT, 0));
  T(!may_write(SAVED, 0xFFFFFFFFu));             // a length that would wrap
  T(!may_write(0xFFFFFF00u, 0x200));
  T(crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u);
  uint8_t g[5]; groups(g, 0xCBF43926u, 5);
  T(g[0] == 0x26 && g[1] == (0xCBF43926u >> 7 & 0x7F) && g[4] == (0xCBF43926u >> 28));
  const uint8_t info[] = {0xF0, 0x7D, 0x66, 0x05, 0x01, 0x00, 0xF7};
  T(op_of(info, 7) == OP_INFO);
  T(op_of(info, 6) == 0);                        // too short
  const uint8_t reply[] = {0xF0, 0x7D, 0x66, 0x45, 0x01, 0x00, 0xF7};
  T(op_of(reply, 7) == 0);                       // a reply's op: never answered
  const uint8_t high[] = {0xF0, 0x7D, 0x66, 0x05, 0x81, 0x00, 0xF7};
  T(op_of(high, 7) == 0);                        // a data byte over 7F
  const uint8_t kit[] = {0xF0, 0x7D, 0x6B, 0x05, 0x01, 0x00, 0xF7};
  T(op_of(kit, 7) == 0);                         // the kit's messages aren't these
  const uint8_t up[] = {0xF0, 0x7D, 0x66, 0x08, 0x55, 0x50, 0xF7}, up_bad[] = {0xF0, 0x7D, 0x66, 0x08, 0x55, 0x51, 0xF7};
  T(is_exactly(up, 7, up, 7) && !is_exactly(up_bad, 7, up, 7) && !is_exactly(up, 6, up, 7));
  T(INFO_LENGTH == 65);
  std::printf("%d failed\n", fails);
  return fails != 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = os.path.join(tmp, "t.cpp"), os.path.join(tmp, "t")
    open(src, "w").write(test)
    built = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", os.path.join(PROJECT, "include"), src, "-o", exe],
                           capture_output=True, text=True)
    ran = subprocess.run([exe], capture_output=True, text=True) if built.returncode == 0 else None
    print(built.stderr.strip() or "", (ran.stdout.strip() if ran else ""), sep="")
    check(ran is not None and ran.returncode == 0, "boot_switch.h's guard, framing, numbers and CRC on this computer")

failed = [w for ok, w in results if not ok]
print("\n%d checks, %d failed" % (len(results), len(failed)))
sys.exit(1 if failed else 0)
