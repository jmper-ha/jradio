'use strict';

/* The mouse wheel on the pages' sliders, from theme.js.
 *
 * What it must not do matters as much as what it does: a page being scrolled
 * keeps scrolling over a slider, and a settings slider is written once when the
 * wheel stops, not once per notch - each write is a flash erase on the radio. */

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

let now = 1000;
let timers = [];
const listeners = {};

class FakeEvent {
  constructor(type) { this.type = type; }
}

function slider(attributes) {
  const events = [];
  return {
    tagName: 'INPUT',
    type: 'range',
    disabled: false,
    min: '0',
    max: '100',
    step: '1',
    value: '50',
    ...attributes,
    events,
    hasAttribute(name) { return name === 'data-no-wheel' && this.noWheel === true; },
    dispatchEvent(event) { events.push(event.type); },
  };
}

const context = {
  Date: {now: () => now},
  Event: FakeEvent,
  setTimeout: (fn, ms) => { timers.push({fn, at: now + ms}); return timers.length; },
  clearTimeout: (id) => { if (id > 0 && timers[id - 1]) timers[id - 1].fn = null; },
  localStorage: {getItem: () => null, setItem: () => {}},
  document: {
    readyState: 'complete',
    documentElement: {getAttribute: () => null, setAttribute: () => {}},
    querySelector: () => null,
    addEventListener(type, fn, options) { listeners[type] = {fn, options}; },
  },
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('data/www/theme.js', 'utf8'), context);

function wheel(target, deltaY, extra) {
  let prevented = false;
  listeners.wheel.fn({
    target, deltaY, deltaMode: 0, ctrlKey: false,
    preventDefault() { prevented = true; },
    ...extra,
  });
  return prevented;
}

function advance(ms) {
  now += ms;
  for (const timer of timers) {
    if (timer.fn && timer.at <= now) {
      const fn = timer.fn;
      timer.fn = null;
      fn();
    }
  }
}

// Not passive: a passive listener cannot keep the page from scrolling.
assert.strictEqual(listeners.wheel.options.passive, false);

// One notch up is one step up; the change waits for the wheel to stop.
{
  const volume = slider();
  assert.strictEqual(wheel(volume, -100), true);
  assert.strictEqual(volume.value, '51');
  assert.deepStrictEqual(volume.events, ['input']);
  advance(100);
  wheel(volume, -100);
  wheel(volume, 100);
  wheel(volume, 100);
  assert.strictEqual(volume.value, '50');
  assert.deepStrictEqual(volume.events, ['input', 'input', 'input', 'input']);
  advance(500);
  assert.deepStrictEqual(volume.events, ['input', 'input', 'input', 'input', 'change']);
}

// The slider's own step and range: the alarm's volume moves by five, and stops at the ends.
{
  advance(1000);
  const alarm = slider({step: '5', value: '95'});
  wheel(alarm, -100);
  wheel(alarm, -100);
  assert.strictEqual(alarm.value, '100');
  assert.deepStrictEqual(alarm.events, ['input']);
  advance(500);
  const bass = slider({min: '-12', max: '12', value: '-12'});
  wheel(bass, 100);
  assert.strictEqual(bass.value, '-12');
  advance(500);
}

// A trackpad's small deltas add up to a step.
{
  advance(1000);
  const brightness = slider({value: '20'});
  wheel(brightness, -15);
  wheel(brightness, -15);
  assert.strictEqual(brightness.value, '20');
  wheel(brightness, -15);
  assert.strictEqual(brightness.value, '21');
  advance(500);
}

// A page being scrolled scrolls on over a slider; a fresh wheel turns it.
{
  advance(1000);
  const page = {tagName: 'DIV'};
  const step = slider({value: '5'});
  assert.strictEqual(wheel(page, 100), false);
  advance(100);
  assert.strictEqual(wheel(step, 100), false);
  advance(100);
  assert.strictEqual(wheel(step, 100), false);
  assert.strictEqual(step.value, '5');
  advance(500);
  assert.strictEqual(wheel(step, 100), true);
  assert.strictEqual(step.value, '4');
  advance(500);
}

// Left alone: disabled, marked off (the track position), and ctrl - the browser's zoom.
{
  advance(1000);
  const seek = slider({noWheel: true});
  assert.strictEqual(wheel(seek, -100), false);
  advance(1000);
  const off = slider({disabled: true});
  assert.strictEqual(wheel(off, -100), false);
  advance(1000);
  const zoom = slider();
  assert.strictEqual(wheel(zoom, -100, {ctrlKey: true}), false);
  assert.strictEqual(zoom.value, '50');
}

// Moving to another slider closes the first one's turn with its change.
{
  advance(1000);
  const bass = slider({value: '0'});
  const treble = slider({value: '0'});
  wheel(bass, -100);
  wheel(treble, -100);
  assert.deepStrictEqual(bass.events, ['input', 'change']);
  assert.deepStrictEqual(treble.events, ['input']);
  advance(500);
  assert.deepStrictEqual(treble.events, ['input', 'change']);
}

console.log('web wheel tests passed');
