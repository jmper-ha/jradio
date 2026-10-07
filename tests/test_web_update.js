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
  add(name) { this.values.add(name); }
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
    this.children = [];
    this.checked = false;
  }
  replaceChildren(...items) { this.children = items; }
  addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
  emit(type) { for (const callback of this.listeners[type] || []) callback({}); }
}

const CARD = ['update-status', 'update-current', 'update-latest', 'update-auto', 'update-check',
              'update-file', 'update-send', 'update-progress'];
const BANNER = ['update-banner', 'update-banner-title', 'update-banner-notes',
                'update-banner-status', 'update-banner-install', 'update-banner-skip'];

/* `page` is which of the two pages: the settings page has the card and the
   notice, the player only the notice. */
function load(otaAnswers, page = 'settings') {
  const elements = {};
  for (const id of page === 'settings' ? CARD.concat(BANNER) : BANNER) {
    elements[`#${id}`] = new Element();
  }
  if (!elements['#update-file']) elements['#update-file'] = new Element();
  /* A file input forgets its files when its value is cleared. */
  Object.defineProperty(elements['#update-file'], 'value', {
    set(next) { if (next === '') this.files = []; },
    get() { return ''; },
  });
  const timers = [];
  const requests = [];
  const state = {ota: otaAnswers.slice(), about: [], fetched: [], actions: [],
                 actionReply: {}};

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
    document: {
      querySelector: (selector) => (selector === '#update-file' && page !== 'settings'
        ? null : elements[selector] || null),
      createElement: () => new Element(),
    },
    window: {
      setTimeout(callback, delay) { timers.push({callback, delay}); return timers.length; },
      fetch(url) {
        state.fetched.push(url);
        if (url === '/api/ota') return answer(state.ota);
        if (url === '/api/about') return answer(state.about);
        if (url.startsWith('/api/ota/action?')) {
          const name = url.slice('/api/ota/action?'.length);
          state.actions.push(name);
          const refusal = state.actionReply[name.split('&')[0]];
          if (refusal) {
            return Promise.resolve({ok: false, json: () => Promise.resolve({error: refusal})});
          }
          return Promise.resolve({ok: true, json: () => Promise.resolve({ok: true})});
        }
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

const quiet = {enabled: true, state: 'done', error: '', installing: false, latest: 'v1.5.5',
               available: false, skipped: '', checked_at: 0, notes_ru: '', notes_en: ''};
const idle = {state: 'idle', done: 0, total: 0, version: '', error: '', running: 'v1.5.5',
              slot: 'factory', check: quiet};
const offering = {...quiet, latest: 'v1.6.0', available: true,
                  notes_ru: 'Новое:\n- Обновление по сети.\n- Темы.', notes_en: 'New:\n- Updates.'};

async function test_a_good_upload_waits_for_the_press_and_sees_the_new_version() {
  const page = load([idle]);
  await settle();
  const {elements, requests, state} = page;
  assert.strictEqual(elements['#update-current'].textContent, 'v1.5.5');
  // Nothing chosen, nothing to send.
  assert.strictEqual(elements['#update-send'].disabled, true);

  const file = {name: 'jradio-v1.6.0-ili9341_320_240.bin', size: 2800000};
  elements['#update-file'].files = [file];
  elements['#update-file'].emit('change');
  assert.strictEqual(elements['#update-send'].disabled, false);
  elements['#update-send'].emit('click');
  await settle();
  assert.strictEqual(requests.length, 1);
  const upload = requests[0];
  assert.strictEqual(upload.method, 'POST');
  assert.strictEqual(upload.url, '/api/ota/app');
  assert.strictEqual(upload.body, file);
  // Sent twice while busy is still one upload.
  elements['#update-send'].emit('click');
  await settle();
  assert.strictEqual(requests.length, 1);
  assert.strictEqual(elements['#update-send'].disabled, true);

  upload.progress(1400000, 2800000);
  assert.strictEqual(elements['#update-progress'].value, 50);
  assert.match(elements['#update-status'].textContent, /50%/);

  upload.answer(200, {version: 'v1.6.0'});
  await settle();
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

async function test_the_web_files_go_first_and_say_a_firmware_follows() {
  const page = load([idle]);
  await settle();
  const {elements, requests} = page;
  const app = {name: 'jradio-v1.6.0-ili9341_320_240.bin', size: 3000};
  const web = {name: 'jradio-v1.6.0-www.tar', size: 1000};
  // Picked in either order; the archive still goes first.
  elements['#update-file'].files = [app, web];
  elements['#update-file'].emit('change');
  assert.strictEqual(elements['#update-status'].textContent,
                     'Выбран файл: jradio-v1.6.0-ili9341_320_240.bin, jradio-v1.6.0-www.tar');
  elements['#update-send'].emit('click');
  await settle();
  assert.strictEqual(requests.length, 1);
  assert.strictEqual(requests[0].url, '/api/ota/www?app=1');
  assert.strictEqual(requests[0].body, web);
  // One bar for both: the archive's whole is a quarter of the way.
  requests[0].progress(1000, 1000);
  assert.strictEqual(elements['#update-progress'].value, 25);
  requests[0].answer(200, {version: 'v1.6.0'});
  await settle();
  assert.strictEqual(requests.length, 2);
  assert.strictEqual(requests[1].url, '/api/ota/app');
  assert.strictEqual(requests[1].body, app);
  requests[1].progress(1500, 3000);
  assert.strictEqual(elements['#update-progress'].value, 62);
  requests[1].answer(200, {version: 'v1.6.0'});
  await settle();
  assert.match(elements['#update-status'].textContent, /энкодер.*v1\.6\.0/);
}

async function test_web_files_alone_are_asked_about_alone() {
  const page = load([idle]);
  await settle();
  const {elements, requests} = page;
  elements['#update-file'].files = [{name: 'jradio-v1.6.0-www.tar', size: 600000}];
  elements['#update-file'].emit('change');
  elements['#update-send'].emit('click');
  await settle();
  assert.strictEqual(requests[0].url, '/api/ota/www?app=0');
  requests[0].answer(200, {version: 'v1.6.0'});
  await settle();
  assert.strictEqual(requests.length, 1);
  assert.match(elements['#update-status'].textContent, /энкодер.*v1\.6\.0/);
}

async function test_a_choice_that_is_not_one_firmware_and_one_archive_is_not_sent() {
  const page = load([idle]);
  await settle();
  const {elements} = page;
  for (const files of [[{name: 'a.bin'}, {name: 'b.bin'}], [{name: 'backup.zip'}],
                       [{name: 'a.bin'}, {name: 'b.tar'}, {name: 'c.tar'}]]) {
    elements['#update-file'].files = files;
    elements['#update-file'].emit('change');
    assert.strictEqual(elements['#update-send'].disabled, true);
    assert.strictEqual(elements['#update-status'].classList.has('is-error'), true);
  }
}

async function test_a_refused_file_is_said_by_its_code() {
  const page = load([idle]);
  await settle();
  const {elements, requests} = page;
  elements['#update-file'].files = [{name: 'jradio-v1.6.0-st7796s_480_320.bin'}];
  elements['#update-file'].emit('change');
  elements['#update-send'].emit('click');
  await settle();
  requests[0].answer(400, {error: 'wrong_display'});
  await settle();
  assert.strictEqual(elements['#update-status'].textContent, 'Прошивка для другого дисплея');
  assert.strictEqual(elements['#update-status'].classList.has('is-error'), true);
  assert.strictEqual(elements['#update-progress'].hidden, true);

  /* A refused archive stops there: no firmware is sent without its pages. */
  const second = load([idle]);
  await settle();
  second.elements['#update-file'].files = [{name: 'a.bin'}, {name: 'w.tar'}];
  second.elements['#update-file'].emit('change');
  second.elements['#update-send'].emit('click');
  await settle();
  second.requests[0].answer(400, {error: 'bad_name'});
  await settle();
  assert.strictEqual(second.requests.length, 1);
  assert.strictEqual(second.elements['#update-status'].textContent,
                     'Это не архив веб-интерфейса jradio');
}

async function test_a_no_on_the_panel_reaches_the_page() {
  const page = load([idle]);
  await settle();
  const {elements, requests, state} = page;
  elements['#update-file'].files = [{name: 'a.bin'}];
  elements['#update-file'].emit('change');
  elements['#update-send'].emit('click');
  await settle();
  requests[0].answer(200, {version: 'v1.6.0'});
  await settle();
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

async function test_the_notice_shows_a_new_release_on_the_player_too() {
  const page = load([{...idle, check: offering}], 'player');
  await settle();
  const {elements} = page;
  assert.strictEqual(elements['#update-banner'].hidden, false);
  assert.strictEqual(elements['#update-banner-title'].textContent, 'Вышла новая версия v1.6.0');
  const notes = elements['#update-banner-notes'].children;
  assert.deepStrictEqual(notes.map((item) => item.textContent),
                         ['Новое:', 'Обновление по сети.', 'Темы.']);
  assert.strictEqual(notes[0].classList.has('is-heading'), true);
  assert.strictEqual(notes[1].classList.has('is-heading'), false);

  // Nothing new: no notice.
  const quietPage = load([idle], 'player');
  await settle();
  assert.strictEqual(quietPage.elements['#update-banner'].hidden, true);
}

async function test_update_downloads_installs_and_comes_back_new() {
  const page = load([{...idle, check: offering}], 'player');
  await settle();
  const {elements, state} = page;
  elements['#update-banner-install'].emit('click');
  await settle();
  assert.deepStrictEqual(state.actions, ['do=install']);
  assert.strictEqual(elements['#update-banner-install'].disabled, true);
  state.ota.push({...idle, state: 'receiving', done: 300000, total: 600000,
                  check: {...offering, installing: true}},
                 {...idle, state: 'restarting', check: {...offering, installing: true}});
  await page.runTimers();
  assert.strictEqual(elements['#update-banner-status'].textContent, 'Загрузка с GitHub… 50%');
  await page.runTimers();
  assert.strictEqual(elements['#update-banner-status'].textContent, 'Перезагрузка…');
  state.about.push({firmware: {version: 'v1.6.0'}});
  // What the radio says once it is back: the new version, nothing to offer.
  state.ota.push({...idle, running: 'v1.6.0', slot: 'ota_0', check: {...quiet, latest: ''}});
  await page.runTimers();
  assert.strictEqual(elements['#update-banner-status'].textContent, 'Установлена v1.6.0');
  await settle();
  // The notice goes, its last word stays readable until then.
  assert.strictEqual(elements['#update-banner'].hidden, true);
}

async function test_the_card_follows_an_update_made_from_the_notice() {
  const page = load([{...idle, check: offering}]);
  await settle();
  const {elements, state} = page;
  assert.strictEqual(elements['#update-current'].textContent, 'v1.5.5');
  elements['#update-banner-install'].emit('click');
  await settle();
  state.ota.push({...idle, state: 'restarting', check: {...offering, installing: true}});
  await page.runTimers();
  state.about.push({firmware: {version: 'v1.6.0'}});
  state.ota.push({...idle, running: 'v1.6.0', slot: 'ota_0', check: {...quiet, latest: ''}});
  await page.runTimers();
  await settle();
  assert.strictEqual(elements['#update-current'].textContent, 'v1.6.0');
  assert.strictEqual(elements['#update-latest'].textContent, '—');
}

async function test_a_failed_download_says_why_and_offers_again() {
  const page = load([{...idle, check: offering}], 'player');
  await settle();
  const {elements, state} = page;
  elements['#update-banner-install'].emit('click');
  await settle();
  state.ota.push({...idle, state: 'failed',
                  check: {...offering, state: 'failed', error: 'checksum', installing: false}});
  await page.runTimers();
  assert.strictEqual(elements['#update-banner-status'].textContent,
                     'Файл скачался с ошибкой — попробуйте ещё раз');
  assert.strictEqual(elements['#update-banner-install'].disabled, false);
  assert.strictEqual(elements['#update-banner'].hidden, false);
}

async function test_skip_hides_the_notice() {
  const page = load([{...idle, check: offering}]);
  await settle();
  const {elements, state} = page;
  elements['#update-banner-skip'].emit('click');
  await settle();
  assert.deepStrictEqual(state.actions, ['do=skip']);
  assert.strictEqual(elements['#update-banner'].hidden, true);
}

async function test_check_now_and_the_switch() {
  const page = load([idle]);
  await settle();
  const {elements, state} = page;
  assert.strictEqual(elements['#update-auto'].checked, true);
  assert.strictEqual(elements['#update-latest'].textContent, 'v1.5.5');

  elements['#update-check'].emit('click');
  await settle();
  assert.deepStrictEqual(state.actions, ['do=check']);
  assert.strictEqual(elements['#update-check'].disabled, true);
  state.ota.push({...idle, check: {...quiet, state: 'checking'}},
                 {...idle, check: {...offering, checked_at: 1791300000}});
  await page.runTimers();
  assert.strictEqual(elements['#update-status'].textContent, 'Проверка…');
  await page.runTimers();
  assert.strictEqual(elements['#update-status'].textContent, 'Вышла новая версия v1.6.0');
  assert.strictEqual(elements['#update-banner'].hidden, false);
  assert.match(elements['#update-latest'].textContent, /^v1\.6\.0 · /);
  assert.strictEqual(elements['#update-check'].disabled, false);

  // Up to date is said as such.
  elements['#update-check'].emit('click');
  await settle();
  state.ota.push(idle);
  await page.runTimers();
  assert.strictEqual(elements['#update-status'].textContent, 'Установлена последняя версия');

  // The switch, and a refusal puts it back.
  elements['#update-auto'].checked = false;
  elements['#update-auto'].emit('change');
  await settle();
  assert.strictEqual(state.actions.at(-1), 'do=auto&on=0');
  state.actionReply['do=auto'] = 'write';
  elements['#update-auto'].checked = true;
  elements['#update-auto'].emit('change');
  await settle();
  assert.strictEqual(elements['#update-auto'].checked, false);
}

(async () => {
  await test_a_good_upload_waits_for_the_press_and_sees_the_new_version();
  await test_the_web_files_go_first_and_say_a_firmware_follows();
  await test_web_files_alone_are_asked_about_alone();
  await test_a_choice_that_is_not_one_firmware_and_one_archive_is_not_sent();
  await test_a_refused_file_is_said_by_its_code();
  await test_a_no_on_the_panel_reaches_the_page();
  await test_a_page_opened_during_the_question_joins_it();
  await test_the_notice_shows_a_new_release_on_the_player_too();
  await test_update_downloads_installs_and_comes_back_new();
  await test_the_card_follows_an_update_made_from_the_notice();
  await test_a_failed_download_says_why_and_offers_again();
  await test_skip_hides_the_notice();
  await test_check_now_and_the_switch();
  console.log('web update tests passed');
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
