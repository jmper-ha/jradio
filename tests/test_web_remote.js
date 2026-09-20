'use strict';

/* The remote's page: the table comes from /api/remote, a Learn arms a
   function on the device and the page polls until the key arrives. Run under
   a fake DOM, the way the hardware editor's test does it. */

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
  }
  get id() { return this.attributes.id || ''; }
  set id(value) { this.attributes.id = value; }
  get className() { return this.attributes.class || ''; }
  set className(value) { this.attributes.class = value; }
  setAttribute(name, value) { this.attributes[name] = String(value); }
  getAttribute(name) { return Object.hasOwn(this.attributes, name) ? this.attributes[name] : null; }
  append(...items) { for (const item of items) { item.parent = this; this.children.push(item); } }
  appendChild(item) { this.append(item); return item; }
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

/* What the device answers: the kit remote's keys, nothing armed. */
let reply = {
  available: true, learning: null, revision: 0, last: null,
  keys: {power: 'nec:0:45', volume_up: 'nec:0:15', volume_down: null, ok: 'nec:0:09'},
};
const learns = [];
const forgets = [];
let fetches = 0;
let polls = 0;
/* setTimeout is captured, not run: the test decides when a poll fires. */
const timers = new Map();
let timerId = 0;

function fakeFetch(url, options) {
  const body = options && options.body ? JSON.parse(options.body) : null;
  if (url === '/api/remote/learn') {
    learns.push(body.function);
    reply = {...reply, learning: body.function, revision: reply.revision + 1};
    return Promise.resolve({ok: true, json: () => Promise.resolve({ok: true})});
  }
  if (url === '/api/remote/forget') {
    forgets.push(body.function);
    reply = {...reply, keys: {...reply.keys, [body.function]: null}, revision: reply.revision + 1};
    return Promise.resolve({ok: true, json: () => Promise.resolve({ok: true})});
  }
  if (url === '/api/remote/last') {
    polls += 1;
    const {revision, learning, last} = reply;
    return Promise.resolve({ok: true, json: () => Promise.resolve({revision, learning, last})});
  }
  assert.strictEqual(url, '/api/remote');
  fetches += 1;
  return Promise.resolve({ok: true, json: () => Promise.resolve(reply)});
}

function loadPage() {
  const body = new Element('body');
  body.setAttribute('data-i18n-page', 'title.remote');
  for (const id of ['remote-status', 'remote-missing', 'remote-groups']) {
    const element = new Element('div');
    element.id = id;
    body.append(element);
  }
  const document = {
    body,
    documentElement: new Element('html'),
    readyState: 'complete',
    title: '',
    hidden: false,
    createElement: (tag) => new Element(tag),
    querySelectorAll: (selector) => body.querySelectorAll(selector),
    querySelector: (selector) => body.querySelector(selector),
    listeners: {},
    addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); },
    emit(type) { for (const callback of this.listeners[type] || []) callback(); },
  };
  const window = {
    localStorage: {getItem: () => null, setItem: () => {}},
    document,
    fetch: fakeFetch,
    setTimeout: (callback, ms) => { timers.set(++timerId, {callback, ms}); return timerId; },
    clearTimeout: (handle) => { timers.delete(handle); },
  };
  window.window = window;
  const context = vm.createContext({window, document, console, fetch: fakeFetch,
                                    setTimeout: window.setTimeout, clearTimeout: window.clearTimeout});
  for (const file of ['i18n.js', 'remote.js']) {
    vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'data', 'www', file), 'utf8'), context,
                    {filename: file});
  }
  return {document, window, i18n: window.jradioI18n};
}

const settle = () => new Promise((resolve) => setImmediate(resolve));
function firePendingPolls() {
  const pending = [...timers.values()];
  timers.clear();
  for (const timer of pending) timer.callback();
  return pending.length;
}

async function main() {
  const {document, i18n} = loadPage();
  await settle();
  await settle();
  const groups = document.body.querySelector('#remote-groups');
  const status = document.body.querySelector('#remote-status');
  const row = (name) => groups.querySelectorAll('.remote-row').find((node) => node.dataset.function === name);

  /* The table was fetched once and every function has a row: a bound one
     shows its code and a Forget, an unbound one a dash and no Forget. */
  assert.strictEqual(fetches, 1);
  assert.strictEqual(groups.querySelectorAll('.remote-row').length, 33);
  assert.ok(row('power') && row('digit_0') && row('source_dlna'));
  assert.strictEqual(row('volume_up').children[1].textContent, 'nec:0:15');
  assert.strictEqual(row('volume_up').children[3].hidden, false);
  assert.strictEqual(row('volume_down').children[1].textContent, '—');
  assert.strictEqual(row('volume_down').children[3].hidden, true);
  /* The page keeps asking while it is in front of someone - the small
     document, for the last key pressed; the table only when the revision
     moved - and stops in a background tab. */
  assert.strictEqual(firePendingPolls(), 1);
  await settle();
  await settle();
  assert.strictEqual(polls, 1);
  assert.strictEqual(fetches, 1);
  document.hidden = true;
  document.emit('visibilitychange');
  assert.strictEqual(firePendingPolls(), 0);
  document.hidden = false;
  document.emit('visibilitychange');
  await settle();
  await settle();
  assert.strictEqual(polls, 2);
  assert.strictEqual(fetches, 1);

  /* A key pressed on the remote lights its row while the press is fresh; a
     key nobody has learned is named in the status line instead. */
  reply = {...reply, last: {function: 'ok', code: 'nec:0:09', age_ms: 120}};
  firePendingPolls();
  await settle();
  await settle();
  assert.strictEqual(row('ok').classList.contains('is-pressed'), true);
  /* A held key keeps its row lit - every repeat frame makes the press fresh
     again - and it goes out soon after the finger. */
  reply = {...reply, last: {function: 'ok', code: 'nec:0:09', age_ms: 200}};
  firePendingPolls();
  await settle();
  await settle();
  assert.strictEqual(row('ok').classList.contains('is-pressed'), true);
  reply = {...reply, last: {function: 'ok', code: 'nec:0:09', age_ms: 600}};
  firePendingPolls();
  await settle();
  await settle();
  assert.strictEqual(row('ok').classList.contains('is-pressed'), false);
  reply = {...reply, last: {function: null, code: 'nec:4:99', age_ms: 300}};
  firePendingPolls();
  await settle();
  await settle();
  assert.strictEqual(status.textContent, 'Кнопка nec:4:99 не обучена');
  reply = {...reply, last: null};

  /* Learn: the function is posted, the arming moves the device's revision
     so the table is read again, the row says to press a key and every Learn
     is off while one is armed. */
  row('volume_down').children[2].emit('click');
  await settle();
  await settle();
  await settle();
  assert.deepStrictEqual(learns, ['volume_down']);
  assert.strictEqual(row('volume_down').classList.contains('is-learning'), true);
  assert.strictEqual(row('volume_down').children[1].textContent, i18n.t('remote.waiting'));
  assert.strictEqual(row('volume_down').children[2].disabled, true);
  assert.strictEqual(row('ok').children[2].disabled, true);
  assert.ok(status.textContent.includes('Тише'));
  /* Still armed and nothing moved: a poll, no table. */
  const fetchesArmed = fetches;
  assert.strictEqual(firePendingPolls(), 1);
  await settle();
  await settle();
  assert.strictEqual(fetches, fetchesArmed);
  assert.strictEqual(timers.size, 1);

  /* The key arrives: the device's revision moves, the table is read again,
     the row shows the code and the status says so. */
  reply = {...reply, learning: null, revision: reply.revision + 1,
           keys: {...reply.keys, volume_down: 'nec:4:03'}};
  firePendingPolls();
  await settle();
  await settle();
  await settle();
  assert.strictEqual(fetches, fetchesArmed + 1);
  assert.strictEqual(row('volume_down').classList.contains('is-learning'), false);
  assert.strictEqual(row('volume_down').children[1].textContent, 'nec:4:03');
  assert.strictEqual(row('volume_down').children[2].disabled, false);
  assert.strictEqual(status.textContent, 'Записано');

  /* An arming that ran out on the device moved no code, and the page says
     that rather than "learned". */
  row('ok').children[2].emit('click');
  await settle();
  await settle();
  await settle();
  assert.strictEqual(row('ok').classList.contains('is-learning'), true);
  reply = {...reply, learning: null, revision: reply.revision + 1};
  firePendingPolls();
  await settle();
  await settle();
  await settle();
  assert.strictEqual(row('ok').classList.contains('is-learning'), false);
  assert.strictEqual(row('ok').children[1].textContent, 'nec:0:09');
  assert.strictEqual(status.textContent, 'Кнопки не дождались');
  assert.strictEqual(status.classList.contains('is-error'), true);

  /* Forget posts and re-reads. */
  row('volume_down').children[3].emit('click');
  await settle();
  await settle();
  await settle();
  assert.deepStrictEqual(forgets, ['volume_down']);
  assert.strictEqual(row('volume_down').children[1].textContent, '—');
  assert.strictEqual(row('volume_down').children[3].hidden, true);

  /* The language change reaches the rows the page built itself. */
  i18n.setLanguage('en');
  assert.strictEqual(row('volume_down').children[0].textContent, 'Volume down');
  assert.strictEqual(row('volume_down').children[2].textContent, 'Learn');
  i18n.setLanguage('ru');

  /* A build without a receiver says so instead of a table. */
  const missing = document.body.querySelector('#remote-missing');
  assert.strictEqual(missing.hidden, true);
  reply = {available: false, learning: null, revision: 0, keys: {}};
  const bare = loadPage();
  await settle();
  await settle();
  assert.strictEqual(bare.document.body.querySelector('#remote-missing').hidden, false);
  assert.strictEqual(bare.document.body.querySelector('#remote-groups').hidden, true);

  console.log('web remote tests passed');
}

main().catch((error) => { console.error(error); process.exit(1); });
