'use strict';

/* The update card of the settings page: data/www/update.js against a fake
   DOM, a fake XMLHttpRequest for the upload and a fake fetch for the polls. */

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.join(__dirname, '..');

class ClassList {
  constructor() { this.values = new Set(); }
  toggle(name, force) {
    if (force) this.values.add(name); else this.values.delete(name);
  }
  has(name) { return this.values.has(name); }
}

class Element {
  constructor() {
    this.listeners = {};
    this.classList = new ClassList();
    this.textContent = '';
    this.disabled = false;
    this.hidden = false;
    this.value = 0;
    this.files = [];
  }
  addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
  emit(type) { for (const callback of this.listeners[type] || []) callback({}); }
}

function load(otaAnswers) {
  const elements = {};
  for (const id of ['update-status', 'update-current', 'update-file', 'update-send',
                    'update-progress']) {
    elements[`#${id}`] = new Element();
  }
  /* A file input forgets its files when its value is cleared. */
  Object.defineProperty(elements['#update-file'], 'value', {
    set(next) { if (next === '') this.files = []; },
    get() { return ''; },
  });
  const timers = [];
  const requests = [];
  const state = {ota: otaAnswers.slice(), about: [], fetched: []};

  class FakeRequest {
    constructor() {
      this.listeners = {};
      this.upload = {listeners: {}, addEventListener(type, callback) {
        (this.listeners[type] ||= []).push(callback);
      }};
      this.headers = {};
      requests.push(this);
    }
    open(method, url) { this.method = method; this.url = url; }
    setRequestHeader(name, value) { this.headers[name] = value; }
    addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
    send(body) { this.body = body; }
    progress(loaded, total) {
      for (const callback of this.upload.listeners.progress || []) {
        callback({lengthComputable: true, loaded, total});
      }
    }
    answer(status, payload) {
      this.status = status;
      this.responseText = JSON.stringify(payload);
      for (const callback of this.listeners.load || []) callback();
    }
  }

  const answer = (queue) => {
    const next = queue.shift();
    if (next === undefined || next === null) return Promise.reject(new Error('down'));
    return Promise.resolve({ok: true, json: () => Promise.resolve(next)});
  };
  const context = {
    console,
    Date,
    Math,
    JSON,
    document: {querySelector: (selector) => elements[selector]},
    window: {
      setTimeout(callback, delay) { timers.push({callback, delay}); return timers.length; },
      fetch(url) {
        state.fetched.push(url);
        if (url === '/api/ota') return answer(state.ota);
        if (url === '/api/about') return answer(state.about);
        return Promise.reject(new Error('unexpected ' + url));
      },
      XMLHttpRequest: FakeRequest,
    },
  };
  context.window.window = context.window;
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(root, 'data/www/i18n.js'), 'utf8'), context);
  vm.runInContext(fs.readFileSync(path.join(root, 'data/www/update.js'), 'utf8'), context);
  const runTimers = async () => {
    const due = timers.splice(0);
    for (const timer of due) timer.callback();
    await settle();
  };
  return {elements, requests, state, runTimers};
}

const settle = () => new Promise((resolve) => setImmediate(resolve));

const idle = {state: 'idle', done: 0, total: 0, version: '', error: '', running: 'v1.5.5',
              slot: 'factory'};

async function test_a_good_upload_waits_for_the_press_and_sees_the_new_version() {
  const page = load([idle]);
  await settle();
  const {elements, requests, state} = page;
  assert.strictEqual(elements['#update-current'].textContent, 'v1.5.5');
  // Nothing chosen, nothing to send.
  assert.strictEqual(elements['#update-send'].disabled, true);

  const file = {name: 'jradio-v1.6.0-ili9341_320_240.bin'};
  elements['#update-file'].files = [file];
  elements['#update-file'].emit('change');
  assert.strictEqual(elements['#update-send'].disabled, false);
  elements['#update-send'].emit('click');
  assert.strictEqual(requests.length, 1);
  const upload = requests[0];
  assert.strictEqual(upload.method, 'POST');
  assert.strictEqual(upload.url, '/api/ota/app');
  assert.strictEqual(upload.body, file);
  // Sent twice while busy is still one upload.
  elements['#update-send'].emit('click');
  assert.strictEqual(requests.length, 1);
  assert.strictEqual(elements['#update-send'].disabled, true);

  upload.progress(1400000, 2800000);
  assert.strictEqual(elements['#update-progress'].value, 50);
  assert.match(elements['#update-status'].textContent, /50%/);

  upload.answer(200, {version: 'v1.6.0'});
  assert.match(elements['#update-status'].textContent, /энкодер.*v1\.6\.0/);

  // Still waiting, then the press, then the radio gone, then back.
  state.ota.push({...idle, state: 'confirm', version: 'v1.6.0'},
                 {...idle, state: 'restarting', version: 'v1.6.0'});
  await page.runTimers();
  await page.runTimers();
  assert.strictEqual(elements['#update-status'].textContent, 'Перезагрузка…');
  state.about.push(null, {firmware: {version: 'v1.6.0'}});
  await page.runTimers();
  assert.strictEqual(elements['#update-status'].textContent, 'Перезагрузка…');
  await page.runTimers();
  assert.strictEqual(elements['#update-status'].textContent, 'Установлена v1.6.0');
  assert.strictEqual(elements['#update-status'].classList.has('is-success'), true);
  assert.strictEqual(elements['#update-current'].textContent, 'v1.6.0');
  // The file is let go: the same upload twice would be a second question.
  assert.strictEqual(elements['#update-file'].files.length, 0);
  assert.strictEqual(elements['#update-send'].disabled, true);
}

async function test_a_refused_file_is_said_by_its_code() {
  const page = load([idle]);
  await settle();
  const {elements, requests} = page;
  elements['#update-file'].files = [{name: 'jradio-v1.6.0-st7796s_480_320.bin'}];
  elements['#update-file'].emit('change');
  elements['#update-send'].emit('click');
  requests[0].answer(400, {error: 'wrong_display'});
  assert.strictEqual(elements['#update-status'].textContent, 'Прошивка для другого дисплея');
  assert.strictEqual(elements['#update-status'].classList.has('is-error'), true);
  assert.strictEqual(elements['#update-progress'].hidden, true);
}

async function test_a_no_on_the_panel_reaches_the_page() {
  const page = load([idle]);
  await settle();
  const {elements, requests, state} = page;
  elements['#update-file'].files = [{name: 'a.bin'}];
  elements['#update-file'].emit('change');
  elements['#update-send'].emit('click');
  requests[0].answer(200, {version: 'v1.6.0'});
  state.ota.push({...idle, state: 'idle', error: 'declined'});
  await page.runTimers();
  assert.strictEqual(elements['#update-status'].textContent, 'Установка отменена на радио');
}

async function test_a_page_opened_during_the_question_joins_it() {
  const page = load([{...idle, state: 'confirm', version: 'v1.6.0'}]);
  await settle();
  const {elements} = page;
  assert.match(elements['#update-status'].textContent, /энкодер.*v1\.6\.0/);
  assert.strictEqual(elements['#update-send'].disabled, true);
}

(async () => {
  await test_a_good_upload_waits_for_the_press_and_sees_the_new_version();
  await test_a_refused_file_is_said_by_its_code();
  await test_a_no_on_the_panel_reaches_the_page();
  await test_a_page_opened_during_the_question_joins_it();
  console.log('web update tests passed');
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
