'use strict';

/* The dictionary against the pages that use it.
 *
 * The failure this catches is the one that reached a real screen: a paragraph
 * marked up with `data-i18n="note.device"` and never added to the dictionary,
 * which shows the key itself where the sentence belongs. Nothing else notices -
 * the page renders, the tests pass, and the text is simply wrong. */

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

/* No localStorage here, which is the case the runtime has to survive: a
   private window, or a browser with site data switched off. */
const context = {
  console,
  document: {
    documentElement: {lang: 'ru'},
    body: {getAttribute: () => null},
    querySelectorAll: () => [],
  },
  window: {},
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('data/www/i18n.js', 'utf8'), context);
const i18n = context.window.jradioI18n;

const PAGES = ['index.html', 'playlist.html', 'settings.html'];
const MARKERS = ['data-i18n', 'data-i18n-aria', 'data-i18n-placeholder', 'data-i18n-title',
                 'data-i18n-attr'];

function keysIn(html) {
  const found = new Set();
  for (const marker of MARKERS) {
    const pattern = new RegExp(`${marker}="([^"]+)"`, 'g');
    let match = pattern.exec(html);
    while (match !== null) {
      found.add(match[1]);
      match = pattern.exec(html);
    }
  }
  return found;
}

// Every key the markup names has to be in the dictionary, in both languages.
let checked = 0;
for (const page of PAGES) {
  const html = fs.readFileSync(`data/www/${page}`, 'utf8');
  for (const key of keysIn(html)) {
    const ru = i18n.t(key);
    i18n.setLanguage('en');
    const en = i18n.t(key);
    i18n.setLanguage('ru');
    /* A key with no entry comes back as itself, which is exactly what showed
       up on the settings page as "note.device". */
    assert.notStrictEqual(ru, key, `${page}: нет русского для ${key}`);
    assert.notStrictEqual(en, key, `${page}: нет английского для ${key}`);
    assert.ok(ru.length > 0 && en.length > 0, `${page}: пустой перевод ${key}`);
    checked += 1;
  }
}
assert.ok(checked > 50, `ожидалось больше ключей в разметке, найдено ${checked}`);

// Every key the three scripts ask for, the same way.
for (const script of ['app.js', 'playlist.js', 'settings.js']) {
  const source = fs.readFileSync(`data/www/${script}`, 'utf8');
  const pattern = /\bt\('([a-z0-9_.]+)'/g;
  let match = pattern.exec(source);
  while (match !== null) {
    const key = match[1];
    assert.notStrictEqual(i18n.t(key), key, `${script}: нет перевода для ${key}`);
    match = pattern.exec(source);
  }
}

/* Nothing in the markup is left in Russian without a key on it.
 *
 * This is the check that was missing: the first sweep for untranslated text
 * matched only text that sat on one line, so two paragraphs at the bottom of
 * the playlist page - wrapped across three lines each - were never found, and
 * shipped in Russian under an English page. The rule is simple enough to be
 * mechanical: every run of Cyrillic between tags must belong to an element
 * that carries a key.
 *
 * The exceptions are each deliberate: <title> is set from `data-i18n-title` on
 * <body>, <noscript> is shown only when no script can translate it and so
 * carries both languages, and the two language names in the picker stay in
 * their own language so a reader who cannot read the current setting can still
 * find the other. */
const ALLOWED_UNTAGGED = [
  /^jRadio — /,                    // <title>, set from the body's key
  /JavaScript/,                    // <noscript>, deliberately bilingual
  /^Русский$/,                     // the picker names itself
];

for (const page of PAGES) {
  const html = fs.readFileSync(`data/www/${page}`, 'utf8');
  const pattern = /<[^<>]*>([^<>]*?[А-Яа-яЁё][^<>]*?)</gs;
  let match = pattern.exec(html);
  while (match !== null) {
    const text = match[1].replace(/\s+/g, ' ').trim();
    const tag = match[0].slice(0, match[0].length - text.length);
    if (!tag.includes('data-i18n') &&
        !ALLOWED_UNTAGGED.some((allowed) => allowed.test(text))) {
      assert.fail(`${page}: текст без ключа — «${text.slice(0, 60)}»`);
    }
    // Overlapping runs: step back so a tag is not skipped over.
    pattern.lastIndex = match.index + 1;
    match = pattern.exec(html);
  }
}

/* The two names of the languages themselves stay put, so somebody who cannot
   read the current setting can still find the other one in the picker. */
const settings = fs.readFileSync('data/www/settings.html', 'utf8');
assert.ok(settings.includes('<option value="ru">Русский</option>'));
assert.ok(settings.includes('<option value="en">English</option>'));

/* Placeholders are filled from the values, and an unknown one is left visible
   rather than silently dropped - a sentence missing its number reads as
   finished and is not. */
i18n.define({'test.count': ['{n} штук', '{n} items']});
assert.strictEqual(i18n.t('test.count', {n: 3}), '3 штук');
i18n.setLanguage('en');
assert.strictEqual(i18n.t('test.count', {n: 3}), '3 items');
assert.strictEqual(i18n.t('test.count', {}), '{n} items');
assert.strictEqual(i18n.t('test.count'), '{n} items');
i18n.setLanguage('ru');

// An unknown language is not a reason to blank the interface.
assert.strictEqual(i18n.setLanguage('de'), false);
assert.strictEqual(i18n.language(), 'ru');

/* A browser that remembers. The page starts in whatever it saw last, so a
   reload does not show Russian for the second before the device answers and
   then swap it under the reader. */
const store = new Map([['jradio.language', 'en']]);
const remembering = {
  console,
  document: {documentElement: {lang: ''}, body: {getAttribute: () => null},
             querySelectorAll: () => []},
  window: {localStorage: {
    getItem: (key) => (store.has(key) ? store.get(key) : null),
    setItem: (key, value) => store.set(key, value),
  }},
};
vm.createContext(remembering);
vm.runInContext(fs.readFileSync('data/www/i18n.js', 'utf8'), remembering);
assert.strictEqual(remembering.window.jradioI18n.language(), 'en');
assert.strictEqual(remembering.document.documentElement.lang, 'en');
remembering.window.jradioI18n.setLanguage('ru');
assert.strictEqual(store.get('jradio.language'), 'ru');

/* The markup is walked once the document is parsed, not when the script runs.
   i18n.js is loaded from <head>, so at that moment <body> does not exist and a
   walk finds nothing. This went unseen while the language always changed a
   moment later - the change did the walk - and surfaced the day the choice was
   remembered between loads: every page then came up in the language its markup
   was written in. */
function fakeElement(attributes) {
  return {
    attributes,
    textContent: 'исходный текст',
    getAttribute: (name) => (Object.hasOwn(attributes, name) ? attributes[name] : null),
    setAttribute(name, value) { this.attributes[name] = value; },
  };
}

const label = fakeElement({'data-i18n': 'nav.settings'});
const field = fakeElement({'data-i18n-placeholder': 'list.search'});
const listeners = {};
const loading = {
  console,
  document: {
    readyState: 'loading',
    documentElement: {lang: ''},
    body: {getAttribute: () => 'title.settings'},
    title: '',
    addEventListener: (type, callback) => { listeners[type] = callback; },
    querySelectorAll: (selector) => {
      if (selector === '[data-i18n]') return [label];
      if (selector === '[data-i18n-placeholder]') return [field];
      return [];
    },
  },
  window: {},
};
vm.createContext(loading);
vm.runInContext(fs.readFileSync('data/www/i18n.js', 'utf8'), loading);

// Nothing has been touched yet: the body did not exist when the script ran.
assert.strictEqual(label.textContent, 'исходный текст');
assert.ok(typeof listeners.DOMContentLoaded === 'function',
          'словарь должен дождаться разбора разметки');

listeners.DOMContentLoaded();
assert.strictEqual(label.textContent, 'Настройки');
assert.strictEqual(field.attributes.placeholder, 'Поиск по списку');
assert.strictEqual(loading.document.title, 'jRadio — настройки');
assert.strictEqual(loading.document.documentElement.lang, 'ru');

/* And the same walk happens on a switch, so a page relabels without a
   reload. */
loading.window.jradioI18n.setLanguage('en');
assert.strictEqual(label.textContent, 'Settings');
assert.strictEqual(field.attributes.placeholder, 'Search the list');
assert.strictEqual(loading.document.title, 'jRadio — settings');

console.log('web i18n tests passed');
