/* The settings page's FM section: the presets the device plays, and the scan
   that finds them.

   The device only plays presets; this is where they are made. A scan runs on
   the device - one pass up the band, half a minute at most - and what it
   finds replaces the list and is saved at once, after a warning. Ticking
   stations one by one was the first way and was tiring; a new city is the
   case the scan is for, and there the whole old list is wrong anyway. A name
   given to a frequency the scan finds again is kept. The list is then edited
   here as a whole (names, order, removal) and goes back in one POST, the
   file's own shape: "name<TAB>kHz[<TAB>picture]" a line.

   settings.js decides whether the card is shown at all; the list is loaded
   regardless, since it is a few hundred bytes. */
(() => {
  'use strict';

  const t = (key, values) => window.jradioI18n.t(key, values);

  const card = document.querySelector('#fm-card');
  if (card === null) return;
  const status = document.querySelector('#fm-status');
  const presetList = document.querySelector('#fm-presets');
  const presetEmpty = document.querySelector('#fm-presets-empty');
  const scanButton = document.querySelector('#fm-scan');
  const saveButton = document.querySelector('#fm-save');

  const POLL_MS = 700;
  const MAX_PRESETS = 40;
  const BAND_MIN = 87000;
  const BAND_MAX = 108000;

  /* {name, khz, icon}, in the order they are shown and saved. */
  let presets = [];
  let dirty = false;

  function setStatus(text, error) {
    status.textContent = text;
    status.classList.toggle('is-error', error === true);
  }

  function mhz(khz) {
    return (Math.round(khz / 100) / 10).toFixed(1);
  }

  /* The file, read leniently: a line the device would skip is skipped here
     too, so the page never shows a station the device will not play. */
  function parse(text) {
    const list = [];
    for (const raw of String(text).split('\n')) {
      const line = raw.replace(/\r$/, '');
      if (line.trim() === '' || line.trim().startsWith('#')) continue;
      const fields = line.split('\t');
      if (fields.length < 2) continue;
      const khz = Number(fields[1].trim());
      if (!Number.isInteger(khz) || khz < BAND_MIN || khz > BAND_MAX) continue;
      list.push({name: fields[0].trim(), khz, icon: (fields[2] || '').trim()});
    }
    return list.slice(0, MAX_PRESETS);
  }

  /* A tab or a line break in a name would split the line it is saved on. */
  function cleanName(name) {
    return String(name).replace(/[\t\r\n]+/g, ' ').trim();
  }

  function serialise(list) {
    return list.map((preset) => {
      const columns = [cleanName(preset.name), String(preset.khz)];
      if (preset.icon) columns.push(preset.icon);
      return `${columns.join('\t')}\n`;
    }).join('');
  }

  function markDirty() {
    dirty = true;
    saveButton.disabled = false;
    setStatus(t('fm.unsaved'), false);
  }

  function smallButton(labelKey, text, onClick) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'secondary-button';
    button.textContent = text;
    button.setAttribute('aria-label', t(labelKey));
    button.setAttribute('title', t(labelKey));
    button.addEventListener('click', onClick);
    return button;
  }

  function nameField(value, onInput) {
    const input = document.createElement('input');
    input.type = 'text';
    input.className = 'fm-name';
    input.maxLength = 40;
    input.value = value;
    input.setAttribute('placeholder', t('fm.name'));
    input.setAttribute('aria-label', t('fm.name'));
    input.addEventListener('input', () => onInput(input.value));
    return input;
  }

  function frequencyLabel(khz) {
    const span = document.createElement('span');
    span.className = 'fm-frequency';
    span.textContent = mhz(khz);
    return span;
  }

  function move(index, step) {
    const target = index + step;
    if (target < 0 || target >= presets.length) return;
    const next = presets.slice();
    [next[index], next[target]] = [next[target], next[index]];
    presets = next;
    markDirty();
    renderPresets();
  }

  function renderPresets() {
    presetList.replaceChildren();
    presets.forEach((preset, index) => {
      const row = document.createElement('li');
      row.dataset.khz = String(preset.khz);
      row.append(frequencyLabel(preset.khz));
      row.append(nameField(preset.name, (value) => {
        presets[index] = {...presets[index], name: value};
        markDirty();
      }));
      const up = smallButton('fm.up', '↑', () => move(index, -1));
      up.disabled = index === 0;
      const down = smallButton('fm.down', '↓', () => move(index, 1));
      down.disabled = index === presets.length - 1;
      const remove = smallButton('fm.remove', '✕', () => {
        presets = presets.filter((item, at) => at !== index);
        markDirty();
        renderPresets();
      });
      row.append(up, down, remove);
      presetList.append(row);
    });
    presetEmpty.hidden = presets.length > 0;
  }

  function load() {
    return window.fetch('/api/fm/presets', {cache: 'no-store'})
      .then((response) => {
        if (!response.ok) throw new Error(String(response.status));
        return response.text();
      })
      .then((text) => {
        presets = parse(text);
        dirty = false;
        saveButton.disabled = true;
        renderPresets();
      })
      .catch(() => setStatus(t('fm.load_failed'), true));
  }

  function save() {
    saveButton.disabled = true;
    return window.fetch('/api/fm/presets', {
      method: 'POST',
      headers: {'Content-Type': 'text/plain; charset=utf-8'},
      body: serialise(presets),
    })
      .then((response) => {
        if (!response.ok) throw new Error(String(response.status));
        return response.json();
      })
      .then((answer) => {
        dirty = false;
        setStatus(t('fm.saved_ok', {count: Number(answer.count) || 0}), false);
        /* Back from the device, so what is shown is what it kept. */
        return load();
      })
      .catch(() => {
        saveButton.disabled = !dirty;
        setStatus(t('fm.save_failed'), true);
      });
  }

  /* The scan's result as the new list: in frequency order, which is how the
     chip found them, with the names already given to those frequencies. */
  function replaceWith(found) {
    const names = new Map(presets.map((preset) => [preset.khz, preset]));
    presets = found
      .filter((station) => Number.isInteger(station.khz))
      .slice(0, MAX_PRESETS)
      .map((station) => {
        const known = names.get(station.khz);
        /* The name given here wins; the one the station sends over RDS is
           the start for a frequency nobody has named. */
        const sent = typeof station.name === 'string' ? cleanName(station.name) : '';
        return {name: known && known.name ? known.name : sent, khz: station.khz,
                icon: known ? known.icon : ''};
      });
    // Unsaved until the device has it, so a failed save leaves Save to press.
    dirty = true;
    renderPresets();
  }

  function poll() {
    return window.fetch('/api/fm/scan', {cache: 'no-store'})
      .then((response) => response.json())
      .then((answer) => {
        if (answer.running === true) {
          setStatus(t('fm.scanning', {frequency: mhz(Number(answer.khz) || BAND_MIN)}), false);
          window.setTimeout(poll, POLL_MS);
          return null;
        }
        scanButton.disabled = false;
        const found = Array.isArray(answer.found) ? answer.found : [];
        /* Nothing found is an aerial that came off, not an empty city:
           wiping the list for it would be the worst answer. */
        if (found.length === 0) {
          setStatus(t('fm.scan_nothing'), true);
          return null;
        }
        replaceWith(found);
        return save().then(() => {
          if (!dirty) setStatus(t('fm.scan_saved', {count: presets.length}), false);
        });
      })
      .catch(() => {
        scanButton.disabled = false;
        setStatus(t('fm.load_failed'), true);
      });
  }

  function startScan() {
    if (presets.length > 0 &&
        !window.confirm(t('fm.replace_confirm', {count: presets.length}))) {
      return Promise.resolve();
    }
    scanButton.disabled = true;
    return window.fetch('/api/fm/scan', {method: 'POST'})
      .then((response) => {
        /* 409 is a scan somebody else started: follow that one. */
        if (!response.ok && response.status !== 409) throw new Error(String(response.status));
        if (response.status === 409) setStatus(t('fm.scan_busy'), false);
        return poll();
      })
      .catch(() => {
        scanButton.disabled = false;
        setStatus(t('fm.load_failed'), true);
      });
  }

  scanButton.addEventListener('click', startScan);
  saveButton.addEventListener('click', save);
  /* Rows are built here, not in the markup, so a language switch has to
     rebuild them for their labels. */
  window.jradioI18n.onChange(renderPresets);

  load();
})();
