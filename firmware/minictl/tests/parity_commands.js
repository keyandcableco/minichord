// node parity_commands.js <minicontrol folder> <describe_data.json> <cases.json>
// minicontrol's commands.js over each case, printed as JSON for parity_test.py to hold minictl's
// commands.py against: the values each place ends with (as changes from where it started), and
// what was understood, noted, not known and asked.
const fs = require("fs");
const path = require("path");
const [dir, dataFile, casesFile] = process.argv.slice(2);
const describe = require(path.resolve(dir, "describe.js"));
const data = JSON.parse(fs.readFileSync(dataFile));
const presets = JSON.parse(fs.readFileSync(path.join(dir, "shared_presets.json"))).shared_presets;
describe.setData(data, presets);
const commands = require(path.resolve(dir, "commands.js"));
commands.setup(data, JSON.parse(fs.readFileSync(path.join(dir, "parameters.json"))));

const out = [];
for (const c of JSON.parse(fs.readFileSync(casesFile))) {
  const base = {}, work = {};
  const ctx = {
    currentBank: c.current_bank != null ? c.current_bank : 2, presetNames: c.preset_names ? presets.map(p => p.name) : [],
    presets, bankNames: c.bank_names || [],
    values: k => {
      if (!work[k]) {
        base[k] = describe.defaultValues();
        for (const [a, v] of Object.entries((c.before || {})[k] || {})) base[k][+a] = v;
        work[k] = base[k].slice();
      }
      return work[k];
    },
    why: () => {},
  };
  for (const k of Object.keys(c.before || {})) ctx.values(k);
  for (const k of commands.placesNamed(c.text, ctx.currentBank, ctx.bankNames)) ctx.values(k);
  let r;
  try {
    r = commands.plan(c.text, ctx);
  } catch (e) {
    out.push({ text: c.text, error: String(e) });
    continue;
  }
  const changed = {};
  for (const k of Object.keys(work).sort()) {
    work[k].forEach((v, a) => { if (v !== base[k][a]) (changed[k] = changed[k] || {})[a] = v; });
  }
  out.push({ text: c.text, changed, understood: r.understood, notes: r.notes, unknown: r.unknown,
             questions: r.questions.map(q => ({ phrase: q.phrase, choices: q.choices.map(x => x.addr),
                                                 clause: q.clause(q.choices[0].addr) })) });
}
process.stdout.write(JSON.stringify(out));
