'use strict';

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

let documentRef;

class ClassList {
  constructor() {
    this.values = new Set();
  }

  add(...names) {
    names.forEach((name) => this.values.add(name));
  }

  remove(...names) {
    names.forEach((name) => this.values.delete(name));
  }

  toggle(name, force) {
    const enabled = force === undefined ? !this.values.has(name) : force;
    if (enabled) this.values.add(name);
    else this.values.delete(name);
    return enabled;
  }
}

class Element {
  constructor(tag = 'div') {
    this.tagName = tag.toUpperCase();
    this.children = [];
    this.dataset = {};
    this.classList = new ClassList();
    this.listeners = {};
    this.attributes = {};
    this.textContent = '';
    this.hidden = false;
    this.disabled = false;
    this.scrollTop = 0;
  }

  append(...items) {
    this.children.push(...items);
  }

  replaceChildren(...items) {
    this.children = [...items];
  }

  addEventListener(type, callback) {
    (this.listeners[type] ||= []).push(callback);
  }

  emit(type, event = {}) {
    for (const callback of this.listeners[type] || []) callback(event);
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }

  removeAttribute(name) {
    delete this.attributes[name];
  }

  contains(target) {
    return this === target || this.children.some((item) =>
      typeof item.contains === 'function' && item.contains(target));
  }

  focus() {
    documentRef.activeElement = this;
  }

  querySelectorAll(selector) {
    return walk(this).filter((item) => matches(item, selector));
  }
}

const ids = [
  'socket-state', 'playlist-link', 'source-tabs', 'mode-label', 'playback-state',
  'track-title', 'track-artist', 'track-context', 'play-toggle', 'next-track',
  'stream-meta', 'player-error', 'command-status', 'media-list',
  'list-title', 'list-count', 'list-items', 'list-empty',
];
const elements = Object.fromEntries(ids.map((id) => [
  `#${id}`, new Element(id === 'play-toggle' || id === 'next-track' ? 'button' : 'div'),
]));
const body = new Element('body');
body.append(...Object.values(elements));

function walk(root) {
  return (root.children || []).flatMap((item) => [item, ...walk(item)]);
}

function matches(item, selector) {
  return selector.split(',').some((part) => {
    const value = part.trim();
    if (value === '#play-toggle') return item === elements['#play-toggle'];
    if (value === 'button[data-command]') {
      return item.tagName === 'BUTTON' && item.dataset.command !== undefined;
    }
    if (value === 'button[data-source]') {
      return item.tagName === 'BUTTON' && item.dataset.source !== undefined;
    }
    if (value === 'button[data-index]') {
      return item.tagName === 'BUTTON' && item.dataset.index !== undefined;
    }
    return false;
  });
}

documentRef = {
  activeElement: null,
  querySelector: (selector) => elements[selector],
  querySelectorAll: (selector) => [body, ...walk(body)]
    .filter((item) => matches(item, selector)),
  createElement: (tag) => new Element(tag),
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

  addEventListener(type, callback) {
    (this.listeners[type] ||= []).push(callback);
  }

  emit(type, event = {}) {
    for (const callback of this.listeners[type] || []) callback(event);
  }

  send(frame) {
    this.sent.push(frame);
  }

  close() {
    this.readyState = 3;
    this.emit('close');
  }
}

const timers = [];
const context = {
  console,
  document: documentRef,
  HTMLElement: Element,
  WebSocket: FakeWebSocket,
  window: {
    location: {protocol: 'http:', host: 'radio.local'},
    setTimeout(callback, delay) {
      timers.push({callback, delay});
      return timers.length;
    },
  },
};

function player(title, rssi) {
  const value = {
    state: 'playing',
    mode: 'Интернет-радио',
    artist: '',
    title,
    context: '',
    codec: 'MP3',
    bitrate_kbps: 128,
    sample_rate_hz: 44100,
    error: '',
  };
  if (rssi !== undefined) value.wifi_rssi_dbm = rssi;
  return value;
}

function sendEvent(socket, payload) {
  socket.emit('message', {data: JSON.stringify(payload)});
}

const baseSnapshot = {
  capabilities: [{
    id: 'internet_radio',
    label: 'Интернет-радио',
    list_kind: 'stations',
  }],
  active_source: 'internet_radio',
  list: {
    kind: 'stations',
    active_index: 0,
    items: [{index: 0, label: 'Радио Шоколад'}],
  },
  wifi: {},
};

vm.createContext(context);
vm.runInContext(fs.readFileSync('data/www/app.js', 'utf8'), context);

const first = FakeWebSocket.instances[0];
assert.equal(first.url, 'ws://radio.local/ws');
first.readyState = FakeWebSocket.OPEN;
first.emit('open');

assert.equal(elements['#playlist-link'].hidden, true);
elements['#play-toggle'].emit('click');
assert.deepEqual(JSON.parse(first.sent.at(-1)), {
  type: 'command', id: 'web-1', action: 'player.toggle',
});
sendEvent(first, {
  type: 'command.result', id: 'web-1', ok: false,
  error: 'Команда отклонена до snapshot',
});
assert.equal(elements['#command-status'].textContent,
  'Команда отклонена до snapshot');

sendEvent(first, {
  type: 'player.update', revision: 2,
  active_source: 'internet_radio', player: player('До snapshot'),
});
assert.equal(elements['#track-title'].textContent, 'Нет воспроизведения');

sendEvent(first, {
  type: 'snapshot', revision: 2, ...baseSnapshot,
  player: player('Новое', -55),
});
assert.equal(elements['#track-title'].textContent, 'Новое');
assert.match(elements['#stream-meta'].textContent, /Wi-Fi -55/);
// Codec, bitrate and sample rate all reach the stream metadata line.
assert.match(elements['#stream-meta'].textContent, /MP3/);
assert.match(elements['#stream-meta'].textContent, /128 кбит\/с/);
assert.match(elements['#stream-meta'].textContent, /44100 Гц/);
assert.equal(elements['#playlist-link'].hidden, false);



sendEvent(first, {
  type: 'player.update', revision: 1,
  active_source: 'internet_radio', player: player('Старое'),
});
assert.equal(elements['#track-title'].textContent, 'Новое');

sendEvent(first, {
  type: 'player.update', revision: 3,
  active_source: 'internet_radio', player: player('Актуальное'),
});
assert.equal(elements['#track-title'].textContent, 'Актуальное');
assert.doesNotMatch(elements['#stream-meta'].textContent, /Wi-Fi/);

first.emit('close');
assert.equal(timers.length, 1);
assert.equal(timers[0].delay, 500);
timers.shift().callback();

const second = FakeWebSocket.instances[1];
second.readyState = FakeWebSocket.OPEN;
second.emit('open');
sendEvent(second, {
  type: 'player.update', revision: 99,
  active_source: 'internet_radio', player: player('До второго snapshot'),
});
assert.equal(elements['#track-title'].textContent, 'Актуальное');
sendEvent(second, {
  type: 'snapshot', revision: 1, ...baseSnapshot,
  player: player('Новое соединение'),
});
assert.equal(elements['#track-title'].textContent, 'Новое соединение');

sendEvent(first, {
  type: 'player.update', revision: 99,
  active_source: 'internet_radio', player: player('Старый сокет'),
});
first.emit('close');
assert.equal(elements['#track-title'].textContent, 'Новое соединение');
assert.equal(elements['#socket-state'].textContent, 'Подключено');
assert.equal(elements['#play-toggle'].disabled, false);

elements['#play-toggle'].emit('click');
assert.deepEqual(JSON.parse(second.sent.at(-1)), {
  type: 'command', id: 'web-2', action: 'player.toggle',
});

/* The skip button belongs to the rotor alone: a station has no next track,
   and a file list advances by itself when one ends. */
assert.equal(elements['#next-track'].hidden, true);
sendEvent(second, {
  type: 'player.update', revision: 2, active_source: 'yandex',
  player: {state: 'playing', mode: 'ЯМузыка', artist: 'Des Rocs',
           title: 'Never Ending Moment', context: 'Моя волна', codec: 'MP3',
           bitrate_kbps: 320, sample_rate_hz: 44100, error: ''},
});
assert.equal(elements['#next-track'].hidden, false);
assert.equal(elements['#next-track'].disabled, false);
/* And the playlist editor is not offered for it: that list is the catalog
   file, which has nothing to do with the account's stations. */
assert.equal(elements['#playlist-link'].hidden, true);
elements['#next-track'].emit('click');
assert.equal(JSON.parse(second.sent.at(-1)).action, 'player.next');

/* Stopped, there is nothing to skip. The button stays in place rather than
   disappearing, so the controls do not move under the cursor. */
sendEvent(second, {
  type: 'player.update', revision: 3, active_source: 'yandex',
  player: {state: 'stopped', mode: 'ЯМузыка', artist: '', title: '',
           context: 'Моя волна', codec: '', bitrate_kbps: 0,
           sample_rate_hz: 0, error: ''},
});
assert.equal(elements['#next-track'].hidden, false);
assert.equal(elements['#next-track'].disabled, true);

/* Back on the radio it goes away again. */
sendEvent(second, {
  type: 'player.update', revision: 4, active_source: 'internet_radio',
  player: player('Радио снова'),
});
assert.equal(elements['#next-track'].hidden, true);

second.emit('close');
assert.equal(elements['#socket-state'].textContent, 'Нет связи');
assert.equal(elements['#play-toggle'].disabled, true);

console.log('web player tests passed');
