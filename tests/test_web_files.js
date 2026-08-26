'use strict';

// The USB listing is the one list the browser does not receive over the
// socket: the frame budget cannot hold a directory of long names, so the
// socket carries a revision and the entries arrive from GET /api/files. That
// split is what these tests cover - especially the ordering hazard it creates,
// where a reply can land after the device has already moved to another folder.

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

  contains(name) {
    return this.values.has(name);
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
    this.style = {};
    this.value = '';
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
  'like-track', 'previous-item', 'next-item',
  'track-cover', 'track-progress', 'track-elapsed', 'track-total',
  'progress-rail', 'progress-fill', 'progress-seek',
  'volume-control', 'volume-input', 'volume-value',
  'stream-meta', 'player-error', 'command-status', 'media-list',
  'list-title', 'list-count', 'list-items', 'list-empty',
];
const buttonIds = new Set([
  'play-toggle', 'next-track', 'like-track', 'previous-item', 'next-item',
]);
const elements = Object.fromEntries(ids.map((id) => [
  `#${id}`,
  new Element(buttonIds.has(id) ? 'button' : 'div'),
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
    if (value === 'button') return item.tagName === 'BUTTON';
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

// Fetches are answered by hand so a reply can be held back and delivered late,
// which is the whole point of the revision guard.
const fetchCalls = [];
let pendingFetch = [];

function fakeFetch(url, options) {
  fetchCalls.push({url, options});
  return new Promise((resolve, reject) => {
    pendingFetch.push({resolve, reject});
  });
}

function respond(payload, {ok = true, status = 200} = {}) {
  const pending = pendingFetch.shift();
  assert.ok(pending, 'a fetch was expected but none was in flight');
  pending.resolve({ok, status, json: async () => payload});
  return flush();
}

// Two turns: one for the await on fetch, one for the await on json().
async function flush() {
  // setImmediate drains the whole microtask queue, which matters because the
  // await chain crosses the vm realm boundary and a fixed number of ticks is
  // not enough to settle it.
  for (let i = 0; i < 4; i += 1) {
    await new Promise((resolve) => setImmediate(resolve));
  }
}

const timers = [];
const context = {
  console,
  document: documentRef,
  fetch: fakeFetch,
  HTMLElement: Element,
  WebSocket: FakeWebSocket,
  Promise,
  window: {
    location: {protocol: 'http:', host: 'radio.local'},
    setTimeout(callback, delay) {
      timers.push({callback, delay});
      return timers.length;
    },
    clearTimeout() {},
    /* The progress poll, which this test never fires: it must not share the
       hand-driven queue above, or a reply meant for the directory listing
       would be handed to it instead. */
    fetch() {
      return new Promise(() => {});
    },
  },
};

function sendEvent(socket, payload) {
  socket.emit('message', {data: JSON.stringify(payload)});
}

function usbPlayer() {
  return {
    state: 'stopped',
    mode: 'USB-плеер',
    artist: '',
    title: '',
    context: '/usb0',
    codec: '',
    bitrate_kbps: 0,
    sample_rate_hz: 0,
    error: '',
  };
}

function rows() {
  return elements['#list-items'].children.map((row) => row.children[0]);
}

function labels() {
  return rows().map((button) => button.children[0].textContent);
}

vm.createContext(context);
vm.runInContext(fs.readFileSync('data/www/app.js', 'utf8'), context);

const socket = FakeWebSocket.instances[0];
socket.readyState = FakeWebSocket.OPEN;
socket.emit('open');

(async () => {
  // A USB snapshot carries a header only, and triggers exactly one fetch.
  sendEvent(socket, {
    type: 'snapshot',
    revision: 1,
    capabilities: [
      {id: 'internet_radio', label: 'Интернет-радио', list_kind: 'stations'},
      {id: 'usb', label: 'USB-накопитель', list_kind: 'files'},
    ],
    active_source: 'usb',
    player: usbPlayer(),
    list: {kind: 'files', active_index: null, path: '/usb0', revision: 1, has_parent: false},
    wifi: {},
  });

  assert.equal(fetchCalls.length, 1, 'a files listing must trigger a fetch');
  assert.equal(fetchCalls[0].url, '/api/files');
  assert.equal(fetchCalls[0].options.cache, 'no-store',
    'a cached listing would show a directory the device has already left');

  await respond({
    path: '/usb0',
    revision: 1,
    has_parent: false,
    items: [
      {index: 0, name: 'Альбом', kind: 'dir'},
      {index: 1, name: 'track.mp3', kind: 'file', format: 'MP3'},
    ],
  });

  assert.deepEqual(labels(), ['Альбом/', 'track.mp3 · MP3']);
  assert.equal(elements['#list-count'].textContent, '2');
  // The heading names the directory: "Файлы" alone leaves the user lost.
  assert.equal(elements['#list-title'].textContent, '/usb0');
  assert.ok(rows()[0].classList.contains('is-directory'));
  assert.ok(!rows()[1].classList.contains('is-directory'));
  // A directory is opened, never played.
  assert.equal(rows()[0].children[1].textContent, 'Открыть');
  assert.equal(rows()[1].children[1].textContent, 'Играет');

  // Clicking an entry selects it by the device's own index.
  rows()[1].emit('click');
  assert.deepEqual(JSON.parse(socket.sent.at(-1)), {
    type: 'command', id: 'web-1', action: 'list.select', index: 1,
  });

  // Player updates repeat the same revision and must not refetch.
  const before = fetchCalls.length;
  sendEvent(socket, {
    type: 'player.update', revision: 2, active_source: 'usb', player: usbPlayer(),
  });
  assert.equal(fetchCalls.length, before, 'an unchanged revision must not refetch');

  // Entering a directory bumps the revision, which is the only signal that the
  // contents changed: the count and active index can stay identical.
  sendEvent(socket, {
    type: 'list.update',
    revision: 3,
    list: {kind: 'files', active_index: null, path: '/usb0/Альбом', revision: 2, has_parent: true},
  });
  assert.equal(fetchCalls.length, before + 1, 'a new revision must refetch');

  await respond({
    path: '/usb0/Альбом',
    revision: 2,
    has_parent: true,
    items: [{index: 0, name: 'a.mp3', kind: 'file', format: 'MP3'}],
  });

  // The parent row leads the list and is not counted as an entry.
  assert.deepEqual(labels(), ['.. (наверх)', 'a.mp3 · MP3']);
  assert.equal(elements['#list-count'].textContent, '1');
  assert.equal(rows()[0].dataset.command, 'browse.up');
  assert.equal(rows()[0].dataset.index, undefined,
    'the parent row must not carry an index, or it would shift every entry');
  // The first real entry keeps the device's index 0 despite being second.
  assert.equal(rows()[1].dataset.index, '0');

  rows()[0].emit('click');
  assert.deepEqual(JSON.parse(socket.sent.at(-1)), {
    type: 'command', id: 'web-2', action: 'browse.up',
  });

  // An empty directory still shows the way back, so it is not "empty".
  sendEvent(socket, {
    type: 'list.update',
    revision: 4,
    list: {kind: 'files', active_index: null, path: '/usb0/Пусто', revision: 3, has_parent: true},
  });
  await respond({path: '/usb0/Пусто', revision: 3, has_parent: true, items: []});
  assert.deepEqual(labels(), ['.. (наверх)']);
  assert.equal(elements['#list-empty'].hidden, true);
  assert.equal(elements['#list-items'].hidden, false);

  // The ordering hazard: two directories opened in quick succession, replies
  // arriving in reverse. The stale one must not overwrite the newer listing.
  sendEvent(socket, {
    type: 'list.update',
    revision: 5,
    list: {kind: 'files', active_index: null, path: '/usb0/Первая', revision: 4, has_parent: true},
  });
  sendEvent(socket, {
    type: 'list.update',
    revision: 6,
    list: {kind: 'files', active_index: null, path: '/usb0/Вторая', revision: 5, has_parent: true},
  });
  assert.equal(pendingFetch.length, 2, 'each new revision fetches once');

  // Answer the newer request first, then let the stale one land.
  pendingFetch[1].resolve({
    ok: true,
    status: 200,
    json: async () => ({
      path: '/usb0/Вторая', revision: 5, has_parent: true,
      items: [{index: 0, name: 'new.mp3', kind: 'file', format: 'MP3'}],
    }),
  });
  await flush();
  assert.deepEqual(labels(), ['.. (наверх)', 'new.mp3 · MP3']);

  pendingFetch[0].resolve({
    ok: true,
    status: 200,
    json: async () => ({
      path: '/usb0/Первая', revision: 4, has_parent: true,
      items: [{index: 0, name: 'stale.mp3', kind: 'file', format: 'MP3'}],
    }),
  });
  await flush();
  assert.deepEqual(labels(), ['.. (наверх)', 'new.mp3 · MP3'],
    'a late reply for an abandoned directory must be discarded');
  assert.equal(elements['#list-title'].textContent, '/usb0/Вторая');

  pendingFetch = [];

  // A failed read says so rather than leaving the previous listing looking
  // current with no explanation.
  sendEvent(socket, {
    type: 'list.update',
    revision: 7,
    list: {kind: 'files', active_index: null, path: '/usb0/Ошибка', revision: 6, has_parent: true},
  });
  await respond({}, {ok: false, status: 404});
  assert.equal(elements['#command-status'].textContent, 'Не удалось прочитать флешку');

  // Switching back to the radio restores the station list unchanged.
  sendEvent(socket, {
    type: 'snapshot',
    revision: 8,
    capabilities: [
      {id: 'internet_radio', label: 'Интернет-радио', list_kind: 'stations'},
      {id: 'usb', label: 'USB-накопитель', list_kind: 'files'},
    ],
    active_source: 'internet_radio',
    player: {...usbPlayer(), mode: 'Интернет-радио', context: 'Радио'},
    list: {kind: 'stations', active_index: 0, items: [{index: 0, label: 'Радио Шоколад'}]},
    wifi: {},
  });
  assert.deepEqual(labels(), ['Радио Шоколад']);
  assert.equal(elements['#list-title'].textContent, 'Станции');

  console.log('web usb tests passed');
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
