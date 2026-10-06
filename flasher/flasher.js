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
const im = window.jradioImprov;
const i18n = window.jradioI18n;
const t = (key, values) => i18n.t(key, values);
const $ = (id) => document.getElementById(id);

let manifest = null;
let wiring = null;       // {values, source: 'draft' | 'default'}
let busy = false;
/* The port a write has just gone through, so the Wi-Fi step straight after
   it does not ask for one again. Used once: any other time the page asks,
   because a port the browser merely remembers may not be the board in front
   of the user - a cable moved to the other socket, a second board. */
let flashedPort = null;
let wifi = null; // the Wi-Fi step's open port: {port, reader, writer, packets}

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
  // Nothing is written while the Wi-Fi step holds the port.
  const portFree = wifi === null;
  $('fl-flash-firmware').disabled = busy || !serial || !portFree || blocking.length > 0;
  $('fl-flash-littlefs').disabled = busy || !serial || !portFree || manifest === null ||
    !$('fl-littlefs-agree').checked;
  // One button both ways: a step left open would keep the port from idf.py.
  const connectKey = portFree ? 'fl.wifi_connect' : 'fl.wifi_disconnect';
  $('fl-wifi-connect').setAttribute('data-i18n', connectKey);
  $('fl-wifi-connect').textContent = t(connectKey);
  $('fl-wifi-connect').disabled = busy || !serial;
  $('fl-wifi-send').disabled = busy || wifi === null;
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
    /* The ROM talks at 115200, which is 11 KB/s - three minutes for the app.
       460800 is what the jradio-bt flasher uses, and within what the
       CP2102, the CH340 and the CH343 all take. */
    await stub.setBaudrate(460800);
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
    for (const piece of files.flatMap((file) => fl.pieces(file))) {
      // The loader takes an ArrayBuffer of exactly the piece, not a view.
      const buffer = piece.data.buffer.slice(piece.data.byteOffset,
                                             piece.data.byteOffset + piece.data.length);
      await stub.flashData(buffer, (written) => {
        progress.value = Math.round((before + Math.min(written, piece.data.length)) * 100 / total);
      }, piece.address, true);
      before += piece.data.length;
    }
    progress.value = 100;
    await loader.hardReset(false);
    connected = false;
    flashedPort = port;
    status(done);
    wifiStatus(t('fl.wifi_after_flash'));
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

/* Wi-Fi over the cable, by Improv: see improv_core.js and components/improv.
   The port is opened at the 115200 the running firmware talks at, read in
   the background into a list of packets, and closed again when the step is
   done or fails - an open port is one nothing else on the machine can use. */

/* A failure is red, the way the settings page marks one; progress is not. */
function wifiStatus(text, failed) {
  $('fl-wifi-status').textContent = text;
  $('fl-wifi-status').classList.toggle('is-error', failed === true);
}

function wifiRevealLabel() {
  const shown = $('fl-wifi-reveal').getAttribute('aria-pressed') === 'true';
  const label = t(shown ? 'common.hide' : 'common.show');
  $('fl-wifi-reveal').setAttribute('aria-label', label);
  $('fl-wifi-reveal').title = label;
}

function wifiReveal(shown) {
  $('fl-wifi-password').type = shown ? 'text' : 'password';
  $('fl-wifi-reveal').setAttribute('aria-pressed', shown ? 'true' : 'false');
  wifiRevealLabel();
}

async function wifiOpen() {
  const port = flashedPort !== null ? flashedPort : await navigator.serial.requestPort();
  flashedPort = null;
  /* The control lines are left as the browser sets them: on the chip's own
     USB a change of DTR and RTS is how the board is told to reset. */
  await port.open({baudRate: 115200});
  const session = {port, reader: port.readable.getReader(), writer: port.writable.getWriter(),
                   packets: [], closed: false};
  const parse = im.createReader();
  (async () => {
    try {
      while (!session.closed) {
        const {value, done} = await session.reader.read();
        if (done) break;
        if (value) session.packets.push(...parse(value));
      }
    } catch (error) {
      // The port went away, or close() cancelled the read.
    }
  })();
  return session;
}

async function wifiClose() {
  if (wifi === null) return;
  const session = wifi;
  wifi = null;
  session.closed = true;
  try { await session.reader.cancel(); } catch (error) { /* already gone */ }
  try { session.reader.releaseLock(); } catch (error) { /* already gone */ }
  try { session.writer.releaseLock(); } catch (error) { /* already gone */ }
  try { await session.port.close(); } catch (error) { /* already gone */ }
}

const sleep = (ms) => new Promise((resolve) => { setTimeout(resolve, ms); });

/* The first packet that matches, waiting up to `ms`; null when none came. */
async function wifiWait(match, ms) {
  const until = Date.now() + ms;
  while (wifi !== null && Date.now() < until) {
    const index = wifi.packets.findIndex(match);
    if (index >= 0) return wifi.packets.splice(index, 1)[0];
    await sleep(50);
  }
  return null;
}

const isResult = (command) => (packet) =>
  packet.type === im.TYPE.result && packet.data[0] === command;

async function wifiAsk(packet, match, ms) {
  await wifi.writer.write(packet);
  return wifiWait(match, ms);
}

function wifiShowLink(url) {
  $('fl-wifi-url').textContent = url;
  $('fl-wifi-url').href = url;
  $('fl-wifi-link').hidden = false;
}

async function wifiStart() {
  if (busy || wifi !== null) return;
  busy = true;
  render();
  $('fl-wifi-form').hidden = true;
  $('fl-wifi-link').hidden = true;
  try {
    wifiStatus(t('fl.pick_port'));
    wifi = await wifiOpen();
    wifiStatus(t('fl.wifi_waiting'));
    // A board just written is still starting: ask once a second for a while.
    let info = null;
    for (let attempt = 0; attempt < 20 && info === null; attempt += 1) {
      info = await wifiAsk(im.rpc(im.COMMAND.info), isResult(im.COMMAND.info), 1000);
    }
    if (info === null) throw new Error(t('fl.wifi_silent'));
    const about = im.resultStrings(info.data).strings;
    $('fl-wifi-device').textContent = t('fl.wifi_device', {name: about[3] || '', version: about[1] || ''});
    wifi.packets.length = 0;
    const state = await wifiAsk(im.rpc(im.COMMAND.state),
                                (packet) => packet.type === im.TYPE.state, 3000);
    if (state !== null && state.data[0] === im.STATE.provisioned) {
      const where = await wifiWait(isResult(im.COMMAND.state), 1000);
      if (where !== null) wifiShowLink(im.resultStrings(where.data).strings[0] || '');
    }
    wifiStatus(t('fl.wifi_scanning'));
    await wifi.writer.write(im.rpc(im.COMMAND.networks));
    const found = [];
    while (true) {
      const packet = await wifiWait(isResult(im.COMMAND.networks), 15000);
      if (packet === null) break;
      const entry = im.network(im.resultStrings(packet.data));
      if (entry === null) break;
      found.push(entry);
    }
    const list = im.networkList(found);
    const select = $('fl-wifi-list');
    select.textContent = '';
    for (const entry of list) {
      const option = document.createElement('option');
      option.value = entry.ssid;
      option.textContent = `${entry.ssid} (${entry.rssi} dBm${entry.secure ? '' : ', ' + t('fl.wifi_open')})`;
      select.append(option);
    }
    const other = document.createElement('option');
    other.value = '';
    other.textContent = t('fl.wifi_other');
    select.append(other);
    $('fl-wifi-list-row').hidden = list.length === 0;
    $('fl-wifi-ssid-row').hidden = list.length > 0;
    $('fl-wifi-form').hidden = false;
    wifiStatus(list.length > 0 ? t('fl.wifi_pick') : t('fl.wifi_type'));
  } catch (error) {
    const text = String(error && error.message ? error.message : error);
    if (/NotFoundError|No port selected/i.test(text)) wifiStatus(t('fl.no_port'), true);
    else if (/lost|busy|not ready|NetworkError|Failed to open|already open/i.test(text)) wifiStatus(t('fl.port_busy'), true);
    else wifiStatus(text, true);
    await wifiClose();
  } finally {
    busy = false;
    render();
  }
}

async function wifiSend() {
  if (wifi === null || busy) return;
  const fromList = !$('fl-wifi-list-row').hidden && $('fl-wifi-list').value !== '';
  const ssid = (fromList ? $('fl-wifi-list').value : $('fl-wifi-ssid').value).trim();
  const passwordField = $('fl-wifi-password');
  let packet;
  try {
    packet = im.wifiRequest(ssid, passwordField.value);
  } catch (error) {
    wifiStatus(t(error.message === 'ssid' ? 'fl.wifi_bad_ssid' : 'fl.wifi_bad_password'), true);
    return;
  }
  // The password has done its job on this page, and goes back to dots.
  passwordField.value = '';
  wifiReveal(false);
  busy = true;
  render();
  try {
    wifi.packets.length = 0;
    wifiStatus(t('fl.wifi_joining', {ssid}));
    await wifi.writer.write(packet);
    packet.fill(0);
    const outcome = await wifiWait((p) => (p.type === im.TYPE.result && p.data[0] === im.COMMAND.wifi) ||
                                          (p.type === im.TYPE.error && p.data[0] !== im.ERROR.none),
                                   60000);
    if (outcome === null) {
      wifiStatus(t('fl.wifi_no_answer'), true);
    } else if (outcome.type === im.TYPE.error) {
      wifiStatus(t(outcome.data[0] === im.ERROR.unableToConnect ? 'fl.wifi_failed' : 'fl.wifi_refused'),
                 true);
    } else {
      const url = im.resultStrings(outcome.data).strings[0] || '';
      wifiShowLink(url);
      $('fl-wifi-form').hidden = true;
      wifiStatus(t('fl.wifi_done', {ssid}));
      await wifiClose();
    }
  } catch (error) {
    wifiStatus(String(error && error.message ? error.message : error), true);
    await wifiClose();
  } finally {
    busy = false;
    render();
  }
}

$('fl-wifi-connect').addEventListener('click', () => {
  if (wifi === null) {
    wifiStart();
    return;
  }
  wifiClose().then(() => {
    $('fl-wifi-form').hidden = true;
    wifiStatus('');
    render();
  });
});
$('fl-wifi-send').addEventListener('click', wifiSend);
$('fl-wifi-reveal').addEventListener('click', () => {
  wifiReveal($('fl-wifi-reveal').getAttribute('aria-pressed') !== 'true');
});
wifiRevealLabel();
i18n.onChange(wifiRevealLabel);
$('fl-wifi-list').addEventListener('change', () => {
  $('fl-wifi-ssid-row').hidden = $('fl-wifi-list').value !== '';
});
window.addEventListener('pagehide', () => { wifiClose(); });
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
