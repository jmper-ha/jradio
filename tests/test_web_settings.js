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
  }
  addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
  emit(type, event = {}) {
    event.preventDefault ||= () => {};
    for (const callback of this.listeners[type] || []) callback(event);
  }
  replaceChildren(...items) { this.children = items; }
  append(...items) { this.children.push(...items); }
}

const ids = [
  'socket-state', 'wifi-form', 'wifi-ssid', 'wifi-password', 'wifi-submit',
  'wifi-status', 'wifi-active', 'wifi-ip', 'saved-networks',
  'saved-networks-empty',
  'yandex-status', 'yandex-code-block', 'yandex-url', 'yandex-code',
  'yandex-countdown', 'yandex-link', 'yandex-cancel', 'yandex-forget',
  'yandex-refresh', 'yandex-stations-block', 'yandex-stations',
  'yandex-stations-empty',
  'device-status', 'device-language', 'device-home-screen', 'device-home-screen-row',
  'device-scroll', 'device-autoplay', 'device-yandex', 'device-yandex-row',
  'device-volume', 'device-volume-value', 'device-brightness',
  'device-brightness-value', 'device-flip-vertical', 'device-flip-horizontal',
];
const elements = Object.fromEntries(ids.map((id) => [`#${id}`, new Element()]));
elements['#wifi-form'].elements = {
  ssid: elements['#wifi-ssid'],
  password: elements['#wifi-password'],
};

const documentRef = {
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
  language: 'ru', home_screen: 'text', scroll: 'bounce', autoplay: false,
  yandex_music: true, flip_vertical: false, flip_horizontal: true,
  brightness: 45, volume: 62,
  available: {home_screen: true, yandex_music: false},
  brightness_min: 10, brightness_max: 90,
};
let settingsPostFails = false;

const timerHandles = new Map();
const context = {
  console,
  document: documentRef,
  WebSocket: FakeWebSocket,
  window: {
    location: {protocol: 'http:', host: 'radio.local'},
    setTimeout(callback, delay) {
      timers.push({callback, delay});
      timerHandles.set(timers.length, timers[timers.length - 1]);
      return timers.length;
    },
    clearTimeout(handle) {
      const entry = timerHandles.get(handle);
      if (entry) entry.cleared = true;
    },
    fetch(url, options) {
      fetchCalls.push({url, options});
      if (String(url).startsWith('/api/settings')) {
        if (options && options.method === 'POST' && settingsPostFails) {
          return Promise.reject(new Error('refused'));
        }
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve(settingsReply),
        });
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
vm.runInContext(fs.readFileSync('data/www/settings.js', 'utf8'), context);

const first = FakeWebSocket.instances[0];
assert.equal(first.url, 'ws://radio.local/ws');
first.readyState = FakeWebSocket.OPEN;
first.emit('open');

sendEvent(first, {
  type: 'wifi.update', revision: 5,
  wifi: {mode: 'sta_connected', active_ssid: 'stale', saved_ssids: ['stale']},
});
assert.notEqual(elements['#wifi-active'].textContent, 'stale');

sendEvent(first, snapshot(5, {
  mode: 'sta_connected', active_ssid: 'home', ip: '192.168.1.10',
  save_pending: false, last_error: 0, saved_ssids: ['home'],
}));
assert.equal(elements['#wifi-active'].textContent, 'home');
assert.equal(elements['#saved-networks'].children.length, 1);

elements['#wifi-ssid'].value = 'new-ap';
elements['#wifi-password'].value = 'topsecret42';
elements['#wifi-form'].emit('submit');
assert.equal(elements['#wifi-password'].value, '');
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
    save_pending: true, last_error: 0, saved_ssids: ['home']},
});
assert.equal(elements['#wifi-status'].textContent, 'Подключение…');
assert.equal(elements['#wifi-submit'].disabled, true);

sendEvent(first, {
  type: 'wifi.update', revision: 7,
  wifi: {mode: 'sta_connected', active_ssid: 'new-ap', ip: '192.168.1.20',
    save_pending: false, last_error: 0, saved_ssids: ['home', 'new-ap']},
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
    save_pending: false, last_error: 202, saved_ssids: ['home', 'new-ap']},
});
assert.match(elements['#wifi-status'].textContent, /парол/i);
assert.equal(elements['#wifi-submit'].disabled, false);

sendEvent(first, {
  type: 'wifi.update', revision: 7,
  wifi: {mode: 'sta_connected', active_ssid: 'stale', ip: '',
    save_pending: false, last_error: 0, saved_ssids: []},
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
  save_pending: false, last_error: 0, saved_ssids: ['home'],
}));
assert.equal(elements['#wifi-active'].textContent, 'home');
sendEvent(second, {
  type: 'wifi.update', revision: 2,
  wifi: {mode: 'sta_connecting', active_ssid: 'other', ip: '',
    save_pending: true, last_error: 0, saved_ssids: ['home']},
});
sendEvent(second, {
  type: 'wifi.update', revision: 3,
  wifi: {mode: 'sta_connected', active_ssid: 'other', ip: '192.168.1.11',
    save_pending: false, last_error: 0, saved_ssids: ['home', 'other']},
});
assert.equal(elements['#wifi-status'].textContent, 'Сохранено');
assert.equal(elements['#wifi-submit'].disabled, false);
sendEvent(first, snapshot(99, {
  mode: 'sta_connected', active_ssid: 'old-socket', ip: '',
  save_pending: false, last_error: 0, saved_ssids: []},
));
assert.equal(elements['#wifi-active'].textContent, 'other');

// Yandex Music: REST, so everything below settles on microtasks rather than
// on socket frames. The synchronous assertions above all ran before the very
// first fetch resolved, which is why this part is at the end and async.
async function settle() {
  // Deep enough for the longest chain on the page: a refused settings write
  // falls into its catch, re-reads the document and only then reports.
  for (let step = 0; step < 24; step += 1) await Promise.resolve();
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
  assert.equal(elements['#device-autoplay'].checked, false);
  assert.equal(elements['#device-flip-horizontal'].checked, true);
  assert.equal(elements['#device-brightness'].value, '45');
  assert.equal(elements['#device-brightness-value'].textContent, '45');
  assert.equal(elements['#device-volume-value'].textContent, '62');
  // The slider stops where the encoder does, and the device says where.
  assert.equal(elements['#device-brightness'].min, '10');
  assert.equal(elements['#device-brightness'].max, '90');
  /* A build without Yandex Music has no such row on its own screen either, so
     the switch goes away rather than sitting there changing nothing. */
  assert.equal(elements['#device-yandex-row'].hidden, true);
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
     readout follows the handle on its own. */
  const beforeDrag = fetchCalls.length;
  elements['#device-volume'].value = '30';
  elements['#device-volume'].emit('input');
  assert.equal(elements['#device-volume-value'].textContent, '30');
  assert.equal(fetchCalls.length, beforeDrag);
  settingsReply = {...settingsReply, volume: 30};
  elements['#device-volume'].emit('change');
  await settle();
  assert.deepEqual(
    JSON.parse(fetchCalls.filter((call) => call.url === '/api/settings' &&
                                           call.options &&
                                           call.options.method === 'POST')
      .at(-1).options.body),
    {field: 'volume', value: 30});

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
  assert.equal(elements['#device-status'].textContent, 'Сохранено');

  /* The other direction, which is what makes this a settings page and not a
     form: the knob and the buttons on the device move these values, and the
     socket is what says so. Nothing else could - settings.csv is eleven reads
     and there is no signal in it that anything has changed. */
  sendEvent(second, {
    type: 'settings.update', revision: 9,
    settings: {...settingsReply, volume: 12, brightness: 70, scroll: 'left'},
  });
  assert.equal(elements['#device-volume'].value, '12');
  assert.equal(elements['#device-volume-value'].textContent, '12');
  assert.equal(elements['#device-brightness-value'].textContent, '70');
  assert.equal(elements['#device-scroll'].value, 'left');

  // A push landing while a slider is held must not pull it out from under the
  // pointer; the next one is 250 ms away.
  elements['#device-volume'].value = '55';
  elements['#device-volume'].emit('input');
  assert.equal(elements['#device-volume-value'].textContent, '55');
  sendEvent(second, {
    type: 'settings.update', revision: 10,
    settings: {...settingsReply, volume: 5},
  });
  assert.equal(elements['#device-volume'].value, '55');

  settingsReply = {...settingsReply, volume: 55};
  elements['#device-volume'].emit('change');
  await settle();
  assert.deepEqual(
    JSON.parse(fetchCalls.filter((call) => call.url === '/api/settings' &&
                                           call.options &&
                                           call.options.method === 'POST')
      .at(-1).options.body),
    {field: 'volume', value: 55});
  assert.equal(elements['#device-volume'].value, '55');

  // A stale revision is ignored here as it is everywhere else on the socket.
  sendEvent(second, {
    type: 'settings.update', revision: 4,
    settings: {...settingsReply, volume: 99},
  });
  assert.equal(elements['#device-volume'].value, '55');

  console.log('web settings tests passed');
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
