'use strict';

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

/* The pure half only: with no #search-panel in the document the script stops
   after exporting its helpers, before it touches the page. */
function load() {
  const module = {exports: {}};
  const context = {
    module,
    window: {jradioI18n: {t: (key) => key}},
    document: {querySelector: () => null},
    TextEncoder,
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync('data/www/stations_search.js', 'utf8'), context,
                  {filename: 'stations_search.js'});
  return module.exports;
}

const search = load();
const limits = {nameBytes: 96, urlBytes: 256};

function station(fields) {
  return Object.assign({
    stationuuid: 'u1', name: 'Jazz FM', url: 'http://a.ru/jazz.pls',
    url_resolved: 'http://a.ru:8000/jazz', favicon: 'https://a.ru/logo.png',
    codec: 'MP3', bitrate: 128, countrycode: 'RU', tags: 'jazz,smooth jazz,lounge,chill',
    votes: 10,
  }, fields);
}

// fitBytes: under the device's limit, cut between characters.
{
  assert.strictEqual(search.fitBytes('abc', 96), 'abc');
  // Cyrillic is two bytes a letter: 5 letters are 10 bytes, so 9 keep four.
  assert.strictEqual(search.fitBytes('Радио', 9), 'Ради');
  assert.strictEqual(search.fitBytes('Радио', 11), 'Радио');
  // Exactly the limit is refused on the device, so it is cut here.
  assert.strictEqual(search.fitBytes('abcd', 4), 'abc');
  assert.strictEqual(new TextEncoder().encode(search.fitBytes('я'.repeat(80), 96)).length, 94);
}

// normalizeStation: what the page needs, and nothing it cannot use.
{
  const found = search.normalizeStation(station({}), limits);
  assert.strictEqual(found.url, 'http://a.ru:8000/jazz', 'the resolved stream, not the .pls');
  assert.strictEqual(found.name, 'Jazz FM');
  assert.deepStrictEqual(Array.from(found.tags), ['jazz', 'smooth jazz', 'lounge']);
  assert.strictEqual(found.fits, true);

  assert.strictEqual(search.normalizeStation(station({url_resolved: '', url: 'http://b.ru/x'}), limits).url,
                     'http://b.ru/x');
  assert.strictEqual(search.normalizeStation(station({name: '  '}), limits), null);
  assert.strictEqual(search.normalizeStation(station({url_resolved: 'rtmp://x/y', url: ''}), limits), null);
  assert.strictEqual(search.normalizeStation(null, limits), null);
  // A tab would split the playlist line; it becomes a space.
  assert.strictEqual(search.normalizeStation(station({name: 'A\tB\n'}), limits).name, 'A B');
  assert.strictEqual(search.normalizeStation(station({favicon: 'data:image/png;base64,AA'}), limits).favicon, '');
  assert.strictEqual(search.normalizeStation(station({favicon: null}), limits).favicon, '');
  const long = search.normalizeStation(station({url_resolved: 'http://a.ru/' + 'x'.repeat(250)}), limits);
  assert.strictEqual(long.fits, false, 'an address the device cannot store');
}

// mergeResults: one copy of each station, most voted first.
{
  const byName = [station({stationuuid: 'a', votes: 5}), station({stationuuid: 'b', votes: 50,
    url_resolved: 'http://b.ru/s'})];
  const byTag = [station({stationuuid: 'b', votes: 50, url_resolved: 'http://b.ru/s'}),
    station({stationuuid: 'c', votes: 20, url_resolved: 'http://a.ru:8000/jazz'}),
    {name: 'broken'}];
  const merged = search.mergeResults([byName, byTag], limits);
  assert.deepStrictEqual(Array.from(merged, (item) => item.uuid), ['b', 'a'],
                         'c repeats a\'s stream and is dropped; the broken one too');
  assert.deepStrictEqual(Array.from(search.mergeResults([null, undefined], limits)), []);
}

// usableOnly: what the radio cannot store is left out, and counted. Ogg stays:
// the radio plays Vorbis and Opus both.
{
  const stations = search.mergeResults([[station({stationuuid: 'mp3'}),
    station({stationuuid: 'ogg', codec: 'OGG', url_resolved: 'http://b.ru/o'}),
    station({stationuuid: 'long', url_resolved: 'http://c.ru/' + 'x'.repeat(250)})]], limits);
  const {usable, hidden} = search.usableOnly(stations);
  assert.deepStrictEqual(Array.from(usable, (item) => item.uuid), ['mp3', 'ogg']);
  assert.strictEqual(hidden, 1);
}

// normalizeName: case and punctuation aside, any script.
{
  assert.strictEqual(search.normalizeName('Radio Jazz 89.1'), 'radiojazz891');
  assert.strictEqual(search.normalizeName('«Радио Джаз»'), 'радиоджаз');
}

// pickIcon: the stream address first, then a name that agrees, then a guess.
{
  const row = {name: 'Радио Джаз', url: 'http://a.ru/jazz'};
  const withIcon = (name, favicon) => ({name, favicon});
  // The answer is an object of the script's own context; compared as a copy.
  const plain = (value) => ({...value});
  assert.deepStrictEqual(plain(search.pickIcon(row, [withIcon('X', ''), withIcon('Jazz', 'https://u/1.png')], [])),
                         {favicon: 'https://u/1.png', name: 'Jazz', how: 'url'});
  assert.deepStrictEqual(
    plain(search.pickIcon(row, [], [withIcon('Jazz Radio', 'https://u/2.png'),
                                    withIcon('радио джаз', 'https://u/3.png')])),
    {favicon: 'https://u/3.png', name: 'радио джаз', how: 'name'});
  assert.deepStrictEqual(plain(search.pickIcon(row, [], [withIcon('Jazz Radio', 'https://u/2.png')])),
                         {favicon: 'https://u/2.png', name: 'Jazz Radio', how: 'guess'});
  assert.strictEqual(search.pickIcon(row, [withIcon('A', '')], [withIcon('B', '')]), null);
  // A row with no name matches nothing by name, however empty the other is.
  assert.strictEqual(search.pickIcon({name: '...', url: ''}, [], [withIcon('!!!', 'https://u/4.png')]).how,
                     'guess');
}

console.log('web station search tests passed');
