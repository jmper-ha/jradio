/* The theme choice, shared by all three pages.

   Linked from <head> without defer: the theme has to be on <html> before the
   first paint, or the page flashes the other theme's background on the way in.
   Dark is the default and the system's preference is not consulted - the
   stylesheet's bare :root is the dark palette, and only an explicit choice
   kept in localStorage swaps it. */
(function () {
  'use strict';

  var STORAGE_KEY = 'jradio-theme';
  var COLORS = {light: '#eceff3', dark: '#0c0f13'};
  var root = document.documentElement;

  function readChoice() {
    /* In a private window, and wherever site data is blocked, touching the
       store throws rather than returning null. */
    try {
      var stored = localStorage.getItem(STORAGE_KEY);
      return stored === 'light' || stored === 'dark' ? stored : null;
    } catch (error) {
      return null;
    }
  }

  function writeChoice(value) {
    try {
      localStorage.setItem(STORAGE_KEY, value);
    } catch (error) {
      /* The choice will not survive a reload - it still applies to this
         page. */
    }
  }

  function current() {
    var chosen = root.getAttribute('data-theme');
    return chosen === 'light' ? 'light' : 'dark';
  }

  /* A phone paints its address bar from this tag: left alone it stays light
     above a dark page. */
  function paintBrowserChrome() {
    var meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute('content', COLORS[current()]);
  }

  var choice = readChoice();
  if (choice) root.setAttribute('data-theme', choice);

  function wire() {
    paintBrowserChrome();
    var button = document.querySelector('#theme-toggle');
    if (!button) return;
    button.addEventListener('click', function () {
      var next = current() === 'dark' ? 'light' : 'dark';
      root.setAttribute('data-theme', next);
      writeChoice(next);
      paintBrowserChrome();
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', wire);
  } else {
    wire();
  }
})();

/* The mouse wheel moves a slider - the volume, the tone, the settings - one
   step per notch. Here because this is the one script every page loads, and a
   listener on the document needs nothing from the page around it.

   A wheel that is scrolling the page goes on scrolling it when a slider slides
   under the pointer: the settings are a long page of sliders, and reading down
   it must not move them. Only a wheel that starts on a slider turns it.

   The slider hears what a drag would have said: an input per step, and one
   change once the wheel has stopped. The change is what writes the settings to
   the card, and a write per notch would be a flash erase per notch. */
(function () {
  'use strict';

  // A trackpad sends a stream of small deltas; this much travel is one step.
  var STEP_TRAVEL = 40;
  var SCROLL_GRACE_MS = 400;
  var SETTLE_MS = 400;
  var scrolledAt = -Infinity;
  var turning = null;
  var travel = 0;
  var timer = 0;

  function settle() {
    var input = turning;
    clearTimeout(timer);
    timer = 0;
    turning = null;
    travel = 0;
    if (input) input.dispatchEvent(new Event('change', {bubbles: true}));
  }

  function turnable(node) {
    return node && node.tagName === 'INPUT' && node.type === 'range' && !node.disabled &&
      !node.hasAttribute('data-no-wheel');
  }

  document.addEventListener('wheel', function (event) {
    var input = event.target;
    var now = Date.now();
    // Ctrl and the wheel is the browser's zoom.
    if (!turnable(input) || event.ctrlKey || !event.deltaY) {
      scrolledAt = now;
      return;
    }
    if (input !== turning && now - scrolledAt < SCROLL_GRACE_MS) {
      scrolledAt = now;
      return;
    }
    event.preventDefault();
    if (turning && turning !== input) settle();
    turning = input;
    clearTimeout(timer);
    timer = setTimeout(settle, SETTLE_MS);

    var up;
    if (event.deltaMode !== 0 || Math.abs(event.deltaY) >= STEP_TRAVEL) {
      travel = 0;
      up = event.deltaY < 0;
    } else {
      travel += event.deltaY;
      if (Math.abs(travel) < STEP_TRAVEL) return;
      up = travel < 0;
      travel = 0;
    }
    var step = Number(input.step) || 1;
    var min = Number(input.min);
    var max = Number(input.max);
    var before = Number(input.value);
    var value = before + (up ? step : -step);
    if (Number.isFinite(min)) value = Math.max(min, value);
    if (Number.isFinite(max)) value = Math.min(max, value);
    if (value === before) return;
    input.value = String(value);
    input.dispatchEvent(new Event('input', {bubbles: true}));
  }, {passive: false});
})();
