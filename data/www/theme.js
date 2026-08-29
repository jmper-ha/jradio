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
