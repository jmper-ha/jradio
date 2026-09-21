'use strict';

/* The wiring editor. The rules live in hardware_core.js with no DOM in them,
   so most of this file tests the model straight through require(); the page
   itself runs once under a fake DOM to prove the clicks reach the model and
   the model reaches the screen. */

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

/* The editor lives on the flasher site, not on the device; only the
   dictionary is the device's. */
const FLASHER = path.join(__dirname, '..', 'flasher');
const WWW = path.join(__dirname, '..', 'data', 'www');
const hw = require(path.join(FLASHER, 'hardware_core.js'));

/* ---- the model --------------------------------------------------------- */

function test_the_readme_board_is_clean() {
  const values = hw.defaults();
  const report = hw.validate(values);
  assert.deepStrictEqual(report.errors, []);
  assert.deepStrictEqual(report.warnings, []);
  /* The README's own numbers, spot-checked: the panel on SPI2's IOMUX pins,
     the sleep button on an RTC pin, the card on SPI3. */
  assert.strictEqual(values.tft_cs, 10);
  assert.strictEqual(values.spi2_sclk, 12);
  assert.strictEqual(values.button_sleep, 21);
  assert.strictEqual(values.sd_spi, '3');
  assert.strictEqual(values.bluetooth, hw.NONE);
  /* SPI2 is on the chip's own pins and nowhere else. */
  assert.strictEqual(values.spi2_sclk, 12);
  assert.strictEqual(values.spi2_mosi, 11);
  assert.strictEqual(values.spi2_miso, 13);
}

function test_the_file_round_trips() {
  const values = hw.defaults();
  const text = hw.toCsv(values);
  assert.ok(text.startsWith('# key,value\nboard_format,1\n'));
  const parsed = hw.parseCsv(text);
  assert.deepStrictEqual(parsed.values, values);
  assert.deepStrictEqual(parsed.unknown, []);
  assert.deepStrictEqual(parsed.bad, []);
  /* Every key is written, in the schema's order, once. */
  const keys = text.trim().split('\n').slice(1).map((line) => line.split(',')[0]);
  assert.deepStrictEqual(keys, hw.FIELDS.map((field) => field.key));
}

function test_the_header_round_trips_and_names_every_option() {
  const values = {...hw.defaults(), board_name: 'bench', dlna: 0, tft_reset: 8};
  const header = hw.toHeader(values);
  /* Every #define the repository's own header carries is here too. */
  for (const name of ['DISPLAY', 'DISPLAY_SPI_PERIPHERAL', 'TFT_CS_GPIO', 'TFT_DC_GPIO', 'TFT_MOSI_GPIO',
                      'TFT_SCLK_GPIO', 'TFT_BACKLIGHT_GPIO', 'ENCODER_RIGHT_GPIO', 'ENCODER_LEFT_GPIO',
                      'ENCODER_BUTTON_GPIO', 'BUTTON_SLEEP_GPIO', 'BUTTON_QUICK_MENU_GPIO', 'BUTTON_PREV_GPIO',
                      'BUTTON_NEXT_GPIO', 'ENCODER_USE_INTERNAL_PULLUPS', 'BUTTONS_USE_INTERNAL_PULLUPS',
                      'AUDIO_DAC', 'I2S_DOUT_GPIO', 'I2S_BCLK_GPIO', 'I2S_LRCK_GPIO', 'AUDIO_DAC_HAS_MCLK',
                      'USB_DM_GPIO', 'USB_DP_GPIO', 'USB_VBUS_SWITCHED', 'SDC_SPI_PERIPHERAL', 'SDC_CS_GPIO',
                      'SDC_SCK_GPIO', 'SDC_MISO_GPIO', 'SDC_MOSI_GPIO', 'SDC_HAS_CARD_DETECT', 'YANDEX_MUSIC',
                      'DLNA', 'TFT_RESET_GPIO']) {
    assert.ok(new RegExp(`^#define ${name} `, 'm').test(header), name);
  }
  assert.ok(header.includes('#define DLNA FEATURE_OFF'));
  const back = hw.parseHeader(header);
  assert.deepStrictEqual(back.values, values);
  assert.deepStrictEqual(back.unknown, []);
  /* The module's RST pad is no #define; it reads back as itself. */
  const tied = hw.toHeader(hw.defaults());
  assert.ok(!/^#define TFT_RESET_GPIO/m.test(tied));
  assert.strictEqual(hw.parseHeader(tied).values.tft_reset, hw.RESET);
  /* A part switched on writes its block; off, a comment in its place. */
  const bt = hw.setDeviceEnabled(hw.defaults(), 'bluetooth', true);
  assert.ok(hw.toHeader(bt).includes('#define BT_UART_TX_GPIO 13'));
  assert.strictEqual(hw.parseHeader(hw.toHeader(bt)).values.bluetooth, 'jradio_bt');
}

function test_a_partial_file_means_the_defaults_for_the_rest() {
  const parsed = hw.parseCsv('bluetooth,jradio_bt\nuart1_tx,13\nuart1_rx,14\n');
  assert.strictEqual(parsed.values.bluetooth, 'jradio_bt');
  assert.strictEqual(parsed.values.uart1_tx, 13);
  assert.strictEqual(parsed.values.tft_cs, 10);
  assert.deepStrictEqual(hw.validate(parsed.values).errors, []);
}

function test_unknown_keys_and_bad_lines_are_reported_not_dropped_silently() {
  const parsed = hw.parseCsv('# comment\n\nfuture_key,7\nencoder_left,seven\nno comma here\n');
  assert.deepStrictEqual(parsed.unknown, [{key: 'future_key', value: '7'}]);
  assert.strictEqual(parsed.bad.length, 2);
  assert.strictEqual(parsed.bad[0].line, 4);
  assert.strictEqual(parsed.bad[1].line, 5);
  /* The bad line left the default alone. */
  assert.strictEqual(parsed.values.encoder_left, 7);
  /* The format line is the editor's, never the file's. */
  assert.strictEqual(hw.parseCsv('board_format,9\n').values.board_format, hw.FORMAT);
}

function test_two_signals_on_one_pin_is_a_conflict_but_a_shared_bus_is_not() {
  const values = {...hw.defaults(), tft_dc: 5};
  const report = hw.validate(values);
  assert.deepStrictEqual(report.errors, [{code: 'pin_conflict', gpio: 5, keys: ['tft_dc', 'encoder_right']}]);

  /* The DAC and the Bluetooth module on the same I2S pins: one bus, no
     conflict. */
  const shared = hw.setDeviceEnabled(hw.defaults(), 'bluetooth', true);
  assert.deepStrictEqual(hw.validate(shared).errors, []);
  assert.deepStrictEqual(hw.pinMap(shared)[18], ['i2s0_bclk']);
  /* SPI2's MISO is the bus's only while the card shares the bus: on its
     own SPI3 the card leaves 13 free, on SPI2 it takes it. */
  assert.strictEqual(hw.pinMap(hw.defaults())[13], undefined);
  assert.deepStrictEqual(hw.pinMap({...hw.defaults(), sd_spi: '2'})[13], ['spi2_miso']);
  /* And SPI2's pins are not the file's to move. */
  assert.strictEqual(hw.parseCsv('spi2_sclk,4\n').values.spi2_sclk, 12);
}

function test_the_pins_the_chip_keeps_for_itself() {
  const base = hw.defaults();
  assert.deepStrictEqual(hw.validate({...base, encoder_left: 43}).errors,
                         [{code: 'pin_console', key: 'encoder_left', gpio: 43}]);
  assert.deepStrictEqual(hw.validate({...base, encoder_left: 36}).errors,
                         [{code: 'pin_psram', key: 'encoder_left', gpio: 36}]);
  /* The display's RST may sit on the module's own RST pad: that value is
     the default, round-trips through the file, and no other pin takes it. */
  assert.strictEqual(base.tft_reset, hw.RESET);
  assert.ok(hw.toCsv(base).includes('tft_reset,rst\n'));
  assert.strictEqual(hw.parseCsv('tft_reset,rst\n').values.tft_reset, hw.RESET);
  assert.strictEqual(hw.parseCsv('spi3_miso,rst\n').bad.length, 1);
  assert.ok(hw.validate({...base, tft_dc: 'rst'}).errors.some((problem) => problem.code === 'bad_value'));
  assert.strictEqual(hw.signals(base).find((signal) => signal.key === 'tft_reset').reset, true);
  /* The module is not a choice: a file naming another one is read as ours. */
  assert.strictEqual(hw.parseCsv('module,esp32s3_n8r2\n').values.module, 'esp32s3_n16r8');
  /* 19 and 20 are USB's, and USB's alone - but USB itself is fine there. */
  const usbErrors = hw.validate({...base, tft_dc: 20}).errors.map((problem) => problem.code);
  assert.ok(usbErrors.includes('pin_usb'));
  assert.deepStrictEqual(hw.validate(base).errors, []);
  /* And it cannot be moved: the controller is hard-wired. */
  assert.deepStrictEqual(hw.validate({...base, usb_dp: 4}).errors,
                         [{code: 'usb_pin_fixed', key: 'usb_dp', gpio: 4}]);
  /* A GPIO that is not on the header at all. */
  assert.deepStrictEqual(hw.validate({...base, tft_dc: 30}).errors,
                         [{code: 'pin_not_on_header', key: 'tft_dc', gpio: 30}]);
}

function test_a_device_needs_the_pins_of_its_bus() {
  const base = hw.defaults();
  assert.deepStrictEqual(hw.validate({...base, spi3_miso: hw.NONE}).errors,
                         [{code: 'bus_unwired', device: 'sd', bus: 'spi3', pin: 'miso'}]);
  /* Off the board, the card no longer cares what SPI3 has. */
  assert.deepStrictEqual(hw.validate({...base, spi3_miso: hw.NONE, sd_cs: hw.NONE}).errors, []);
  /* The module switched on by type alone needs its UART; the enable helper
     wires it. Its sound rides the I2S bus the DAC already has. */
  const codes = hw.validate({...base, bluetooth: 'jradio_bt'}).errors.map((problem) => `${problem.bus}.${problem.pin}`);
  assert.deepStrictEqual(codes, ['uart1.tx', 'uart1.rx']);
  assert.deepStrictEqual(hw.validate(hw.setDeviceEnabled(base, 'bluetooth', true)).errors, []);
  const noClock = {...hw.setDeviceEnabled(base, 'bluetooth', true), i2s0_bclk: hw.NONE};
  assert.ok(hw.validate(noClock).errors.some((problem) => problem.device === 'bluetooth' && problem.pin === 'bclk'));
}

function test_switching_a_device_off_and_on() {
  let values = hw.defaults();
  assert.ok(hw.deviceEnabled(values, 'sd'));
  values = hw.setDeviceEnabled(values, 'sd', false);
  assert.ok(!hw.deviceEnabled(values, 'sd'));
  assert.strictEqual(values.sd_cs, hw.NONE);
  /* Its pins leave the map, so the picture shows them free - and so do the
     pins of SPI3, which nothing else is on; the numbers stay in the file. */
  assert.strictEqual(hw.pinMap(values)[1], undefined);
  assert.strictEqual(hw.pinMap(values)[41], undefined);
  assert.strictEqual(values.spi3_sclk, 41);
  assert.ok(!hw.busUsed(values, 'spi3'));
  /* The same for the module's UART: off by default, its pins are nobody's. */
  assert.ok(!hw.busUsed(values, 'uart1'));
  assert.deepStrictEqual(hw.validate({...values, tft_dc: 41}).errors, []);
  values = hw.setDeviceEnabled(values, 'sd', true);
  assert.strictEqual(values.sd_cs, 1);
  /* Switching a device on never moves a bus pin that is already wired. */
  values = {...values, uart1_tx: 15, uart1_rx: 8};
  values = hw.setDeviceEnabled(values, 'bluetooth', true);
  assert.strictEqual(values.uart1_tx, 15);
  assert.strictEqual(values.bluetooth, 'jradio_bt');
  /* The parts that are always there have no switch: the display and the
     encoder, which the firmware does not run without, and the DAC. */
  for (const device of ['tft', 'encoder', 'dac']) {
    assert.ok(hw.deviceEnabled(values, device));
    assert.deepStrictEqual(hw.setDeviceEnabled(values, device, false), values);
  }
}

function test_the_warnings_that_do_not_stop_a_file() {
  const base = hw.defaults();
  assert.deepStrictEqual(hw.validate({...base, button_sleep: 38}).warnings,
                         [{code: 'sleep_not_rtc', key: 'button_sleep', gpio: 38}]);
  assert.deepStrictEqual(hw.validate({...base, tft_cs: 4}).warnings,
                         [{code: 'spi_not_iomux', key: 'tft_cs', gpio: 4}]);
  /* The display's bus is SPI2 and not a choice: a file putting it on SPI3
     is read back onto SPI2, and the IOMUX advice stays. */
  assert.strictEqual(hw.parseCsv('tft_spi,3\n').values.tft_spi, '2');
  assert.deepStrictEqual(hw.validate({...base, tft_cs: 4, sd_spi: '2'}).warnings,
                         [{code: 'spi_not_iomux', key: 'tft_cs', gpio: 4}]);
  /* The receiver, switched on, lands on a wake-capable pin; moved off one,
     the page says the remote will not wake the board. */
  const ir = hw.setDeviceEnabled(base, 'ir', true);
  assert.strictEqual(ir.ir_receiver, 4);
  assert.deepStrictEqual(hw.validate(ir).warnings, []);
  assert.deepStrictEqual(hw.validate({...ir, ir_receiver: 38}).warnings,
                         [{code: 'ir_not_rtc', key: 'ir_receiver', gpio: 38}]);
  /* The README's buttons sit on strapping pins and its card on the JTAG
     pins: notes on the picture, never a report. */
  assert.deepStrictEqual(hw.validate(base).warnings, []);
  assert.strictEqual(hw.pinNote(45, 'esp32s3_n16r8').code, 'pin_strapping');
  assert.strictEqual(hw.pinNote(40, 'esp32s3_n16r8').code, 'pin_jtag');
  assert.strictEqual(hw.pinNote(4, 'esp32s3_n16r8'), null);
}

function test_the_header_is_the_devkit() {
  const gpios = hw.headerGpios();
  assert.strictEqual(hw.headerPins().length, 44);
  assert.strictEqual(gpios.length, 36);
  /* Neither the flash's pins nor the PSRAM's 33/34 are on it. */
  for (const gpio of [26, 27, 28, 29, 30, 31, 32, 33, 34]) assert.ok(!gpios.includes(gpio), `GPIO ${gpio}`);
  /* Every default pin of the README board is a header pin. */
  for (const signal of hw.signals(hw.defaults())) {
    if (signal.gpio !== null) assert.ok(gpios.includes(signal.gpio), `${signal.key} on ${signal.gpio}`);
  }
}

/* ---- the page ---------------------------------------------------------- */

class ClassList {
  constructor(owner) { this.owner = owner; }
  get values() { return new Set((this.owner.attributes.class || '').split(/\s+/).filter(Boolean)); }
  set values(set) { this.owner.attributes.class = [...set].join(' '); }
  add(...names) { const set = this.values; names.forEach((name) => set.add(name)); this.values = set; }
  remove(...names) { const set = this.values; names.forEach((name) => set.delete(name)); this.values = set; }
  contains(name) { return this.values.has(name); }
  toggle(name, force) {
    const set = this.values;
    const on = force === undefined ? !set.has(name) : force;
    if (on) set.add(name); else set.delete(name);
    this.values = set;
    return on;
  }
}

class Element {
  constructor(tag) {
    this.tagName = tag.toUpperCase();
    this.children = [];
    this.parent = null;
    this.attributes = {};
    this.dataset = {};
    this.listeners = {};
    this.classList = new ClassList(this);
    this.textContent = '';
    this.value = '';
    this.checked = false;
    this.hidden = false;
    this.disabled = false;
  }
  get id() { return this.attributes.id || ''; }
  set id(value) { this.attributes.id = value; }
  get className() { return this.attributes.class || ''; }
  set className(value) { this.attributes.class = value; }
  setAttribute(name, value) { this.attributes[name] = String(value); }
  getAttribute(name) { return Object.hasOwn(this.attributes, name) ? this.attributes[name] : null; }
  append(...items) { for (const item of items) { item.parent = this; this.children.push(item); } }
  replaceChildren(...items) { this.children = []; this.append(...items); }
  remove() { if (this.parent) this.parent.children = this.parent.children.filter((child) => child !== this); }
  addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
  emit(type, event = {}) { for (const callback of this.listeners[type] || []) callback(event); }
  click() { this.emit('click'); }
  *walk() { for (const child of this.children) { yield child; yield* child.walk(); } }
  matches(selector) {
    if (selector.startsWith('.')) return this.classList.contains(selector.slice(1));
    if (selector.startsWith('#')) return this.id === selector.slice(1);
    if (selector.startsWith('[')) return Object.hasOwn(this.attributes, selector.slice(1, -1));
    return this.tagName === selector.toUpperCase();
  }
  querySelectorAll(selector) { return [...this.walk()].filter((node) => node.matches(selector)); }
  querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
}

function makeDocument() {
  const body = new Element('body');
  body.setAttribute('data-i18n-page', 'title.hardware');
  const ids = ['hw-parts', 'hw-svg', 'hw-hint', 'hw-hint-stack', 'hw-hint-picking', 'hw-legend', 'hw-report', 'hw-csv', 'hw-status',
               'hw-download', 'hw-copy', 'hw-import', 'hw-import-apply', 'hw-reset', 'hw-import-status'];
  for (const id of ids) {
    const element = new Element(id === 'hw-svg' ? 'svg' : 'div');
    element.id = id;
    body.append(element);
  }
  const document = {
    body,
    documentElement: new Element('html'),
    readyState: 'complete',
    title: '',
    createElement: (tag) => new Element(tag),
    createElementNS: (namespace, tag) => new Element(tag),
    getElementById: (id) => body.querySelector(`#${id}`),
    querySelectorAll: (selector) => body.querySelectorAll(selector),
    addEventListener: () => {},
  };
  return document;
}

function loadPage() {
  const document = makeDocument();
  const storage = {};
  const window = {
    localStorage: {
      getItem: (key) => (Object.hasOwn(storage, key) ? storage[key] : null),
      setItem: (key, value) => { storage[key] = String(value); },
    },
    document,
    navigator: {},
    URL: {createObjectURL: () => 'blob:x', revokeObjectURL: () => {}},
    Blob: function Blob() {},
  };
  window.window = window;
  window.self = window;
  const context = vm.createContext({window, document, self: window, navigator: window.navigator,
                                    URL: window.URL, Blob: window.Blob, console});
  for (const [dir, file] of [[WWW, 'i18n.js'], [FLASHER, 'hardware_core.js'], [FLASHER, 'hardware.js']]) {
    vm.runInContext(fs.readFileSync(path.join(dir, file), 'utf8'), context, {filename: file});
  }
  return {document, window, storage};
}

function test_the_page_builds_every_part_and_follows_the_clicks() {
  const {document, storage} = loadPage();
  const parts = document.getElementById('hw-parts');
  const sections = parts.children;
  const devices = sections.map((section) => section.dataset.device);
  /* One group per device, in the model's order - board first, buses last. */
  assert.deepStrictEqual(devices, hw.DEVICES);
  /* The parts that can be absent have a switch; the ones always there do not. */
  const sd = sections.find((section) => section.dataset.device === 'sd');
  const encoder = sections.find((section) => section.dataset.device === 'encoder');
  const tft = sections.find((section) => section.dataset.device === 'tft');
  const dac = sections.find((section) => section.dataset.device === 'dac');
  assert.ok(sd.querySelector('.hw-group-toggle'));
  assert.strictEqual(encoder.querySelector('.hw-group-toggle'), null);
  assert.strictEqual(tft.querySelector('.hw-group-toggle'), null);
  assert.strictEqual(dac.querySelector('.hw-group-toggle'), null);
  /* A device with one possible bus names it and points at the bus's card
     instead of offering a drop-down of one item. */
  const dacBus = parts.querySelectorAll('.hw-row').find((row) => row.dataset.key === 'dac_i2s');
  const busLink = dacBus.querySelector('a');
  assert.strictEqual(busLink.href, '#hw-dev-i2s0');
  assert.ok(sections.some((section) => section.id === 'hw-dev-i2s0'));
  assert.strictEqual(dacBus.querySelector('select'), null);
  /* The module is there from the start, switched off: its type row stays
     live, its bus rows are hidden. */
  const bt = sections.find((section) => section.dataset.device === 'bluetooth');
  assert.ok(bt.classList.contains('is-off'));
  assert.ok(bt.querySelectorAll('.hw-row').some((row) => row.dataset.key === 'bluetooth' && !row.hidden));
  assert.ok(bt.querySelectorAll('.hw-row').some((row) => row.dataset.key === 'bt_i2s' && row.hidden));
  /* A bus nobody is on reads as off: UART1 with the module absent. */
  const uart = sections.find((section) => section.dataset.device === 'uart1');
  assert.ok(uart.classList.contains('is-off'));
  assert.ok(uart.querySelectorAll('.hw-row').every((row) => row.hidden));
  /* SPI2 sits right under the display, shown and not edited; its MISO row
     appears only once the card shares the bus. */
  const spi2 = sections.find((section) => section.dataset.device === 'spi2');
  assert.strictEqual(devices.indexOf('spi2'), devices.indexOf('tft') + 1);
  assert.strictEqual(spi2.querySelector('select'), null);
  const miso = spi2.querySelectorAll('.hw-row').find((row) => row.dataset.key === 'spi2_miso');
  assert.ok(miso.hidden);

  /* The file under the editor is the README board, as the header the
     firmware is built from - the CSV is the draft's, not the reader's. */
  const csv = document.getElementById('hw-csv');
  assert.ok(csv.textContent.includes('\n#define TFT_CS_GPIO 10\n'));
  assert.ok(csv.textContent.includes('No Bluetooth module'));
  assert.ok(!csv.textContent.includes('BLUETOOTH BLUETOOTH_'));
  assert.ok(csv.textContent.includes('#define DLNA FEATURE_ON'));

  /* The list runs by number, and the row's label carries the header's name
     for the same setting. */
  const select = document.getElementById('hw-tft_dc');
  const numbers = select.children.map((option) => Number(option.value)).filter((value) => !Number.isNaN(value));
  assert.deepStrictEqual(numbers, [...numbers].sort((a, b) => a - b));
  const dcRow = parts.querySelectorAll('.hw-row').find((row) => row.dataset.key === 'tft_dc');
  assert.strictEqual(dcRow.querySelector('label').title, 'TFT_DC_GPIO');

  /* A pin picked from the list reaches the file and the picture. */
  select.value = '4';
  select.emit('change');
  assert.ok(csv.textContent.includes('\n#define TFT_DC_GPIO 4\n'));
  const svg = document.getElementById('hw-svg');
  const pin4 = svg.querySelectorAll('.hw-gpio').find((node) => node.dataset.gpio === '4');
  assert.ok(pin4.classList.contains('is-used'));
  assert.ok(pin4.classList.contains('hw-dev-tft'));
  /* And the draft went to storage. */
  assert.ok(storage['jradio.board.csv'].includes('tft_dc,4'));

  /* Arm a signal, click a pin. */
  const arm = parts.querySelectorAll('.hw-arm').find((button) => button.dataset.key === 'encoder_left');
  arm.click();
  assert.ok(document.getElementById('hw-hint').textContent.length > 0);
  const pin8 = svg.querySelectorAll('.hw-gpio').find((node) => node.dataset.gpio === '8');
  assert.ok(pin8.classList.contains('is-target'));
  pin8.click();
  assert.ok(csv.textContent.includes('\n#define ENCODER_LEFT_GPIO 8\n'));
  /* The armed state is spent by the click. */
  assert.ok(!pin8.classList.contains('is-target'));

  /* The display's RST moves between a GPIO and the module's RST pad from
     the list as well as from the picture; the CSV follows. */
  const reset = document.getElementById('hw-tft_reset');
  assert.ok(reset.children.some((option) => option.value === 'rst'));
  reset.value = '8';
  reset.emit('change');
  assert.ok(document.getElementById('hw-csv').textContent.includes('#define TFT_RESET_GPIO 8\n'));
  reset.value = 'rst';
  reset.emit('change');
  assert.ok(document.getElementById('hw-csv').textContent.includes("tied to the module's RST"));

  /* A conflict shows on both pins and in the report, and blocks the download. */
  const cs = document.getElementById('hw-tft_cs');
  cs.value = '8';
  cs.emit('change');
  assert.ok(pin8.classList.contains('is-conflict'));
  const report = document.getElementById('hw-report');
  assert.ok(report.children.some((item) => item.classList.contains('is-error') && item.textContent.includes('GPIO 8')));
  assert.ok(document.getElementById('hw-download').disabled);

  /* A reserved pin refuses an armed signal. */
  arm.click();
  const pin43 = svg.querySelectorAll('.hw-gpio').find((node) => node.dataset.gpio === '43');
  assert.ok(pin43.classList.contains('is-blocked'));
  pin43.click();
  assert.ok(!csv.textContent.includes('ENCODER_LEFT_GPIO 43'));
  assert.ok(document.getElementById('hw-status').classList.contains('is-error'));

  /* Switching the module on wires its UART and colours the pins. */
  const btToggle = sections.find((section) => section.dataset.device === 'bluetooth').querySelector('.hw-group-toggle');
  btToggle.checked = true;
  btToggle.emit('change');
  assert.ok(csv.textContent.includes('\n#define BLUETOOTH BLUETOOTH_JRADIO_BT\n'));
  const pin13 = svg.querySelectorAll('.hw-gpio').find((node) => node.dataset.gpio === '13');
  assert.ok(pin13.classList.contains('hw-dev-uart1'));

  /* Pasting a header replaces the board: a line the editor does not know
     is reported, a commented-out part is off, the software switches follow. */
  document.getElementById('hw-import').value =
    '#define TFT_CS_GPIO 10\n#define TFT_DC_GPIO 47\n#define ENCODER_LEFT_GPIO 7\n' +
    '/* #define SDC_CS_GPIO 1 */\n#define DLNA FEATURE_OFF\n#define FUTURE_GPIO 1\n';
  document.getElementById('hw-import-apply').click();
  assert.ok(csv.textContent.includes('\n#define TFT_DC_GPIO 47\n'));
  assert.ok(csv.textContent.includes('No microSD slot'));
  assert.ok(csv.textContent.includes('#define DLNA FEATURE_OFF'));
  assert.ok(document.getElementById('hw-import-status').textContent.includes('FUTURE_GPIO'));
  /* The CSV still pastes, for the flasher's own files. */
  document.getElementById('hw-import').value = 'tft_dc,4\n';
  document.getElementById('hw-import-apply').click();
  assert.ok(csv.textContent.includes('\n#define TFT_DC_GPIO 4\n'));

  /* And the reset button is the README board again. */
  document.getElementById('hw-reset').click();
  assert.strictEqual(csv.textContent, hw.toHeader(hw.defaults()));
}

function test_every_label_the_page_needs_is_in_the_dictionary() {
  const {window} = loadPage();
  const t = window.jradioI18n.t;
  for (const field of hw.FIELDS) {
    if (field.fixed !== undefined) continue;
    assert.notStrictEqual(t(`hw.f.${field.key}`), `hw.f.${field.key}`, `label for ${field.key}`);
    if (field.kind === 'choice') {
      for (const option of field.options) {
        assert.notStrictEqual(t(`hw.opt.${option}`), `hw.opt.${option}`, `option ${option}`);
      }
    }
  }
  for (const device of hw.DEVICES) {
    assert.notStrictEqual(t(`hw.dev.${device}`), `hw.dev.${device}`, `device ${device}`);
  }
  for (const code of ['pin_conflict', 'pin_missing', 'pin_not_on_header', 'pin_psram', 'pin_console', 'pin_usb',
                      'usb_pin_fixed', 'bus_unwired', 'bad_value', 'sleep_not_rtc', 'ir_not_rtc', 'spi_not_iomux']) {
    assert.notStrictEqual(t(`hw.err.${code}`), `hw.err.${code}`, `report ${code}`);
  }
  for (const code of ['pin_psram', 'pin_console', 'pin_usb', 'pin_strapping', 'pin_jtag']) {
    assert.notStrictEqual(t(`hw.note.${code}`), `hw.note.${code}`, `note ${code}`);
  }
}

test_the_readme_board_is_clean();
test_the_file_round_trips();
test_the_header_round_trips_and_names_every_option();
test_a_partial_file_means_the_defaults_for_the_rest();
test_unknown_keys_and_bad_lines_are_reported_not_dropped_silently();
test_two_signals_on_one_pin_is_a_conflict_but_a_shared_bus_is_not();
test_the_pins_the_chip_keeps_for_itself();
test_a_device_needs_the_pins_of_its_bus();
test_switching_a_device_off_and_on();
test_the_warnings_that_do_not_stop_a_file();
test_the_header_is_the_devkit();
test_the_page_builds_every_part_and_follows_the_clicks();
test_every_label_the_page_needs_is_in_the_dictionary();
console.log('web hardware tests passed');
