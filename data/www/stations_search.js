/* Finding stations, and pictures for the ones already in the list, in Radio
   Browser's open catalogue - asked directly (radio-browser.info) or through
   jRadio's own copy of it on GitHub Pages (github.com/jmper-ha/jradio-stations).

   Two sources because of where the radios are. The catalogue's API answers
   from one German host, and from Russia that host is sometimes not reachable
   at all: three Russian nodes got 502 from it on 2026-10-11, the day after
   most requests got through. GitHub Pages was reachable from all three. The
   copy is rebuilt once a day, so it is at most a day behind, and it carries
   the pictures of the stations most likely to be looked for already scaled.
   Whichever source is chosen, the other is asked when it does not answer.

   Asked from this browser, not from the device: both answer any page, the
   phone or the computer has the time and the memory for a search, and the
   device has neither to spare while it plays. What the browser cannot do is
   read a picture off a station's own site - most of them do not allow it, so
   a canvas holding one is closed to the page - and that is what the device's
   /api/icon-fetch is for, when the copy has no picture of its own.

   The list itself is playlist.js's; this file only adds to it through
   window.jradioStations.radio, and nothing reaches the device before the list
   is saved. */
(() => {
  'use strict';

  const t = (key, values) => window.jradioI18n.t(key, values);

  const MIRROR = 'https://jmper-ha.github.io/jradio-stations';
  /* The catalogue lists its own mirrors. Asked once; if that fails too, the
     names it has had for years. */
  const SERVER_LIST = 'https://all.api.radio-browser.info/json/servers';
  const FALLBACK_SERVERS = ['de1.api.radio-browser.info', 'all.api.radio-browser.info'];
  const REQUEST_TIMEOUT_MS = 10000;
  // The copy's file for "any country" is 2.9 MB compressed.
  const MIRROR_TIMEOUT_MS = 60000;
  /* Three tries, across the catalogue's mirrors: from Russia without a VPN a
     request to its German host is often reset on the way, and a second try
     sometimes gets through. */
  const ATTEMPTS = 3;
  const RESULT_LIMIT = 40;
  // Countries with fewer stations than this make the list longer, not better.
  const COUNTRY_MIN_STATIONS = 20;
  const SOURCE_KEY = 'jradio.stationSource';

  const encoder = new TextEncoder();
  const byteLength = (value) => encoder.encode(value).length;

  /* The longest prefix of `text` that is under `limit` bytes, cut between
     characters: the device refuses a name of `limit` bytes or more outright. */
  function fitBytes(text, limit) {
    if (byteLength(text) < limit) return text;
    let out = '';
    let used = 0;
    for (const character of text) {
      const size = byteLength(character);
      if (used + size >= limit) break;
      out += character;
      used += size;
    }
    return out.trimEnd();
  }

  const isWebAddress = (value) => /^https?:\/\/[^\s/?#]/i.test(value);

  /* One station out of the catalogue's answer, in the shape this page uses, or
     null when there is nothing to add: no name, or no address the device can
     open. `limits` are the device's field sizes, from playlist.js.

     `picture` is where its picture is to be had and `direct` whether this page
     may read it itself; from the catalogue it is the station's own site, so
     the device fetches it. */
  function normalizeStation(raw, limits) {
    if (raw === null || typeof raw !== 'object') return null;
    const name = String(raw.name || '').replace(/[\t\r\n]+/g, ' ').trim();
    // url_resolved is the stream behind a .pls or .m3u; the device wants that.
    const url = String(raw.url_resolved || raw.url || '').trim();
    if (name === '' || !isWebAddress(url)) return null;
    const favicon = String(raw.favicon || '').trim();
    return {
      uuid: String(raw.stationuuid || url),
      name: fitBytes(name, limits.nameBytes),
      url,
      fits: !url.includes('\t') && byteLength(url) < limits.urlBytes,
      picture: isWebAddress(favicon) ? favicon : '',
      direct: false,
      codec: String(raw.codec || '').toUpperCase(),
      bitrate: Number.isFinite(Number(raw.bitrate)) ? Number(raw.bitrate) : 0,
      country: String(raw.countrycode || ''),
      tags: String(raw.tags || '').split(',').map((tag) => tag.trim()).filter(Boolean).slice(0, 3),
      votes: Number(raw.votes) || 0,
      live: true,
    };
  }

  /* One station out of the copy: [name, url, codec, bitrate, country, tags,
     votes, favicon, icon] - see the copy's README. Its own picture, where it
     has one, the page reads itself; otherwise the station's site, through the
     device, as for the catalogue. */
  function mirrorStation(item, base, limits) {
    if (!Array.isArray(item) || item.length < 9) return null;
    const [name, url, codec, bitrate, country, tags, votes, favicon, icon] = item;
    if (typeof name !== 'string' || name === '' || !isWebAddress(String(url))) return null;
    const own = typeof icon === 'string' && /^[0-9a-f]+\.(png|jpg)$/.test(icon);
    return {
      uuid: url,
      name: fitBytes(name, limits.nameBytes),
      url,
      fits: !url.includes('\t') && byteLength(url) < limits.urlBytes,
      picture: own ? `${base}/icons/${icon}` : (isWebAddress(String(favicon)) ? favicon : ''),
      direct: own,
      codec: String(codec || '').toUpperCase(),
      bitrate: Number(bitrate) || 0,
      country: String(country || ''),
      tags: String(tags || '').split(',').filter(Boolean).slice(0, 3),
      votes: Number(votes) || 0,
      live: false,
    };
  }

  /* The name search and the genre search answer separately and often with the
     same stations; one copy of each, most voted first. */
  function mergeResults(lists, limits) {
    const seen = new Set();
    const merged = [];
    for (const list of lists) {
      for (const raw of Array.isArray(list) ? list : []) {
        const station = normalizeStation(raw, limits);
        if (station === null || seen.has(station.uuid) || seen.has(station.url)) continue;
        seen.add(station.uuid);
        seen.add(station.url);
        merged.push(station);
      }
    }
    return merged.sort((a, b) => b.votes - a.votes);
  }

  /* The copy has no search of its own - it is files - so it is searched here:
     the query in the name or among the genres, as the catalogue's two
     searches together would find it. The list comes most voted first and
     stays so. */
  function searchMirror(items, query, limit, base, limits) {
    const wanted = query.trim().toLowerCase();
    const out = [];
    for (const item of Array.isArray(items) ? items : []) {
      if (wanted !== '' && !(String(item[0]).toLowerCase().includes(wanted) ||
                             String(item[5]).split(',').includes(wanted))) {
        continue;
      }
      const station = mirrorStation(item, base, limits);
      if (station === null) continue;
      out.push(station);
      if (out.length >= limit) break;
    }
    return out;
  }

  /* Only what the radio can store: an address past its 255 bytes was listed
     with a warning at first, and a list of things one cannot add is only
     noise; how many were left out is said instead. Every codec the catalogue
     names is played - Ogg Vorbis too, since cb5c4e8; this page once hid the
     catalogue's OGG on a comment that had outlived that commit, and the OGG
     there is often Opus anyway. */
  function usableOnly(stations) {
    const usable = stations.filter((station) => station.fits);
    return {usable, hidden: stations.length - usable.length};
  }

  // Case, spaces and punctuation aside: "Radio Jazz 89.1" is "radiojazz891".
  const normalizeName = (name) => String(name).toLowerCase().replace(/[^\p{L}\p{N}]+/gu, '');

  /* What the copy knows of a station in the list: the one with its stream
     address, and the most voted ten whose names contain its name - the
     catalogue's own name search, done here. */
  function mirrorLookup(stations, row) {
    const byUrl = stations.filter((station) => station.url === row.url);
    const wanted = normalizeName(row.name);
    const byName = wanted === '' ? [] : stations
      .filter((station) => normalizeName(station.name).includes(wanted)).slice(0, 10);
    return {byUrl, byName};
  }

  /* A picture for a station already in the list. The stream address is the
     catalogue's own key, so a station found by it is that station; one found
     by name is that station when the names agree, and only a guess when they
     do not - which the page leaves unticked. */
  function pickIcon(row, byUrl, byName) {
    const found = (station, how) =>
      ({picture: station.picture, direct: station.direct, name: station.name, how});
    const exact = byUrl.find((station) => station.picture !== '');
    if (exact) return found(exact, 'url');
    const wanted = normalizeName(row.name);
    const same = byName.find((station) =>
      station.picture !== '' && wanted !== '' && normalizeName(station.name) === wanted);
    if (same) return found(same, 'name');
    const guess = byName.find((station) => station.picture !== '');
    if (guess) return found(guess, 'guess');
    return null;
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {fitBytes, normalizeStation, mirrorStation, mergeResults, searchMirror,
                      usableOnly, normalizeName, mirrorLookup, pickIcon};
  }

  const searchPanel = document.querySelector('#search-panel');
  const radio = window.jradioStations && window.jradioStations.radio;
  if (!searchPanel || !radio) return;

  const searchOpen = document.querySelector('#search-open');
  const searchForm = document.querySelector('#search-form');
  const searchQuery = document.querySelector('#search-query');
  const searchCountry = document.querySelector('#search-country');
  const searchSource = document.querySelector('#search-source');
  const searchGo = document.querySelector('#search-go');
  const searchStatus = document.querySelector('#search-status');
  const searchResults = document.querySelector('#search-results');
  const iconsOpen = document.querySelector('#icons-open');
  const iconsPanel = document.querySelector('#icons-panel');
  const iconsStatus = document.querySelector('#icons-status');
  const iconsResults = document.querySelector('#icons-results');
  const iconsApply = document.querySelector('#icons-apply');
  const limits = {nameBytes: radio.nameBytes, urlBytes: radio.urlBytes};

  function setText(element, text, isError = false) {
    element.textContent = text;
    element.classList.toggle('is-error', isError);
  }

  function fetchJson(url, timeout = REQUEST_TIMEOUT_MS, cache = 'no-store') {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);
    return fetch(url, {signal: controller.signal, cache})
      .then((response) => {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json();
      })
      .finally(() => clearTimeout(timer));
  }

  /* --- the catalogue, directly ------------------------------------------- */

  let servers = null;

  async function serverNames() {
    if (servers !== null) return servers;
    try {
      const list = await fetchJson(SERVER_LIST);
      const names = [...new Set(list.map((entry) => entry && entry.name).filter(Boolean))];
      servers = names.length > 0 ? names : FALLBACK_SERVERS;
    } catch (error) {
      servers = FALLBACK_SERVERS;
    }
    // Spread over the mirrors rather than all on the first one.
    servers = servers.slice().sort(() => Math.random() - 0.5);
    return servers;
  }

  async function catalogue(path) {
    const names = await serverNames();
    let lastError = null;
    for (let attempt = 0; attempt < ATTEMPTS; attempt += 1) {
      try {
        return await fetchJson(`https://${names[attempt % names.length]}${path}`);
      } catch (error) {
        lastError = error;
      }
    }
    throw lastError || new Error('no answer');
  }

  /* --- the copy ------------------------------------------------------------ */

  /* Files, kept for as long as the page is open: the copy changes once a day,
     and a second search of the same country should not fetch it again. The
     browser's own cache keeps them between visits. */
  const mirrorFiles = new Map();
  function mirrorFile(path) {
    if (!mirrorFiles.has(path)) {
      const loading = fetchJson(`${MIRROR}/${path}`, MIRROR_TIMEOUT_MS, 'default');
      loading.catch(() => mirrorFiles.delete(path));
      mirrorFiles.set(path, loading);
    }
    return mirrorFiles.get(path);
  }

  /* --- the two, behind one face ------------------------------------------ */

  const sources = {
    mirror: {
      async countries() {
        return (await mirrorFile('countries.json'))
          .map(([code, count]) => ({code, count}));
      },
      async search(query, country) {
        const items = await mirrorFile(`stations/${country || 'all'}.json`);
        return searchMirror(items, query, RESULT_LIMIT * 2, MIRROR, limits);
      },
      async lookup(row) {
        const all = await mirrorFile('stations/all.json');
        if (!this.stations) {
          this.stations = all.map((item) => mirrorStation(item, MIRROR, limits)).filter(Boolean);
        }
        return mirrorLookup(this.stations, row);
      },
    },
    live: {
      async countries() {
        return (await catalogue('/json/countries?hidebroken=true'))
          .map((country) => ({code: country.iso_3166_1, count: Number(country.stationcount)}));
      },
      async search(query, country) {
        const common = `limit=${RESULT_LIMIT}&hidebroken=true&order=votes&reverse=true` +
          (country !== '' ? `&countrycode=${encodeURIComponent(country)}` : '');
        /* By name and by genre at once: "jazz" is a genre to one station and
           part of the name of another, and a person typing it means both. */
        const lists = query === ''
          ? [await catalogue(`/json/stations/search?${common}`)]
          : await Promise.all([
            catalogue(`/json/stations/search?name=${encodeURIComponent(query)}&${common}`),
            catalogue(`/json/stations/search?tag=${encodeURIComponent(query.toLowerCase())}&${common}`),
          ]);
        return mergeResults(lists, limits);
      },
      async lookup(row) {
        const byUrl = mergeResults(
          [await catalogue(`/json/stations/byurl?url=${encodeURIComponent(row.url)}`)], limits);
        const byName = byUrl.some((station) => station.picture !== '') ? [] : mergeResults(
          [await catalogue(`/json/stations/search?name=${encodeURIComponent(row.name)}` +
                           '&limit=10&hidebroken=true&order=votes&reverse=true')], limits);
        return {byUrl, byName};
      },
    },
  };

  function chosenSource() {
    return searchSource && searchSource.value === 'live' ? 'live' : 'mirror';
  }

  /* The chosen source, and the other when it does not answer. Says which one
     answered, so the page can tell the reader it was not the one chosen. */
  async function ask(what, ...args) {
    const first = chosenSource();
    const second = first === 'mirror' ? 'live' : 'mirror';
    try {
      return {value: await sources[first][what](...args), source: first};
    } catch (error) {
      return {value: await sources[second][what](...args), source: second};
    }
  }

  const sourceName = (source) => t(source === 'live' ? 'search.source_live_short' : 'search.source_mirror_short');

  /* --- pictures ------------------------------------------------------------ */

  /* The device fetches one picture at a time, and does not keep the request
     open while it does: 202 means "still at it, ask again". A 1.37 MB favicon
     took fourteen seconds on a playing board before the device stopped taking
     pictures that large; the patience is for a slow site, not a big file. */
  const DEVICE_POLL_MS = 500;
  const DEVICE_PATIENCE_MS = 60000;
  let deviceQueue = Promise.resolve();
  async function askDevice(url) {
    const started = Date.now();
    const address = `/api/icon-fetch?url=${encodeURIComponent(url)}`;
    while (Date.now() - started < DEVICE_PATIENCE_MS) {
      const response = await fetch(address, {cache: 'no-store'});
      if (response.status === 200) return response.blob();
      // 409: someone else's fetch - another tab's - is still running.
      if (response.status !== 202 && response.status !== 409) {
        throw new Error(`HTTP ${response.status}`);
      }
      await new Promise((resolve) => setTimeout(resolve, DEVICE_POLL_MS));
    }
    throw new Error('timeout');
  }
  function pictureViaDevice(url) {
    const job = deviceQueue.then(() => askDevice(url));
    deviceQueue = job.catch(() => {});
    return job;
  }

  /* The copy's own pictures straight from it; anything else through the
     device. */
  function pictureOf(found) {
    if (!found.direct) return pictureViaDevice(found.picture);
    return fetch(found.picture).then((response) => {
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      return response.blob();
    });
  }

  /* --- search ------------------------------------------------------------ */

  let countriesFor = '';
  async function loadCountries() {
    const wanted = chosenSource();
    if (countriesFor === wanted) return;
    countriesFor = wanted;
    const keep = searchCountry.value;
    // Everything but the first option, "any country".
    while (searchCountry.options && searchCountry.options.length > 1) searchCountry.remove(1);
    try {
      const {value: list} = await ask('countries');
      const language = window.jradioI18n.language ? window.jradioI18n.language() : 'ru';
      let names = null;
      try {
        names = new Intl.DisplayNames([language], {type: 'region'});
      } catch (error) {
        names = null;
      }
      const options = list
        .filter((country) => /^[A-Z]{2}$/.test(country.code) && country.count >= COUNTRY_MIN_STATIONS)
        .map((country) => ({code: country.code, label: (names && names.of(country.code)) || country.code}))
        .sort((a, b) => a.label.localeCompare(b.label, language));
      for (const country of options) {
        const option = document.createElement('option');
        option.value = country.code;
        option.textContent = country.label;
        searchCountry.appendChild(option);
      }
      searchCountry.value = keep;
      if (searchCountry.value !== keep) searchCountry.value = '';
    } catch (error) {
      // Searching without a country still works; the list stays at "any".
      countriesFor = '';
    }
  }

  const preview = {audio: null, uuid: ''};

  function stopPreview() {
    if (preview.audio !== null) {
      preview.audio.pause();
      preview.audio.removeAttribute('src');
      preview.audio.load();
    }
    preview.uuid = '';
    searchResults.querySelectorAll('.search-result-play').forEach((button) => {
      button.textContent = '▶';
      button.setAttribute('aria-label', t('search.listen'));
    });
  }

  function togglePreview(station, button) {
    const playing = preview.uuid === station.uuid;
    stopPreview();
    if (playing) return;
    if (preview.audio === null) preview.audio = new Audio();
    preview.audio.src = station.url;
    preview.uuid = station.uuid;
    button.textContent = '■';
    button.setAttribute('aria-label', t('playlist.stop'));
    preview.audio.play().catch(() => {
      stopPreview();
      setText(searchStatus, t('search.browser_cannot', {name: station.name}), true);
    });
  }

  function thumbnail(url) {
    const image = document.createElement('img');
    image.className = 'search-result-thumb';
    image.alt = '';
    image.loading = 'lazy';
    image.referrerPolicy = 'no-referrer';
    if (url !== '') {
      image.src = url;
      image.addEventListener('error', () => { image.hidden = true; });
    } else {
      image.hidden = true;
    }
    return image;
  }

  function describe(station) {
    const parts = [];
    if (station.codec !== '' && station.codec !== 'UNKNOWN') {
      parts.push(station.bitrate > 0 ? `${station.codec} ${station.bitrate}` : station.codec);
    }
    if (station.country !== '') parts.push(station.country);
    if (station.tags.length > 0) parts.push(station.tags.join(', '));
    return parts.join(' · ');
  }

  async function addStation(station, button) {
    if (!radio.loaded()) {
      setText(searchStatus, t('search.list_not_loaded'), true);
      return;
    }
    if (radio.has(station.url)) {
      setText(searchStatus, t('search.already', {name: station.name}));
      return;
    }
    const id = radio.add(station.name, station.url);
    if (id === 0) return;
    button.disabled = true;
    button.textContent = t('search.added_button');
    // The catalogue counts what people pick; the request is all it asks.
    if (station.live) {
      serverNames().then((names) =>
        fetch(`https://${names[0]}/json/url/${encodeURIComponent(station.uuid)}`))
        .catch(() => {});
    }
    if (station.picture === '') {
      setText(searchStatus, t('search.added', {name: station.name}));
      return;
    }
    setText(searchStatus, t('search.added_fetching', {name: station.name}));
    try {
      await radio.setPicture(id, await pictureOf(station));
      setText(searchStatus, t('search.added', {name: station.name}));
    } catch (error) {
      const tooLarge = error && error.message === 'HTTP 413';
      setText(searchStatus, t(tooLarge ? 'search.added_icon_too_large' : 'search.added_no_icon',
                              {name: station.name}));
    }
  }

  function renderResults(stations) {
    stopPreview();
    searchResults.replaceChildren(...stations.map((station) => {
      const item = document.createElement('li');
      item.className = 'search-result';
      const text = document.createElement('div');
      text.className = 'search-result-text';
      const name = document.createElement('span');
      name.className = 'search-result-name';
      name.textContent = station.name;
      const meta = document.createElement('span');
      meta.className = 'search-result-meta';
      meta.textContent = describe(station);
      text.append(name, meta);
      const play = document.createElement('button');
      play.type = 'button';
      play.className = 'icon-button search-result-play';
      play.textContent = '▶';
      play.setAttribute('aria-label', t('search.listen'));
      play.addEventListener('click', () => togglePreview(station, play));
      const add = document.createElement('button');
      add.type = 'button';
      add.className = 'secondary-button search-result-add';
      const known = radio.has(station.url);
      add.textContent = known ? t('search.added_button') : t('search.add');
      add.disabled = known;
      add.addEventListener('click', () => { addStation(station, add); });
      item.append(thumbnail(station.picture), text, play, add);
      return item;
    }));
  }

  async function runSearch() {
    const query = searchQuery.value.trim();
    const country = searchCountry.value;
    if (query === '' && country === '') {
      setText(searchStatus, t('search.need_query'), true);
      return;
    }
    searchGo.disabled = true;
    setText(searchStatus, t('search.searching'));
    try {
      const {value, source} = await ask('search', query, country);
      const {usable: stations, hidden} = usableOnly(value);
      renderResults(stations);
      let text = stations.length > 0 ? t('search.found', {n: stations.length}) : t('search.nothing');
      if (hidden > 0) text += ` ${t('search.hidden', {n: hidden})}`;
      if (source !== chosenSource()) {
        text += ` ${t('search.fell_back', {source: sourceName(chosenSource()), other: sourceName(source)})}`;
      }
      setText(searchStatus, text);
    } catch (error) {
      setText(searchStatus, t('search.failed'), true);
    } finally {
      searchGo.disabled = false;
    }
  }

  /* --- pictures for the list --------------------------------------------- */

  let iconCandidates = [];
  let iconsRunning = false;

  function updateApply() {
    iconsApply.disabled = iconsRunning || !iconCandidates.some((candidate) => candidate.checked);
  }

  function renderCandidates() {
    iconsResults.replaceChildren(...iconCandidates.map((candidate) => {
      const item = document.createElement('li');
      item.className = 'search-result';
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.checked = candidate.checked;
      box.setAttribute('aria-label', candidate.row.name);
      box.addEventListener('change', () => {
        candidate.checked = box.checked;
        updateApply();
      });
      const text = document.createElement('div');
      text.className = 'search-result-text';
      const name = document.createElement('span');
      name.className = 'search-result-name';
      name.textContent = candidate.row.name;
      const meta = document.createElement('span');
      meta.className = 'search-result-meta';
      meta.textContent = t(`icons.how_${candidate.match.how}`, {name: candidate.match.name});
      text.append(name, meta);
      item.append(box, thumbnail(candidate.match.picture), text);
      return item;
    }));
    updateApply();
  }

  async function findIcons() {
    if (iconsRunning) return;
    if (!radio.loaded()) {
      setText(iconsStatus, t('search.list_not_loaded'), true);
      return;
    }
    const rows = radio.withoutPicture();
    iconCandidates = [];
    renderCandidates();
    if (rows.length === 0) {
      setText(iconsStatus, t('icons.all_have'));
      return;
    }
    iconsRunning = true;
    updateApply();
    let failed = 0;
    for (const [index, row] of rows.entries()) {
      setText(iconsStatus, t('icons.looking', {k: index + 1, n: rows.length}));
      try {
        const {value: {byUrl, byName}} = await ask('lookup', row);
        const match = pickIcon(row, byUrl, byName);
        if (match !== null) {
          iconCandidates.push({row, match, checked: match.how !== 'guess'});
          renderCandidates();
        }
      } catch (error) {
        failed += 1;
      }
    }
    iconsRunning = false;
    const summary = t('icons.found', {n: iconCandidates.length, total: rows.length});
    setText(iconsStatus, failed > 0 ? `${summary} ${t('icons.some_failed', {n: failed})}` : summary,
            failed > 0 && iconCandidates.length === 0);
    updateApply();
  }

  async function applyIcons() {
    const chosen = iconCandidates.filter((candidate) => candidate.checked);
    if (chosen.length === 0 || iconsRunning) return;
    iconsRunning = true;
    updateApply();
    let done = 0;
    let failed = 0;
    for (const [index, candidate] of chosen.entries()) {
      setText(iconsStatus, t('icons.fetching', {k: index + 1, n: chosen.length}));
      try {
        await radio.setPicture(candidate.row.id, await pictureOf(candidate.match));
        done += 1;
      } catch (error) {
        failed += 1;
      }
    }
    iconsRunning = false;
    // What is left is what did not take; the rest now has a picture.
    iconCandidates = iconCandidates.filter((candidate) => !candidate.checked);
    renderCandidates();
    setText(iconsStatus, failed > 0
      ? t('icons.applied_some', {n: done, failed})
      : t('icons.applied', {n: done}), failed > 0 && done === 0);
  }

  /* --- the two panels ---------------------------------------------------- */

  function setPanel(button, panel, open) {
    panel.hidden = !open;
    button.setAttribute('aria-expanded', String(open));
    button.classList.toggle('is-active', open);
  }

  // The choice of source is this browser's, not the device's.
  try {
    const remembered = window.localStorage.getItem(SOURCE_KEY);
    if (searchSource && (remembered === 'live' || remembered === 'mirror')) searchSource.value = remembered;
  } catch (error) {
    // No storage, as in a private window: the copy, as by default.
  }
  if (searchSource) {
    searchSource.addEventListener('change', () => {
      try {
        window.localStorage.setItem(SOURCE_KEY, chosenSource());
      } catch (error) {
        // Remembered for this page only.
      }
      loadCountries();
    });
  }

  searchOpen.addEventListener('click', () => {
    const open = searchPanel.hidden;
    setPanel(searchOpen, searchPanel, open);
    if (open) {
      setPanel(iconsOpen, iconsPanel, false);
      loadCountries();
      searchQuery.focus();
    } else {
      stopPreview();
    }
  });
  iconsOpen.addEventListener('click', () => {
    const open = iconsPanel.hidden;
    setPanel(iconsOpen, iconsPanel, open);
    if (open) {
      setPanel(searchOpen, searchPanel, false);
      stopPreview();
      findIcons();
    }
  });
  document.querySelectorAll('.search-close').forEach((button) => {
    button.addEventListener('click', () => {
      if (button.dataset.close === 'search') {
        setPanel(searchOpen, searchPanel, false);
        stopPreview();
        searchOpen.focus();
      } else {
        setPanel(iconsOpen, iconsPanel, false);
        iconsOpen.focus();
      }
    });
  });
  searchForm.addEventListener('submit', (event) => {
    event.preventDefault();
    runSearch();
  });
  iconsApply.addEventListener('click', applyIcons);
})();
