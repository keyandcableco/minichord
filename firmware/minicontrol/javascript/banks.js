//-->>BANKS: READ EVERY BANK, REORDER, BULK EDIT, PROFILES
// Reading twelve banks walks the minichord audibly through every preset, so it
// happens once, on request, and is then kept. Moving banks and bulk edits are
// staged against that copy; applying writes back only the banks that changed,
// because every write is a flash erase.
//
// Needs control command 4 (load a bank), so the button that opens this only
// becomes active on firmware that has it.

const bankState = {
  slots: null,       // [{ id, values, dirty, name }] in slot order, or null when unread
  original: null,    // bank -> the values it held when read (or last written)
  originalNames: null,
  read: false,
  stale: false,
  busy: false,
};
// Bulk edits staged so far, one row per setting, each remembering what every
// bank held before it so the row can be taken back. The before values are kept
// against the slot rather than its position, so they follow a bank when it is
// dragged.
let bulkStaged = [];

const BANK_COUNT = 12;
const RHYTHM_FIRST = 220, RHYTHM_LAST = 235;
const DEGREE_LABELS = ["1", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7"];
const SECTION_LABEL = { global_parameter: "Global", harp_parameter: "Harp", chord_parameter: "Chord" };

let bankParams = null;   // address -> parameters.json entry, with .section added
let bankParamOrder = []; // entries in the order parameters.json lists them

async function loadBankParams() {
  if (bankParams) return;
  const d = await (await fetch('./json/parameters.json')).json();
  bankParams = {};
  bankParamOrder = [];
  Object.keys(SECTION_LABEL).forEach(section => (d[section] || []).forEach(p => {
    const entry = Object.assign({ section }, p);
    bankParams[p.sysex_adress] = entry;
    bankParamOrder.push(entry);
  }));
}

// Anything that writes a bank from outside the sheet (saving, resetting a bank,
// wiping memory) leaves the copy wrong. Say so rather than show stale banks.
function bankCacheStale() {
  if (bankState.read) bankState.stale = true;
  if (bankSheetOpen()) renderBankSheet();
}

//-->>names: typed by the player, kept in this browser, and following the bank when it moves
const BANK_NAMES_KEY = "minicontrol_bank_names";
function bankNamesGet() {
  let raw = [];
  try { raw = JSON.parse(localStorage.getItem(BANK_NAMES_KEY)) || []; } catch (e) { raw = []; }
  const out = [];
  for (let i = 0; i < BANK_COUNT; i++) out.push(typeof raw[i] === "string" ? raw[i] : "");
  return out;
}
function bankNamesSet(names) {
  try { localStorage.setItem(BANK_NAMES_KEY, JSON.stringify(names.slice(0, BANK_COUNT))); } catch (e) { }
}

//-->>naming settings and values
// A setting whose value is itself an address: the knob assignments and the
// double tap controls.
function isTargetSetting(p) {
  return !!p && / control( \d)?$/.test(p.name) && p.max_value === 219;
}

function paramLabel(p) {
  if (!p) return "";
  return [SECTION_LABEL[p.section], p.group, p.name].join(" · ");
}

// Every setting a knob or double tap can point at, by name rather than number.
let targetOptionsCache = null;
function targetOptions() {
  if (targetOptionsCache) return targetOptionsCache;
  const values = [0], labels = ["none"];
  bankParamOrder.forEach(p => {
    const a = p.sysex_adress;
    if (a < 21 || a > 219 || p.group === "hidden" || isTargetSetting(p) || p.follows_target != null) return;
    if (values.includes(a)) return;
    values.push(a);
    labels.push(paramLabel(p));
  });
  targetOptionsCache = { values, labels };
  return targetOptionsCache;
}

// A double tap value means whatever its control points at, so its range and
// type come from that target. Across twelve banks the control can differ; it
// is only known when the same staged set (or profile) names it.
function valueMeta(p, edits) {
  if (!p || p.follows_target == null) return p;
  const control = (edits || []).find(e => e.addr === p.follows_target);
  const target = control && bankParams[control.value];
  if (!target || target.follows_target != null || isTargetSetting(target)) return p;
  return Object.assign({}, target, { sysex_adress: p.sysex_adress, name: p.name, group: p.group, section: p.section, targetName: target.name });
}

function valueLabel(p, v, edits) {
  if (!p) return String(v);
  if (isTargetSetting(p)) {
    const opts = targetOptions();
    const i = opts.values.indexOf(v);
    return i >= 0 ? opts.labels[i] : "address " + v;
  }
  const m = valueMeta(p, edits);
  if (m.data_type === "degrees") {
    const on = DEGREE_LABELS.filter((_, bit) => v & (1 << bit));
    return on.length ? on.join(" ") : "no notes";
  }
  if (m.data_type === "float") return String(v / miniChordController.float_multiplier);
  return String(v);
}

//-->>reading and writing the device
async function readAllBanks(onProgress) {
  if (!miniChordController.isConnected()) throw new Error("no minichord connected");
  const startingBank = miniChordController.active_bank_number;
  const names = bankNamesGet();
  const slots = [], original = [];
  for (let b = 0; b < BANK_COUNT; b++) {
    if (onProgress) onProgress(b, BANK_COUNT);
    const values = await miniChordController.readBank(b, 3000, true);
    const copy = Array.from(values, v => (v == null ? 0 : v));
    slots.push({ id: b, values: copy, dirty: false, name: names[b] });
    original[b] = copy.slice();
  }
  await returnToBank(startingBank);
  bankState.slots = slots;
  bankState.original = original;
  bankState.originalNames = names;
  bankState.read = true;
  bankState.stale = false;
  bulkStaged = [];
  return slots;
}

// Leave the minichord on the bank it was on, and let the page show it once.
async function returnToBank(bank) {
  if (bank >= 0 && bank < BANK_COUNT) {
    try { await miniChordController.readBank(bank, 3000, true); } catch (e) { }
  }
  miniChordController.requestCurrentData();
}

// Write one bank. It is loaded first and its report awaited, so the page's pot
// re-centring for that load has already gone out before the bank's own values
// are written over it, then saved. The short wait after the load lets a second
// report (one asked for while the load was still going) arrive and be
// re-centred before the writes rather than after them.
async function writeBank(bank, values) {
  await miniChordController.readBank(bank, 3000, true);
  await new Promise(r => setTimeout(r, 150));
  // 0 is the command address, 1 the bank number, 7 the firmware's own version
  for (let a = 2; a < values.length; a++) {
    if (a === miniChordController.firmware_adress || values[a] == null) continue;
    miniChordController.sendParameter(a, values[a]);
    if ((a & 31) === 0) await new Promise(r => setTimeout(r, 1));
  }
  await new Promise(r => setTimeout(r, 40));
  miniChordController.saveCurrentSettings(bank);
  await new Promise(r => setTimeout(r, 120));   // the write is to flash
}

// Write back the slots that changed, and only those: each write is a flash erase.
async function applyBankChanges(onProgress) {
  if (!miniChordController.isConnected()) throw new Error("no minichord connected");
  if (!bankState.slots) return 0;
  const dirty = [];
  bankState.slots.forEach((slot, i) => { if (slot.dirty) dirty.push(i); });
  if (!dirty.length) return 0;
  const startingBank = miniChordController.active_bank_number;
  for (let n = 0; n < dirty.length; n++) {
    const i = dirty[n];
    if (onProgress) onProgress(n, dirty.length);
    const slot = bankState.slots[i];
    await writeBank(i, slot.values);
    slot.dirty = false;
    bankState.original[i] = slot.values.slice();
    bankState.originalNames[i] = slot.name || "";
  }
  // what was written is now what is in each bank: it is the new starting
  // point, and the staged rows no longer describe anything to go back to
  bankState.slots.forEach((slot, i) => { slot.id = i; });
  bulkStaged = [];
  await returnToBank(startingBank);
  return dirty.length;
}

//-->>whole-device backup and restore
// Every bank in one file, so an instrument can be restored or swapped
// wholesale. The device has no bulk transfer, so this walks the banks with the
// load bank command. The file records the firmware it came from, since a
// restore into different firmware may be reading addresses that have since
// moved, and names each address once at the top so it stays readable. The
// format is Sound Lab's, so a backup made in either restores in the other.
const BACKUP_FORMAT = 1;

async function backupAllBanks(onProgress) {
  if (!miniChordController.isConnected()) throw new Error("no minichord connected");
  await loadBankParams();
  const startingBank = miniChordController.active_bank_number;
  const names = bankNamesGet();
  const banks = [];
  for (let b = 0; b < BANK_COUNT; b++) {
    if (onProgress) onProgress(b, BANK_COUNT);
    const values = await miniChordController.readBank(b, 3000, true);
    banks.push({ bank: b, name: names[b] || "", values: Array.from(values, v => (v == null ? 0 : v)) });
  }
  await returnToBank(startingBank);
  const address_names = {};
  bankParamOrder.forEach(p => { address_names[p.sysex_adress] = paramLabel(p); });
  return {
    minichord_backup: BACKUP_FORMAT,
    created: new Date().toISOString(),
    firmware_version: banks[0].values[miniChordController.firmware_adress],
    parameter_size: miniChordController.parameter_size,
    address_names,
    banks,
  };
}

// A backup file is untrusted: every bank is checked before anything is written,
// so a damaged file is refused rather than half restored.
function backupProblem(data) {
  if (!data || !data.minichord_backup) return "That isn't a minichord backup file";
  if (!Array.isArray(data.banks) || !data.banks.length || data.banks.length > BANK_COUNT) return "That backup has no banks in it";
  const seen = new Set();
  for (const entry of data.banks) {
    if (!entry || !Number.isInteger(entry.bank) || entry.bank < 0 || entry.bank >= BANK_COUNT || seen.has(entry.bank)) return "That backup's bank numbers don't make sense";
    seen.add(entry.bank);
    if (!Array.isArray(entry.values) || entry.values.length > miniChordController.parameter_size
      || !entry.values.every(v => v == null || (Number.isInteger(v) && v >= 0 && v < 16384))) return "Bank " + (entry.bank + 1) + " in that backup is damaged";
    if (entry.name != null && typeof entry.name !== "string") return "That backup's bank names are damaged";
  }
  return null;
}

async function restoreAllBanks(data, onProgress) {
  if (!miniChordController.isConnected()) throw new Error("no minichord connected");
  const startingBank = miniChordController.active_bank_number;
  const names = bankNamesGet();
  for (let i = 0; i < data.banks.length; i++) {
    const entry = data.banks[i];
    if (onProgress) onProgress(i, data.banks.length);
    const values = entry.values.slice();
    // Backups from Sound Lab test firmware that kept master tuning as device
    // state, at address 255, carry the tuning in every bank and nothing at 109.
    // Give each such bank that tuning, so a tuned device stays tuned.
    const legacyTuning = values[255];
    if (!values[109] && legacyTuning >= 4320 && legacyTuning <= 4460) values[109] = legacyTuning;
    await writeBank(entry.bank, values);
    if (typeof entry.name === "string") names[entry.bank] = entry.name.slice(0, 24);
  }
  bankNamesSet(names);
  bankCacheStale();
  await returnToBank(startingBank);
}

function bankStatus(text) {
  const el = document.getElementById("banks_status");
  if (el) el.textContent = text;
}

async function backup_all_banks() {
  if (!miniChordController.isConnected()) { document.getElementById("information_zone").focus(); return; }
  if (bankState.busy) return;
  bankState.busy = true;
  try {
    const data = await backupAllBanks((i, n) => bankStatus("Reading bank " + (i + 1) + " of " + n + "\u2026"));
    downloadJson(data, "minichord-backup-" + data.created.slice(0, 10) + ".json");
    bankStatus("Backed up all twelve banks");
  } catch (e) {
    bankStatus("Backup failed: " + e.message);
  }
  bankState.busy = false;
}

function restore_all_banks() {
  if (!miniChordController.isConnected()) { document.getElementById("information_zone").focus(); return; }
  if (bankState.busy) return;
  const inp = document.createElement("input");
  inp.type = "file"; inp.accept = "application/json,.json";
  inp.addEventListener("change", async () => {
    const f = inp.files && inp.files[0];
    if (!f) return;
    let data;
    try { data = JSON.parse(await f.text()); } catch (e) { bankStatus("That file isn't valid JSON"); return; }
    const problem = backupProblem(data);
    if (problem) { bankStatus(problem); return; }
    const count = data.banks.length;
    const fw = miniChordController.firmware_version;
    const note = (data.firmware_version != null && fw != null && data.firmware_version !== fw)
      ? "\n\nThe backup was made on firmware " + data.firmware_version +
        " and this minichord has firmware " + fw + ". Settings may have moved between versions."
      : "";
    if (!confirm("Replace " + (count === BANK_COUNT ? "all twelve banks" : count + (count === 1 ? " bank" : " banks")) +
      " on the minichord with this backup?" + note)) return;
    bankState.busy = true;
    try {
      await restoreAllBanks(data, (i, n) => bankStatus("Writing bank " + (i + 1) + " of " + n + "\u2026"));
      bankStatus("Restored " + count + (count === 1 ? " bank" : " banks"));
    } catch (e) {
      bankStatus("Restore failed: " + e.message + ". Some banks may already have been written.");
    }
    bankState.busy = false;
  });
  inp.click();
}

//-->>staging
function moveBankSlot(from, to) {
  if (!bankState.slots || from === to) return;
  const slots = bankState.slots;
  const [moved] = slots.splice(from, 1);
  slots.splice(to, 0, moved);
  recomputeDirty();
  bankNamesSet(slots.map(sl => sl.name || ""));   // the name belongs to the bank, not the slot
}

// Which slots now differ from what that position held when read. Worked out
// afresh rather than flagged, so a withdrawn row or a bank dragged back where
// it was leaves nothing to write.
function recomputeDirty() {
  if (!bankState.slots || !bankState.original) return;
  bankState.slots.forEach((slot, i) => {
    const was = bankState.original[i];
    slot.dirty = !was || slot.values.some((v, a) => v !== was[a]);
  });
}

function bulkSetParameter(addr, value) {
  let changed = 0;
  bankState.slots.forEach(slot => {
    if (slot.values[addr] === value) return;
    slot.values[addr] = value;
    changed++;
  });
  recomputeDirty();
  return changed;
}

// Stage one setting in every bank, replacing a row for the same setting.
function stageSetting(addr, value) {
  const p = bankParams[addr];
  const before = bankState.slots.map(slot => ({ slot, value: slot.values[addr] }));
  const changed = bulkSetParameter(addr, value);
  const existing = bulkStaged.findIndex(e => e.addr === addr);
  const entry = { addr, value, before: existing >= 0 ? bulkStaged[existing].before : before };
  if (existing >= 0) bulkStaged[existing] = entry; else bulkStaged.push(entry);
  return { changed, param: p };
}

function unstageSetting(index) {
  const entry = bulkStaged[index];
  if (!entry) return;
  entry.before.forEach(b => { b.slot.values[entry.addr] = b.value; });
  bulkStaged.splice(index, 1);
  recomputeDirty();
}

//-->>profiles
// A profile is a named, SPARSE set of address/value pairs. It is not a small
// backup: a backup replaces every address in every bank, so it can only bring
// back presets you already had. A profile touches the addresses it names and
// nothing else, which is what lets it repoint every knob in someone else's
// presets without disturbing the sounds they made.
//
// Captured from whatever is staged, and applying one stages its settings
// rather than writing them, so the rows can still be taken back one at a time.
// The file format is the same one Sound Lab uses, so a profile made in either
// opens in the other.
const PROFILES_KEY = "minicontrol_bulk_profiles";

// An imported file is untrusted: the whole list is checked before anything is
// kept, so a malformed file is refused rather than half imported.
function profilesValid(v) {
  return Array.isArray(v) && v.length <= 24 && v.every(pr =>
    pr && typeof pr.name === "string" && pr.name.length > 0 && pr.name.length <= 40
    && Array.isArray(pr.edits) && pr.edits.length > 0 && pr.edits.length <= 64
    && pr.edits.every(e => e && Number.isInteger(e.addr) && e.addr >= 2 && e.addr <= 255
      && Number.isInteger(e.value)));
}
function readProfiles() {
  try {
    const v = JSON.parse(localStorage.getItem(PROFILES_KEY));
    return profilesValid(v) ? v : [];
  } catch (e) { return []; }
}
function writeProfiles(list) {
  if (!profilesValid(list)) return false;
  try { localStorage.setItem(PROFILES_KEY, JSON.stringify(list)); return true; } catch (e) { return false; }
}

function applyProfile(pr) {
  let staged = 0, already = 0, unknown = 0;
  pr.edits.forEach(e => {
    const p = bankParams[e.addr];
    // an address this page has no control for, or a setting this firmware
    // doesn't have yet: skipped rather than written blind
    if (!p || p.group === "hidden" || p.introduction_version > deviceFirmwareVersion()) { unknown++; return; }
    // staged either way: a profile says what it sets, and seeing every row is
    // how you know it arrived whole
    if (stageSetting(e.addr, e.value).changed) staged++; else already++;
  });
  return { staged, already, unknown };
}

function deviceFirmwareVersion() {
  const slot = bankState.slots && bankState.slots[0];
  return slot ? slot.values[miniChordController.firmware_adress] : Infinity;
}

function downloadJson(data, filename) {
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

//-->>the sheet
let bankSheetEl = null, bankSheetPrevFocus = null;

function bankSheetOpen() {
  return !!bankSheetEl && bankSheetEl.classList.contains("open");
}

function bankAnnounce(text) {
  const el = bankSheetEl && bankSheetEl.querySelector(".bank-status");
  if (el) el.textContent = text;
}

function mkBankBtn(label, title, cls) {
  const b = document.createElement("button");
  b.type = "button";
  b.textContent = label;
  b.title = title;
  b.className = "bank-btn" + (cls ? " " + cls : "");
  return b;
}

function buildBankSheet() {
  const overlay = document.createElement("div");
  overlay.className = "bank-sheet";
  const card = document.createElement("div");
  card.className = "bank-sheet-card";
  card.setAttribute("role", "dialog");
  card.setAttribute("aria-modal", "true");
  card.setAttribute("aria-label", "Banks");
  card.tabIndex = -1;
  overlay.appendChild(card);
  overlay.addEventListener("click", e => { if (e.target === overlay) close_bank_sheet(); });
  // on the document, so Escape closes the sheet wherever focus has wandered
  // (a file picker, a prompt) while it is open
  document.addEventListener("keydown", e => {
    if (e.key === "Escape" && bankSheetOpen()) { e.preventDefault(); close_bank_sheet(); }
  });
  document.body.appendChild(overlay);
  return overlay;
}

function renderBankSheet() {
  const card = bankSheetEl.querySelector(".bank-sheet-card");
  const status = card.querySelector(".bank-status");
  const keptStatus = status ? status.textContent : "";
  card.innerHTML = "";

  const head = document.createElement("div");
  head.className = "bank-sheet-head";
  const h = document.createElement("h5");
  h.textContent = "banks";
  const closeBtn = mkBankBtn("×", "Close", "bank-x");
  closeBtn.addEventListener("click", close_bank_sheet);
  head.append(h, closeBtn);
  card.appendChild(head);

  const intro = document.createElement("p");
  intro.className = "bank-sheet-intro";
  card.appendChild(intro);

  const statusEl = document.createElement("p");
  statusEl.className = "bank-status";
  statusEl.setAttribute("aria-live", "polite");
  statusEl.textContent = keptStatus;

  const actions = document.createElement("div");
  actions.className = "bank-sheet-actions";

  if (!bankState.read) {
    intro.textContent = "Reading the banks walks the minichord through all twelve presets, " +
      "so it is done once, on request. Everything after that is staged here until you apply it.";
    const readBtn = mkBankBtn("read banks", "Read all twelve banks off the minichord", "primary");
    readBtn.addEventListener("click", async () => {
      if (!miniChordController.isConnected()) { bankAnnounce("Connect a minichord first"); return; }
      readBtn.disabled = true;
      try {
        await loadBankParams();
        await readAllBanks((i, n) => { intro.textContent = "Reading bank " + (i + 1) + " of " + n + "…"; });
        bankAnnounce("");
      } catch (e) {
        bankAnnounce("Couldn't read the banks: " + e.message +
          ". Loading a bank from here needs firmware with the load bank command.");
      }
      renderBankSheet();
    });
    actions.appendChild(readBtn);
    card.append(actions, statusEl);
    return;
  }

  const dirtyCount = bankState.slots.filter(s => s.dirty).length;

  if (bankState.stale) {
    const warn = document.createElement("p");
    warn.className = "bank-sheet-stale";
    warn.textContent = dirtyCount
      ? "A bank has been written since these were read, so this list is out of date. Reading again will discard the staged changes below. "
      : "A bank has been written since these were read, so this list is out of date. ";
    const reread = mkBankBtn("read again", "Read all twelve banks off the minichord again");
    reread.addEventListener("click", async () => {
      if (dirtyCount && !confirm("Read the banks again? The staged changes will be discarded.")) return;
      reread.disabled = true;
      try {
        await readAllBanks((i, n) => { intro.textContent = "Reading bank " + (i + 1) + " of " + n + "…"; });
      } catch (e) {
        bankAnnounce("Couldn't read the banks: " + e.message);
      }
      renderBankSheet();
    });
    warn.appendChild(reread);
    card.appendChild(warn);
  }

  intro.textContent = dirtyCount
    ? dirtyCount + (dirtyCount === 1 ? " bank has" : " banks have") + " unsaved changes. " +
      "Nothing is written to the minichord until you apply."
    : "Drag a bank to move it. Changes are staged until you apply.";

  // ---- the twelve banks ----
  const grid = document.createElement("div");
  grid.className = "bank-grid";
  card.appendChild(grid);
  bankState.slots.forEach((slot, i) => {
    const cell = document.createElement("div");
    cell.className = "bank-cell" + (slot.dirty ? " dirty" : "");
    cell.draggable = true;
    cell.dataset.index = String(i);

    const swatch = document.createElement("span");
    swatch.className = "bank-swatch";
    const hue = slot.values[miniChordController.color_hue_sysex_adress];
    if (hue != null) swatch.style.background = "hsl(" + hue + ", 100%, 50%)";

    const num = document.createElement("span");
    num.className = "bank-num";
    num.textContent = String(i + 1);
    if (slot.id !== i) num.title = "was bank " + (slot.id + 1);

    const name = document.createElement("input");
    name.className = "bank-name";
    name.type = "text";
    name.maxLength = 24;
    name.value = slot.name || "";
    name.placeholder = slot.id !== i ? "from " + (slot.id + 1) : "—";
    name.setAttribute("aria-label", "Name for bank " + (i + 1));
    // typing a name should not start a drag
    name.addEventListener("mousedown", e => e.stopPropagation());
    name.addEventListener("focus", () => { cell.draggable = false; });
    name.addEventListener("blur", () => {
      cell.draggable = true;
      const v = name.value.trim().slice(0, 24);
      if (v === (slot.name || "")) return;
      slot.name = v;
      // a name is not staged: it stands even if the moves are discarded
      bankState.originalNames[slot.id] = v;
      bankNamesSet(bankState.slots.map(sl => sl.name || ""));
    });
    name.addEventListener("keydown", e => {
      if (e.key === "Enter") { e.preventDefault(); name.blur(); }
      if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); name.value = slot.name || ""; name.blur(); }
    });

    cell.append(swatch, num, name);
    cell.addEventListener("dragstart", e => {
      e.dataTransfer.setData("text/plain", String(i));
      cell.classList.add("dragging");
    });
    cell.addEventListener("dragend", () => cell.classList.remove("dragging"));
    cell.addEventListener("dragover", e => { e.preventDefault(); cell.classList.add("over"); });
    cell.addEventListener("dragleave", () => cell.classList.remove("over"));
    cell.addEventListener("drop", e => {
      e.preventDefault();
      cell.classList.remove("over");
      const from = parseInt(e.dataTransfer.getData("text/plain"), 10);
      const to = parseInt(cell.dataset.index, 10);
      if (!isNaN(from) && !isNaN(to)) { moveBankSlot(from, to); renderBankSheet(); }
    });
    grid.appendChild(cell);
  });

  // ---- bulk edit ----
  const bulk = document.createElement("div");
  bulk.className = "bank-bulk";
  const bulkTitle = document.createElement("h5");
  bulkTitle.textContent = "set across every bank:";
  bulk.appendChild(bulkTitle);
  card.appendChild(bulk);

  const stagedEdits = bulkStaged.map(e => ({ addr: e.addr, value: e.value }));
  bulkStaged.forEach((entry, i) => {
    const p = bankParams[entry.addr];
    const row = document.createElement("div");
    row.className = "bank-row";
    const label = document.createElement("span");
    label.className = "bank-row-name";
    label.textContent = paramLabel(p) || ("address " + entry.addr);
    const val = document.createElement("span");
    val.className = "bank-row-val";
    val.textContent = valueLabel(p, entry.value, stagedEdits);
    const undo = mkBankBtn("×", "Put this setting back to what each bank had", "bank-x");
    undo.addEventListener("click", () => { unstageSetting(i); renderBankSheet(); });
    row.append(label, val, undo);
    bulk.appendChild(row);
  });

  // the picker: a setting, the value it takes, and a button to stage it
  const picker = document.createElement("div");
  picker.className = "bank-picker";
  bulk.appendChild(picker);

  const firmware = deviceFirmwareVersion();
  const paramSel = document.createElement("select");
  paramSel.setAttribute("aria-label", "Setting");
  Object.keys(SECTION_LABEL).forEach(section => {
    const group = document.createElement("optgroup");
    group.label = SECTION_LABEL[section];
    bankParamOrder.filter(p => p.section === section).forEach(p => {
      const a = p.sysex_adress;
      if (p.group === "hidden" || (a >= RHYTHM_FIRST && a <= RHYTHM_LAST)) return;
      if (p.introduction_version > firmware) return;
      const o = document.createElement("option");
      o.value = String(a);
      o.textContent = p.group + " · " + p.name;
      group.appendChild(o);
    });
    if (group.childNodes.length) paramSel.appendChild(group);
  });
  if (bankSheetEl.dataset.picked && paramSel.querySelector('option[value="' + bankSheetEl.dataset.picked + '"]')) {
    paramSel.value = bankSheetEl.dataset.picked;
  }
  picker.appendChild(paramSel);

  const valWrap = document.createElement("span");
  valWrap.className = "bank-value";
  picker.appendChild(valWrap);

  // reads the value field back as the wire value
  let readValue = () => null;
  function renderValueField() {
    valWrap.innerHTML = "";
    const p = bankParams[parseInt(paramSel.value, 10)];
    if (!p) return;
    bankSheetEl.dataset.picked = paramSel.value;
    // a setting whose value is an address gets the named list, not a number box
    if (isTargetSetting(p)) {
      const opts = targetOptions();
      const sel = document.createElement("select");
      sel.setAttribute("aria-label", "Value");
      opts.values.forEach((v, i) => {
        if (v === 0 && p.min_value > 0) return;
        const o = document.createElement("option");
        o.value = String(v); o.textContent = opts.labels[i];
        sel.appendChild(o);
      });
      valWrap.appendChild(sel);
      readValue = () => parseInt(sel.value, 10);
      return;
    }
    const m = valueMeta(p, stagedEdits);
    if (m.data_type === "degrees") {
      const row = document.createElement("span");
      row.className = "degree_row";
      const boxes = DEGREE_LABELS.map((label, bit) => {
        const l = document.createElement("label");
        l.className = "degree";
        const box = document.createElement("input");
        box.type = "checkbox";
        box.className = "degree_box";
        box.checked = !!(m.default_value & (1 << bit));
        const span = document.createElement("span");
        span.textContent = label;
        l.append(box, span);
        row.appendChild(l);
        return box;
      });
      valWrap.appendChild(row);
      readValue = () => boxes.reduce((mask, b, bit) => mask | (b.checked ? 1 << bit : 0), 0);
      return;
    }
    const float = m.data_type === "float";
    const inp = document.createElement("input");
    inp.type = "number";
    inp.setAttribute("aria-label", "Value");
    inp.min = String(m.min_value); inp.max = String(m.max_value);
    inp.step = float ? "0.01" : "1";
    inp.value = String(m.default_value);
    valWrap.appendChild(inp);
    if (m !== p) {
      const hint = document.createElement("span");
      hint.className = "bank-hint";
      hint.textContent = "as " + m.targetName;
      valWrap.appendChild(hint);
    }
    readValue = () => {
      const v = Number(inp.value);
      if (inp.value === "" || isNaN(v)) return null;
      const clamped = Math.min(m.max_value, Math.max(m.min_value, v));
      return Math.round(float ? clamped * miniChordController.float_multiplier : clamped);
    };
  }
  paramSel.addEventListener("change", renderValueField);
  renderValueField();

  const setBtn = mkBankBtn("set in all banks", "Stage this value in every bank, then pick another");
  setBtn.addEventListener("click", () => {
    const addr = parseInt(paramSel.value, 10);
    const v = readValue();
    if (!bankParams[addr] || v == null) { bankAnnounce("Give it a value first"); return; }
    const r = stageSetting(addr, v);
    bankAnnounce(r.changed ? "Staged in " + r.changed + (r.changed === 1 ? " bank" : " banks")
      : "Every bank already has that value");
    renderBankSheet();
  });
  picker.appendChild(setBtn);

  // ---- profiles ----
  const prof = document.createElement("div");
  prof.className = "bank-bulk";
  card.appendChild(prof);
  const profHead = document.createElement("div");
  profHead.className = "bank-row";
  const profTitle = document.createElement("h5");
  profTitle.textContent = "profiles:";
  profTitle.className = "bank-row-name";

  const saveBtn = mkBankBtn("save as profile", "Keep the staged settings as a named set you can apply again");
  saveBtn.disabled = !bulkStaged.length;
  saveBtn.addEventListener("click", () => {
    const name = (prompt("Name this profile", "") || "").trim().slice(0, 40);
    if (!name) return;
    const edits = bulkStaged.map(e => ({ addr: e.addr, value: e.value }));
    const next = readProfiles().filter(pr => pr.name !== name);
    next.push({ name, edits });
    if (!writeProfiles(next)) { bankAnnounce("Couldn't save that profile"); return; }
    bankAnnounce("Saved “" + name + "”");
    renderBankSheet();
  });

  const importBtn = mkBankBtn("import", "Load profiles from a file someone shared");
  importBtn.addEventListener("click", () => {
    const inp = document.createElement("input");
    inp.type = "file"; inp.accept = "application/json,.json";
    inp.addEventListener("change", () => {
      const f = inp.files && inp.files[0];
      if (!f) return;
      const r = new FileReader();
      r.onload = () => {
        let incoming;
        try { incoming = JSON.parse(r.result); } catch (e) { bankAnnounce("That file isn't JSON"); return; }
        const arr = Array.isArray(incoming) ? incoming : incoming && incoming.profiles;
        if (!Array.isArray(arr) || !arr.length) { bankAnnounce("That file didn't look like profiles"); return; }
        const merged = readProfiles();
        arr.forEach(pr => {
          const i = merged.findIndex(x => x.name === (pr && pr.name));
          if (i >= 0) merged[i] = pr; else merged.push(pr);
        });
        if (!writeProfiles(merged)) { bankAnnounce("That file didn't look like profiles"); return; }
        bankAnnounce("Imported " + arr.length + (arr.length === 1 ? " profile" : " profiles"));
        renderBankSheet();
      };
      r.readAsText(f);
    });
    inp.click();
  });
  profHead.append(profTitle, saveBtn, importBtn);
  prof.appendChild(profHead);

  readProfiles().forEach(pr => {
    const row = document.createElement("div");
    row.className = "bank-row";
    const name = document.createElement("span");
    name.className = "bank-row-name";
    name.textContent = pr.name;
    // what it sets, so a profile can be read before it is applied
    name.title = pr.edits.map(e => {
      const p = bankParams[e.addr];
      return (paramLabel(p) || "address " + e.addr) + ": " + valueLabel(p, e.value, pr.edits);
    }).join("\n");
    const count = document.createElement("span");
    count.className = "bank-row-val";
    count.textContent = pr.edits.length + (pr.edits.length === 1 ? " setting" : " settings");

    const use = mkBankBtn("apply", "Stage this profile's settings across every bank");
    use.addEventListener("click", () => {
      const r = applyProfile(pr);
      const bits = [r.staged + (r.staged === 1 ? " setting" : " settings") + " staged"];
      if (r.already) bits.push(r.already + " already matching");
      if (r.unknown) bits.push(r.unknown + " not on this firmware");
      bankAnnounce(pr.name + ": " + bits.join(", "));
      renderBankSheet();
    });
    const out = mkBankBtn("export", "Save this profile to a file");
    out.addEventListener("click", () => {
      downloadJson({ profiles: [pr] }, (pr.name.replace(/[^\w.-]+/g, "-") || "profile") + ".profile.json");
    });
    const del = mkBankBtn("×", "Forget this profile", "bank-x");
    del.addEventListener("click", () => {
      if (!confirm("Forget the profile “" + pr.name + "”?")) return;
      writeProfiles(readProfiles().filter(x => x.name !== pr.name));
      renderBankSheet();
    });
    row.append(name, count, use, out, del);
    prof.appendChild(row);
  });

  // ---- apply / discard ----
  const applyBtn = mkBankBtn("apply", "Write the staged changes to the minichord", "primary");
  applyBtn.disabled = !dirtyCount || bankState.busy;
  applyBtn.addEventListener("click", async () => {
    if (!miniChordController.isConnected()) { bankAnnounce("Connect a minichord first"); return; }
    if (!confirm("Write " + dirtyCount + (dirtyCount === 1 ? " bank" : " banks") +
      " to the minichord? Export anything you want to keep first.")) return;
    bankState.busy = true;
    applyBtn.disabled = true;
    try {
      const n = await applyBankChanges((i, t) => { intro.textContent = "Writing " + (i + 1) + " of " + t + "…"; });
      bankAnnounce("Wrote " + n + (n === 1 ? " bank" : " banks"));
    } catch (e) {
      bankAnnounce("Couldn't write: " + e.message);
    }
    bankState.busy = false;
    renderBankSheet();
  });

  const discardBtn = mkBankBtn("discard", "Throw away the staged changes");
  discardBtn.disabled = !dirtyCount && !bulkStaged.length;
  discardBtn.addEventListener("click", () => {
    bankState.slots = bankState.original.map((values, i) =>
      ({ id: i, values: values.slice(), dirty: false, name: bankState.originalNames[i] }));
    bankNamesSet(bankState.originalNames);
    bulkStaged = [];
    bankAnnounce("Discarded the staged changes");
    renderBankSheet();
  });

  actions.append(applyBtn, discardBtn);
  card.append(actions, statusEl);
}

function open_bank_sheet() {
  if (!miniChordController.isConnected()) {
    document.getElementById("information_zone").focus();
    return;
  }
  bankSheetPrevFocus = document.activeElement;
  if (!bankSheetEl) bankSheetEl = buildBankSheet();
  loadBankParams().catch(() => { }).then(() => {
    renderBankSheet();
    bankSheetEl.classList.add("open");
    bankSheetEl.querySelector(".bank-sheet-card").focus();
  });
}

function close_bank_sheet() {
  if (!bankSheetOpen()) return;
  bankSheetEl.classList.remove("open");
  if (bankSheetPrevFocus && bankSheetPrevFocus.focus) bankSheetPrevFocus.focus();
  bankSheetPrevFocus = null;
}
