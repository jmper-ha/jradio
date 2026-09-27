'use strict';

/* The settings page's FM section (fm.js): the saved presets from
   /api/fm/presets, edited and sent back whole, and a scan polled from
   /api/fm/scan whose stations replace the list after a warning. Run under a fake DOM, the way the
   remote's page is tested. */

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

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
    this.hidden = false;
    this.disabled = false;
    this.value = '';
    this.checked = false;
  }
  get id() { return this.attributes.id || ''; }
  set id(value) { this.attributes.id = value; }
  get className() { return this.attributes.class || ''; }
  set className(value) { this.attributes.class = value; }
  setAttribute(name, value) { this.attributes[name] = String(value); }
  getAttribute(name) { return Object.hasOwn(this.attributes, name) ? this.attributes[name] : null; }
  append(...items) { for (const item of items) { item.parent = this; this.children.push(item); } }
  appendChild(item) { this.append(item); return item; }
  replaceChildren(...items) { this.children = []; this.append(...items); }
  addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
  emit(type, event = {}) { for (const callback of this.listeners[type] || []) callback(event); }
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


let presetsText = 'Первая\t88300\n\t101200\n';
let scanReply = {running: false, khz: 87000, bars: 5, found: []};
const posts = [];
let scanStarts = 0;
const confirms = [];
let confirmAnswer = true;
const timers = new Map();
let timerId = 0;

function fakeFetch(url, options) {
  const method = options && options.method ? options.method : 'GET';
  if (url === '/api/fm/presets' && method === 'POST') {
    posts.push(options.body);
    presetsText = options.body;
    const count = options.body.split('\n').filter(Boolean).length;
    return Promise.resolve({ok: true, json: () => Promise.resolve({count, skipped: 0})});
  }
  if (url === '/api/fm/presets') {
    return Promise.resolve({ok: true, text: () => Promise.resolve(presetsText)});
  }
  if (url === '/api/fm/scan' && method === 'POST') {
    scanStarts += 1;
    return Promise.resolve({ok: true, status: 202, json: () => Promise.resolve({started: true})});
  }
  assert.strictEqual(url, '/api/fm/scan');
  return Promise.resolve({ok: true, json: () => Promise.resolve(scanReply)});
}

function loadPage() {
  const body = new Element('body');
  const card = new Element('section');
  card.id = 'fm-card';
  body.append(card);
  for (const [tag, id] of [['span', 'fm-status'], ['ul', 'fm-presets'], ['p', 'fm-presets-empty'],
                           ['button', 'fm-scan'], ['button', 'fm-save']]) {
    const element = new Element(tag);
    element.id = id;
    card.append(element);
  }
  // As the markup has them.
  body.querySelector('#fm-save').disabled = true;
  const document = {
    body,
    documentElement: new Element('html'),
    readyState: 'complete',
    createElement: (tag) => new Element(tag),
    querySelectorAll: (selector) => body.querySelectorAll(selector),
    querySelector: (selector) => body.querySelector(selector),
    addEventListener() {},
  };
  const window = {
    localStorage: {getItem: () => null, setItem: () => {}},
    document,
    fetch: fakeFetch,
    confirm: (text) => { confirms.push(text); return confirmAnswer; },
    setTimeout: (callback, ms) => { timers.set(++timerId, {callback, ms}); return timerId; },
    clearTimeout: (handle) => { timers.delete(handle); },
  };
  window.window = window;
  const context = vm.createContext({window, document, console, fetch: fakeFetch,
                                    setTimeout: window.setTimeout, clearTimeout: window.clearTimeout});
  for (const file of ['i18n.js', 'fm.js']) {
    vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'data', 'www', file), 'utf8'), context,
                    {filename: file});
  }
  return {document};
}

const settle = async () => { for (let i = 0; i < 4; ++i) await new Promise((resolve) => setImmediate(resolve)); };
function firePending() {
  const pending = [...timers.values()];
  timers.clear();
  for (const timer of pending) timer.callback();
}

async function main() {
  const {document} = loadPage();
  await settle();
  const $ = (id) => document.body.querySelector(`#${id}`);
  const rows = (id) => $(id).querySelectorAll('li');
  const input = (row) => row.querySelector('input');
  const button = (row, label) => row.querySelectorAll('button').find((node) => node.getAttribute('title') === label);

  /* The saved list: in the file's order, the frequency beside a name field,
     a nameless preset left empty for its name. Nothing to save yet. */
  assert.deepStrictEqual(rows('fm-presets').map((row) => row.dataset.khz), ['88300', '101200']);
  assert.strictEqual(rows('fm-presets')[0].querySelector('.fm-frequency').textContent, '88.3');
  assert.strictEqual(input(rows('fm-presets')[0]).value, 'Первая');
  assert.strictEqual(input(rows('fm-presets')[1]).value, '');
  assert.strictEqual($('fm-presets-empty').hidden, true);
  assert.strictEqual($('fm-save').disabled, true);

  /* Order, removal and a name, then saved whole in the file's shape - the
     tab in a name made a space. */
  button(rows('fm-presets')[1], 'Выше').emit('click');
  assert.deepStrictEqual(rows('fm-presets').map((row) => row.dataset.khz), ['101200', '88300']);
  assert.strictEqual(button(rows('fm-presets')[0], 'Выше').disabled, true);
  assert.strictEqual($('fm-save').disabled, false);
  const nameless = input(rows('fm-presets')[0]);
  nameless.value = 'Вторая\tстанция';
  nameless.emit('input');
  $('fm-save').emit('click');
  await settle();
  assert.deepStrictEqual(posts, ['Вторая станция\t101200\nПервая\t88300\n']);
  assert.strictEqual($('fm-save').disabled, true);
  button(rows('fm-presets')[1], 'Удалить').emit('click');
  assert.deepStrictEqual(rows('fm-presets').map((row) => row.dataset.khz), ['101200']);
  $('fm-save').emit('click');
  await settle();
  assert.strictEqual(posts.at(-1), 'Вторая станция\t101200\n');

  /* A scan over a list that has stations asks first; saying no changes
     nothing and starts nothing. */
  confirms.length = 0;
  confirmAnswer = false;
  $('fm-scan').emit('click');
  await settle();
  assert.deepStrictEqual(confirms, ['Все сохранённые станции (1) будут заменены найденными. Продолжить?']);
  assert.strictEqual(scanStarts, 0);

  /* Saying yes: polled while it runs, then what it found is the list and is
     saved at once - a frequency that already had a name keeps it, and a new
     one takes the name the station sends. */
  confirmAnswer = true;
  scanReply = {running: true, khz: 95600, bars: 5, found: [{khz: 94800, signal: 5, stereo: true}]};
  $('fm-scan').emit('click');
  await settle();
  assert.strictEqual(scanStarts, 1);
  assert.strictEqual($('fm-scan').disabled, true);
  assert.match($('fm-status').textContent, /95\.6/);
  scanReply = {running: false, khz: 108000, bars: 5,
               found: [{khz: 94800, signal: 5, stereo: true, name: 'EUROPA'},
                       {khz: 101200, signal: 4, stereo: true, name: 'RDS2'},
                       {khz: 106200, signal: 3, stereo: false, name: ''}]};
  firePending();
  await settle();
  assert.strictEqual($('fm-scan').disabled, false);
  // A name sent over RDS fills a new frequency; the one given here is kept.
  assert.strictEqual(posts.at(-1), 'EUROPA\t94800\nВторая станция\t101200\n\t106200\n');
  assert.deepStrictEqual(rows('fm-presets').map((row) => row.dataset.khz), ['94800', '101200', '106200']);
  assert.strictEqual($('fm-status').textContent, 'Найдено и сохранено станций: 3');

  /* A scan that finds nothing leaves the list alone. */
  const before = posts.length;
  scanReply = {running: false, khz: 108000, bars: 5, found: []};
  $('fm-scan').emit('click');
  await settle();
  assert.strictEqual(posts.length, before);
  assert.strictEqual(rows('fm-presets').length, 3);
  assert.match($('fm-status').textContent, /не изменён/);

  console.log('web fm tests passed');
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
