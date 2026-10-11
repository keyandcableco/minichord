"""minictl: the minichord from the command line. `minictl help` lists the commands."""
import argparse, datetime, json, os, re, sys, time

from . import device as dev
from .settings import Settings, BANKS, RHYTHM_FIRST, RHYTHM_LAST, pm

S = None          # Settings, loaded once


def settings():
    global S
    if S is None:
        S = Settings()
    return S


# ---- helpers ---------------------------------------------------------------------------------------

def confirm(question, args):
    """a yes from the person, or --yes; refuses when nobody is there to ask"""
    if args.yes:
        return True
    if not sys.stdin.isatty():
        raise RuntimeError(f"{question} Pass --yes to go ahead without asking.")
    return input(f"{question} [y/N] ").strip().lower() in ("y", "yes")


def bank_arg(text):
    try:
        b = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"{text!r} isn't a bank number")
    if not 1 <= b <= BANKS:
        raise argparse.ArgumentTypeError(f"banks go from 1 to {BANKS}")
    return b - 1


def banks_arg(text):
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-", 1)
            out += range(bank_arg(a), bank_arg(b) + 1)
        else:
            out.append(bank_arg(part))
    return sorted(set(out))


def open_device(args):
    return dev.Minichord(args.port)


def page1_defaults():
    s = settings()
    return {a: pm.stored_default(p) for a, p in s.params.items() if a >= dev.PAGE}


def print_json(x):
    json.dump(x, sys.stdout, indent=1)
    print()


def preset_code_from(text):
    """a code typed, or a file holding one, or a shared preset's name"""
    s = settings()
    if text == "defaults":
        return s.defaults(), "a plain starting point"
    for p in s.shared_presets():
        if p["name"].lower() == text.lower():
            return s.decode(p["value"]), p["name"]
    if os.path.isfile(text):
        with open(text) as f:
            text = f.read()
    return s.decode(text), "the code given"


def walk_banks(d, which, show=None):
    """each bank's stored settings, loading them in turn, then back to the bank it was on"""
    start = d.read()[1]
    out = {}
    try:
        for b in which:
            out[b] = d.load_bank(b)
            if show:
                show(b, out[b])
    finally:
        d.load_bank(start)
    return out


NOTE = re.compile(r"([A-Ga-g])([#b]?)(-?\d)?$")


def note_number(text):
    if re.fullmatch(r"\d+", text):
        return int(text)
    m = NOTE.match(text)
    if not m:
        raise ValueError(f"{text!r} isn't a note (C4, Eb3, F#5 or a MIDI number)")
    n = "C D EF G A B".index(m.group(1).upper())
    n += {"#": 1, "b": -1, "": 0}[m.group(2)]
    return 12 * (int(m.group(3) or 4) + 1) + n


# ---- the commands ----------------------------------------------------------------------------------

def cmd_ports(args):
    outs, ins = dev.list_ports()
    print("MIDI out:", *(f"  {n}" for n in outs or ["(none)"]), sep="\n")
    print("MIDI in:", *(f"  {n}" for n in ins or ["(none)"]), sep="\n")
    i = dev.pick_port(outs, 1, args.port)
    print(f"\nminictl would use: {outs[i] if i is not None else 'nothing (no minichord found)'}")
    try:
        sp = dev.serial_port()
        print(f"serial: {sp or 'none'}")
    except RuntimeError as e:
        print(f"serial: {e}")


def cmd_info(args):
    s = settings()
    with open_device(args) as d:
        v = d.read()
    info = {"port": d.port_name, "firmware": v[7], "bank": v[1] + 1, "name": s.preset_name(v),
            "settings": len(v), "this_checkout_firmware": s.version}
    if args.json:
        return print_json(info)
    print(f"minichord on {d.port_name}")
    print(f"firmware {v[7]}" + (f" (this checkout builds {s.version})" if v[7] != s.version else ""))
    print(f"bank {v[1] + 1}" + (f": {info['name']}" if info["name"] else ""))
    if v[7] < dev.PAGE1_VERSION:
        print("page 0 only: settings 256 and up need firmware 31 or later")


def cmd_list(args):
    s = settings()
    hits = s.search(args.words) if args.words else sorted(s.params)
    if not args.all:
        hits = [a for a in hits if s.params[a]["group"] != "hidden" and a not in (0, 1, 7)]
    if args.json:
        return print_json([{"address": a, "name": s.names[a], "title": s.title(a), "range": s.describe_range(a),
                            "help": s.params[a]["tooltip"].strip()} for a in hits])
    for a in hits:
        print(f"{a:4} {s.names[a]:40} {s.describe_range(a)}")
        if args.verbose:
            print(f"     {s.params[a]['tooltip'].strip()}\n")
    if not hits:
        print("nothing matches")


def cmd_get(args):
    s = settings()
    if args.code:
        values, _ = preset_code_from(args.code)
    elif args.bank is not None:
        with open_device(args) as d:
            values = walk_banks(d, [args.bank])[args.bank]
    else:
        with open_device(args) as d:
            values = d.read()
    if args.settings:
        which = [s.find(w) for w in args.settings]
    else:
        which = [a for a in sorted(s.params) if a < len(values) and s.params[a]["group"] != "hidden"
                 and (args.all or values[a] != pm.stored_default(s.params[a]))]
        if args.all:
            which = sorted(s.params)
    which = [a for a in which if a < len(values)]
    if args.json:
        return print_json({s.names.get(a, str(a)): {"address": a, "stored": values[a],
                                                     "value": s.shown(a, values[a], values)} for a in which})
    if args.raw:
        for a in which:
            print(values[a])
        return
    for a in which:
        print(f"{a:4} {s.names.get(a, '?'):40} {s.shown(a, values[a], values)}")
    if not args.settings and not args.all:
        print(f"({len(which)} settings away from their defaults; --all shows every one)")


def cmd_set(args):
    s = settings()
    if len(args.pairs) % 2:
        raise RuntimeError("set takes a setting and a value, as many pairs as you like")
    with open_device(args) as d:
        values = d.read()
        changes, raw = [], []
        for name, text in zip(args.pairs[::2], args.pairs[1::2]):
            a = s.find(name)
            if args.raw:
                raw.append((a, int(text)))
            else:
                changes.append((a, s.parse(a, text, values)))
        after, made, notes = s.apply(values, changes)
        for a, v in raw:
            if a in dev.RESERVED or a == 0:
                raise RuntimeError(f"{a} can't be written")
            if a >= len(values):
                raise RuntimeError(f"this firmware has no setting {a}")
            made.append((a, after[a], v))
            after[a] = v
        for n in notes:
            print(f"! {n}")
        for a, old, new in made:
            d.set(a, new)
            print(f"{s.names.get(a, a)}: {s.shown(a, old, values)} -> {s.shown(a, new, after)}")
        if not made:
            print("nothing changed")
        known = set(s.conflicts(values)) if made else set()
        for n in s.conflicts(after) if made else []:
            if n not in known:
                print(f"  ! {n}")
        if args.save and made:
            b = values[1]
            if confirm(f"Save to bank {b + 1}, replacing what is stored there?", args):
                d.save(b)
                print(f"saved to bank {b + 1}")
        elif made:
            print("(live only: `minictl save` keeps it)")


def cmd_bank(args):
    s = settings()
    with open_device(args) as d:
        v = d.load_bank(args.bank) if args.bank is not None else d.read()
    name = s.preset_name(v)
    print(f"bank {v[1] + 1}" + (f": {name}" if name else ""))


def cmd_banks(args):
    s = settings()
    rows = []

    def show(b, v):
        rows.append({"bank": b + 1, "name": s.preset_name(v), "code": s.encode(v)})
        if not args.json:
            line = f"{b + 1:3}  {rows[-1]['name'] or '(no name)'}"
            print(line + (f"\n     {rows[-1]['code']}" if args.codes else ""), flush=True)

    with open_device(args) as d:
        walk_banks(d, range(BANKS), show)
    if args.json:
        print_json(rows)


def cmd_save(args):
    with open_device(args) as d:
        v = d.read()
        b = v[1] if args.bank is None else args.bank
        if confirm(f"Save the live settings to bank {b + 1}, replacing what is stored there?", args):
            d.save(b)
            print(f"saved to bank {b + 1}")


def cmd_reset(args):
    with open_device(args) as d:
        if confirm(f"Put bank {args.bank + 1} back to its factory preset? What is stored there is lost.", args):
            d.reset(args.bank)
            print(f"bank {args.bank + 1} is its factory preset again, and playing")


def cmd_name(args):
    s = settings()
    with open_device(args) as d:
        v = d.read()
        if v[7] < 50:
            raise RuntimeError("preset names are kept in the preset from firmware 50")
        if not args.name:
            print(s.preset_name(v) or "(no name)")
            return
        after = list(v)
        s.set_preset_name(after, " ".join(args.name))
        for a in range(290, 302):
            if after[a] != v[a]:
                d.set(a, after[a])
        print(f"named {s.preset_name(after)!r} (live: `minictl save` keeps it)")


def cmd_backup(args):
    s = settings()
    with open_device(args) as d:
        def show(b, v):
            print(f"bank {b + 1}: {s.preset_name(v)}", file=sys.stderr, flush=True)
        got = walk_banks(d, range(BANKS), show)
    banks = [{"bank": b, "name": s.preset_name(v), "values": v} for b, v in sorted(got.items())]
    data = {"minichord_backup": 1, "created": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "firmware_version": banks[0]["values"][7], "parameter_size": len(banks[0]["values"]),
            "address_names": {str(a): s.title(a) for a in sorted(s.params)}, "banks": banks}
    path = args.file or f"minichord-backup-{datetime.date.today()}.json"
    with open(path, "w") as f:
        json.dump(data, f, indent=1)
    print(f"{len(banks)} banks -> {path}")


def backup_problem(data, size):
    """minicontrol's checks: a damaged file is refused before anything is written"""
    if not isinstance(data, dict) or not data.get("minichord_backup"):
        return "that isn't a minichord backup file"
    banks = data.get("banks")
    if not isinstance(banks, list) or not banks or len(banks) > BANKS:
        return "that backup has no banks in it"
    seen = set()
    for e in banks:
        b = e.get("bank") if isinstance(e, dict) else None
        if not isinstance(b, int) or not 0 <= b < BANKS or b in seen:
            return "that backup's bank numbers don't make sense"
        seen.add(b)
        vals = e.get("values")
        if not isinstance(vals, list) or len(vals) > size or \
                not all(v is None or (isinstance(v, int) and 0 <= v < 16384) for v in vals):
            return f"bank {b + 1} in that backup is damaged"
    return None


def cmd_restore(args):
    s = settings()
    with open(args.file) as f:
        data = json.load(f)
    with open_device(args) as d:
        live = d.read()
        problem = backup_problem(data, len(live))
        if problem:
            raise RuntimeError(f"{problem}")
        entries = [e for e in data["banks"] if args.banks is None or e["bank"] in args.banks]
        if not entries:
            raise RuntimeError("none of those banks are in the file")
        fw = data.get("firmware_version")
        print(f"{args.file}: {len(entries)} banks" + (f", from firmware {fw}" if fw else "") +
              f"; the minichord has firmware {live[7]}")
        for e in entries:
            print(f"  bank {e['bank'] + 1}: {e.get('name') or ''}")
        if not confirm(f"Write these {len(entries)} banks, replacing what each holds?", args):
            return
        start = live[1]
        try:
            for e in entries:
                values = list(e["values"])
                # Sound Lab test firmware kept the master tuning at 255, not 109
                if len(values) > 255 and not values[109] and values[255] and 4320 <= values[255] <= 4460:
                    values[109] = values[255]
                # a backup from before names were kept in the bank brings its name along
                if live[7] >= 50 and e.get("name") and len(values) > 301 and not s.preset_name(values):
                    s.set_preset_name(values, e["name"])
                d.load_bank(e["bank"])
                time.sleep(0.15)
                d.apply(values, page1_defaults())
                time.sleep(0.04)
                d.save(e["bank"])
                print(f"bank {e['bank'] + 1} written", flush=True)
            bad = 0
            for e in entries:
                got = d.load_bank(e["bank"])
                diff = [a for a, v in enumerate(e["values"]) if a not in (0, 1, 7) and a not in dev.RESERVED
                        and v is not None and a < len(got) and got[a] != v and not (a == 109 and not v)
                        and not (290 <= a < 302 and not v)]
                if diff:
                    bad += 1
                    print(f"bank {e['bank'] + 1}: {len(diff)} settings differ from the file, first "
                          + ", ".join(s.names.get(a, str(a)) for a in diff[:6]))
            print("every bank matches the file" if not bad else f"{bad} banks differ")
        finally:
            d.load_bank(start)


def cmd_code(args):
    s = settings()
    if args.code:
        values, what = preset_code_from(args.code)
        with open_device(args) as d:
            d.read()
            d.apply(values, page1_defaults())
            d.command(0)
            print(f"playing {what} on the minichord (live: `minictl save` keeps it)")
        return
    with open_device(args) as d:
        v = walk_banks(d, [args.bank])[args.bank] if args.bank is not None else d.read()
    print(s.encode(v))


def cmd_describe(args):
    s = settings()
    import interpret
    presets = s.shared_presets()
    names = [p["name"] for p in presets]
    d = None
    try:
        if args.base in (None, "live"):
            try:
                d = open_device(args)
                values, base_name = d.read(), "the minichord's live preset"
            except dev.NotFound:
                if args.base == "live":
                    raise
                values, base_name = s.defaults(), "a plain starting point"
        elif args.base == "defaults":
            values, base_name = s.defaults(), "a plain starting point"
        else:
            values, base_name = preset_code_from(args.base)
        values = list(values) + [0] * (pm.PARAMETER_SIZE - len(values))
        values[7] = s.version
        text = " ".join(args.text)
        result = interpret.interpret(text, values, s.params, names, first=True)
        if result.base:
            base = next(p for p in presets if p["name"] == result.base)
            values, base_name = s.decode(base["value"]), result.base
            values[7] = s.version
            result = interpret.interpret(text, values, s.params, names, first=True)
        after, made, notes = pm.apply_changes(values, result.changes, s.params)
        if args.json:
            return print_json({"base": base_name, "understood": result.understood, "unknown": result.unknown,
                               "heard_as": result.heard_as, "notes": result.notes + notes,
                               "changes": [{"name": s.names.get(a), "address": a, "from": o, "to": n}
                                           for a, o, n in made], "code": s.encode(after)})
        print(f"Starting from {base_name}.")
        print("\nUnderstood:" if result.understood else "\nNothing in that matched a word I know.")
        for line in result.understood:
            print(f"  {line}")
        for heard, taken in result.heard_as:
            print(f"  (took \"{heard}\" as \"{taken}\")")
        if result.unknown:
            print(f"Not understood: {', '.join(result.unknown)}  (`minictl describe --words` lists what I know)")
        for n in result.notes + notes:
            print(f"  ! {n}")
        if not made:
            return
        print(f"\n{len(made)} settings changed:")
        for a, old, new in made:
            print(f"  {s.names.get(a, a)}: {s.shown(a, old, values)} -> {s.shown(a, new, after)}")
        print(f"\nPreset code:\n{s.encode(after)}")
        if args.send or args.save is not None:
            if d is None:
                d = open_device(args)
                d.read()
            d.apply(after, page1_defaults())
            d.command(0)
            print("\nPlaying on the minichord (live).")
            if args.save is not None and confirm(f"Save it to bank {args.save + 1}, replacing what is there?", args):
                d.save(args.save)
                print(f"saved to bank {args.save + 1}")
    finally:
        if d:
            d.close()


def cmd_words(args):
    import interpret
    interpret.print_vocabulary()


def cmd_conflicts(args):
    s = settings()
    if args.code:
        values, what = preset_code_from(args.code)
    else:
        with open_device(args) as d:
            values = walk_banks(d, [args.bank])[args.bank] if args.bank is not None else d.read()
        what = f"bank {values[1] + 1}" + ("" if args.bank is not None else ", live")
    values = list(values) + [0] * (pm.PARAMETER_SIZE - len(values))
    notes = s.conflicts(values)
    print(f"{what}: " + ("no settings undo one another" if not notes else f"{len(notes)} conflict" + ("s" if len(notes) > 1 else "")))
    for n in notes:
        print(f"  ! {n}")
    return 1 if notes and args.strict else 0


def cmd_play(args):
    notes = [note_number(n) for n in args.notes]
    with open_device(args) as d:
        v = d.read()
        if v[7] >= 29 and not v[8]:
            print("! MIDI in plays (setting 8) is off in this preset, so these notes are silent: "
                  "`minictl set midi-in-plays on`")
        cable = 2 if args.harp else 1
        for n in notes:
            d.note(True, n, args.velocity, args.channel, cable)
            if args.arp:
                time.sleep(args.arp)
        time.sleep(args.length)
        for n in notes:
            d.note(False, n, 0, args.channel, cable)


def cmd_panic(args):
    with open_device(args) as d:
        d.panic()
    print("all notes off, on every channel of both ports")


def cmd_rhythm(args):
    with open_device(args) as d:
        d.command(8, {"off": 0, "on": 1, "toggle": 2}[args.state])
    print(f"rhythm mode {args.state}")


def cmd_snapshot(args):
    with open_device(args) as d:
        d.command(5, 1)
    print("the live settings are remembered: `minictl revert` puts them back (until another bank loads)")


def cmd_revert(args):
    with open_device(args) as d:
        d.command(6)
    print("back to the snapshot (if there was one)")


def cmd_monitor(args):
    show_midi = args.midi or not args.serial
    show_serial = args.serial or not args.midi
    d = ser = None
    try:
        if show_midi:
            d = open_device(args)

            def heard(msg):
                if msg[0] == 0xF0:
                    print(f"{time.strftime('%H:%M:%S')} sysex {len(msg)} bytes", flush=True)
                elif msg[0] & 0xF0 in (0x80, 0x90, 0xB0, 0xE0, 0xD0, 0xA0, 0xC0):
                    kind = {0x80: "off", 0x90: "on", 0xA0: "aftertouch", 0xB0: "cc", 0xC0: "program",
                            0xD0: "pressure", 0xE0: "bend"}[msg[0] & 0xF0]
                    print(f"{time.strftime('%H:%M:%S')} ch{(msg[0] & 15) + 1:<2} {kind:10} {' '.join(map(str, msg[1:]))}",
                          flush=True)
            d.listeners.append(heard)
        if show_serial:
            import serial
            port = dev.serial_port()
            if port is None:
                print("(no serial port from the minichord)", file=sys.stderr)
            else:
                ser = serial.Serial(port, 115200, timeout=0.2)
        print("listening; Ctrl-C stops", file=sys.stderr)
        while True:
            if ser:
                line = ser.readline().decode(errors="replace").rstrip()
                if line:
                    print(f"{time.strftime('%H:%M:%S')} | {line}", flush=True)
            else:
                time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        if d:
            d.close()
        if ser:
            ser.close()


def cmd_record(args):
    used = dev.record(args.file, args.seconds, args.device)
    print(f"{args.seconds:g} s from {used} -> {args.file}")


def cmd_flash(args):
    if not os.path.isfile(args.hex):
        raise RuntimeError(f"no file {args.hex}")
    problems = dev.flash_hazards()
    for p in problems:
        print(f"! {p}")
    if problems and not args.force:
        raise RuntimeError("not flashing (--force flashes anyway)")
    before = None
    try:
        with open_device(args) as d:
            before = d.read()[7]
    except (dev.NotFound, RuntimeError):
        pass
    print(f"{args.hex}: {os.path.getsize(args.hex) // 1024} kB" +
          (f"; the minichord has firmware {before}" if before is not None else "; no minichord answering"))
    print("Close every program using the minichord's MIDI (minicontrol, a DAW) first. A firmware that changes "
          "the preset storage wipes the banks: `minictl backup` keeps them.")
    if not confirm("Flash it?", args):
        return
    took = dev.flash(args.hex)
    with open_device(args) as d:
        after = d.read()[7]
    print(f"back after {took:.0f} s, on firmware {after}")


# ---- the parser ------------------------------------------------------------------------------------

def parser():
    def common(p, default):
        kw = {} if default else {"default": argparse.SUPPRESS}
        p.add_argument("--port", help="the minichord's MIDI port, by a piece of its name (or MINICTL_PORT)", **kw)
        p.add_argument("-y", "--yes", action="store_true", help="don't ask before saving, restoring or flashing", **kw)
        p.add_argument("--json", action="store_true", help="JSON output, for scripts", **kw)

    ap = argparse.ArgumentParser(prog="minictl", description="The minichord from the command line.")
    common(ap, True)
    shared = argparse.ArgumentParser(add_help=False)
    common(shared, False)
    sub = ap.add_subparsers(dest="command", metavar="command")

    def add(name, fn, help, aliases=()):
        p = sub.add_parser(name, help=help, description=help, aliases=list(aliases), parents=[shared])
        p.set_defaults(fn=fn)
        return p

    add("info", cmd_info, "the minichord's firmware, bank and preset name")
    add("ports", cmd_ports, "the MIDI and serial ports, and which one minictl takes")
    p = add("list", cmd_list, "the settings, by name, with their ranges (words narrow it down)", ["ls"])
    p.add_argument("words", nargs="*")
    p.add_argument("-a", "--all", action="store_true", help="hidden settings too")
    p.add_argument("-v", "--verbose", action="store_true", help="what each one does")

    p = add("get", cmd_get, "read settings: those named, or every one away from its default")
    p.add_argument("settings", nargs="*", help="names or addresses")
    p.add_argument("-a", "--all", action="store_true", help="every setting")
    p.add_argument("-b", "--bank", type=bank_arg, help="a stored bank instead of the live settings (loads it)")
    p.add_argument("-c", "--code", help="a preset code, file or shared preset name instead of the minichord")
    p.add_argument("--raw", action="store_true", help="stored values only, one a line")

    p = add("set", cmd_set, "change live settings: set reverb-size 80 chord-attack 200")
    p.add_argument("pairs", nargs="+", metavar="setting value")
    p.add_argument("-s", "--save", action="store_true", help="then save to the current bank (asks)")
    p.add_argument("--raw", action="store_true", help="stored values to any address, unchecked")

    p = add("bank", cmd_bank, "which bank is playing, or load one: bank 3")
    p.add_argument("bank", nargs="?", type=bank_arg)
    p = add("banks", cmd_banks, "every bank's name (walks through them, then back)")
    p.add_argument("--codes", action="store_true", help="and each bank's preset code")
    p = add("save", cmd_save, "save the live settings to the current bank, or the one given (asks)")
    p.add_argument("bank", nargs="?", type=bank_arg)
    p = add("reset", cmd_reset, "put a bank back to its factory preset (asks)")
    p.add_argument("bank", type=bank_arg)
    p = add("name", cmd_name, "the live preset's name, or name it (firmware 50 and on)")
    p.add_argument("name", nargs="*")
    p = add("backup", cmd_backup, "every bank to a file, minicontrol's backup format")
    p.add_argument("file", nargs="?")
    p = add("restore", cmd_restore, "write banks from a backup file, then read them back (asks)")
    p.add_argument("file")
    p.add_argument("--banks", type=banks_arg, help="only these: 1,3,5-7")
    p = add("code", cmd_code, "the live preset as a share code, or play a code (or shared preset's name)")
    p.add_argument("code", nargs="?")
    p.add_argument("-b", "--bank", type=bank_arg, help="a stored bank's code (loads it)")

    p = add("describe", cmd_describe, "a preset from words, by preset_maker's rules: describe \"darker, more reverb\"")
    p.add_argument("text", nargs="+")
    p.add_argument("--base", help="start from: live (the default when a minichord is plugged in), defaults, "
                                  "a code, a file or a shared preset's name")
    p.add_argument("--send", action="store_true", help="play the result on the minichord (live)")
    p.add_argument("--save", type=bank_arg, metavar="BANK", help="play it and save it to a bank (asks)")
    add("words", cmd_words, "every word and phrase describe understands")
    p = add("conflicts", cmd_conflicts, "settings that undo one another, in the live preset or a code")
    p.add_argument("code", nargs="?")
    p.add_argument("-b", "--bank", type=bank_arg)
    p.add_argument("--strict", action="store_true", help="exit 1 when there are any")

    p = add("play", cmd_play, "play notes through MIDI in: play C4 E4 G4")
    p.add_argument("notes", nargs="+")
    p.add_argument("--harp", action="store_true", help="on the harp's port (cable 2)")
    p.add_argument("-l", "--length", type=float, default=1.0, help="seconds held")
    p.add_argument("-v", "--velocity", type=int, default=100)
    p.add_argument("-c", "--channel", type=int, default=1)
    p.add_argument("--arp", type=float, default=0, help="seconds between the notes' starts")
    add("panic", cmd_panic, "all notes off")
    p = add("rhythm", cmd_rhythm, "rhythm mode on, off or toggle")
    p.add_argument("state", choices=["on", "off", "toggle"])
    add("snapshot", cmd_snapshot, "remember the live settings, to revert to")
    add("revert", cmd_revert, "back to the snapshot")
    p = add("monitor", cmd_monitor, "print what the minichord sends: MIDI and its serial log")
    p.add_argument("--midi", action="store_true", help="MIDI only")
    p.add_argument("--serial", action="store_true", help="serial only")
    p = add("record", cmd_record, "record the minichord's USB audio to a wav")
    p.add_argument("file")
    p.add_argument("-s", "--seconds", type=float, default=10)
    p.add_argument("--device", help="the audio input (or MINICTL_AUDIO)")
    p = add("flash", cmd_flash, "put a firmware .hex on the minichord (asks)")
    p.add_argument("hex")
    p.add_argument("--force", action="store_true", help="flash despite a warning")
    sub.add_parser("help", help="this list").set_defaults(fn=None)
    return ap


def main(argv=None):
    ap = parser()
    args = ap.parse_args(argv)
    if not getattr(args, "fn", None):
        ap.print_help()
        return 0
    try:
        return args.fn(args) or 0
    except KeyboardInterrupt:
        print()
        return 130
    except BrokenPipeError:            # piped into head, which stopped reading
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        return 141
    except (RuntimeError, ValueError, OSError) as e:
        print(f"minictl: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
