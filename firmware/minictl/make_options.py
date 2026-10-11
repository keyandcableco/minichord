#!/usr/bin/env python3
"""make_options.py [minicontrol/parameters.json]: refreshes minictl/options.json, the names minicontrol's
dropdowns give each setting's values (the firmware's parameters.json carries only the numbers). Run it
after minicontrol's generate.py adds or renames a choice."""
import json, os, sys

src = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/minicontrol/parameters.json")
with open(src) as f:
    d = json.load(f)
out = {}
for section in d.values():
    for p in section if isinstance(section, list) else []:
        if p.get("options"):
            out[str(p["sysex_adress"])] = {str(o["value"]): o["label"] for o in p["options"]}
dest = os.path.join(os.path.dirname(os.path.abspath(__file__)), "minictl", "options.json")
with open(dest, "w") as f:
    json.dump(dict(sorted(out.items(), key=lambda kv: int(kv[0]))), f, indent=1, ensure_ascii=False)
    f.write("\n")
print(f"{len(out)} settings with named values -> {dest}")
