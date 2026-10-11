"""parity_test.py [minicontrol folder]: minictl's commands.py against minicontrol's commands.js, over
the same instructions (tests/commands_cases.json: minicontrol's own tests and suggestions, and more).
Both read the describer data exported from this checkout and minicontrol's presets and dropdowns, and
each instruction must change the same values and say the same things. Needs node.

    python tests/parity_test.py ~/minicontrol
"""
import json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from minictl import commands as C          # noqa: E402
from minictl.settings import pm, interpret  # noqa: E402


def python_side(cases, data, minicontrol):
    with open(os.path.join(minicontrol, "parameters.json")) as f:
        parameters = json.load(f)
    options = {}
    for section in parameters.values():
        for p in section if isinstance(section, list) else []:
            if p.get("options"):
                options[str(p["sysex_adress"])] = {str(o["value"]): o["label"] for o in p["options"]}
    C.setup(options, data)
    with open(os.path.join(minicontrol, "shared_presets.json")) as f:
        presets = json.load(f)["shared_presets"]
    out = []
    for c in cases:
        base, work = {}, {}

        def values(k):
            if k not in work:
                base[k] = pm.default_values(C.NAMER.params, data["firmware_version"])
                for a, v in ((c.get("before") or {}).get(k) or {}).items():
                    base[k][int(a)] = v
                work[k] = list(base[k])
            return work[k]

        ctx = C.Context(values, c.get("current_bank", 2), [p["name"] for p in presets] if c.get("preset_names") else [],
                        presets, c.get("bank_names") or [])
        for k in (c.get("before") or {}):
            values(k)
        for k in C.places_named(c["text"], ctx.current_bank, ctx.bank_names):
            values(k)
        try:
            r = C.plan(c["text"], ctx)
        except Exception as e:
            out.append({"text": c["text"], "error": f"{type(e).__name__}: {e}"})
            continue
        changed = {}
        for k in sorted(work):
            for a, v in enumerate(work[k]):
                if v != base[k][a]:
                    changed.setdefault(k, {})[str(a)] = v
        out.append({"text": c["text"], "changed": changed, "understood": r["understood"], "notes": r["notes"],
                    "unknown": r["unknown"],
                    "questions": [{"phrase": q.phrase, "choices": [x["addr"] for x in q.choices],
                                   "clause": q.clause(q.choices[0]["addr"])} for q in r["questions"]]})
    return out


def main():
    minicontrol = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.environ.get("MINICONTROL", "~/minicontrol"))
    with open(os.path.join(HERE, "commands_cases.json")) as f:
        cases = json.load(f)
    params = pm.load_parameters()
    data = json.loads(json.dumps(interpret.export_data(params, pm.firmware_version(params))))
    with tempfile.TemporaryDirectory() as tmp:
        data_file = os.path.join(tmp, "describe_data.json")
        cases_file = os.path.join(tmp, "cases.json")
        with open(data_file, "w") as f:
            json.dump(data, f)
        with open(cases_file, "w") as f:
            json.dump(cases, f)
        js = json.loads(subprocess.run(["node", os.path.join(HERE, "parity_commands.js"), minicontrol, data_file, cases_file],
                                       capture_output=True, text=True, check=True).stdout)
    py = python_side(cases, data, minicontrol)
    failed = 0
    for j, p in zip(js, py):
        diffs = [k for k in ("error", "changed", "understood", "notes", "unknown", "questions") if j.get(k) != p.get(k)]
        if diffs:
            failed += 1
            print(f'DIFFERS "{j["text"]}"')
            for k in diffs:
                print(f"  {k}\n    js: {json.dumps(j.get(k))[:400]}\n    py: {json.dumps(p.get(k))[:400]}")
    print(f"{len(cases) - failed} of {len(cases)} instructions read alike")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
