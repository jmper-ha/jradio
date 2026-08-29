(() => {
  'use strict';

  // Mirror sizeof(name)/sizeof(url) in station_catalog_entry_t. The device's
  // station_catalog_copy_field() rejects a field whose length is >= the buffer
  // size because it still needs room for the terminating NUL, and it drops the
  // whole line rather than truncating it - so a value of exactly this many
  // bytes must be refused here too, or the station vanishes on save.
  const STATION_NAME_BUFFER_BYTES = 96;
  const STATION_URL_BUFFER_BYTES = 256;
  // STATION_CATALOG_MAX_ENTRIES in station_catalog.h. It was 32 here long
  // after the device grew to 99, and an import of a longer file silently
  // dropped the tail.
  const STATION_MAX_ENTRIES = 99;

  const statusEl = document.querySelector('#playlist-status');
  const rowsEl = document.querySelector('#playlist-rows');
  const emptyEl = document.querySelector('#playlist-empty');
  const addButton = document.querySelector('#playlist-add');
  const exportButton = document.querySelector('#playlist-export');
  const importInput = document.querySelector('#playlist-import-input');
  const saveButton = document.querySelector('#playlist-save');
  const saveStatusEl = document.querySelector('#playlist-save-status');

  const encoder = new TextEncoder();
  function byteLength(value) {
    return encoder.encode(value).length;
  }

  let nextRowId = 1;
  function makeRow(name, url, flag, icon) {
    return {id: nextRowId++, name, url, flag: flag ? 1 : 0, icon: icon || ''};
  }

  /* The same rule station_catalog_icon_is_valid() applies on the device: the
     name is joined to one directory, and anything that could reach out of it
     takes the line down rather than being quietly cleaned up. */
  function isIconName(value) {
    return value === '' || (/^[A-Za-z0-9._-]+$/.test(value) && value[0] !== '.');
  }

  const state = {
    rows: [],
    loaded: false,
    saving: false,
    lastSyncedText: '',
  };

  function isObject(value) {
    return value !== null && typeof value === 'object' && !Array.isArray(value);
  }

  // Mirrors station_catalog_parse_line() in
  // components/internet_radio/station_catalog.c: exactly two tab
  // separators, flag is strictly "0" or "1"; anything else is skipped.
  function parseCatalogLine(line) {
    const trimmed = line.replace(/[\r\n]+$/, '');
    if (trimmed.length === 0) return null;
    const parts = trimmed.split('\t');
    // The picture is the optional fourth column: a playlist written before it
    // existed has three and still loads.
    if (parts.length !== 3 && parts.length !== 4) return null;
    const [name, url, flagText] = parts;
    const icon = parts.length === 4 ? parts[3] : '';
    if (name.length === 0 || url.length === 0) return null;
    if (flagText !== '0' && flagText !== '1') return null;
    if (!isIconName(icon)) return null;
    return {name, url, flag: flagText === '1' ? 1 : 0, icon};
  }

  function parseCatalogText(text) {
    const rows = [];
    let skipped = 0;
    let truncated = false;
    for (const line of text.split('\n')) {
      const parsed = parseCatalogLine(line);
      if (parsed === null) {
        if (line.replace(/[\r\n]+$/, '').length > 0) skipped += 1;
        continue;
      }
      if (rows.length >= STATION_MAX_ENTRIES) {
        truncated = true;
        continue;
      }
      rows.push(makeRow(parsed.name, parsed.url, parsed.flag, parsed.icon));
    }
    return {rows, skipped, truncated};
  }

  function serializeCatalog(rows) {
    // Three columns for a station with no picture: an untouched playlist comes
    // back byte for byte as it was.
    return rows.map((row) => {
      const line = `${row.name}\t${row.url}\t${row.flag ? 1 : 0}`;
      return row.icon ? `${line}\t${row.icon}` : line;
    }).join('\n') + (rows.length > 0 ? '\n' : '');
  }

  function rowError(row) {
    if (row.name.length === 0) return 'Укажите название станции';
    if (row.name.includes('\t')) return 'Название не может содержать символ табуляции';
    if (byteLength(row.name) >= STATION_NAME_BUFFER_BYTES) return 'Название слишком длинное';
    if (row.url.length === 0) return 'Укажите адрес потока';
    if (row.url.includes('\t')) return 'Адрес не может содержать символ табуляции';
    if (byteLength(row.url) >= STATION_URL_BUFFER_BYTES) return 'Адрес слишком длинный';
    return '';
  }

  function hasErrors() {
    return state.rows.some((row) => rowError(row) !== '');
  }

  function isDirty() {
    return serializeCatalog(state.rows) !== state.lastSyncedText;
  }

  function updateSaveAvailability() {
    // An empty playlist is never sent: the device rejects an empty request
    // body outright, and "no stations at all" is not something this editor
    // offers as an explicit action.
    saveButton.disabled = !state.loaded || state.saving ||
      state.rows.length === 0 || hasErrors();
  }

  function setStatus(text, isError = false) {
    statusEl.textContent = text;
    statusEl.classList.toggle('is-error', isError);
    statusEl.classList.remove('is-success');
  }

  function setSaveStatus(text, isError = false) {
    saveStatusEl.textContent = text;
    saveStatusEl.classList.toggle('is-error', isError);
  }

  function updateRowErrorDisplay(rowEl, row) {
    const error = rowError(row);
    const errorEl = rowEl.querySelector('.playlist-row-error');
    errorEl.textContent = error;
    errorEl.hidden = error === '';
    rowEl.classList.toggle('has-error', error !== '');
  }

  /* One test at a time, whichever way it plays: two streams at once tells the
     listener nothing, and the device has one output anyway. */
  const test = {browser: null, device: null, audio: null};

  function stopBrowserTest() {
    if (test.audio !== null) {
      test.audio.pause();
      test.audio.removeAttribute('src');
      test.audio.load();
    }
    test.browser = null;
    refreshTestButtons();
  }

  function refreshTestButtons() {
    rowsEl.querySelectorAll('.playlist-row-test-browser').forEach((button) => {
      const playing = button.dataset.rowId === String(test.browser);
      button.textContent = playing ? 'Остановить' : 'Тест в браузере';
      button.classList.toggle('is-testing', playing);
    });
    rowsEl.querySelectorAll('.playlist-row-test-device').forEach((button) => {
      const playing = button.dataset.rowId === String(test.device);
      button.textContent = playing ? 'Остановить' : 'Тест на устройстве';
      button.classList.toggle('is-testing', playing);
    });
  }

  function setRowNotice(item, text, isError) {
    const notice = item.querySelector('.playlist-row-error');
    notice.textContent = text;
    notice.hidden = text === '';
    notice.classList.toggle('is-notice', text !== '' && !isError);
  }

  /* The browser plays the URL itself, which is the honest answer to "does this
     address work at all" - it is a different player from the device's, so a
     stream only one of them can decode is worth knowing about too. */
  function playInBrowser(item, row) {
    if (test.browser === row.id) {
      stopBrowserTest();
      return;
    }
    stopBrowserTest();
    if (test.audio === null) test.audio = new Audio();
    test.audio.src = row.url;
    test.browser = row.id;
    setRowNotice(item, 'Пробуем в браузере…', false);
    refreshTestButtons();
    test.audio.play().then(() => {
      setRowNotice(item, 'Играет в браузере', false);
    }).catch(() => {
      test.browser = null;
      refreshTestButtons();
      setRowNotice(item, 'Браузер не смог открыть поток. Формат вроде HLS он не играет.', true);
    });
  }

  /* The device plays it without the station being saved: the catalogue on the
     device is untouched, so a station can be tried before it is committed. */
  function playOnDevice(item, row) {
    const stopping = test.device === row.id;
    const body = stopping ? {url: ''} : {url: row.url, name: row.name};
    setRowNotice(item, stopping ? 'Останавливаем…' : 'Отправляем на устройство…', false);
    window.fetch('/api/station-test', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body),
    })
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        test.device = stopping ? null : row.id;
        refreshTestButtons();
        setRowNotice(item, stopping ? '' : 'Играет на устройстве', false);
      })
      .catch(() => {
        setRowNotice(item, 'Устройство не приняло запрос', true);
      });
  }

  /* The device draws the picture through the same album_art the covers of
     files go through, and that fits everything into a 96 px square before it
     publishes it. Scaling here rather than there means the device stores a few
     kilobytes instead of a photograph, and decodes something it has already
     been told the size of. */
  const ICON_SIZE = 96;
  const ICON_MAX_BYTES = 32768;

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
        /* Painted onto black first: the panel has no alpha, and a logo with a
           transparent background would otherwise arrive with whatever the
           decoder happens to leave behind it. */
        context.fillStyle = '#000000';
        context.fillRect(0, 0, canvas.width, canvas.height);
        context.drawImage(image, 0, 0, canvas.width, canvas.height);
        canvas.toBlob((blob) => {
          if (blob === null) reject(new Error('encode failed'));
          else resolve(blob);
        }, 'image/png');
      };
      image.onerror = () => {
        URL.revokeObjectURL(objectUrl);
        reject(new Error('decode failed'));
      };
      image.src = objectUrl;
    });
  }

  function uploadIcon(file) {
    return scaleImage(file).then((blob) => {
      if (blob.size > ICON_MAX_BYTES) throw new Error('too large');
      return window.fetch('/api/station-icon', {
        method: 'POST',
        headers: {'Content-Type': 'image/png'},
        body: blob,
      });
    }).then((response) => {
      if (!response || response.ok !== true) throw new Error('rejected');
      return response.json();
    }).then((payload) => {
      const name = payload && typeof payload.file === 'string' ? payload.file : '';
      if (!isIconName(name) || name === '') throw new Error('bad name');
      return name;
    });
  }

  function field(labelText, input) {
    const label = document.createElement('label');
    label.className = 'playlist-row-field';
    const caption = document.createElement('span');
    caption.textContent = labelText;
    label.append(caption, input);
    return label;
  }

  function buildRow(row) {
    const item = document.createElement('li');
    item.className = 'playlist-row';
    item.dataset.rowId = String(row.id);

    /* The row itself is read-only. Editing a list by typing into it is how the
       previous editor worked, and with a name field, an address field and a
       switch on every line there was no list left to read - only a wall of
       inputs. The fields live in a panel that one row at a time opens. */
    const head = document.createElement('div');
    head.className = 'playlist-row-head';

    const nameText = document.createElement('span');
    nameText.className = 'playlist-row-name';
    const urlText = document.createElement('span');
    urlText.className = 'playlist-row-url';
    const editButton = document.createElement('button');
    editButton.type = 'button';
    editButton.className = 'icon-button playlist-row-edit';
    editButton.setAttribute('aria-label', 'Редактировать станцию');
    editButton.setAttribute('aria-expanded', 'false');
    /* Two glyphs in one button: the pencil says the row can be changed, and
       once the panel is open the arrow says how to close it again. */
    editButton.innerHTML =
      '<svg class="icon-pencil" viewBox="0 0 24 24" aria-hidden="true" focusable="false">' +
      '<path d="M4 20h4l10-10-4-4L4 16zM14 6l4 4"/></svg>' +
      '<svg class="icon-collapse" viewBox="0 0 24 24" aria-hidden="true" focusable="false">' +
      '<path d="m6 15 6-6 6 6"/></svg>';

    const deleteButton = document.createElement('button');
    deleteButton.type = 'button';
    deleteButton.className = 'icon-button playlist-row-delete';
    deleteButton.setAttribute('aria-label', 'Удалить станцию');
    deleteButton.innerHTML =
      '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">' +
      '<path d="M5 7h14M9 7V5h6v2m-9 0 1 12h10l1-12"/></svg>';

    /* The picture, where there is one: the row is what the list is read by,
       and a station with a logo is found by the logo. */
    const thumb = document.createElement('img');
    thumb.className = 'playlist-row-thumb';
    thumb.alt = '';
    thumb.hidden = true;

    head.append(thumb, nameText, urlText, editButton, deleteButton);

    const editor = document.createElement('div');
    editor.className = 'playlist-row-editor';
    editor.hidden = true;

    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.className = 'playlist-row-name-input';
    nameInput.value = row.name;
    nameInput.maxLength = STATION_NAME_BUFFER_BYTES - 1;
    nameInput.setAttribute('aria-label', 'Название станции');

    const urlInput = document.createElement('input');
    urlInput.type = 'text';
    urlInput.className = 'playlist-row-url-input';
    urlInput.value = row.url;
    urlInput.maxLength = STATION_URL_BUFFER_BYTES - 1;
    urlInput.setAttribute('aria-label', 'Адрес потока');

    const flagCaption = 'Показывать название из списка, а не из потока';
    const flagLabel = document.createElement('label');
    flagLabel.className = 'playlist-row-flag';
    const flagInput = document.createElement('input');
    flagInput.type = 'checkbox';
    flagInput.checked = row.flag === 1;
    flagInput.setAttribute('aria-label', flagCaption);
    flagLabel.append(flagInput, document.createTextNode(flagCaption));

    /* The picture: a file chooser, a preview of what is stored, and a way to
       take it off again. The file is scaled and uploaded at once - by the time
       the playlist is saved the device already holds the picture, and the row
       carries only its name. */
    const iconRow = document.createElement('div');
    iconRow.className = 'playlist-row-icon';
    const iconPreview = document.createElement('img');
    iconPreview.className = 'playlist-row-icon-preview';
    iconPreview.alt = '';
    iconPreview.hidden = true;
    const iconPick = document.createElement('label');
    iconPick.className = 'secondary-button file-button';
    const iconCaption = document.createElement('span');
    const iconInput = document.createElement('input');
    iconInput.type = 'file';
    iconInput.accept = 'image/*';
    iconInput.hidden = true;
    iconPick.append(iconCaption, iconInput);
    const iconClear = document.createElement('button');
    iconClear.type = 'button';
    iconClear.className = 'secondary-button playlist-row-icon-clear';
    iconClear.textContent = 'Убрать';
    iconClear.hidden = true;
    iconRow.append(iconPreview, iconPick, iconClear);

    const tests = document.createElement('div');
    tests.className = 'playlist-row-tests';
    const testBrowser = document.createElement('button');
    testBrowser.type = 'button';
    testBrowser.className = 'secondary-button playlist-row-test-browser';
    testBrowser.dataset.rowId = String(row.id);
    testBrowser.textContent = 'Тест в браузере';
    const testDevice = document.createElement('button');
    testDevice.type = 'button';
    testDevice.className = 'secondary-button playlist-row-test-device';
    testDevice.dataset.rowId = String(row.id);
    testDevice.textContent = 'Тест на устройстве';
    tests.append(testBrowser, testDevice);

    const errorEl = document.createElement('p');
    errorEl.className = 'playlist-row-error';
    errorEl.hidden = true;

    editor.append(field('Название', nameInput), field('Адрес потока', urlInput),
                  flagLabel, iconRow, tests, errorEl);
    item.append(head, editor);

    function paintIcon() {
      const source = row.icon ? `/api/station-icon?file=${encodeURIComponent(row.icon)}` : '';
      thumb.hidden = source === '';
      iconPreview.hidden = source === '';
      iconClear.hidden = source === '';
      if (source !== '') {
        thumb.src = source;
        iconPreview.src = source;
      } else {
        thumb.removeAttribute('src');
        iconPreview.removeAttribute('src');
      }
      iconCaption.textContent = row.icon ? 'Заменить иконку' : 'Иконка станции';
    }

    function paintHead() {
      nameText.textContent = row.name === '' ? 'Без названия' : row.name;
      urlText.textContent = row.url;
    }
    paintHead();
    paintIcon();

    function setOpen(open) {
      editor.hidden = !open;
      item.classList.toggle('is-editing', open);
      editButton.setAttribute('aria-expanded', String(open));
    }
    item.setOpen = setOpen;
    item.focusName = () => nameInput.focus();

    editButton.addEventListener('click', () => {
      const opening = editor.hidden;
      closeAllEditors();
      setOpen(opening);
      if (opening) nameInput.focus();
    });
    nameInput.addEventListener('input', () => {
      row.name = nameInput.value;
      paintHead();
      updateRowErrorDisplay(item, row);
      updateSaveAvailability();
    });
    urlInput.addEventListener('input', () => {
      row.url = urlInput.value;
      paintHead();
      updateRowErrorDisplay(item, row);
      updateSaveAvailability();
    });
    flagInput.addEventListener('change', () => {
      row.flag = flagInput.checked ? 1 : 0;
      updateSaveAvailability();
    });
    iconInput.addEventListener('change', () => {
      const file = iconInput.files && iconInput.files[0];
      if (!file) return;
      setRowNotice(item, 'Готовим и отправляем иконку…', false);
      uploadIcon(file).then((name) => {
        row.icon = name;
        paintIcon();
        updateSaveAvailability();
        setRowNotice(item,
          'Иконка загружена. Нажмите «Сохранить на устройстве», чтобы закрепить её за станцией.',
          false);
      }).catch(() => {
        setRowNotice(item, 'Не удалось загрузить иконку', true);
      }).then(() => {
        // Cleared so that choosing the same file again is a change.
        iconInput.value = '';
      });
    });
    iconClear.addEventListener('click', () => {
      row.icon = '';
      paintIcon();
      updateSaveAvailability();
      setRowNotice(item, 'Иконка снимется при сохранении', false);
    });
    testBrowser.addEventListener('click', () => {
      if (row.url === '') {
        setRowNotice(item, 'Сначала укажите адрес потока', true);
        return;
      }
      playInBrowser(item, row);
    });
    testDevice.addEventListener('click', () => {
      if (row.url === '') {
        setRowNotice(item, 'Сначала укажите адрес потока', true);
        return;
      }
      playOnDevice(item, row);
    });
    deleteButton.addEventListener('click', () => {
      if (test.browser === row.id) stopBrowserTest();
      state.rows = state.rows.filter((candidate) => candidate.id !== row.id);
      renderRows();
      updateSaveAvailability();
    });

    updateRowErrorDisplay(item, row);
    return item;
  }

  /* Only one panel is open at a time: with several the list is again a wall of
     fields, which is what the panel exists to avoid. */
  function closeAllEditors() {
    Array.from(rowsEl.children).forEach((item) => {
      if (typeof item.setOpen === 'function') item.setOpen(false);
    });
  }

  function renderRows() {
    rowsEl.replaceChildren(...state.rows.map(buildRow));
    rowsEl.hidden = state.rows.length === 0;
    emptyEl.hidden = state.rows.length !== 0;
  }

  function loadPlaylist() {
    setStatus('Загрузка…');
    fetch('/api/playlist')
      .then((response) => {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.text();
      })
      .then((text) => {
        const {rows, skipped} = parseCatalogText(text);
        state.rows = rows;
        state.loaded = true;
        state.lastSyncedText = serializeCatalog(state.rows);
        renderRows();
        updateSaveAvailability();
        setStatus(skipped > 0
          ? `Загружено, ${skipped} строк пропущено (неверный формат)`
          : 'Загружено с устройства');
      })
      .catch((error) => {
        state.loaded = false;
        setStatus(`Не удалось загрузить плейлист: ${error.message}`, true);
        updateSaveAvailability();
      });
  }

  function addRow() {
    if (state.rows.length >= STATION_MAX_ENTRIES) {
      setStatus(`Достигнут предел в ${STATION_MAX_ENTRIES} станций`, true);
      return;
    }
    state.rows.push(makeRow('', '', 0));
    renderRows();
    updateSaveAvailability();
    // A new station has nothing to read, so it opens on its fields.
    const added = rowsEl.children[rowsEl.children.length - 1];
    if (added && typeof added.setOpen === 'function') {
      added.setOpen(true);
      added.focusName();
    }
  }

  function exportPlaylist() {
    const text = serializeCatalog(state.rows);
    const blob = new Blob([text], {type: 'text/plain;charset=utf-8'});
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = 'playlist.csv';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  }

  function importPlaylist(file) {
    if (state.loaded && isDirty() &&
        !window.confirm('Несохранённые изменения будут потеряны. Импортировать файл?')) {
      return;
    }
    const reader = new FileReader();
    reader.addEventListener('load', () => {
      const {rows, skipped, truncated} = parseCatalogText(String(reader.result));
      state.rows = rows;
      renderRows();
      updateSaveAvailability();
      const notes = [];
      if (skipped > 0) notes.push(`${skipped} строк пропущено`);
      if (truncated) notes.push(`оставлены первые ${STATION_MAX_ENTRIES} станций`);
      setStatus(notes.length > 0
        ? `Импортировано ${rows.length} станций (${notes.join(', ')})`
        : `Импортировано ${rows.length} станций`);
    });
    reader.addEventListener('error', () => {
      setStatus('Не удалось прочитать файл', true);
    });
    reader.readAsText(file);
  }

  function savePlaylist() {
    if (hasErrors() || state.saving) return;
    state.saving = true;
    updateSaveAvailability();
    setSaveStatus('Сохранение…');
    const text = serializeCatalog(state.rows);
    fetch('/api/playlist', {
      method: 'POST',
      headers: {'Content-Type': 'text/plain;charset=utf-8'},
      body: text,
    })
      .then(async (response) => {
        if (!response.ok) {
          const message = await response.text().catch(() => '');
          throw new Error(message || `HTTP ${response.status}`);
        }
        return response.json();
      })
      .then((payload) => {
        const expected = state.rows.length;
        const count = isObject(payload) && Number.isSafeInteger(payload.count) ? payload.count : expected;
        if (count !== expected) {
          // The device parsed fewer stations than were sent, so its playlist
          // differs from what is on screen. Leave the editor marked dirty:
          // treating this as a successful sync would hide the loss until the
          // next page load.
          setSaveStatus(
            `Устройство приняло ${count} станций из ${expected}: часть строк отклонена. ` +
            'Проверьте список и сохраните снова.', true);
          return;
        }
        state.lastSyncedText = text;
        setSaveStatus(`Сохранено: ${count} станций`);
      })
      .catch((error) => {
        setSaveStatus(`Не удалось сохранить: ${error.message}`, true);
      })
      .finally(() => {
        state.saving = false;
        updateSaveAvailability();
      });
  }

  addButton.addEventListener('click', addRow);
  exportButton.addEventListener('click', exportPlaylist);
  saveButton.addEventListener('click', savePlaylist);
  importInput.addEventListener('change', () => {
    const file = importInput.files && importInput.files[0];
    importInput.value = '';
    if (file) importPlaylist(file);
  });

  renderRows();
  updateSaveAvailability();
  loadPlaylist();

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {parseCatalogText, serializeCatalog, rowError};
  }
})();
