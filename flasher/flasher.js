/* The flasher page: jRadio onto an ESP32-S3 from the browser, with two
   buttons for the two things that get written - the firmware with the
   wiring, and the data partition - because an update wants the first and
   never the second, which wipes the networks and the playlist.

   tasmota-webserial-esptool, not esptool-js: on Linux with the board's
   CH343 bridge, esptool-js lost the port the moment the chip was reset -
   "The device has been lost" after the first bytes, every time, the chip
   left in its loader - while this loader, the one ESPConnect runs, reads
   and writes the same board. Not ESP Web Tools either: that one probes for
   Improv first, which loses a CP2102's port on Linux (the jradio-bt
   flasher's note). Everything the page decides is in flasher_core.js,
   which the tests run under Node. */
import {ESPLoader} from 'https://cdn.jsdelivr.net/npm/tasmota-webserial-esptool@7.3.10/+esm';

const hw = window.jradioHardware;
const fl = window.jradioFlasher;
const i18n = window.jradioI18n;
const t = (key, values) => i18n.t(key, values);
const $ = (id) => document.getElementById(id);

let manifest = null;
let wiring = null;       // {values, source: 'draft' | 'default'}
let busy = false;

/* The wiring: the editor's draft on this site when there is one - the page
   tells which - and the README board otherwise. */
function readWiring() {
  let draft = null;
  try {
    draft = window.localStorage.getItem(fl.DRAFT_KEY);
  } catch (error) {
    draft = null;
  }
  if (draft) return {values: hw.parseCsv(draft).values, source: 'draft'};
  return {values: hw.defaults(), source: 'default'};
}

// Worded as the editor words it, with its labels for keys and parts.
function problemText(problem) {
  const field = (key) => t(`hw.f.${key}`);
  const params = {
    key: problem.key ? field(problem.key) : '',
    gpio: problem.gpio === undefined ? '' : problem.gpio,
    device: problem.device ? t(`hw.dev.${problem.device}`) : '',
    bus: problem.bus ? problem.bus.toUpperCase() : '',
    pin: problem.pin ? problem.pin.toUpperCase() : '',
    keys: problem.keys ? problem.keys.map(field).join(', ') : '',
    value: problem.value === undefined ? '' : String(problem.value),
  };
  return t(`hw.err.${problem.code}`, params);
}

/* What stands in the way of the firmware button, or an empty list. */
function blockers() {
  const list = [];
  if (manifest === null) return [t('fl.no_manifest')];
  const report = hw.validate(wiring.values);
  for (const problem of report.errors) list.push(problemText(problem));
  if (fl.buildFor(manifest, String(wiring.values.display)) === null) {
    list.push(t('fl.no_build', {display: t(`hw.opt.${wiring.values.display}`)}));
  }
  return list;
}

function render() {
  wiring = readWiring();
  $('fl-version').textContent = manifest && manifest.version ? manifest.version : '';
  $('fl-board-source').textContent =
    wiring.source === 'draft' ? t('fl.source_draft') : t('fl.source_default');

  const summary = $('fl-summary');
  summary.replaceChildren();
  const add = (term, detail) => {
    const dt = document.createElement('dt');
    dt.textContent = term;
    const dd = document.createElement('dd');
    dd.textContent = detail;
    summary.append(dt, dd);
  };
  const name = String(wiring.values.board_name || '').trim();
  if (name) add(t('hw.f.board_name'), name);
  add(t('hw.dev.tft'), t(`hw.opt.${wiring.values.display}`));
  const parts = ['ir', 'amp', 'power', 'usb', 'sd', 'bluetooth']
    .filter((device) => hw.deviceEnabled(wiring.values, device))
    .map((device) => t(`hw.dev.${device}`));
  add(t('fl.parts'), parts.length ? parts.join(', ') : '—');

  const problems = $('fl-board-problems');
  problems.replaceChildren();
  const blocking = blockers();
  for (const text of blocking) {
    const item = document.createElement('li');
    item.className = 'hw-problem is-error';
    item.textContent = text;
    problems.append(item);
  }
  const serial = 'serial' in navigator;
  $('fl-flash-firmware').disabled = busy || !serial || blocking.length > 0;
  $('fl-flash-littlefs').disabled = busy || !serial || manifest === null || !$('fl-littlefs-agree').checked;
}

const status = (text) => { $('fl-status').textContent = text; };
const line = (text) => { $('fl-log').textContent += `${text}\n`; $('fl-log-box').hidden = false; };
const logger = {
  log: (...parts) => line(parts.join(' ')),
  error: (...parts) => line(parts.join(' ')),
  debug: () => {},
};

async function load(part) {
  if (part.data) return {data: part.data, address: part.address};
  const response = await fetch(part.path, {cache: 'no-store'});
  if (!response.ok) throw new Error(t('fl.missing_file', {file: part.path}));
  /* Bytes, never text: a binary string fed to pako is UTF-8-encoded first,
     and every byte over 0x7F grows into two - see the jradio-bt flasher. */
  return {data: new Uint8Array(await response.arrayBuffer()), address: part.address};
}

async function write(parts, eraseAll, done) {
  busy = true;
  render();
  const progress = $('fl-progress');
  progress.hidden = true;
  $('fl-log').textContent = '';
  let port = null;
  let loader = null;
  let connected = false;
  try {
    status(t('fl.pick_port'));
    port = await navigator.serial.requestPort();
    const info = port.getInfo();
    line(`${t('fl.port')}: USB ${(info.usbVendorId || 0).toString(16)}:${(info.usbProductId || 0).toString(16)}`);
    await port.open({baudRate: 115200});
    loader = new ESPLoader(port, logger);
    status(t('fl.connecting'));
    await loader.initialize();
    connected = true;
    const chip = loader.chipName || '';
    line(`${t('fl.chip')}: ${chip}`);
    if (!/ESP32-S3/i.test(chip)) throw new Error(t('fl.wrong_chip', {chip}));
    const stub = await loader.runStub();
    status(t('fl.downloading'));
    const files = [];
    for (const part of parts) {
      const file = await load(part);
      line(`0x${file.address.toString(16)}: ${part.path || 'board.csv'}, ${file.data.length} B`);
      files.push(file);
    }
    progress.hidden = false;
    progress.value = 0;
    if (eraseAll) {
      status(t('fl.erasing'));
      await stub.eraseFlash();
    }
    status(t('fl.writing'));
    const total = files.reduce((sum, file) => sum + file.data.length, 0);
    let before = 0;
    for (const file of files) {
      // The loader takes an ArrayBuffer of exactly the file, not a view.
      const buffer = file.data.buffer.slice(file.data.byteOffset,
                                            file.data.byteOffset + file.data.length);
      await stub.flashData(buffer, (written) => {
        progress.value = Math.round((before + Math.min(written, file.data.length)) * 100 / total);
      }, file.address, true);
      before += file.data.length;
    }
    progress.value = 100;
    await loader.hardReset(false);
    connected = false;
    status(done);
  } catch (error) {
    const text = String(error && error.message ? error.message : error);
    line(`${t('fl.error')}: ${text}`);
    if (/NotFoundError|No port selected/i.test(text)) status(t('fl.no_port'));
    else if (/sync|Failed to connect|timed out/i.test(text)) status(t('fl.no_answer'));
    else if (/lost|busy|not ready|NetworkError|Failed to open/i.test(text)) status(t('fl.port_busy'));
    else status(t('fl.failed', {error: text}));
  } finally {
    /* A write that failed half way leaves the chip in its ROM loader, and a
       board that sits there looks dead: start it again whatever it now
       holds. The files already written are whole, or the ROM refused them. */
    if (connected && loader !== null) {
      try {
        await loader.hardReset(false);
      } catch (error) {
        line(`${t('fl.error')}: ${error && error.message ? error.message : error}`);
      }
    }
    /* The loader lets go of the streams but leaves the port open, and an
       open port is one nothing else on the machine can use - idf.py
       included - until the tab is closed. */
    try {
      if (loader !== null) await loader.disconnect();
    } catch (error) {
      // The streams are gone already.
    }
    try {
      if (port !== null && port.readable) await port.close();
    } catch (error) {
      // The port is gone already; nothing to close.
    }
    busy = false;
    render();
  }
}

function flashFirmware() {
  if (blockers().length > 0) return;
  const csv = hw.toCsv(wiring.values);
  const parts = fl.firmwareParts(manifest, String(wiring.values.display), fl.boardBlob(csv));
  write(parts, $('fl-erase').checked, t('fl.done_firmware', {version: manifest.version || ''}));
}

function flashLittlefs() {
  if (!$('fl-littlefs-agree').checked) return;
  write(fl.littlefsParts(manifest), false, t('fl.done_littlefs'));
}

$('fl-flash-firmware').addEventListener('click', flashFirmware);
$('fl-flash-littlefs').addEventListener('click', flashLittlefs);
$('fl-littlefs-agree').addEventListener('change', render);
$('fl-unsupported').hidden = 'serial' in navigator;
i18n.onChange(render);
// The editor on another tab can change the draft while this one is open.
window.addEventListener('storage', (event) => { if (event.key === fl.DRAFT_KEY) render(); });
window.addEventListener('focus', render);

const languageButton = $('fl-language');
languageButton.addEventListener('click', () => {
  i18n.setLanguage(i18n.language() === 'ru' ? 'en' : 'ru');
});
// First visit: the browser's language, as on the editor.
let chosen = null;
try {
  chosen = window.localStorage.getItem('jradio.language');
} catch (error) {
  chosen = null;
}
const browser = String(navigator.language || '').toLowerCase();
if (chosen === null && browser !== '' && !browser.startsWith('ru')) i18n.setLanguage('en');

render();
fetch('manifest.json', {cache: 'no-store'})
  .then((response) => (response.ok ? response.json() : Promise.reject(new Error(String(response.status)))))
  .then((data) => { manifest = data; render(); })
  .catch(() => { manifest = null; render(); });
