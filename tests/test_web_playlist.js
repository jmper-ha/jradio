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
  window: {confirm: () => true},
  module: {exports: {}},
};

function flush() {
  return new Promise((resolve) => setImmediate(resolve));
}

vm.createContext(context);
vm.runInContext(fs.readFileSync('data/www/playlist.js', 'utf8'), context);
const {parseCatalogText, serializeCatalog, rowError} = context.module.exports;

(async () => {
  // parseCatalogText / serializeCatalog mirror station_catalog_parse_line()
  // in components/internet_radio/station_catalog.c: tab-separated
  // name/url/flag, flag strictly "0" or "1", malformed lines are skipped.
  const sample = 'Радио Шоколад\thttp://choco.example/fm\t0\nJazz\thttps://jazz.example\t1\n';
  const parsed = parseCatalogText(sample);
  assert.equal(parsed.rows.length, 2);
  assert.equal(parsed.skipped, 0);
  assert.equal(parsed.rows[0].name, 'Радио Шоколад');
  assert.equal(parsed.rows[1].flag, 1);
  assert.equal(serializeCatalog(parsed.rows), sample);

  const withGarbage = parseCatalogText(
    'ok\thttp://x\t0\nbroken line without tabs\nAlso\thttp://y\t2\n');
  assert.equal(withGarbage.rows.length, 1);
  assert.equal(withGarbage.skipped, 2);

  const many = Array.from({length: 40}, (_, i) => `s${i}\thttp://${i}\t0`).join('\n');
  const overflow = parseCatalogText(many);
  assert.equal(overflow.rows.length, 32);
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
  assert.equal(fetchCalls[1].options.body, 'Новая станция\thttp://example.com/stream\t0\n');
  assert.equal(elements['#playlist-save-status'].textContent, 'Сохранено: 1 станций');
  assert.equal(elements['#playlist-save'].disabled, false);

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
