"""minictl's tests: names, values and codes offline, then every device command against a fake minichord
on virtual MIDI ports (Linux and macOS). Nothing here talks to a real minichord.

    python tests/test_minictl.py
"""
import contextlib, io, json, os, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from minictl import cli, device                    # noqa: E402
from minictl.settings import Settings, pm          # noqa: E402

S = Settings()


def read_json(path):
    with open(path) as f:
        return json.load(f)


def write_json(path, data):
    with open(path, "w") as f:
        json.dump(data, f)


def run(*argv):
    """minictl's output and exit code for these arguments"""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = cli.main(list(argv))
    return code, out.getvalue() + err.getvalue()


class Offline(unittest.TestCase):
    def test_names_are_unique_and_found(self):
        self.assertEqual(len(set(S.names.values())), len(S.names))
        for a, n in S.names.items():
            self.assertEqual(S.find(n), a, n)

    def test_find(self):
        self.assertEqual(S.find("mod-knob"), 14)
        self.assertEqual(S.find("reverb size"), 24)
        self.assertEqual(S.find("key"), 35)
        self.assertEqual(S.find("237"), 237)
        self.assertEqual(S.find("mod-main-control"), 14)       # the generated name stays an alias
        with self.assertRaises(ValueError):
            S.find("attack")                                     # chord or harp, filter or not: says which
        with self.assertRaises(ValueError):
            S.find("no-such-thing")

    def test_parse_and_show(self):
        v = S.defaults()
        self.assertEqual(S.parse(237, "werck", v), 4)
        self.assertEqual(S.parse(265, "choir", v), 3)
        self.assertEqual(S.parse(14, "reverb-size", v), 24)
        self.assertEqual(S.parse(14, "none", v), 0)
        self.assertEqual(S.parse(8, "on", v), 1)
        self.assertEqual(S.shown(237, 4), "4 (Werckmeister III)")
        self.assertEqual(S.shown(14, 24), "reverb-size")
        self.assertEqual(S.shown(24, 50), "0.5")
        with self.assertRaises(ValueError):
            S.parse(265, "banjo", v)

    def test_preset_names(self):
        v = [0] * 512
        Settings.set_preset_name(v, "Café night")
        self.assertEqual(Settings.preset_name(v), "Cafe night")
        Settings.set_preset_name(v, "x" * 40)
        self.assertEqual(len(Settings.preset_name(v)), 24)

    def test_code_round_trip(self):
        for p in S.shared_presets()[:10]:
            v = S.decode(p["value"])
            self.assertEqual(S.decode(S.encode(v)), v, p["name"])

    def test_get_from_code(self):
        code, out = run("get", "-c", "Jazzy Rhodes", "chord-voice", "temperament")
        self.assertEqual(code, 0)
        self.assertIn("(Equal)", out)

    def test_describe_offline(self):
        code, out = run("--json", "describe", "--base", "defaults", "warm pad chords, the mod knob opens the filter")
        self.assertEqual(code, 0)
        d = json.loads(out)
        self.assertIn("mod-knob", [c["name"] for c in d["changes"]])
        self.assertTrue(S.decode(d["code"]))

    def test_conflicts(self):
        _, out = run("--json", "describe", "--base", "Jazzy Rhodes", "drums only")
        code, out = run("conflicts", "--strict", json.loads(out)["code"])
        self.assertEqual(code, 1)
        self.assertIn("drums only", out)

    def test_ports_are_picked_by_name(self):
        linux = ["Midi Through:Midi Through Port-0 14:0", "minichord:minichord MIDI 1 24:0",
                 "minichord:minichord MIDI 2 24:1"]
        windows = ["Microsoft GS Wavetable Synth 0", "minichord 1", "MIDIOUT2 (minichord) 2"]
        mac = ["IAC Driver Bus 1", "minichord Port 2", "minichord Port 1"]
        for names in (linux, windows, mac):
            self.assertIn("1", names[device.pick_port(names, 1)].replace("MIDI 1", "1"))
            self.assertRegex(names[device.pick_port(names, 2)], r"2")
        self.assertIsNone(device.pick_port(["IAC Driver Bus 1"], 1))

    def test_notes(self):
        self.assertEqual(cli.note_number("C4"), 60)
        self.assertEqual(cli.note_number("Eb3"), 51)
        self.assertEqual(cli.note_number("F#5"), 78)
        self.assertEqual(cli.note_number("64"), 64)


@unittest.skipIf(os.name == "nt", "virtual MIDI ports don't exist on Windows")
class WithFakeMinichord(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from fake_minichord import FakeMinichord
        cls.fake = FakeMinichord()
        os.environ["MINICTL_PORT"] = "fakechord"

    @classmethod
    def tearDownClass(cls):
        cls.fake.close()
        os.environ.pop("MINICTL_PORT", None)

    def setUp(self):
        self.fake.live = list(self.fake.banks[0])
        self.fake.saves.clear()

    def test_info(self):
        code, out = run("--json", "info")
        self.assertEqual(code, 0, out)
        d = json.loads(out)
        self.assertEqual((d["firmware"], d["bank"], d["name"]), (S.version, 1, "Bank 1"))

    def test_set_and_get(self):
        code, out = run("set", "reverb-size", "0.8", "temperament", "kellner", "mod-knob", "chord-attack")
        self.assertEqual(code, 0, out)
        self.assertEqual(self.fake.live[24], 80)
        self.assertEqual(self.fake.live[237], 8)
        self.assertEqual(self.fake.live[14], S.find("chord-attack"))
        self.assertIn("live only", out)
        self.assertEqual(self.fake.saves, [])
        code, out = run("get", "reverb-size", "temperament")
        self.assertIn("0.8", out)
        self.assertIn("Kellner", out)

    def test_set_clamps_and_refuses(self):
        code, out = run("set", "reverb-size", "7")
        self.assertIn("!", out)
        self.assertEqual(self.fake.live[24], 100)
        code, out = run("set", "usb-audio", "1")             # the instrument's own setup isn't a preset's
        self.assertIn("isn't a setting a preset can change", out)

    def test_save_asks(self):
        run("set", "reverb-size", "0.2")
        code, out = run("save")                               # no terminal to ask: refused
        self.assertEqual(self.fake.saves, [])
        code, out = run("--yes", "save", "3")
        self.assertEqual(self.fake.saves, [2])
        self.assertEqual(self.fake.banks[2][24], 20)

    def test_banks_walk_and_return(self):
        self.fake.command(4, 4)
        code, out = run("banks")
        self.assertEqual(code, 0, out)
        self.assertIn("12  Bank 12", out)
        self.assertEqual(self.fake.live[1], 4)                # back on the bank it was on

    def test_bank_load(self):
        run("bank", "6")
        self.assertEqual(self.fake.live[1], 5)
        self.assertEqual(self.fake.live[20], 150)
        run("bank", "1")

    def test_backup_and_restore(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "b.json")
            code, out = run("backup", path)
            self.assertEqual(code, 0, out)
            data = read_json(path)
            self.assertEqual(len(data["banks"]), 12)
            self.assertEqual(data["banks"][3]["name"], "Bank 4")
            data["banks"][3]["values"][24] = 33
            data["banks"][3]["name"] = "Changed"
            write_json(path, data)
            code, out = run("--yes", "restore", path, "--banks", "4")
            self.assertEqual(code, 0, out)
            self.assertIn("every bank matches", out)
            self.assertEqual(self.fake.banks[3][24], 33)
            self.assertEqual(self.fake.saves, [3])
            write_json(path, {"minichord_backup": 1, "banks": [{"bank": 40, "values": []}]})
            code, out = run("--yes", "restore", path)
            self.assertEqual(code, 1)
            self.assertIn("don't make sense", out)

    def test_code_both_ways(self):
        code, out = run("code", "Raver")
        self.assertEqual(code, 0, out)
        raver = S.decode(next(p["value"] for p in S.shared_presets() if p["name"] == "Raver"))
        self.assertEqual([self.fake.live[a] for a in range(8, 256)], raver[8:256])
        code, out = run("code")
        self.assertEqual(S.decode(out.strip())[8:256], raver[8:256])

    def test_describe_live_and_send(self):
        code, out = run("describe", "--send", "lots of reverb on the chords")
        self.assertEqual(code, 0, out)
        self.assertIn("live preset", out)
        self.assertGreater(self.fake.live[184], self.fake.banks[0][184])

    def test_name(self):
        run("name", "Night", "drive")
        self.assertEqual(Settings.preset_name(self.fake.live), "Night drive")

    def test_play_rhythm_snapshot(self):
        self.fake.notes.clear()
        run("set", "midi-in-plays", "on")
        run("play", "C4", "E4", "-l", "0.05")
        run("play", "--harp", "G4", "-l", "0.05")
        import time
        time.sleep(0.1)
        cables = [(c, m[0] & 0xF0, m[1]) for c, m in self.fake.notes]
        self.assertIn((1, 0x90, 60), cables)
        self.assertIn((2, 0x90, 67), cables)
        run("rhythm", "on")
        self.assertTrue(self.fake.rhythm)
        run("snapshot")
        run("set", "reverb-size", "0.1")
        run("revert")
        self.assertEqual(self.fake.live[24], self.fake.banks[0][24])


if __name__ == "__main__":
    unittest.main(verbosity=1)
