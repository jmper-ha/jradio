/* Finding stations, and pictures for the ones already in the list, in Radio
   Browser's open catalogue (radio-browser.info).

   Asked from this browser, not from the device: the catalogue answers any page
   (Access-Control-Allow-Origin: *), the phone or the computer has the time and
   the memory for a search, and the device has neither to spare while it plays.
   The one thing the browser cannot do is read a picture off a station's own
   site - most of them do not allow it, so a canvas holding one is closed to the
   page - and that is what the device's /api/icon-fetch is for.

   The list itself is playlist.js's; this file only adds to it through
   window.jradioStations.radio, and nothing reaches the device before the list
   is saved. */
(() => {
  'use strict';

  const t = (key, values) => window.jradioI18n.t(key, values);

  /* The catalogue lists its own mirrors. Asked once; if that fails too, the
     names it has had for years. */
  const SERVER_LIST = 'https://all.api.radio-browser.info/json/servers';
  const FALLBACK_SERVERS = ['de1.api.radio-browser.info', 'all.api.radio-browser.info'];
  const REQUEST_TIMEOUT_MS = 10000;
  /* Three tries, across the mirrors: from Russia without a VPN about one
     request in ten to the catalogue's German host is reset on the way
     (measured from three Russian nodes, 2026-10-10), and a second try mostly
     gets through. */
  const ATTEMPTS = 3;
  const RESULT_LIMIT = 40;
  // Countries with fewer stations than this make the list longer, not better.
  const COUNTRY_MIN_STATIONS = 20;

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
     open. `limits` are the device's field sizes, from playlist.js. */
  function normalizeStation(raw, limits) {
    if (raw === null || typeof raw !== 'object') return null;
    const name = String(raw.name || '').replace(/[\t\r\n]+/g, ' ').trim();
    // url_resolved is the stream behind a .pls or .m3u; the device wants that.
    const url = String(raw.url_resolved || raw.url || '').trim();
    if (name === '' || !isWebAddress(url)) return null;
    const codec = String(raw.codec || '').toUpperCase();
    const favicon = String(raw.favicon || '').trim();
    return {
      uuid: String(raw.stationuuid || url),
      name: fitBytes(name, limits.nameBytes),
      url,
      // An address the device cannot store is shown, but cannot be added.
      fits: !url.includes('\t') && byteLength(url) < limits.urlBytes,
      favicon: isWebAddress(favicon) ? favicon : '',
      codec,
      bitrate: Number.isFinite(Number(raw.bitrate)) ? Number(raw.bitrate) : 0,
      country: String(raw.countrycode || ''),
      tags: String(raw.tags || '').split(',').map((tag) => tag.trim()).filter(Boolean).slice(0, 3),
      votes: Number(raw.votes) || 0,
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

  /* A picture for a station already in the list. The stream address is the
     catalogue's own key, so a station found by it is that station; one found
     by name is that station when the names agree, and only a guess when they
     do not - which the page leaves unticked. */
  function pickIcon(row, byUrl, byName) {
    const exact = byUrl.find((station) => station.favicon !== '');
    if (exact) return {favicon: exact.favicon, name: exact.name, how: 'url'};
    const wanted = normalizeName(row.name);
    const same = byName.find((station) =>
      station.favicon !== '' && wanted !== '' && normalizeName(station.name) === wanted);
    if (same) return {favicon: same.favicon, name: same.name, how: 'name'};
    const guess = byName.find((station) => station.favicon !== '');
    if (guess) return {favicon: guess.favicon, name: guess.name, how: 'guess'};
    return null;
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {fitBytes, normalizeStation, mergeResults, usableOnly, normalizeName, pickIcon};
  }

  const searchPanel = document.querySelector('#search-panel');
  const radio = window.jradioStations && window.jradioStations.radio;
  if (!searchPanel || !radio) return;

  const searchOpen = document.querySelector('#search-open');
  const searchForm = document.querySelector('#search-form');
  const searchQuery = document.querySelector('#search-query');
  const searchCountry = document.querySelector('#search-country');
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

  /* --- the catalogue ---------------------------------------------------- */

  let servers = null;

  function fetchJson(url) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
    return fetch(url, {signal: controller.signal, cache: 'no-store'})
      .then((response) => {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json();
      })
      .finally(() => clearTimeout(timer));
  }

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

  /* --- search ------------------------------------------------------------ */

  let countriesLoaded = false;
  async function loadCountries() {
    if (countriesLoaded) return;
    countriesLoaded = true;
    try {
      const list = await catalogue('/json/countries?hidebroken=true');
      const language = window.jradioI18n.language ? window.jradioI18n.language() : 'ru';
      let names = null;
      try {
        names = new Intl.DisplayNames([language], {type: 'region'});
      } catch (error) {
        names = null;
      }
      const options = list
        .filter((country) => country && /^[A-Z]{2}$/.test(country.iso_3166_1) &&
          Number(country.stationcount) >= COUNTRY_MIN_STATIONS)
        .map((country) => ({
          code: country.iso_3166_1,
          label: (names && names.of(country.iso_3166_1)) || country.name,
        }))
        .sort((a, b) => a.label.localeCompare(b.label, language));
      for (const country of options) {
        const option = document.createElement('option');
        option.value = country.code;
        option.textContent = country.label;
        searchCountry.appendChild(option);
      }
    } catch (error) {
      // Searching without a country still works; the list stays at "any".
      countriesLoaded = false;
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
    fetch(`https://${(await serverNames())[0]}/json/url/${encodeURIComponent(station.uuid)}`)
      .catch(() => {});
    if (station.favicon === '') {
      setText(searchStatus, t('search.added', {name: station.name}));
      return;
    }
    setText(searchStatus, t('search.added_fetching', {name: station.name}));
    try {
      await radio.setPicture(id, await pictureViaDevice(station.favicon));
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
      item.append(thumbnail(station.favicon), text, play, add);
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
    const common = `limit=${RESULT_LIMIT}&hidebroken=true&order=votes&reverse=true` +
      (country !== '' ? `&countrycode=${encodeURIComponent(country)}` : '');
    searchGo.disabled = true;
    setText(searchStatus, t('search.searching'));
    try {
      /* By name and by genre at once: "jazz" is a genre to one station and
         part of the name of another, and a person typing it means both. */
      const lists = query === ''
        ? [await catalogue(`/json/stations/search?${common}`)]
        : await Promise.all([
          catalogue(`/json/stations/search?name=${encodeURIComponent(query)}&${common}`),
          catalogue(`/json/stations/search?tag=${encodeURIComponent(query.toLowerCase())}&${common}`),
        ]);
      const {usable: stations, hidden} = usableOnly(mergeResults(lists, limits));
      renderResults(stations);
      const found = stations.length > 0
        ? t('search.found', {n: stations.length})
        : t('search.nothing');
      setText(searchStatus, hidden > 0 ? `${found} ${t('search.hidden', {n: hidden})}` : found);
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
      item.append(box, thumbnail(candidate.match.favicon), text);
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
        const byUrl = mergeResults(
          [await catalogue(`/json/stations/byurl?url=${encodeURIComponent(row.url)}`)], limits);
        const byName = byUrl.some((station) => station.favicon !== '') ? [] : mergeResults(
          [await catalogue(`/json/stations/search?name=${encodeURIComponent(row.name)}` +
                           '&limit=10&hidebroken=true&order=votes&reverse=true')], limits);
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
        await radio.setPicture(candidate.row.id, await pictureViaDevice(candidate.match.favicon));
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
