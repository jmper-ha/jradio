/* The stations page's FM tab: the presets the device plays, and the scan
   that finds them.

   The device only plays presets; this is where they are made. A scan runs on
   the device - one pass up the band, half a minute at most - and what it
   finds replaces the list and is saved at once, after a warning. Ticking
   stations one by one was the first way and was tiring; a new city is the
   case the scan is for, and there the whole old list is wrong anyway. A name
   given to a frequency the scan finds again is kept. The list is then edited
   here as a whole (names, pictures, order, removal) and goes back in one
   POST, the file's own shape: "name<TAB>kHz[<TAB>picture]" a line.

   A picture hangs off the preset, never off the frequency: 101.2 is another
   station in every city, and the list is the one its owner made. The device
   shows it in the player's tile while the tuner stands on that preset. It is
   scaled here and goes up with the list when it is saved - see
   uploadPendingPictures().

   The list is loaded whether or not the tab is shown, since it is a few
   hundred bytes. */
(() => {
  'use strict';

  const t = (key, values) => window.jradioI18n.t(key, values);

  /* A button that floats over the page while the open list's Save button is
     out of view, and takes the page down to it: a playlist of 99 stations is
     a long scroll to get there, and nothing on the way says how far it is.
     Here because this script knows which tab is open. */
  function setupToSave() {
    const button = document.querySelector('#stations-to-save');
    if (button === null) return;
    const target = () => {
      const fmOpen = document.querySelector('#stations-fm');
      const onFm = fmOpen !== null && !fmOpen.hidden;
      return document.querySelector(onFm ? '#fm-save' : '#playlist-save');
    };
    /* Written only when it changes: the observer below watches this very
       attribute, and setting it to what it already is still counts as a
       change to it - the page hung in a loop of the two. */
    const show = (visible) => {
      if (button.hidden === !visible) return;
      button.hidden = !visible;
    };
    const update = () => {
      const save = target();
      if (save === null || typeof save.getBoundingClientRect !== 'function') {
        show(false);
        return;
      }
      const box = save.getBoundingClientRect();
      const height = window.innerHeight || document.documentElement.clientHeight || 0;
      // Shown while the button is below the bottom edge; at it, or above, there is no need.
      show(height > 0 && box.top > height - 8);
    };
    button.addEventListener('click', () => {
      const save = target();
      if (save === null) return;
      if (typeof save.scrollIntoView === 'function') save.scrollIntoView({behavior: 'smooth', block: 'center'});
      if (typeof save.focus === 'function') save.focus({preventScroll: true});
    });
    if (typeof window.addEventListener === 'function') {
      window.addEventListener('scroll', update, {passive: true});
      window.addEventListener('resize', update);
    }
    // The lists grow after load, from the device, and the tabs switch them.
    if (typeof window.MutationObserver === 'function') {
      new window.MutationObserver(update).observe(document.body, {childList: true, subtree: true,
                                                                  attributes: true, attributeFilter: ['hidden']});
    }
    for (const tab of document.querySelectorAll('[data-station-tab]')) tab.addEventListener('click', update);
    update();
  }

  /* The stations page has a tab for the radio's playlist and one for the
     tuner's presets, which is there only on a board with a tuner - the
     settings say so. The choice rides in the address, so the player's link
     can open the page on FM and a reload comes back to the same tab. */
  function setupTabs() {
    const tabs = [...document.querySelectorAll('[data-station-tab]')];
    if (tabs.length === 0) return;
    const panels = {
      radio: document.querySelector('#stations-radio'),
      fm: document.querySelector('#stations-fm'),
    };
    const show = (name) => {
      for (const tab of tabs) {
        const active = tab.dataset.stationTab === name;
        tab.classList.toggle('is-active', active);
        tab.setAttribute('aria-selected', active ? 'true' : 'false');
      }
      for (const [key, panel] of Object.entries(panels)) {
        if (panel) panel.hidden = key !== name;
      }
    };
    for (const tab of tabs) {
      tab.addEventListener('click', () => {
        show(tab.dataset.stationTab);
        if (window.history && window.history.replaceState) {
          window.history.replaceState(null, '', tab.dataset.stationTab === 'fm' ? '#fm' : '#radio');
        }
      });
    }
    const fmTab = tabs.find((tab) => tab.dataset.stationTab === 'fm');
    window.fetch('/api/settings', {cache: 'no-store'})
      .then((response) => response.json())
      .then((settings) => {
        const available = settings && settings.available && settings.available.fm === true;
        tunerAvailable = available;
        if (fmTab) fmTab.hidden = !available;
        const wanted = window.location && window.location.hash === '#fm';
        show(available && wanted ? 'fm' : 'radio');
      })
      .catch(() => show('radio'));
  }

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

  /* {name, khz, icon, picture}, in the order they are shown and saved. `icon`
     is the name of a picture the device already holds; `picture` is one chosen
     here that it does not, and the two are never both set. */
  let presets = [];
  let dirty = false;
  // Whether the board has a tuner, which the settings say; the import and the
  // export of the page are told only once it is known.
  let tunerAvailable = false;

  function setStatus(text, error) {
    status.textContent = text;
    status.classList.toggle('is-error', error === true);
  }

  function mhz(khz) {
    return (Math.round(khz / 100) / 10).toFixed(1);
  }

  function isIconName(value) {
    return value === '' || (/^[A-Za-z0-9._-]+$/.test(value) && value[0] !== '.');
  }

  /* The picture, prepared as playlist.js prepares a station's: the device
     draws it through the same album_art, in a square no larger than 160 px, and
     stores a few kilobytes rather than a photograph. PNG first - a logo is flat
     colour - then JPEG, for the photograph that will not fit the 32 KB the
     device takes as a PNG. */
  const ICON_SIZE = 160;
  const ICON_MAX_BYTES = 32768;
  const ICON_TOO_LARGE = 'too large';
  const ICON_ENCODINGS = [
    {type: 'image/png', quality: undefined},
    {type: 'image/jpeg', quality: 0.85},
    {type: 'image/jpeg', quality: 0.7},
    {type: 'image/jpeg', quality: 0.55},
  ];

  function encodeWithinLimit(encode, limit, index) {
    const step = index || 0;
    if (step >= ICON_ENCODINGS.length) return Promise.reject(new Error(ICON_TOO_LARGE));
    const {type, quality} = ICON_ENCODINGS[step];
    return new Promise((resolve, reject) => {
      encode((blob) => {
        if (blob === null) reject(new Error('encode failed'));
        else resolve(blob);
      }, type, quality);
    }).then((blob) => (blob.size <= limit ? blob : encodeWithinLimit(encode, limit, step + 1)));
  }

  function scaleImage(file) {
    return new Promise((resolve, reject) => {
      const image = new Image();
      const objectUrl = URL.createObjectURL(file);
      image.onload = () => {
        URL.revokeObjectURL(objectUrl);
        const scale = Math.min(ICON_SIZE / image.width, ICON_SIZE / image.height, 1);
        const canvas = document.createElement('canvas');
        canvas.width = Math.max(1, Math.round(image.width * scale));
        canvas.height = Math.max(1, Math.round(image.height * scale));
        const context = canvas.getContext('2d');
        // On black first: the panel has no alpha.
        context.fillStyle = '#000000';
        context.fillRect(0, 0, canvas.width, canvas.height);
        context.drawImage(image, 0, 0, canvas.width, canvas.height);
        encodeWithinLimit((callback, type, quality) => canvas.toBlob(callback, type, quality),
                          ICON_MAX_BYTES).then(resolve, reject);
      };
      image.onerror = () => {
        URL.revokeObjectURL(objectUrl);
        reject(new Error('decode failed'));
      };
      image.src = objectUrl;
    });
  }

  /* A picture that is not on the device yet carries a name of its own, which an
     export has to call it by before the device has named it. */
  let nextPictureId = 1;
  function makePicture(blob) {
    const type = blob.type === 'image/jpeg' ? 'image/jpeg' : 'image/png';
    return {blob, type, url: URL.createObjectURL(blob),
            name: `fm${nextPictureId++}.${type === 'image/jpeg' ? 'jpg' : 'png'}`};
  }

  /* Straight through when it already fits: one out of an archive was scaled by
     whoever exported it, and decoding and encoding it again would cost quality
     to arrive where it started. */
  function fitPicture(blob) {
    const known = blob.type === 'image/png' || blob.type === 'image/jpeg';
    if (known && blob.size <= ICON_MAX_BYTES) return Promise.resolve(makePicture(blob));
    return scaleImage(blob).then(makePicture);
  }

  function dropPicture(preset) {
    if (preset.picture !== null && preset.picture.url !== null) {
      URL.revokeObjectURL(preset.picture.url);
    }
    preset.picture = null;
  }

  function uploadPicture(picture) {
    return window.fetch('/api/station-icon', {
      method: 'POST',
      headers: {'Content-Type': picture.type},
      body: picture.blob,
    }).then((response) => {
      if (!response || response.ok !== true) throw new Error('rejected');
      return response.json();
    }).then((payload) => {
      const name = payload && typeof payload.file === 'string' ? payload.file : '';
      if (!isIconName(name) || name === '') throw new Error('bad name');
      return name;
    });
  }

  /* Pictures go up when the list is saved, not when they are chosen: one
     chosen and then abandoned would stay on the device until some later save
     swept it. One at a time - esp_http_server has a single worker. */
  async function uploadPendingPictures() {
    for (const preset of presets) {
      if (preset.picture === null) continue;
      const name = await uploadPicture(preset.picture);
      dropPicture(preset);
      preset.icon = name;
    }
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
      const icon = (fields[2] || '').trim();
      // The same rule the device applies: the name is joined to one directory.
      if (!isIconName(icon)) continue;
      list.push({name: fields[0].trim(), khz, icon, picture: null});
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

  /* The picture's place in the row: a tile to choose one by, showing the
     current one, and a small control to take it off. */
  function pictureCell(index) {
    const preset = presets[index];
    const cell = document.createElement('span');
    cell.className = 'fm-picture-cell';
    const pick = document.createElement('label');
    pick.className = 'fm-picture';
    pick.setAttribute('title', t('playlist.icon'));
    let source = '';
    if (preset.picture !== null) source = preset.picture.url;
    else if (preset.icon !== '') source = `/api/station-icon?file=${encodeURIComponent(preset.icon)}`;
    if (source !== '') {
      const image = document.createElement('img');
      image.alt = '';
      image.src = source;
      pick.append(image);
    } else {
      pick.textContent = '+';
    }
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = 'image/*';
    input.setAttribute('aria-label', t('playlist.icon'));
    input.addEventListener('change', () => {
      const file = input.files && input.files[0];
      if (!file) return;
      setStatus(t('playlist.icon_preparing'), false);
      scaleImage(file).then((blob) => {
        dropPicture(presets[index]);
        presets[index] = {...presets[index], icon: '', picture: makePicture(blob)};
        markDirty();
        renderPresets();
      }).catch((error) => {
        const key = error && error.message === ICON_TOO_LARGE
          ? 'playlist.icon_too_large' : 'playlist.icon_failed';
        setStatus(t(key, {kb: Math.round(ICON_MAX_BYTES / 1024)}), true);
      });
    });
    pick.append(input);
    cell.append(pick);
    if (source !== '') {
      const clear = smallButton('playlist.icon_remove', '×', () => {
        dropPicture(presets[index]);
        presets[index] = {...presets[index], icon: '', picture: null};
        markDirty();
        renderPresets();
      });
      clear.classList.add('fm-picture-clear');
      cell.append(clear);
    }
    return cell;
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
      row.append(pictureCell(index));
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
        dropPicture(presets[index]);
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
    return uploadPendingPictures().catch(() => {
      saveButton.disabled = !dirty;
      setStatus(t('fm.picture_upload_failed'), true);
      throw new Error('picture upload');
    }).then(() => window.fetch('/api/fm/presets', {
      method: 'POST',
      headers: {'Content-Type': 'text/plain; charset=utf-8'},
      body: serialise(presets),
    }))
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
      .catch((error) => {
        saveButton.disabled = !dirty;
        // A picture that would not go up has said so already.
        if (!error || error.message !== 'picture upload') setStatus(t('fm.save_failed'), true);
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
                icon: known ? known.icon : '', picture: known ? known.picture : null};
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

  /* What the page's one import and one export - they are playlist.js's, and
     serve both lists - need of this one. */
  function pictureBytes(preset) {
    if (preset.picture !== null) {
      return preset.picture.blob.arrayBuffer().then((buffer) => new Uint8Array(buffer));
    }
    return window.fetch(`/api/station-icon?file=${encodeURIComponent(preset.icon)}`)
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('missing');
        return response.arrayBuffer();
      })
      .then((buffer) => new Uint8Array(buffer));
  }

  function exportData() {
    const named = presets.map((preset) => ({
      ...preset, icon: preset.picture !== null ? preset.picture.name : preset.icon}));
    return {
      text: serialise(named),
      count: presets.length,
      pictures: presets
        .filter((preset) => preset.picture !== null || preset.icon !== '')
        .map((preset) => ({
          name: preset.picture !== null ? preset.picture.name : preset.icon,
          bytes: () => pictureBytes(preset),
        })),
    };
  }

  /* The presets of a file in place of these: lenient like the device's own
     reading, the pictures it names taken from the archive and held until the
     list is saved, and a name that finds none counted. */
  async function importData(text, pictures) {
    const list = parse(text);
    let missing = 0;
    for (const preset of list) {
      const wanted = preset.icon;
      preset.icon = '';
      if (wanted === '') continue;
      const blob = pictures.get(wanted);
      if (blob === undefined) {
        missing += 1;
        continue;
      }
      try {
        preset.picture = await fitPicture(blob);
      } catch (error) {
        missing += 1;
      }
    }
    presets.forEach(dropPicture);
    presets = list;
    renderPresets();
    markDirty();
    return {count: list.length, missing};
  }

  window.jradioStations = window.jradioStations || {};
  window.jradioStations.fm = {
    available: () => tunerAvailable,
    isDirty: () => dirty,
    exportData,
    importData,
  };

  scanButton.addEventListener('click', startScan);
  saveButton.addEventListener('click', save);
  /* Rows are built here, not in the markup, so a language switch has to
     rebuild them for their labels. */
  window.jradioI18n.onChange(renderPresets);

  setupTabs();
  setupToSave();
  load();
})();
