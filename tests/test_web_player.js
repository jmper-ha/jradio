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
    /* setProperty is needed as much as plain assignment: the player bar paints
       its own --played variable through it. */
    this.style = {
      setProperty(name, value) { this[name] = value; },
    };
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
  'like-track', 'dislike-track', 'previous-item', 'next-item',
  'track-cover', 'track-progress', 'track-elapsed', 'track-total',
  'progress-rail', 'progress-fill', 'progress-seek',
  'volume-control', 'volume-input', 'volume-value',
  'stream-meta', 'player-error', 'command-status', 'media-list',
  'list-title', 'list-count', 'list-items', 'list-empty', 'list-loading', 'list-search',
  'player-bar', 'player-expand',
];
const buttonIds = new Set([
  'play-toggle', 'next-track', 'like-track', 'dislike-track', 'previous-item',
  'next-item',
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
const timerHandles = new Map();
const progressCalls = [];
// What GET /api/progress would answer; each test sets it before firing the
// timer the page schedules.
let progressReply = {cover: {present: false, generation: 0, width: 0, height: 0}};
let progressFails = false;
const context = {
  console,
  document: documentRef,
  HTMLElement: Element,
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
      // The station names are fetched, not sent in the frame; this test is
      // about the player controls, so it answers that once and lets the
      // progress poll have the rest.
      if (typeof url === 'string' && url.startsWith('/api/stations')) {
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve({
            kind: 'stations', revision: 1, count: 1,
            items: [{index: 0, label: 'Радио Шоколад'}],
          }),
        });
      }
      progressCalls.push({url, options});
      if (progressFails) return Promise.reject(new Error('offline'));
      return Promise.resolve({ok: true, json: () => Promise.resolve(progressReply)});
    },
  },
};

// The page reschedules its poll on every answer, so a test that wants one poll
// takes the newest live timer rather than shifting the queue.
function firePendingTimer() {
  for (let index = timers.length - 1; index >= 0; --index) {
    if (!timers[index].cleared && !timers[index].fired) {
      timers[index].fired = true;
      timers[index].callback();
      return true;
    }
  }
  return false;
}

function settle() {
  return new Promise((resolve) => setImmediate(resolve));
}

// The reconnect timer has to be picked out by its delay now that the progress
// poll keeps one of its own in the same queue.
function fireReconnect(delay) {
  for (let index = timers.length - 1; index >= 0; --index) {
    const entry = timers[index];
    if (!entry.cleared && !entry.fired && entry.delay === delay) {
      entry.fired = true;
      entry.callback();
      return true;
    }
  }
  return false;
}

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
    // The device is on a network unless a test says otherwise: without one the
    // radio tab is deliberately unusable, which every other test would trip on.
    wifi_connected: true,
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
    revision: 1,
    count: 1,
  },
  wifi: {},
  settings: {
    language: 'ru', home_screen: 'text', scroll: 'bounce', autoplay: false,
    yandex_music: true, flip_vertical: false, flip_horizontal: false,
    brightness: 45, volume: 62,
    available: {home_screen: true, yandex_music: true},
    brightness_min: 10, brightness_max: 90,
  },
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
/* The volume rides in the snapshot's settings section rather than in the
   player: it is a stored device setting, not a piece of playback state. */
assert.equal(elements['#volume-control'].hidden, false);
assert.equal(elements['#volume-input'].value, '62');
assert.equal(elements['#volume-value'].textContent, '62');



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
assert.ok(fireReconnect(500));

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
/* The rotor is not a list: its chain has no previous track and no index in
   anything, so the two track keys are absent rather than merely disabled. */
assert.equal(elements['#previous-item'].hidden, true);
assert.equal(elements['#next-item'].hidden, true);
/* And the playlist editor is not offered for it: that list is the catalog
   file, which has nothing to do with the account's stations. */
assert.equal(elements['#playlist-link'].hidden, true);
elements['#next-track'].emit('click');
assert.equal(JSON.parse(second.sent.at(-1)).action, 'player.next');

/* The two marks belong to the rotor, so they are on screen for the whole of
   it - the frame above carried no "liked", and they wait greyed rather than
   disappearing. Hiding them between tracks made them blink out on every skip
   and shifted the buttons either side of them. */
assert.equal(elements['#like-track'].hidden, false);
assert.equal(elements['#like-track'].disabled, true);
assert.equal(elements['#dislike-track'].hidden, false);
assert.equal(elements['#dislike-track'].disabled, true);
assert.equal(elements['#like-track'].classList.values.has('is-liked'), false);
sendEvent(second, {
  type: 'player.update', revision: 3, active_source: 'yandex',
  player: {state: 'playing', mode: 'ЯМузыка', artist: 'Des Rocs',
           title: 'Never Ending Moment', context: 'Моя волна', codec: 'MP3',
           bitrate_kbps: 320, sample_rate_hz: 44100, error: '', liked: false},
});
assert.equal(elements['#like-track'].hidden, false);
assert.equal(elements['#like-track'].classList.values.has('is-liked'), false);
assert.equal(elements['#like-track'].attributes['aria-pressed'], 'false');
elements['#like-track'].emit('click');
assert.equal(JSON.parse(second.sent.at(-1)).action, 'player.like');

/* The rejection appears and disappears with the like, because the device sends
   the two together: one test of "does this track belong to a library" decides
   both buttons. */
assert.equal(elements['#dislike-track'].hidden, false);
assert.equal(elements['#dislike-track'].classList.values.has('is-disliked'), false);
assert.equal(elements['#dislike-track'].attributes['aria-pressed'], 'false');
elements['#dislike-track'].emit('click');
assert.equal(JSON.parse(second.sent.at(-1)).action, 'player.dislike');

/* The device answers with the mark it actually got, so the heart fills in only
   once Yandex has taken it - never on the press alone. */
sendEvent(second, {
  type: 'player.update', revision: 4, active_source: 'yandex',
  player: {state: 'playing', mode: 'ЯМузыка', artist: 'Des Rocs',
           title: 'Never Ending Moment', context: 'Моя волна', codec: 'MP3',
           bitrate_kbps: 320, sample_rate_hz: 44100, error: '', liked: true},
});
assert.equal(elements['#like-track'].classList.values.has('is-liked'), true);
assert.equal(elements['#like-track'].attributes['aria-pressed'], 'true');
assert.equal(elements['#dislike-track'].classList.values.has('is-disliked'), false);

/* And the other way: the two marks exclude each other, so the frame that says
   one is set says the other is not, and the browser draws exactly what it was
   told rather than working it out. */
sendEvent(second, {
  type: 'player.update', revision: 5, active_source: 'yandex',
  player: {state: 'playing', mode: 'ЯМузыка', artist: 'Des Rocs',
           title: 'Never Ending Moment', context: 'Моя волна', codec: 'MP3',
           bitrate_kbps: 320, sample_rate_hz: 44100, error: '',
           liked: false, disliked: true},
});
assert.equal(elements['#like-track'].classList.values.has('is-liked'), false);
assert.equal(elements['#dislike-track'].classList.values.has('is-disliked'), true);
assert.equal(elements['#dislike-track'].attributes['aria-pressed'], 'true');

/* A device that sends only the like - an older one, or one whose frame lost
   the field - still draws a heart rather than nothing at all. */
sendEvent(second, {
  type: 'player.update', revision: 6, active_source: 'yandex',
  player: {state: 'playing', mode: 'ЯМузыка', artist: 'Des Rocs',
           title: 'Never Ending Moment', context: 'Моя волна', codec: 'MP3',
           bitrate_kbps: 320, sample_rate_hz: 44100, error: '', liked: true},
});
assert.equal(elements['#like-track'].hidden, false);
assert.equal(elements['#dislike-track'].classList.values.has('is-disliked'), false);

/* Stopped, there is nothing to skip. The button stays in place rather than
   disappearing, so the controls do not move under the cursor. */
sendEvent(second, {
  type: 'player.update', revision: 7, active_source: 'yandex',
  player: {state: 'stopped', mode: 'ЯМузыка', artist: '', title: '',
           context: 'Моя волна', codec: '', bitrate_kbps: 0,
           sample_rate_hz: 0, error: ''},
});
assert.equal(elements['#next-track'].hidden, false);
assert.equal(elements['#next-track'].disabled, true);
/* And with nothing playing there is no track to mark - the pair stays in
   place, greyed, for the same reason the skip button does. */
assert.equal(elements['#like-track'].hidden, false);
assert.equal(elements['#like-track'].disabled, true);
assert.equal(elements['#dislike-track'].hidden, false);
assert.equal(elements['#dislike-track'].disabled, true);

/* Back on the radio it goes away again. */
sendEvent(second, {
  type: 'player.update', revision: 8, active_source: 'internet_radio',
  player: player('Радио снова'),
});
assert.equal(elements['#next-track'].hidden, true);
assert.equal(elements['#like-track'].hidden, true);
assert.equal(elements['#dislike-track'].hidden, true);

/* The catalog is a list, so the two track keys are back - they move the
   playing station along it, exactly as the buttons on the front do. */
assert.equal(elements['#previous-item'].hidden, false);
assert.equal(elements['#next-item'].hidden, false);
assert.equal(elements['#previous-item'].disabled, false);
elements['#next-item'].emit('click');
assert.equal(JSON.parse(second.sent.at(-1)).action, 'player.next_item');
elements['#previous-item'].emit('click');
assert.equal(JSON.parse(second.sent.at(-1)).action, 'player.previous_item');

(async () => {
  /* Position and cover come over REST rather than the socket: the device
     diffs its snapshot and broadcasts on every change, so a second counter
     would push a frame a second to every open browser. */
  sendEvent(second, {
    type: 'player.update', revision: 9, active_source: 'usb',
    player: {state: 'playing', mode: 'USB-накопитель', artist: 'Kaleo',
             title: 'Way Down We Go', context: '/usb0/Kaleo', codec: 'MP3',
             bitrate_kbps: 320, sample_rate_hz: 44100, error: ''},
  });
  progressReply = {
    elapsed_seconds: 74, total_seconds: 217,
    cover: {present: true, generation: 4, width: 96, height: 96},
  };
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(progressCalls.at(-1).url, '/api/progress');
  assert.equal(elements['#track-progress'].hidden, false);
  assert.equal(elements['#track-elapsed'].textContent, '1:14');
  assert.equal(elements['#track-total'].textContent, '3:37');
  assert.equal(elements['#progress-fill'].style.width, '34%');
  /* The handle counts seconds, which read aloud is a number nobody wants; what
     it announces is the same time the label shows. */
  assert.equal(elements['#progress-seek'].attributes['aria-valuetext'], '1:14');
  assert.equal(elements['#progress-seek'].value, '74');
  assert.equal(elements['#progress-seek'].max, '217');
  assert.equal(elements['#progress-seek'].disabled, false);

  /* Dragging the handle: the bar and the time follow it, and nothing is sent
     until it is let go. */
  const beforeDrag = second.sent.length;
  elements['#progress-seek'].value = '150';
  elements['#progress-seek'].emit('input');
  assert.equal(elements['#track-elapsed'].textContent, '2:30');
  assert.equal(elements['#progress-fill'].style.width, '69%');
  assert.equal(second.sent.length, beforeDrag);

  elements['#progress-seek'].emit('change');
  const seekFrame = JSON.parse(second.sent.at(-1));
  assert.equal(seekFrame.action, 'player.seek');
  assert.equal(seekFrame.position, 150);

  /* Until the device answers that exact command the poll leaves the handle
     alone: an answer still describing the old position would drag it back. */
  progressReply = {
    elapsed_seconds: 75, total_seconds: 217,
    cover: {present: true, generation: 4, width: 96, height: 96},
  };
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(elements['#progress-seek'].value, '150');

  sendEvent(second, {type: 'command.result', id: seekFrame.id, ok: true});
  progressReply = {
    elapsed_seconds: 151, total_seconds: 217,
    cover: {present: true, generation: 4, width: 96, height: 96},
  };
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(elements['#progress-seek'].value, '151');
  assert.equal(elements['#track-elapsed'].textContent, '2:31');
  /* The generation is in the URL, so the browser fetches the picture exactly
     when it changes and takes it from its own cache the rest of the time. */
  assert.equal(elements['#track-cover'].hidden, false);
  assert.equal(elements['#track-cover'].src, '/api/cover?g=4');

  /* A track whose length the device cannot estimate yet loses the bar and
     keeps the cover: a bar drawn at nothing would claim a position. */
  progressReply = {cover: {present: true, generation: 4, width: 96, height: 96}};
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(elements['#track-progress'].hidden, true);
  assert.equal(elements['#track-cover'].hidden, false);
  // Nothing to jump to without a length, so the handle goes with the bar.
  assert.equal(elements['#progress-seek'].disabled, true);

  /* The picture is named by what it is, not by how many covers have gone by:
     the generation restarts at zero on every boot, and the day-long cache on
     /api/cover then hands back whichever picture that browser saw first under
     the same ?g=. */
  progressReply = {
    cover: {present: true, generation: 4, signature: 3735928559, width: 96, height: 96},
  };
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(elements['#track-cover'].src, '/api/cover?s=3735928559');

  /* Where the device hands over an address - the rotor's tracks - the browser
     fetches the picture itself, at the size the page draws rather than the
     96 px the panel decoded. */
  progressReply = {
    cover: {
      present: true, generation: 4, width: 96, height: 96,
      url: 'http://avatars.yandex.net/get-music-content/1/a.a.1/400x400',
    },
  };
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(elements['#track-cover'].src,
               'http://avatars.yandex.net/get-music-content/1/a.a.1/400x400');

  // An address the device did not mean is ignored rather than loaded: only a
  // plain http:// picture address is followed.
  progressReply = {
    cover: {present: true, generation: 6, width: 96, height: 96, url: 'javascript:0'},
  };
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(elements['#track-cover'].src, '/api/cover?g=6');

  // And a track with no picture at all takes the tile away rather than
  // leaving the previous album's cover under a new title.
  progressReply = {cover: {present: false, generation: 5, width: 0, height: 0}};
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(elements['#track-cover'].hidden, true);

  // With nothing playing there is nothing to ask about, so the poll goes
  // quiet instead of running all night in a forgotten tab.
  sendEvent(second, {
    type: 'player.update', revision: 10, active_source: 'usb',
    player: {state: 'stopped', mode: 'USB-накопитель', artist: '', title: '',
             context: '/usb0/Kaleo', codec: '', bitrate_kbps: 0,
             sample_rate_hz: 0, error: ''},
  });
  const quiet = progressCalls.length;
  assert.ok(firePendingTimer());
  await settle();
  assert.equal(progressCalls.length, quiet);
  assert.equal(elements['#track-progress'].hidden, true);
  // Stopped, the track keys have no place in the list to move from.
  assert.equal(elements['#previous-item'].disabled, true);
  assert.equal(elements['#next-item'].disabled, true);

  /* The knob on the front of the device moves the volume and nothing else
     would tell the browser, which is the whole reason the settings travel over
     the socket. */
  sendEvent(second, {type: 'settings.update', revision: 11, settings: {volume: 30}});
  assert.equal(elements['#volume-input'].value, '30');
  assert.equal(elements['#volume-value'].textContent, '30');

  // The readout follows the handle; nothing is written until it is let go.
  const beforeVolume = progressCalls.length;
  elements['#volume-input'].value = '80';
  elements['#volume-input'].emit('input');
  assert.equal(elements['#volume-value'].textContent, '80');
  assert.equal(progressCalls.length, beforeVolume);
  // And a push landing mid-drag must not pull the handle out from under it.
  sendEvent(second, {type: 'settings.update', revision: 12, settings: {volume: 31}});
  assert.equal(elements['#volume-input'].value, '80');

  elements['#volume-input'].emit('change');
  await settle();
  const volumePost = progressCalls.filter((call) => call.url === '/api/settings').at(-1);
  assert.equal(volumePost.options.method, 'POST');
  assert.deepEqual(JSON.parse(volumePost.options.body), {field: 'volume', value: 80});
  /* Written over REST rather than as a socket command: the volume is a stored
     setting, and this is the endpoint the settings page uses too. The handle
     stays where it was put rather than flicking back to the level of the last
     push. */
  assert.equal(elements['#volume-input'].value, '80');

  /* Each write rewrites settings.csv, so only one is ever in the air. A change
     arriving while one is - an arrow key held down fires one per repeat - is
     remembered rather than dropped, or the device would end a step behind
     where the handle was left. */
  const beforeBurst = progressCalls.filter((call) => call.url === '/api/settings').length;
  elements['#volume-input'].value = '90';
  elements['#volume-input'].emit('change');
  elements['#volume-input'].value = '95';
  elements['#volume-input'].emit('change');
  elements['#volume-input'].value = '99';
  elements['#volume-input'].emit('change');
  await settle();
  const burst = progressCalls.filter((call) => call.url === '/api/settings');
  assert.equal(burst.length - beforeBurst, 2);
  assert.deepEqual(JSON.parse(burst.at(-1).options.body),
                   {field: 'volume', value: 99});
  assert.equal(elements['#volume-input'].value, '99');

  // No network: the two sources that stream from the internet stay on the
  // strip and stop being pressable, the same answer the device's own screen
  // gives. Removing them would reshuffle the strip every time the Wi-Fi drops.
  sendEvent(second, {
    type: 'player.update', revision: 900,
    player: {...player('Радио'), wifi_connected: false},
  });
  const tabOf = (id) => elements['#source-tabs'].children.find(
    (child) => child.dataset && child.dataset.source === id);
  assert.equal(tabOf('internet_radio').disabled, true);
  assert.ok(tabOf('internet_radio').classList.values.has('is-unavailable'));
  sendEvent(second, {
    type: 'player.update', revision: 901,
    player: {...player('Радио'), wifi_connected: true},
  });
  assert.equal(tabOf('internet_radio').disabled, false);
  assert.ok(!tabOf('internet_radio').classList.values.has('is-unavailable'));

  second.emit('close');
  assert.equal(elements['#socket-state'].textContent, 'Нет связи');
  assert.equal(elements['#play-toggle'].disabled, true);
  assert.equal(elements['#volume-input'].disabled, true);

  console.log('web player tests passed');
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
