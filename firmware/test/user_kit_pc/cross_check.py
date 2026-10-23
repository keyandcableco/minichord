#!/usr/bin/env python3
# The other two builders against the test vectors the PC harness feeds the firmware's core: minictl's
# kit.py (~/minictl-userkit, or MINICTL) and minicontrol's kits.js (~/minicontrol-userkit, or
# MINICONTROL), each where it's there. What they make from the vectors must be the vectors to the byte:
# then the harness's upload of synthetic.frames is their upload too.
#
#   python3 cross_check.py <test/kits folder>
#
# minictl: synthetic.mckit and synthetic.image framed by kit.Kit must be synthetic.frames, the .mckit
# read and written again must be itself, and its user_kit.py must be the firmware's generator/user_kit.py
# (sync.py copies it). minicontrol, through node: synthetic.json built by buildImage must be
# synthetic.image, framed by frames() synthetic.frames, written by writeMckit synthetic.mckit, and every
# broken image must fail checkImage with expected.json's reason. A builder that isn't there is noted
# and passed over. Exit status 1 if anything differs.
import json
import os
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True   # nothing left in the minictl checkout
kits = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "kits"))
firmware = os.path.dirname(os.path.dirname(kits))
minictl = os.environ.get("MINICTL", os.path.expanduser("~/minictl-userkit"))
minicontrol = os.environ.get("MINICONTROL", os.path.expanduser("~/minicontrol-userkit"))
failures = 0
frames_want = [line for line in open(os.path.join(kits, "synthetic.frames")).read().split("\n") if line]


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


# ---- minictl ----
kit_py = os.path.join(minictl, "minictl", "kit.py")
if not os.path.exists(kit_py):
    print(f"skip minictl: {kit_py} isn't there yet")
else:
    sys.path.insert(0, minictl)
    from minictl import kit as K
    for name in ("synthetic.mckit", "synthetic.image"):
        k = K.Kit.load(os.path.join(kits, name))
        check(k.reason() == 0, f"minictl: {name} passes its check")
        got = [bytes(m).hex() for m in k.frames()]
        check(got == frames_want, f"minictl: {name} framed is synthetic.frames ({len(got)} messages)")
    mckit = open(os.path.join(kits, "synthetic.mckit"), "rb").read()
    k = K.Kit.load(os.path.join(kits, "synthetic.mckit"))
    check(k.image == open(os.path.join(kits, "synthetic.image"), "rb").read(), "minictl: synthetic.mckit's image is synthetic.image")
    check(K.U.write_mckit(k.meta, k.image) == mckit, "minictl: synthetic.mckit written again is itself")
    vendored = os.path.join(minictl, "minictl", "user_kit.py")
    reference = os.path.join(firmware, "generator", "user_kit.py")
    check(open(vendored, "rb").read() == open(reference, "rb").read(), "minictl: its user_kit.py is the firmware's generator/user_kit.py")

# ---- minicontrol ----
kits_js = os.path.join(minicontrol, "kits.js")
node = shutil.which("node")
if not os.path.exists(kits_js):
    print(f"skip minicontrol: {kits_js} isn't there yet")
elif not node:
    print("skip minicontrol: no node on the PATH")
else:
    script = r"""
const fs = require("fs"), path = require("path");
const K = require(process.argv[1]);
const kits = process.argv[2];
const synth = JSON.parse(fs.readFileSync(path.join(kits, "synthetic.json")));
const voices = synth.voices.map(v => {
  if (!v) return null;
  const raw = Buffer.from(v.samples, "base64");
  const samples = [];
  for (let i = 0; i < raw.length; i += 2) samples.push(raw.readInt16LE(i));
  return {samples, rate: v.rate, level: v.level, pan: v.pan, choke: v.choke, flags: v.flags, pitch: v.pitch};
});
const image = K.buildImage({base_kit: synth.base_kit, name: synth.name, voices});
const hex = b => Buffer.from(b).toString("hex");
const broken = JSON.parse(fs.readFileSync(path.join(kits, "broken", "expected.json")));
const reasons = {};
for (const f of Object.keys(broken)) reasons[f] = K.checkImage(new Uint8Array(fs.readFileSync(path.join(kits, "broken", f))));
process.stdout.write(JSON.stringify({
  image: hex(image),
  frames: K.frames(image).map(hex),
  mckit: hex(K.writeMckit(synth.meta, image)),
  reasons,
}));
"""
    try:
        out = subprocess.run([node, "-e", script, kits_js, kits], capture_output=True, text=True, check=True).stdout
        got = json.loads(out)
        image = open(os.path.join(kits, "synthetic.image"), "rb").read()
        check(bytes.fromhex(got["image"]) == image, "minicontrol: synthetic.json built by buildImage is synthetic.image")
        check(got["frames"] == frames_want, f"minicontrol: frames() of it is synthetic.frames ({len(got['frames'])} messages)")
        check(bytes.fromhex(got["mckit"]) == open(os.path.join(kits, "synthetic.mckit"), "rb").read(),
              "minicontrol: writeMckit of it is synthetic.mckit")
        expected = json.load(open(os.path.join(kits, "broken", "expected.json")))
        wrong = {f: (got["reasons"][f], r) for f, r in expected.items() if got["reasons"].get(f) != r}
        check(not wrong, f"minicontrol: checkImage gives expected.json's reason for all {len(expected)} broken images"
              + (f": wrong {wrong}" if wrong else ""))
    except subprocess.CalledProcessError as e:
        check(False, "minicontrol: node failed: " + e.stderr.strip().splitlines()[-1] if e.stderr.strip() else "minicontrol: node failed")

print(f"{failures} failed")
sys.exit(1 if failures else 0)
