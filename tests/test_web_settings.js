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
}

class Element {
  constructor() {
    this.children = [];
    this.classList = new ClassList();
    this.listeners = {};
    this.textContent = '';
    this.value = '';
    this.hidden = false;
    this.disabled = false;
    this.dataset = {};
    this.type = '';
    this.attributes = {};
  }
  /* A real file input drops its selection when its value is cleared, and the
     page leans on exactly that to stop the same upload being restored twice. */
  set value(next) {
    this.currentValue = String(next);
    if (this.currentValue === '' && Array.isArray(this.files)) this.files = [];
  }
  get value() { return this.currentValue; }
  setAttribute(name, value) { this.attributes[name] = String(value); }
  getAttribute(name) { return Object.hasOwn(this.attributes, name) ? this.attributes[name] : null; }
  addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
  emit(type, event = {}) {
    event.preventDefault ||= () => {};
    for (const callback of this.listeners[type] || []) callback(event);
  }
  replaceChildren(...items) { this.children = items; }
  append(...items) { this.children.push(...items); }
  /* Enough of a selector engine for the folding sections: a class name, looked
     for among this element's own children. */
  querySelector(selector) {
    const name = selector.replace('.', '');
    return this.children.find((child) => child.classList.values.has(name)) || null;
  }
}

/* The five folding sections of the settings page, as the markup builds them:
   a card holding the heading's button and the body that folds away. */
const SECTION_NAMES = ['device', 'backup', 'wifi', 'yandex', 'about'];
const sectionCards = SECTION_NAMES.map((name) => {
  const card = new Element();
  card.dataset.section = name;
  const toggle = new Element();
  toggle.classList.add('card-toggle');
  const body = new Element();
  body.classList.add('card-body');
  card.append(toggle, body);
  card.toggle = toggle;
  card.body = body;
  return card;
});
const section = Object.fromEntries(sectionCards.map((card) => [card.dataset.section, card]));

const ids = [
  'socket-state', 'wifi-form', 'wifi-ssid', 'wifi-password', 'wifi-password-reveal',
  'wifi-submit',
  'about-firmware', 'about-built', 'about-web', 'about-idf', 'about-notice',
  'about-author',
  'wifi-status', 'wifi-active', 'wifi-ip', 'saved-networks',
  'saved-networks-empty', 'wifi-add', 'wifi-cancel', 'wifi-scan',
  'wifi-scan-block', 'scan-networks', 'scan-empty', 'wifi-chosen',
  'wifi-chosen-name', 'wifi-ssid-row',
  'yandex-status', 'yandex-code-block', 'yandex-url', 'yandex-code',
  'yandex-countdown', 'yandex-link', 'yandex-cancel', 'yandex-forget',
  'yandex-refresh', 'yandex-stations-block', 'yandex-stations',
  'yandex-stations-empty',
  'device-status', 'device-language', 'device-home-screen', 'device-home-screen-row',
  'device-scroll', 'device-buffer-view',
  'device-autoplay', 'device-yandex', 'device-yandex-row',
  'device-dlna', 'device-dlna-row',
  'device-brightness',
  'device-brightness-value', 'device-flip-vertical', 'device-flip-horizontal',
  'device-timezone', 'device-ntp',
  'device-weather', 'device-weather-latitude', 'device-weather-longitude',
  'device-weather-key', 'device-weather-key-row', 'device-weather-now-row',
  'device-weather-now',
  'backup-status', 'backup-file', 'backup-restore',
];
const elements = Object.fromEntries(ids.map((id) => [`#${id}`, new Element()]));
elements['#wifi-form'].elements = {
  ssid: elements['#wifi-ssid'],
  password: elements['#wifi-password'],
};

const documentRef = {
  /* The i18n pass walks the markup for `data-i18n`; these elements are built
     here rather than parsed from settings.html, so it finds none and leaves
     them alone. What the tests below check is the text the page writes. */
  documentElement: {lang: 'ru'},
  querySelectorAll(selector) {
    return selector === '.card[data-section]' ? sectionCards : [];
  },
  querySelector(selector) { return elements[selector]; },
  createElement() { return new Element(); },
};

class FakeWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static instances = [];
  constructor(url) {
    this.url = url;
    this.readyState = FakeWebSocket.CONNECTING;
    this.listeners = {};
    this.sent = [];
    FakeWebSocket.instances.push(this);
  }
  addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
  emit(type, event = {}) {
    for (const callback of this.listeners[type] || []) callback(event);
  }
  send(frame) { this.sent.push(frame); }
  close() { this.readyState = 3; this.emit('close'); }
}

const timers = [];
const fetchCalls = [];
// Answers the Yandex Music polling; each test sets what the device would say.
let yandexReply = {
  state: 'idle', error: 'none', user_code: '', verification_url: '',
  seconds_left: 0,
};
let yandexFetchFails = false;
// What GET /api/settings would answer. Deliberately not the defaults: a page
// that ignored the document entirely would still look right against them.
let settingsReply = {
  language: 'ru', home_screen: 'text', scroll: 'bounce', buffer_view: 'graph',
  autoplay: false,
  yandex_music: true, dlna: false, flip_vertical: false, flip_horizontal: true,
  brightness: 45, volume: 62,
  available: {home_screen: true, yandex_music: false, dlna: true},
  brightness_min: 10, brightness_max: 90,
  timezone: 'asia/yekaterinburg', ntp_server: 'ntp.example.lan',
  weather: 'off', weather_latitude: '55.75', weather_longitude: '37.62',
  openweathermap_key_set: false, weather_state: 'off', weather_http_status: 0,
  weather_report: null,
  timezones: [
    {id: 'europe/moscow', label: 'Москва (UTC+3)'},
    {id: 'asia/yekaterinburg', label: 'Екатеринбург (UTC+5)'},
  ],
};
let settingsPostFails = false;
// GET /api/about. The versions differ on purpose: a page that ignored the
// answer and printed the same string twice would still look right if they
// matched.
let aboutReply = {
  firmware: {version: 'v1.2.0', built: 'Sep  6 2026', present: true},
  web: {version: 'v1.1.0', built: '2026-09-01', present: true},
  idf: 'v5.5.5', matched: false, author: 'someone@example.com',
};
// What window.confirm() answers, and every question it was asked.
let confirmAnswer = true;
const confirmCalls = [];
// The scan the setup page runs: what POST /api/wifi-scan reports and what the
// GET after it answers.
let scanStartOk = true;
let scanReply = {state: 'done', networks: []};
// What POST /api/restore answers, and the status it answers with.
let restoreReply = {restored: ['wifi.json', 'settings.csv'], warnings: [], reboot: true};
let restoreOk = true;
let restoreFetchFails = false;

const timerHandles = new Map();
// What the browser remembers between loads: the language and the open section.
const store = {};
/* How wide the page is being looked at. The sections fold on a phone and stand
   open on a screen with room for the two columns, so both have to be drivable.
   Everything below is a phone unless a test says otherwise. */
let narrowScreen = true;
const mediaListeners = [];
const resize = (narrow) => {
  narrowScreen = narrow;
  for (const listener of mediaListeners) listener({matches: narrow});
};
const context = {
  console,
  document: documentRef,
  WebSocket: FakeWebSocket,
  window: {
    location: {protocol: 'http:', host: 'radio.local'},
    matchMedia: () => ({
      get matches() { return narrowScreen; },
      addEventListener: (type, callback) => { mediaListeners.push(callback); },
    }),
    localStorage: {
      getItem: (key) => (Object.hasOwn(store, key) ? store[key] : null),
      setItem: (key, value) => { store[key] = String(value); },
    },
    setTimeout(callback, delay) {
      timers.push({callback, delay});
      timerHandles.set(timers.length, timers[timers.length - 1]);
      return timers.length;
    },
    clearTimeout(handle) {
      const entry = timerHandles.get(handle);
      if (entry) entry.cleared = true;
    },
    confirm(message) {
      confirmCalls.push(message);
      return confirmAnswer;
    },
    fetch(url, options) {
      fetchCalls.push({url, options});
      if (String(url).startsWith('/api/wifi-scan')) {
        if (options && options.method === 'POST') {
          return Promise.resolve({ok: scanStartOk, json: () => Promise.resolve({})});
        }
        return Promise.resolve({ok: true, json: () => Promise.resolve(scanReply)});
      }
      if (String(url).startsWith('/api/settings')) {
        if (options && options.method === 'POST' && settingsPostFails) {
          return Promise.reject(new Error('refused'));
        }
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve(settingsReply),
        });
      }
      if (String(url).startsWith('/api/restore')) {
        if (restoreFetchFails) return Promise.reject(new Error('offline'));
        return Promise.resolve({ok: restoreOk, json: () => Promise.resolve(restoreReply)});
      }
      if (String(url).startsWith('/api/about')) {
        return Promise.resolve({ok: true, json: () => Promise.resolve(aboutReply)});
      }
      if (yandexFetchFails) return Promise.reject(new Error('offline'));
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve(yandexReply),
      });
    },
  },
};

function sendEvent(socket, payload) {
  socket.emit('message', {data: JSON.stringify(payload)});
}

function snapshot(revision, wifi) {
  return {
    type: 'snapshot', revision, capabilities: [], active_source: 'none',
    player: {}, list: {kind: '', active_index: null, items: []}, wifi,
  };
}

vm.createContext(context);
vm.runInContext(fs.readFileSync('data/www/i18n.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('data/www/settings.js', 'utf8'), context);

const first = FakeWebSocket.instances[0];
assert.equal(first.url, 'ws://radio.local/ws');
first.readyState = FakeWebSocket.OPEN;
first.emit('open');

sendEvent(first, {
  type: 'wifi.update', revision: 5,
  wifi: {mode: 'sta_connected', active_ssid: 'stale', saved: [{ssid: 'stale', blocked: false}]},
});
assert.notEqual(elements['#wifi-active'].textContent, 'stale');

sendEvent(first, snapshot(5, {
  mode: 'sta_connected', active_ssid: 'home', ip: '192.168.1.10',
  save_pending: false, last_error: 0, saved: [{ssid: 'home', blocked: false}],
}));
assert.equal(elements['#wifi-active'].textContent, 'home');
assert.equal(elements['#saved-networks'].children.length, 1);

// The field starts hidden, whatever the markup happened to say.
assert.equal(elements['#wifi-password'].type, 'password');
assert.equal(elements['#wifi-password-reveal'].textContent, 'Показать');
assert.equal(elements['#wifi-password-reveal'].getAttribute('aria-pressed'), 'false');

// And the button shows it, then hides it again.
elements['#wifi-password-reveal'].emit('click');
assert.equal(elements['#wifi-password'].type, 'text');
assert.equal(elements['#wifi-password-reveal'].textContent, 'Скрыть');
assert.equal(elements['#wifi-password-reveal'].getAttribute('aria-pressed'), 'true');
elements['#wifi-password-reveal'].emit('click');
assert.equal(elements['#wifi-password'].type, 'password');
assert.equal(elements['#wifi-password-reveal'].textContent, 'Показать');

elements['#wifi-ssid'].value = 'new-ap';
elements['#wifi-password'].value = 'topsecret42';
// Revealed while it is being typed, which is the whole point of the button.
elements['#wifi-password-reveal'].emit('click');
assert.equal(elements['#wifi-password'].type, 'text');
elements['#wifi-form'].emit('submit');
assert.equal(elements['#wifi-password'].value, '');
/* Sending empties the field, and a field left in "text" would then show the
   next password before anyone asked. */
assert.equal(elements['#wifi-password'].type, 'password');
assert.equal(elements['#wifi-password-reveal'].getAttribute('aria-pressed'), 'false');
assert.equal(elements['#wifi-submit'].disabled, true);
assert.equal(elements['#wifi-status'].textContent, 'Проверка…');
assert.deepEqual(JSON.parse(first.sent.at(-1)), {
  type: 'command', id: 'settings-1', action: 'wifi.save',
  ssid: 'new-ap', password: 'topsecret42',
});

elements['#wifi-password'].value = 'must-not-send';
elements['#wifi-form'].emit('submit');
assert.equal(first.sent.length, 1);
assert.equal(elements['#wifi-password'].value, '');

sendEvent(first, {type: 'command.result', id: 'settings-1', ok: true});
sendEvent(first, {
  type: 'wifi.update', revision: 6,
  wifi: {mode: 'sta_connecting', active_ssid: 'new-ap', ip: '',
    save_pending: true, last_error: 0, saved: [{ssid: 'home', blocked: false}]},
});
assert.equal(elements['#wifi-status'].textContent, 'Подключение…');
assert.equal(elements['#wifi-submit'].disabled, true);

sendEvent(first, {
  type: 'wifi.update', revision: 7,
  wifi: {mode: 'sta_connected', active_ssid: 'new-ap', ip: '192.168.1.20',
    save_pending: false, last_error: 0, saved: [{ssid: 'home', blocked: false}, {ssid: 'new-ap', blocked: false}]},
});
assert.equal(elements['#wifi-status'].textContent, 'Сохранено');
assert.equal(elements['#wifi-submit'].disabled, false);

elements['#wifi-ssid'].value = 'bad-ap';
elements['#wifi-password'].value = 'wrong';
elements['#wifi-form'].emit('submit');
sendEvent(first, {type: 'command.result', id: 'settings-2', ok: true});
sendEvent(first, {
  type: 'wifi.update', revision: 8,
  wifi: {mode: 'sta_connected', active_ssid: 'home', ip: '192.168.1.10',
    save_pending: false, last_error: 202, saved: [{ssid: 'home', blocked: false}, {ssid: 'new-ap', blocked: false}]},
});
assert.match(elements['#wifi-status'].textContent, /парол/i);
assert.equal(elements['#wifi-submit'].disabled, false);

sendEvent(first, {
  type: 'wifi.update', revision: 7,
  wifi: {mode: 'sta_connected', active_ssid: 'stale', ip: '',
    save_pending: false, last_error: 0, saved: []},
});
assert.equal(elements['#wifi-active'].textContent, 'home');

first.emit('close');
assert.equal(timers[0].delay, 500);
timers.shift().callback();
const second = FakeWebSocket.instances[1];
second.readyState = FakeWebSocket.OPEN;
second.emit('open');
sendEvent(second, snapshot(1, {
  mode: 'sta_connected', active_ssid: 'home', ip: '192.168.1.10',
  save_pending: false, last_error: 0, saved: [{ssid: 'home', blocked: false}],
}));
assert.equal(elements['#wifi-active'].textContent, 'home');
sendEvent(second, {
  type: 'wifi.update', revision: 2,
  wifi: {mode: 'sta_connecting', active_ssid: 'other', ip: '',
    save_pending: true, last_error: 0, saved: [{ssid: 'home', blocked: false}]},
});
sendEvent(second, {
  type: 'wifi.update', revision: 3,
  wifi: {mode: 'sta_connected', active_ssid: 'other', ip: '192.168.1.11',
    save_pending: false, last_error: 0, saved: [{ssid: 'home', blocked: false}, {ssid: 'other', blocked: false}]},
});
assert.equal(elements['#wifi-status'].textContent, 'Сохранено');
assert.equal(elements['#wifi-submit'].disabled, false);
sendEvent(first, snapshot(99, {
  mode: 'sta_connected', active_ssid: 'old-socket', ip: '',
  save_pending: false, last_error: 0, saved: []},
));
assert.equal(elements['#wifi-active'].textContent, 'other');

// Yandex Music: REST, so everything below settles on microtasks rather than
// on socket frames. The synchronous assertions above all ran before the very
// first fetch resolved, which is why this part is at the end and async.
/* Asks the page for the device document again the way a change does: a
   coordinate written back unchanged is the cheapest request that answers with
   the whole document. */
async function refreshSettings() {
  elements['#device-weather-latitude'].value = settingsReply.weather_latitude;
  elements['#device-weather-latitude'].emit('change');
  await settle();
}

async function settle() {
  // Deep enough for the longest chain on the page: a refused settings write
  // falls into its catch, re-reads the document and only then reports.
  for (let step = 0; step < 24; step += 1) await Promise.resolve();
}

/* Runs the page again, which is the only way to see what a different answer
   to a load-time request produces: the About card is fetched once and never
   polled, because versions cannot change while the device runs.

   The second run re-queries the same fake elements and adds another set of
   listeners to them, so this is only safe at the very end of the file - which
   is where the About assertions are. */
async function reload() {
  vm.runInContext(fs.readFileSync('data/www/i18n.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('data/www/settings.js', 'utf8'), context);
  await settle();
}

function lastYandexTimer() {
  return timers.filter((entry) => !entry.cleared).at(-1);
}

(async () => {
  await settle();
  assert.ok(fetchCalls.some((call) => call.url === '/api/yandex'));
  assert.equal(elements['#yandex-status'].textContent, 'Аккаунт не привязан');
  assert.equal(elements['#yandex-link'].hidden, false);
  assert.equal(elements['#yandex-cancel'].hidden, true);
  assert.equal(elements['#yandex-forget'].hidden, true);
  assert.equal(elements['#yandex-code-block'].hidden, true);
  // Nothing is happening, so the page must not poll every couple of seconds.
  assert.equal(lastYandexTimer().delay, 15000);

  yandexReply = {
    state: 'waiting', error: 'none', user_code: 'gm2anfv7',
    verification_url: 'https://ya.ru/device', seconds_left: 287,
  };
  elements['#yandex-link'].emit('click');
  await settle();
  const post = fetchCalls.find((call) => call.options && call.options.method === 'POST');
  assert.deepEqual(JSON.parse(post.options.body), {action: 'begin'});
  assert.equal(elements['#yandex-code'].textContent, 'gm2anfv7');
  assert.equal(elements['#yandex-url'].href, 'https://ya.ru/device');
  assert.equal(elements['#yandex-code-block'].hidden, false);
  assert.match(elements['#yandex-countdown'].textContent, /287/);
  assert.equal(elements['#yandex-link'].hidden, true);
  assert.equal(elements['#yandex-cancel'].hidden, false);
  // A live code needs a visible countdown, so the poll tightens up.
  assert.equal(lastYandexTimer().delay, 2000);

  // The address comes off the network and is written into a link the visitor
  // clicks; anything but http(s) must not survive into href.
  yandexReply = {
    state: 'waiting', error: 'none', user_code: 'abcd1234',
    verification_url: 'javascript:alert(1)', seconds_left: 100,
  };
  lastYandexTimer().callback();
  await settle();
  assert.equal(elements['#yandex-url'].href, '#');
  assert.equal(elements['#yandex-url'].textContent, 'javascript:alert(1)');

  yandexReply = {
    state: 'authorized', error: 'none', user_code: '', verification_url: '',
    seconds_left: 0,
  };
  lastYandexTimer().callback();
  await settle();
  assert.equal(elements['#yandex-status'].textContent, 'Аккаунт привязан');
  assert.equal(elements['#yandex-forget'].hidden, false);
  assert.equal(elements['#yandex-link'].hidden, true);
  assert.equal(elements['#yandex-cancel'].hidden, true);
  assert.equal(elements['#yandex-code-block'].hidden, true);

  // Linked, and the dashboard is on its way.
  yandexReply = {
    state: 'authorized', error: 'none', user_code: '', verification_url: '',
    seconds_left: 0, catalog: 'loading', stations: [],
  };
  lastYandexTimer().callback();
  await settle();
  assert.equal(elements['#yandex-status'].textContent, 'Загрузка станций…');
  assert.equal(elements['#yandex-refresh'].disabled, true);
  // Polls quickly, so the list appears without the visitor doing anything.
  assert.equal(lastYandexTimer().delay, 2000);

  yandexReply = {
    state: 'authorized', error: 'none', user_code: '', verification_url: '',
    seconds_left: 0, catalog: 'ready',
    stations: [
      {id: 'user:onyourwave', name: 'Моя волна'},
      {id: 'genre:jazz', name: 'Джаз'},
      {id: 'micro-genre:swing', name: 'Свинг'},
    ],
  };
  lastYandexTimer().callback();
  await settle();
  assert.equal(elements['#yandex-stations-block'].hidden, false);
  assert.equal(elements['#yandex-stations'].children.length, 3);
  assert.equal(elements['#yandex-stations'].children[0].textContent, 'Моя волна');
  assert.equal(elements['#yandex-stations-empty'].hidden, true);
  assert.equal(elements['#yandex-refresh'].hidden, false);
  assert.equal(elements['#yandex-refresh'].disabled, false);
  // Nothing left to wait for, so back to the slow poll.
  assert.equal(lastYandexTimer().delay, 15000);

  elements['#yandex-refresh'].emit('click');
  await settle();
  assert.deepEqual(
    JSON.parse(fetchCalls.filter((call) => call.options && call.options.method === 'POST')
      .at(-1).options.body),
    {action: 'refresh'});

  // Junk in the stations array must not reach the page.
  yandexReply = {
    state: 'authorized', error: 'none', user_code: '', verification_url: '',
    seconds_left: 0, catalog: 'ready',
    stations: [{name: 'Джаз'}, {id: 'x'}, 'not an object', null, {name: 42}],
  };
  lastYandexTimer().callback();
  await settle();
  assert.equal(elements['#yandex-stations'].children.length, 1);
  assert.equal(elements['#yandex-stations'].children[0].textContent, 'Джаз');

  // Unlinked: the stations belonged to that account and go with it.
  yandexReply = {
    state: 'idle', error: 'none', user_code: '', verification_url: '',
    seconds_left: 0, catalog: 'empty', stations: [],
  };
  lastYandexTimer().callback();
  await settle();
  assert.equal(elements['#yandex-stations-block'].hidden, true);
  assert.equal(elements['#yandex-refresh'].hidden, true);

  yandexReply = {
    state: 'failed', error: 'timeout', user_code: '', verification_url: '',
    seconds_left: 0,
  };
  lastYandexTimer().callback();
  await settle();
  assert.match(elements['#yandex-status'].textContent, /истёк/);
  assert.equal(elements['#yandex-status'].classList.values.has('is-error'), true);
  // A failed attempt is offered again rather than leaving a dead end.
  assert.equal(elements['#yandex-link'].hidden, false);

  yandexFetchFails = true;
  lastYandexTimer().callback();
  await settle();
  assert.equal(elements['#yandex-status'].textContent, 'Нет связи с устройством');
  // Still scheduled: the device coming back must not need a page reload.
  assert.equal(lastYandexTimer().delay, 15000);

  /* The device settings, which are the same settings.csv the panel writes.
     The values above are what the device answered on load - a page that
     ignored the document would have to show the HTML defaults instead. */
  assert.equal(elements['#device-status'].textContent, 'Готово');
  assert.equal(elements['#device-language'].value, 'ru');
  assert.equal(elements['#device-scroll'].value, 'bounce');
  assert.equal(elements['#device-buffer-view'].value, 'graph');
  assert.equal(elements['#device-autoplay'].checked, false);
  assert.equal(elements['#device-flip-horizontal'].checked, true);
  assert.equal(elements['#device-brightness'].value, '45');
  assert.equal(elements['#device-brightness-value'].textContent, '45');
  // The slider stops where the encoder does, and the device says where.
  assert.equal(elements['#device-brightness'].min, '10');
  assert.equal(elements['#device-brightness'].max, '90');
  /* A build without Yandex Music has no such row on its own screen either, so
     the switch goes away rather than sitting there changing nothing. The media
     server is built into this fixture, so its row stays and shows the state
     the device reported. */
  assert.equal(elements['#device-yandex-row'].hidden, true);
  assert.equal(elements['#device-dlna-row'].hidden, false);
  assert.equal(elements['#device-dlna'].checked, false);
  assert.equal(elements['#device-home-screen-row'].hidden, false);

  settingsReply = {...settingsReply, autoplay: true};
  elements['#device-autoplay'].checked = true;
  elements['#device-autoplay'].emit('change');
  await settle();
  const settingsPost = fetchCalls
    .filter((call) => call.url === '/api/settings' && call.options &&
                      call.options.method === 'POST')
    .at(-1);
  assert.deepEqual(JSON.parse(settingsPost.options.body),
                   {field: 'autoplay', value: true});
  assert.equal(elements['#device-status'].textContent, 'Сохранено');
  assert.equal(elements['#device-autoplay'].disabled, false);

  /* A slider writes when it is let go, not while it is being dragged: the
     readout follows the handle on its own. Brightness is the page's only
     slider now - the volume left it, having a knob on the device and a
     control on the player page already. */
  const beforeDrag = fetchCalls.length;
  elements['#device-brightness'].value = '30';
  elements['#device-brightness'].emit('input');
  assert.equal(elements['#device-brightness-value'].textContent, '30');
  assert.equal(fetchCalls.length, beforeDrag);
  settingsReply = {...settingsReply, brightness: 30};
  elements['#device-brightness'].emit('change');
  await settle();
  assert.deepEqual(
    JSON.parse(fetchCalls.filter((call) => call.url === '/api/settings' &&
                                           call.options &&
                                           call.options.method === 'POST')
      .at(-1).options.body),
    {field: 'brightness', value: 30});

  /* A write the device refuses puts the control back to what it actually
     holds: a switch left showing a change that never landed is worse than no
     answer at all. */
  settingsPostFails = true;
  elements['#device-flip-vertical'].checked = true;
  elements['#device-flip-vertical'].emit('change');
  await settle();
  assert.equal(elements['#device-status'].textContent, 'Не удалось сохранить');
  assert.equal(elements['#device-status'].classList.values.has('is-error'), true);
  assert.equal(elements['#device-flip-vertical'].checked, false);
  // And the fields are usable again rather than left disabled by the failure.
  assert.equal(elements['#device-flip-vertical'].disabled, false);

  settingsPostFails = false;
  settingsReply = {...settingsReply, language: 'en'};
  elements['#device-language'].value = 'en';
  elements['#device-language'].emit('change');
  await settle();
  assert.deepEqual(
    JSON.parse(fetchCalls.filter((call) => call.url === '/api/settings' &&
                                           call.options &&
                                           call.options.method === 'POST')
      .at(-1).options.body),
    {field: 'language', value: 'en'});
  /* The page itself is now in English - which is the whole point of the
     switch, and what it did not do before: it used to relabel the device's
     screen and leave the browser in Russian. */
  assert.equal(elements['#device-status'].textContent, 'Saved');
  assert.equal(documentRef.documentElement.lang, 'en');

  // Back to Russian, which is the language the rest of this file is written in.
  settingsReply = {...settingsReply, language: 'ru'};
  elements['#device-language'].value = 'ru';
  elements['#device-language'].emit('change');
  await settle();
  assert.equal(documentRef.documentElement.lang, 'ru');
  assert.equal(elements['#device-status'].textContent, 'Сохранено');


  /* The other direction, which is what makes this a settings page and not a
     form: the knob and the buttons on the device move these values, and the
     socket is what says so. Nothing else could - settings.csv is eleven reads
     and there is no signal in it that anything has changed. */
  sendEvent(second, {
    type: 'settings.update', revision: 9,
    settings: {...settingsReply, brightness: 70, scroll: 'left', buffer_view: 'text'},
  });
  assert.equal(elements['#device-brightness'].value, '70');
  assert.equal(elements['#device-brightness-value'].textContent, '70');
  assert.equal(elements['#device-scroll'].value, 'left');
  /* The buffer reading has the same two-way life as the rest: it is changed
     on the device screen as readily as here. */
  assert.equal(elements['#device-buffer-view'].value, 'text');

  // A push landing while a slider is held must not pull it out from under the
  // pointer; the next one is 250 ms away.
  elements['#device-brightness'].value = '55';
  elements['#device-brightness'].emit('input');
  assert.equal(elements['#device-brightness-value'].textContent, '55');
  sendEvent(second, {
    type: 'settings.update', revision: 10,
    settings: {...settingsReply, brightness: 15},
  });
  assert.equal(elements['#device-brightness'].value, '55');

  settingsReply = {...settingsReply, brightness: 55};
  elements['#device-brightness'].emit('change');
  await settle();
  assert.deepEqual(
    JSON.parse(fetchCalls.filter((call) => call.url === '/api/settings' &&
                                           call.options &&
                                           call.options.method === 'POST')
      .at(-1).options.body),
    {field: 'brightness', value: 55});
  assert.equal(elements['#device-brightness'].value, '55');

  // A stale revision is ignored here as it is everywhere else on the socket.
  sendEvent(second, {
    type: 'settings.update', revision: 4,
    settings: {...settingsReply, brightness: 90},
  });
  assert.equal(elements['#device-brightness'].value, '55');


  // Each saved network carries its own buttons, and which ones depends on where
  // it sits in the list and whether it is the one holding the connection.
  sendEvent(second, {
    type: 'wifi.update', revision: 30,
    wifi: {mode: 'sta_connected', active_ssid: 'other', ip: '192.168.1.11',
      save_pending: false, last_error: 0,
      saved: [{ssid: 'home', blocked: false}, {ssid: 'other', blocked: true}]},
  });
  const savedRows = elements['#saved-networks'].children;
  assert.equal(savedRows.length, 2);
  const labelsOf = (row) => row.children.map((child) => child.textContent);
  // The first row is the one the device tries first, and says so.
  assert.ok(labelsOf(savedRows[0]).includes('первая'));
  assert.ok(!labelsOf(savedRows[0]).includes('Сделать первой'));
  assert.ok(labelsOf(savedRows[1]).includes('Сделать первой'));
  assert.ok(labelsOf(savedRows[1]).includes('выключена до перезагрузки'));
  // Only the network carrying the connection can be disconnected from.
  assert.ok(!labelsOf(savedRows[0]).includes('Отключиться'));
  assert.ok(labelsOf(savedRows[1]).includes('Отключиться'));

  const buttonOf = (row, action) =>
    row.children.find((child) => child.dataset && child.dataset.action === action);

  // Forgetting is asked about first, and a "no" sends nothing at all.
  confirmAnswer = false;
  const sentBeforeForget = second.sent.length;
  buttonOf(savedRows[0], 'wifi.forget').emit('click');
  assert.equal(second.sent.length, sentBeforeForget);
  assert.match(confirmCalls.at(-1), /Забыть сеть «home»/);
  // It is not the active network, so nothing is promised about this page.
  assert.ok(!/эта страница/.test(confirmCalls.at(-1)));

  confirmAnswer = true;
  buttonOf(savedRows[0], 'wifi.forget').emit('click');
  assert.deepEqual(JSON.parse(second.sent.at(-1)),
    {type: 'command', id: 'settings-3', action: 'wifi.forget', ssid: 'home'});
  // One at a time: the device runs them serially anyway.
  const sentAfterForget = second.sent.length;
  buttonOf(savedRows[1], 'wifi.forget').emit('click');
  assert.equal(second.sent.length, sentAfterForget);
  /* The acknowledgement only says the command was queued: the work runs on the
     device afterwards, and a name that is no longer saved is refused there
     with nothing sent back. So "Готово" waits for the list to prove it. */
  sendEvent(second, {type: 'command.result', id: 'settings-3', ok: true});
  assert.equal(elements['#wifi-status'].textContent, 'Выполняем…');
  sendEvent(second, {
    type: 'wifi.update', revision: 33,
    wifi: {mode: 'sta_connected', active_ssid: 'other', ip: '192.168.1.11',
      save_pending: false, last_error: 0, saved: [{ssid: 'other', blocked: true}]},
  });
  assert.equal(elements['#wifi-status'].textContent, 'Готово');

  // A device that never carries it out says so on a timer rather than leaving
  // the page reporting work that did not happen.
  const rowsAfterForget = elements['#saved-networks'].children;
  buttonOf(rowsAfterForget[0], 'wifi.forget').emit('click');
  sendEvent(second, {type: 'command.result', id: 'settings-4', ok: true});
  const editTimeout = timers.filter((entry) => !entry.cleared && entry.delay === 5000).at(-1);
  editTimeout.cleared = true;
  editTimeout.callback();
  assert.equal(elements['#wifi-status'].textContent, 'Устройство не выполнило команду');

  // Disconnecting warns that it takes this page down, and names no network: the
  // device knows which one is carrying the connection.
  sendEvent(second, {
    type: 'wifi.update', revision: 34,
    wifi: {mode: 'sta_connected', active_ssid: 'other', ip: '192.168.1.11',
      save_pending: false, last_error: 0,
      saved: [{ssid: 'home', blocked: false}, {ssid: 'other', blocked: false}]},
  });
  const rows2 = elements['#saved-networks'].children;
  buttonOf(rows2[1], 'wifi.disconnect').emit('click');
  assert.match(confirmCalls.at(-1), /Отключиться от сети «other»/);
  assert.match(confirmCalls.at(-1), /эта страница перестанет отвечать/);
  assert.deepEqual(JSON.parse(second.sent.at(-1)),
    {type: 'command', id: 'settings-5', action: 'wifi.disconnect'});
  sendEvent(second, {type: 'command.result', id: 'settings-5', ok: true});
  // Switched off is what proves it landed, the same way a missing row proves a
  // forget did.
  sendEvent(second, {
    type: 'wifi.update', revision: 35,
    wifi: {mode: 'sta_connected', active_ssid: 'other', ip: '192.168.1.11',
      save_pending: false, last_error: 0,
      saved: [{ssid: 'home', blocked: false}, {ssid: 'other', blocked: true}]},
  });
  assert.equal(elements['#wifi-status'].textContent, 'Готово');

  // Reordering changes nothing that needs undoing, so it just happens.
  const confirmsBeforePriority = confirmCalls.length;
  buttonOf(elements['#saved-networks'].children[1], 'wifi.prioritize').emit('click');
  assert.equal(confirmCalls.length, confirmsBeforePriority);
  assert.deepEqual(JSON.parse(second.sent.at(-1)),
    {type: 'command', id: 'settings-6', action: 'wifi.prioritize', ssid: 'other'});
  sendEvent(second, {type: 'command.result', id: 'settings-6', ok: false, error: 'Занято'});
  assert.equal(elements['#wifi-status'].textContent, 'Занято');

  // A sixth network is something the visitor can act on, unlike a failed write.
  sendEvent(second, {
    type: 'wifi.update', revision: 36,
    wifi: {mode: 'sta_connected', active_ssid: 'other', ip: '192.168.1.11',
      save_pending: false, last_error: 257,
      saved: [{ssid: 'home', blocked: false}]},
  });
  assert.match(elements['#wifi-status'].textContent, /максимум 5 сетей/);

  // With no network at all the way in is the list of what is around, so the
  // page asks for it itself rather than waiting to be told to.
  scanReply = {state: 'done', networks: [
    {ssid: 'neighbour', rssi: -42, secure: true},
    {ssid: 'cafe', rssi: -81, secure: false},
  ]};
  sendEvent(second, {
    type: 'wifi.update', revision: 37,
    wifi: {mode: 'ap_setup', active_ssid: 'jradio-1A2B', ip: '192.168.4.1',
      save_pending: false, last_error: 0, saved: []},
  });
  assert.equal(elements['#wifi-scan-block'].hidden, false);
  // Nothing for it to open that the list has not already opened.
  assert.equal(elements['#wifi-add'].hidden, true);
  await settle();
  assert.ok(fetchCalls.some((call) => call.url === '/api/wifi-scan' &&
                                      call.options && call.options.method === 'POST'));
  const scanTimer = timers.filter((entry) => !entry.cleared && entry.delay === 700).at(-1);
  scanTimer.cleared = true;
  scanTimer.callback();
  await settle();
  const scanRows = elements['#scan-networks'].children;
  // Two networks, strongest first as the device sorted them, plus the way in
  // for a network that never announces itself.
  assert.equal(scanRows.length, 3);
  assert.equal(scanRows[0].children[0].textContent, 'neighbour');
  assert.match(scanRows[0].children[1].textContent, /-42 dBm/);
  assert.match(scanRows[1].children[1].textContent, /-81 dBm/);
  assert.ok(scanRows[1].children.some((child) => child.textContent === 'без пароля'));
  assert.equal(scanRows[2].children[0].textContent, 'Другая сеть…');

  // Picking one off the list means the name is shown, not typed.
  scanRows[0].children[0].emit('click');
  assert.equal(elements['#wifi-form'].hidden, false);
  assert.equal(elements['#wifi-chosen-name'].textContent, 'neighbour');
  assert.equal(elements['#wifi-ssid-row'].hidden, true);
  elements['#wifi-password'].value = 'letmein';
  elements['#wifi-form'].emit('submit');
  assert.deepEqual(JSON.parse(second.sent.at(-1)),
    {type: 'command', id: 'settings-7', action: 'wifi.save',
     ssid: 'neighbour', password: 'letmein'});

  // A hidden network is reached by typing, which is what the last row is for.
  scanRows[2].children[0].emit('click');
  assert.equal(elements['#wifi-ssid-row'].hidden, false);
  assert.equal(elements['#wifi-chosen'].hidden, true);

  // The About card. Its whole reason for existing is the case where the two
  // halves disagree, so that is what the reply above sets up.
  assert.ok(fetchCalls.some((call) => call.url === '/api/about'));
  assert.equal(elements['#about-firmware'].textContent, 'v1.2.0');
  assert.equal(elements['#about-built'].textContent, 'Sep  6 2026');
  assert.equal(elements['#about-web'].textContent, 'v1.1.0');
  assert.equal(elements['#about-idf'].textContent, 'v5.5.5');
  assert.equal(elements['#about-notice'].hidden, false);
  // The address is the device's answer, not a string written into the page,
  // and it is offered as something to write to.
  assert.equal(elements['#about-author'].textContent, 'someone@example.com');
  assert.equal(elements['#about-author'].href, 'mailto:someone@example.com');

  // Agreement is the quiet case.
  aboutReply = {
    firmware: {version: 'v1.2.0', built: 'Sep  6 2026', present: true},
    web: {version: 'v1.2.0', built: '2026-09-06', present: true},
    idf: 'v5.5.5', matched: true, author: 'someone@example.com',
  };
  await reload();
  assert.equal(elements['#about-web'].textContent, 'v1.2.0');
  assert.equal(elements['#about-notice'].hidden, true);

  // A web half the device could not read says so, and does *not* raise the
  // mismatch notice: an image flashed before the stamp existed is old, not
  // mismatched, and crying wolf there would teach the reader to ignore it.
  aboutReply = {
    firmware: {version: 'v1.2.0', built: 'Sep  6 2026', present: true},
    web: {version: '', built: '', present: false},
    idf: 'v5.5.5', matched: false, author: 'someone@example.com',
  };
  await reload();
  assert.equal(elements['#about-web'].textContent, 'неизвестно');
  assert.equal(elements['#about-notice'].hidden, true);

  /* The clock. The zone menu is built from what the device sent, not from the
     markup: the list lives in the firmware, and a page carrying its own copy
     would offer a zone the device cannot translate the day one is added. */
  assert.equal(elements['#device-timezone'].children.length, 2);
  assert.equal(elements['#device-timezone'].children[0].value, 'europe/moscow');
  assert.equal(elements['#device-timezone'].children[1].textContent, 'Екатеринбург (UTC+5)');
  assert.equal(elements['#device-timezone'].value, 'asia/yekaterinburg');
  assert.equal(elements['#device-ntp'].value, 'ntp.example.lan');

  elements['#device-timezone'].value = 'europe/moscow';
  elements['#device-timezone'].emit('change');
  await settle();
  assert.deepEqual(JSON.parse(fetchCalls.at(-1).options.body),
                   {field: 'timezone', value: 'europe/moscow'});

  /* Typing is not saving: a write per keystroke would put a dozen half-typed
     host names on the card, each one a flash erase. */
  const beforeTyping = fetchCalls.length;
  elements['#device-ntp'].value = 'ntp.example.l';
  elements['#device-ntp'].emit('input');
  await settle();
  assert.equal(fetchCalls.length, beforeTyping);

  /* And the field being typed into is not overwritten by an answer arriving
     underneath it. */
  sendEvent(first, {
    type: 'settings.update', revision: 20,
    settings: Object.assign({}, settingsReply, {volume: 71}),
  });
  assert.equal(elements['#device-ntp'].value, 'ntp.example.l');

  elements['#device-ntp'].value = '  time.cloudflare.com  ';
  elements['#device-ntp'].emit('change');
  await settle();
  // Trimmed on the way out: a host name with a space at either end resolves
  // to nothing, and the space is a typo rather than a choice.
  assert.deepEqual(JSON.parse(fetchCalls.at(-1).options.body),
                   {field: 'ntp_server', value: 'time.cloudflare.com'});

  /* The weather. Off on this device, so the key row and the reading are both
     out of the way, and the coordinates show what the card holds. */
  assert.equal(elements['#device-weather'].value, 'off');
  assert.equal(elements['#device-weather-latitude'].value, '55.75');
  assert.equal(elements['#device-weather-longitude'].value, '37.62');
  assert.equal(elements['#device-weather-key-row'].hidden, true);
  assert.equal(elements['#device-weather-now-row'].hidden, true);

  /* Choosing the service that wants a key brings the key row out, and the
     device says there is no key yet. The field carries nothing: the device
     never sends a key back, only whether it has one. */
  settingsReply = {...settingsReply, weather: 'openweathermap', weather_state: 'no_key'};
  elements['#device-weather'].value = 'openweathermap';
  elements['#device-weather'].emit('change');
  await settle();
  assert.deepEqual(JSON.parse(fetchCalls.at(-1).options.body),
                   {field: 'weather', value: 'openweathermap'});
  assert.equal(elements['#device-weather-key-row'].hidden, false);
  assert.equal(elements['#device-weather-key'].value, '');
  assert.equal(elements['#device-weather-key'].placeholder, 'не задан');
  assert.equal(elements['#device-weather-now-row'].hidden, false);
  assert.equal(elements['#device-weather-now'].textContent, 'нет ключа');

  /* The key goes out as its own field and comes back as a state, never as
     itself; the field is emptied once the device has it. */
  settingsReply = {...settingsReply, openweathermap_key_set: true, weather_state: 'waiting'};
  elements['#device-weather-key'].value = ' 0123456789abcdef0123456789abcdef ';
  elements['#device-weather-key'].emit('change');
  await settle();
  assert.deepEqual(JSON.parse(fetchCalls.at(-1).options.body),
                   {field: 'openweathermap_key', value: '0123456789abcdef0123456789abcdef'});
  assert.equal(elements['#device-weather-key'].value, '');
  assert.equal(elements['#device-weather-key'].placeholder, 'задан');
  assert.equal(elements['#device-weather-now'].textContent, 'ожидание ответа…');
  /* "Waiting" asks again on its own a few seconds later, so the line does not
     say so until somebody reloads. */
  const waitingRefresh = timers.at(-1);
  assert.equal(waitingRefresh.delay, 5000);
  const beforeRefresh = fetchCalls.length;
  settingsReply = {...settingsReply, weather_state: 'ok', weather_http_status: 200,
                   weather_report: {temperature: 13, icon: 'partly_cloudy_night'}};
  waitingRefresh.callback();
  await settle();
  assert.equal(fetchCalls.length, beforeRefresh + 1);
  assert.equal(elements['#device-weather-now'].textContent, '+13°, переменная облачность');
  assert.equal(elements['#device-weather-now'].classList.values.has('is-ok'), true);

  /* A refused key is said as such, not as a number. And a service that is
     simply down says which number it answered. */
  settingsReply = {...settingsReply, weather_state: 'failed', weather_http_status: 401,
                   weather_report: null};
  await refreshSettings();
  assert.equal(elements['#device-weather-now'].textContent, 'ключ не принят');
  assert.equal(elements['#device-weather-now'].classList.values.has('is-ok'), false);
  settingsReply = {...settingsReply, weather: 'wttr', weather_http_status: 503};
  await refreshSettings();
  assert.equal(elements['#device-weather-now'].textContent, 'сервис ответил 503');
  assert.equal(elements['#device-weather-key-row'].hidden, true);
  /* Below zero the sign is the reading's own; zero has none. */
  settingsReply = {...settingsReply, weather_state: 'ok', weather_http_status: 200,
                   weather_report: {temperature: -7, icon: 'snow'}};
  await refreshSettings();
  assert.equal(elements['#device-weather-now'].textContent, '-7°, снег');
  settingsReply = {...settingsReply, weather_report: {temperature: 0, icon: 'none'}};
  await refreshSettings();
  assert.equal(elements['#device-weather-now'].textContent, '0°');

  /* A live update over the socket carries the service but not the state, and
     leaves the line alone rather than blanking it. */
  sendEvent(second, {
    type: 'settings.update', revision: 40,
    settings: {...settingsReply, weather: 'open_meteo'},
  });
  assert.equal(elements['#device-weather'].value, 'open_meteo');
  assert.equal(elements['#device-weather-now'].textContent, '0°');

  /* Off takes the line away with it. */
  settingsReply = {...settingsReply, weather: 'off', weather_state: 'off', weather_report: null};
  await refreshSettings();
  assert.equal(elements['#device-weather-now-row'].hidden, true);

  /* Backup and restore. Nothing is chosen yet, so there is nothing to send -
     a Restore button that is live before a file is picked is a click that
     reaches the device with an empty body. */
  assert.equal(elements['#backup-restore'].disabled, true);

  const archive = {name: 'jradio-20260908.zip'};
  elements['#backup-file'].files = [archive];
  elements['#backup-file'].emit('change');
  assert.equal(elements['#backup-restore'].disabled, false);
  assert.equal(elements['#backup-status'].textContent, 'Выбран файл: jradio-20260908.zip');

  // Answering no to the question leaves the device alone.
  confirmAnswer = false;
  const beforeRefusal = fetchCalls.length;
  elements['#backup-restore'].emit('click');
  await settle();
  assert.equal(fetchCalls.length, beforeRefusal);
  assert.equal(confirmCalls.at(-1).includes('jradio-20260908.zip'), true);

  confirmAnswer = true;
  elements['#backup-restore'].emit('click');
  await settle();
  const restoreCall = fetchCalls.at(-1);
  // The name travels in the query: a single file is recognised by it, and an
  // archive would otherwise arrive with nothing to call it.
  assert.equal(restoreCall.url, '/api/restore?name=jradio-20260908.zip');
  assert.equal(restoreCall.options.method, 'POST');
  assert.equal(restoreCall.options.body, archive);
  assert.equal(elements['#backup-status'].textContent,
               'Восстановлено: wifi.json, settings.csv. Устройство перезагружается…');
  assert.equal(elements['#backup-status'].classList.values.has('is-success'), true);
  /* Cleared afterwards: the same file sent again after the reboot would put
     back settings that were deliberately changed in between. */
  assert.equal(elements['#backup-file'].value, '');
  assert.equal(elements['#backup-restore'].disabled, true);

  // A file the device wrote but cannot read back is not a success.
  elements['#backup-file'].files = [{name: 'wifi.json'}];
  elements['#backup-file'].emit('change');
  restoreReply = {restored: ['wifi.json'], warnings: ['wifi.json'], reboot: true};
  elements['#backup-restore'].emit('click');
  await settle();
  assert.equal(fetchCalls.at(-1).url, '/api/restore?name=wifi.json');
  assert.equal(elements['#backup-status'].textContent.includes('не смогло прочитать'), true);
  assert.equal(elements['#backup-status'].classList.values.has('is-error'), true);

  /* A refusal is said in Russian, by code: matching on the device's English
     sentence would leave the page blank the day one of them is reworded. */
  elements['#backup-file'].files = [{name: 'explorer.zip'}];
  elements['#backup-file'].emit('change');
  restoreOk = false;
  restoreReply = {error: 'compressed'};
  elements['#backup-restore'].emit('click');
  await settle();
  assert.equal(elements['#backup-status'].textContent.startsWith('Архив упакован способом'), true);
  assert.equal(elements['#backup-status'].classList.values.has('is-error'), true);
  // The choice survives a refusal, so the same file can be sent again.
  assert.equal(elements['#backup-restore'].disabled, false);

  // A code this page has never heard of still says something.
  restoreReply = {error: 'something-new'};
  elements['#backup-restore'].emit('click');
  await settle();
  assert.equal(elements['#backup-status'].textContent, 'Устройство не приняло файл');

  // And a device that vanished mid-upload is not silence either.
  restoreFetchFails = true;
  elements['#backup-restore'].emit('click');
  await settle();
  assert.equal(elements['#backup-status'].textContent, 'Не удалось отправить файл');
  restoreFetchFails = false;
  restoreOk = true;

  /* The other direction, which is what makes this one switch rather than two:
     the language turned on the device's own screen arrives over the socket and
     relabels this page. Revisions well past anything above, since a stale one
     is ignored by design. */
  assert.equal(documentRef.documentElement.lang, 'ru');
  sendEvent(second, {
    type: 'settings.update', revision: 90,
    settings: {...settingsReply, language: 'en'},
  });
  assert.equal(documentRef.documentElement.lang, 'en');
  assert.equal(elements['#device-language'].value, 'en');
  /* The account card too, and this is the trap it guards: its status words
     used to be frozen into a lookup table when the page loaded, so the first
     language the page ever saw was the one it kept for ever. */
  await settle();
  /* Whatever the card is saying at this point, it is saying it in English -
     which it could not before: its words were frozen into a lookup table when
     the page loaded, so the first language the page ever saw was the one it
     kept for ever. (The device is unreachable here, from a test above, so what
     it says is the failure line.) */
  assert.equal(elements['#yandex-status'].textContent, 'No connection to the device');
  sendEvent(second, {
    type: 'settings.update', revision: 91,
    settings: {...settingsReply, language: 'ru'},
  });
  assert.equal(documentRef.documentElement.lang, 'ru');

  /* The page folds away into five headings, which is what makes it a page a
     phone can hold: the markup arrives with the device section open and the
     rest as one line each. */
  assert.equal(section.device.body.hidden, false);
  assert.equal(section.wifi.body.hidden, true);
  assert.equal(section.device.toggle.getAttribute('aria-expanded'), 'true');
  assert.equal(section.about.toggle.getAttribute('aria-expanded'), 'false');

  // Opening one folds whatever was open: never two at a time.
  section.wifi.toggle.emit('click');
  assert.equal(section.wifi.body.hidden, false);
  assert.equal(section.device.body.hidden, true);
  assert.equal(section.device.classList.values.has('is-collapsed'), true);
  assert.equal(section.wifi.classList.values.has('is-collapsed'), false);
  assert.equal(SECTION_NAMES.filter((name) => !section[name].body.hidden).length, 1);
  assert.equal(section.device.toggle.getAttribute('aria-expanded'), 'false');
  assert.equal(section.wifi.toggle.getAttribute('aria-expanded'), 'true');

  // Tapping the open one folds it: "none of these" has to be sayable, or
  // something is always eating half the screen.
  section.wifi.toggle.emit('click');
  assert.equal(section.wifi.body.hidden, true);
  assert.equal(SECTION_NAMES.some((name) => !section[name].body.hidden), false);

  /* A screen with room for the two columns shows the lot, as it always has:
     the fold is an answer to a phone, and clicking a heading there does
     nothing. */
  section.about.toggle.emit('click');
  resize(false);
  assert.equal(SECTION_NAMES.every((name) => !section[name].body.hidden), true);
  assert.equal(section.device.toggle.getAttribute('tabindex'), '-1');
  section.wifi.toggle.emit('click');
  assert.equal(SECTION_NAMES.every((name) => !section[name].body.hidden), true);
  // Turned back on its side, the phone folds again around what was open.
  resize(true);
  assert.equal(SECTION_NAMES.filter((name) => !section[name].body.hidden).join(), 'about');
  assert.equal(section.about.toggle.getAttribute('tabindex'), '0');

  /* And the choice outlives the load, so a device that reboots after a restore
     comes back with the section that was being worked in. */
  section.yandex.toggle.emit('click');
  assert.equal(store['jradio.settings.section'], 'yandex');
  for (const card of sectionCards) {
    card.body.hidden = false;
    card.classList.remove('is-collapsed');
    card.toggle.listeners = {};
  }
  vm.runInContext(fs.readFileSync('data/www/settings.js', 'utf8'), context);
  assert.equal(section.yandex.body.hidden, false);
  assert.equal(section.device.body.hidden, true);

  console.log('web settings tests passed');
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
