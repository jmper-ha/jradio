'use strict';

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class ClassList {
  constructor() { this.values = new Set(); }
  add(...names) { names.forEach((name) => this.values.add(name)); }
  remove(...names) { names.forEach((name) => this.values.delete(name)); }
  toggle(name, force) {
    const enabled = force === undefined ? !this.values.has(name) : force;
    if (enabled) this.values.add(name); else this.values.delete(name);
    return enabled;
  }
  contains(name) { return this.values.has(name); }
}

// Real DOM keeps `.className` and `.classList` as two views of the same
// attribute; mirror that here since production code sets either depending
// on whether it needs one class (`className = 'x'`) or several (`classList`).

function elementMatches(el, selector) {
  return typeof selector === 'string' && selector.startsWith('.') &&
    el.classList.contains(selector.slice(1));
}

class Element {
  constructor(tag = 'div') {
    this.tagName = tag;
    this.children = [];
    this.classList = new ClassList();
    this.listeners = {};
    this.textContent = '';
    this.value = '';
    this.checked = false;
    this.hidden = false;
    this.disabled = false;
    this.dataset = {};
    this.attributes = {};
    this.type = '';
    this.maxLength = 0;
  }
  get className() { return Array.from(this.classList.values).join(' '); }
  set className(value) {
    this.classList.values = new Set(String(value).split(' ').filter(Boolean));
  }
  addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
  emit(type, event = {}) {
    event.preventDefault ||= () => {};
    for (const callback of this.listeners[type] || []) callback(event);
  }
  replaceChildren(...items) { this.children = items; }
  append(...items) { this.children.push(...items); }
  appendChild(item) { this.children.push(item); return item; }
  setAttribute(name, value) { this.attributes[name] = value; }
  removeAttribute(name) { delete this.attributes[name]; }
  // The row's picture is an <img>; src is a plain property here, and the code
  // takes it off again with removeAttribute when there is no picture.
  get firstChild() { return this.children[0]; }
  querySelector(selector) {
    for (const child of this.children) {
      if (elementMatches(child, selector)) return child;
      const nested = child.querySelector(selector);
      if (nested) return nested;
    }
    return null;
  }
  querySelectorAll(selector) {
    const found = [];
    for (const child of this.children) {
      if (elementMatches(child, selector)) found.push(child);
      found.push(...child.querySelectorAll(selector));
    }
    return found;
  }
  focus() {}
}

const ids = [
  'playlist-status', 'playlist-rows', 'playlist-empty', 'playlist-add',
  'playlist-export', 'playlist-import-input', 'playlist-save', 'playlist-save-status',
];
const elements = Object.fromEntries(ids.map((id) => [`#${id}`, new Element()]));

const documentRef = {
  querySelector(selector) { return elements[selector]; },
  createElement(tag) { return new Element(tag); },
  createTextNode(text) {
    const node = new Element('#text');
    node.textContent = text;
    return node;
  },
};

const fetchCalls = [];
let nextGetResponse = () => ({ok: true, status: 200, text: async () => ''});
let nextPostResponse = () => ({ok: true, status: 200, json: async () => ({count: 0})});

function fetchMock(url, options) {
  fetchCalls.push({url, options});
  if (options && options.method === 'POST') return Promise.resolve(nextPostResponse());
  return Promise.resolve(nextGetResponse());
}

const context = {
  console,
  document: documentRef,
  fetch: fetchMock,
  TextEncoder,
  TextDecoder,
  // The zip reader inflates whatever another tool compressed; nothing else in
  // the page needs it.
  DecompressionStream,
  window: {confirm: () => true},
  module: {exports: {}},
};

function flush() {
  return new Promise((resolve) => setImmediate(resolve));
}

vm.createContext(context);
vm.runInContext(fs.readFileSync('data/www/playlist.js', 'utf8'), context);
const {parseCatalogText, serializeCatalog, rowError, buildZip, readZip} =
  context.module.exports;

/* A zip with one deflated entry, which is what any other tool writes: the page
   only ever stores, so nothing here would exercise the inflate path. */
function zipWithDeflatedEntry(name, bytes) {
  const zlib = require('zlib');
  const nameBytes = Buffer.from(name, 'utf8');
  const deflated = zlib.deflateRawSync(Buffer.from(bytes));
  const crc = zlib.crc32 !== undefined
    ? zlib.crc32(Buffer.from(bytes))
    : (() => {
      let value = 0xffffffff;
      for (const byte of bytes) {
        value ^= byte;
        for (let bit = 0; bit < 8; bit += 1) {
          value = (value & 1) !== 0 ? (0xedb88320 ^ (value >>> 1)) : (value >>> 1);
        }
      }
      return (value ^ 0xffffffff) >>> 0;
    })();
  const local = Buffer.alloc(30);
  local.writeUInt32LE(0x04034b50, 0);
  local.writeUInt16LE(20, 4);
  local.writeUInt16LE(0, 6);
  local.writeUInt16LE(8, 8);
  local.writeUInt32LE(crc, 14);
  local.writeUInt32LE(deflated.length, 18);
  local.writeUInt32LE(bytes.length, 22);
  local.writeUInt16LE(nameBytes.length, 26);
  const directory = Buffer.alloc(46);
  directory.writeUInt32LE(0x02014b50, 0);
  directory.writeUInt16LE(20, 4);
  directory.writeUInt16LE(20, 6);
  directory.writeUInt16LE(8, 10);
  directory.writeUInt32LE(crc, 16);
  directory.writeUInt32LE(deflated.length, 20);
  directory.writeUInt32LE(bytes.length, 24);
  directory.writeUInt16LE(nameBytes.length, 28);
  directory.writeUInt32LE(0, 42);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(1, 8);
  end.writeUInt16LE(1, 10);
  end.writeUInt32LE(directory.length + nameBytes.length, 12);
  end.writeUInt32LE(local.length + nameBytes.length + deflated.length, 16);
  return new Uint8Array(Buffer.concat(
    [local, nameBytes, deflated, directory, nameBytes, end]));
}

(async () => {
  // parseCatalogText / serializeCatalog mirror station_catalog_parse_line()
  // in components/internet_radio/station_catalog.c: the third column is `S`
  // or `L` in our own lists, and malformed lines are skipped.
  const sample = 'Радио Шоколад\thttp://choco.example/fm\tS\nJazz\thttps://jazz.example\tL\n';
  const parsed = parseCatalogText(sample);
  assert.equal(parsed.rows.length, 2);
  assert.equal(parsed.skipped, 0);
  assert.equal(parsed.rows[0].name, 'Радио Шоколад');
  assert.equal(parsed.rows[1].flag, 1);
  assert.equal(serializeCatalog(parsed.rows), sample);

  const withGarbage = parseCatalogText(
    'ok\thttp://x\tS\nbroken line without tabs\nAlso\thttp://y\tjazz\n');
  assert.equal(withGarbage.rows.length, 1);
  assert.equal(withGarbage.skipped, 2);

  /* A playlist written for another device carries a volume correction where
     ours carries the letter. Only the name and the address are taken from it:
     this device has one volume and no per-station gain, and nothing in that
     dialect can name a picture. */
  const foreign = parseCatalogText(
    'A\thttp://a\t0\nB\thttp://b\t-3\nC\thttp://c\t+2\nD\thttp://d\t\nE\thttp://e\n');
  assert.equal(foreign.rows.length, 5);
  assert.equal(foreign.skipped, 0);
  assert.deepEqual(foreign.rows.map((row) => row.flag), [0, 0, 0, 0, 0]);
  assert.deepEqual(foreign.rows.map((row) => row.icon), ['', '', '', '', '']);
  // Saving one back writes it in ours, correction dropped and letter in place.
  assert.equal(serializeCatalog(foreign.rows.slice(0, 1)), 'A\thttp://a\tS\n');
  // A picture needs the letter: no other dialect has a fourth column.
  assert.equal(parseCatalogText('A\thttp://a\t0\ts1.png\n').rows.length, 0);
  assert.equal(parseCatalogText('A\thttp://a\tS\ts1.png\n').rows[0].icon, 's1.png');
  assert.equal(parseCatalogText('A\thttp://a\tS\ts1.jpg\n').rows[0].icon, 's1.jpg');

  // The archive is written stored and read back whole, names and bytes alike.
  const picture = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 1, 2, 3]);
  const archive = buildZip([
    {name: 'playlist.csv', bytes: new TextEncoder().encode(sample)},
    {name: 'radio_img/s1.png', bytes: picture},
  ]);
  assert.deepEqual(Array.from(archive.slice(0, 4)), [0x50, 0x4b, 0x03, 0x04]);
  const unpacked = await readZip(archive);
  assert.deepEqual(unpacked.map((file) => file.name),
    ['playlist.csv', 'radio_img/s1.png']);
  assert.equal(new TextDecoder().decode(unpacked[0].bytes), sample);
  assert.deepEqual(Array.from(unpacked[1].bytes), Array.from(picture));

  // And an archive from anywhere else, where the entry is deflated.
  const deflated = await readZip(
    zipWithDeflatedEntry('playlist.csv', new TextEncoder().encode(sample)));
  assert.equal(deflated.length, 1);
  assert.equal(new TextDecoder().decode(deflated[0].bytes), sample);

  // The ceiling is STATION_CATALOG_MAX_ENTRIES, which is 99: the editor kept
  // saying 32 long after the device grew, and an import of a longer file lost
  // its tail without a word.
  const many = Array.from({length: 120}, (_, i) => `s${i}\thttp://${i}\t0`).join('\n');
  const overflow = parseCatalogText(many);
  assert.equal(overflow.rows.length, 99);
  assert.equal(overflow.truncated, true);

  // rowError validation, mirrors the on-device length/format limits
  // (STATION_CATALOG_NAME_MAX_LEN=96, STATION_CATALOG_URL_MAX_LEN=256).
  assert.equal(rowError({name: '', url: 'http://x', flag: 0}), 'Укажите название станции');
  assert.equal(rowError({name: 'a\tb', url: 'http://x', flag: 0}),
    'Название не может содержать символ табуляции');
  assert.equal(rowError({name: 'x'.repeat(97), url: 'http://x', flag: 0}), 'Название слишком длинное');
  assert.equal(rowError({name: 'ok', url: '', flag: 0}), 'Укажите адрес потока');
  assert.equal(rowError({name: 'ok', url: 'http://x\ty', flag: 0}),
    'Адрес не может содержать символ табуляции');
  assert.equal(rowError({name: 'ok', url: 'http://x', flag: 0}), '');

  // Length boundary: station_catalog_copy_field() needs room for the NUL, so
  // a field filling the whole buffer is dropped by the device rather than
  // truncated. The editor must refuse it instead of silently losing the row.
  assert.equal(rowError({name: 'x'.repeat(95), url: 'http://x', flag: 0}), '');
  assert.equal(rowError({name: 'x'.repeat(96), url: 'http://x', flag: 0}), 'Название слишком длинное');
  assert.equal(rowError({name: 'ok', url: 'h'.repeat(255), flag: 0}), '');
  assert.equal(rowError({name: 'ok', url: 'h'.repeat(256), flag: 0}), 'Адрес слишком длинный');
  // Byte length, not code-unit length: Cyrillic is two UTF-8 bytes per char.
  assert.equal(rowError({name: 'я'.repeat(47), url: 'http://x', flag: 0}), '');
  assert.equal(rowError({name: 'я'.repeat(48), url: 'http://x', flag: 0}), 'Название слишком длинное');

  // Script loads and immediately fetches the current playlist.
  await flush();
  await flush();
  assert.equal(fetchCalls.length, 1);
  assert.equal(elements['#playlist-status'].textContent, 'Загружено с устройства');
  assert.equal(elements['#playlist-rows'].children.length, 0);
  assert.equal(elements['#playlist-empty'].hidden, false);
  assert.equal(elements['#playlist-save'].disabled, true);

  // Adding a station starts empty/invalid, so Save stays disabled.
  elements['#playlist-add'].emit('click');
  assert.equal(elements['#playlist-rows'].children.length, 1);
  assert.equal(elements['#playlist-empty'].hidden, true);
  assert.equal(elements['#playlist-save'].disabled, true);

  const row = elements['#playlist-rows'].children[0];
  const nameInput = row.querySelector('.playlist-row-name-input');
  const urlInput = row.querySelector('.playlist-row-url-input');
  const rowErrorEl = row.querySelector('.playlist-row-error');
  assert.equal(rowErrorEl.hidden, false);

  nameInput.value = 'Новая станция';
  nameInput.emit('input');
  assert.equal(rowErrorEl.hidden, false);
  assert.equal(elements['#playlist-save'].disabled, true);

  urlInput.value = 'http://example.com/stream';
  urlInput.emit('input');
  assert.equal(rowErrorEl.hidden, true);
  assert.equal(elements['#playlist-save'].disabled, false);

  // Save posts the serialized playlist and reports the returned count.
  nextPostResponse = () => ({ok: true, status: 200, json: async () => ({count: 1})});
  elements['#playlist-save'].emit('click');
  assert.equal(elements['#playlist-save'].disabled, true);
  await flush();
  await flush();
  assert.equal(fetchCalls.length, 2);
  assert.equal(fetchCalls[1].options.method, 'POST');
  assert.equal(fetchCalls[1].options.body, 'Новая станция\thttp://example.com/stream\tS\n');
  assert.equal(elements['#playlist-save-status'].textContent, 'Сохранено: 1 станций');
  assert.equal(elements['#playlist-save'].disabled, false);

  // If the device reports fewer stations than were sent, it silently dropped
  // rows: report that as an error and keep the editor dirty so the mismatch
  // is not hidden until the next page load.
  nextPostResponse = () => ({ok: true, status: 200, json: async () => ({count: 0})});
  elements['#playlist-save'].emit('click');
  await flush();
  await flush();
  assert.match(elements['#playlist-save-status'].textContent, /приняло 0 станций из 1/);
  assert.equal(elements['#playlist-save-status'].classList.values.has('is-error'), true);
  assert.equal(elements['#playlist-save'].disabled, false);
  nextPostResponse = () => ({ok: true, status: 200, json: async () => ({count: 1})});

  // A malformed row (tab inside a field) blocks saving again.
  nameInput.value = 'bad\tname';
  nameInput.emit('input');
  assert.equal(elements['#playlist-save'].disabled, true);
  nameInput.value = 'Новая станция';
  nameInput.emit('input');
  assert.equal(elements['#playlist-save'].disabled, false);

  // Deleting the only row empties the list and disables Save again.
  const deleteButton = row.querySelector('.playlist-row-delete');
  deleteButton.emit('click');
  assert.equal(elements['#playlist-rows'].children.length, 0);
  assert.equal(elements['#playlist-empty'].hidden, false);
  assert.equal(elements['#playlist-save'].disabled, true);

  // A failed save surfaces the error and re-enables the button.
  elements['#playlist-add'].emit('click');
  const secondRow = elements['#playlist-rows'].children[0];
  secondRow.querySelector('.playlist-row-name-input').value = 'X';
  secondRow.querySelector('.playlist-row-name-input').emit('input');
  secondRow.querySelector('.playlist-row-url-input').value = 'http://x';
  secondRow.querySelector('.playlist-row-url-input').emit('input');
  nextPostResponse = () => ({ok: false, status: 500, text: async () => 'boom'});
  elements['#playlist-save'].emit('click');
  await flush();
  await flush();
  assert.match(elements['#playlist-save-status'].textContent, /не удалось сохранить/i);
  assert.equal(elements['#playlist-save'].disabled, false);

  console.log('web playlist tests passed');
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
