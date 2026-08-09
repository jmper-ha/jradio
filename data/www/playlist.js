(() => {
  'use strict';

  const STATION_NAME_MAX_BYTES = 96;
  const STATION_URL_MAX_BYTES = 256;
  const STATION_MAX_ENTRIES = 32;

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
  function makeRow(name, url, flag) {
    return {id: nextRowId++, name, url, flag: flag ? 1 : 0};
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
    if (parts.length !== 3) return null;
    const [name, url, flagText] = parts;
    if (name.length === 0 || url.length === 0) return null;
    if (flagText !== '0' && flagText !== '1') return null;
    return {name, url, flag: flagText === '1' ? 1 : 0};
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
      rows.push(makeRow(parsed.name, parsed.url, parsed.flag));
    }
    return {rows, skipped, truncated};
  }

  function serializeCatalog(rows) {
    return rows.map((row) => `${row.name}\t${row.url}\t${row.flag ? 1 : 0}`).join('\n') +
      (rows.length > 0 ? '\n' : '');
  }

  function rowError(row) {
    if (row.name.length === 0) return 'Укажите название станции';
    if (row.name.includes('\t')) return 'Название не может содержать символ табуляции';
    if (byteLength(row.name) > STATION_NAME_MAX_BYTES) return 'Название слишком длинное';
    if (row.url.length === 0) return 'Укажите адрес потока';
    if (row.url.includes('\t')) return 'Адрес не может содержать символ табуляции';
    if (byteLength(row.url) > STATION_URL_MAX_BYTES) return 'Адрес слишком длинный';
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

  function buildRow(row) {
    const item = document.createElement('li');
    item.className = 'playlist-row';
    item.dataset.rowId = String(row.id);

    const fields = document.createElement('div');
    fields.className = 'playlist-row-fields';

    const nameLabel = document.createElement('label');
    nameLabel.textContent = 'Название';
    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.className = 'playlist-row-name-input';
    nameInput.value = row.name;
    nameInput.maxLength = STATION_NAME_MAX_BYTES;
    nameInput.setAttribute('aria-label', 'Название станции');
    nameLabel.appendChild(nameInput);

    const urlLabel = document.createElement('label');
    urlLabel.textContent = 'Адрес потока (URL)';
    const urlInput = document.createElement('input');
    urlInput.type = 'text';
    urlInput.className = 'playlist-row-url-input';
    urlInput.value = row.url;
    urlInput.maxLength = STATION_URL_MAX_BYTES;
    urlInput.setAttribute('aria-label', 'Адрес потока');
    urlLabel.appendChild(urlInput);

    const flagLabel = document.createElement('label');
    flagLabel.className = 'playlist-row-flag';
    const flagInput = document.createElement('input');
    flagInput.type = 'checkbox';
    flagInput.checked = row.flag === 1;
    flagLabel.append(flagInput, document.createTextNode(' Показывать название потока вместо имени'));

    fields.append(nameLabel, urlLabel, flagLabel);

    const actions = document.createElement('div');
    actions.className = 'playlist-row-actions';
    const deleteButton = document.createElement('button');
    deleteButton.type = 'button';
    deleteButton.className = 'icon-button playlist-row-delete';
    deleteButton.setAttribute('aria-label', 'Удалить станцию');
    deleteButton.innerHTML =
      '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">' +
      '<path d="M5 7h14M9 7V5h6v2m-9 0 1 13h10l1-13"/></svg>';
    actions.appendChild(deleteButton);

    const errorEl = document.createElement('p');
    errorEl.className = 'playlist-row-error';
    errorEl.hidden = true;

    item.append(fields, actions, errorEl);

    nameInput.addEventListener('input', () => {
      row.name = nameInput.value;
      updateRowErrorDisplay(item, row);
      updateSaveAvailability();
    });
    urlInput.addEventListener('input', () => {
      row.url = urlInput.value;
      updateRowErrorDisplay(item, row);
      updateSaveAvailability();
    });
    flagInput.addEventListener('change', () => {
      row.flag = flagInput.checked ? 1 : 0;
    });
    deleteButton.addEventListener('click', () => {
      state.rows = state.rows.filter((candidate) => candidate.id !== row.id);
      renderRows();
      updateSaveAvailability();
    });

    updateRowErrorDisplay(item, row);
    return item;
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
    const inputs = rowsEl.querySelectorAll('.playlist-row:last-child input[type="text"]');
    if (inputs.length > 0) inputs[0].focus();
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
        state.lastSyncedText = text;
        const count = isObject(payload) && Number.isSafeInteger(payload.count) ? payload.count : state.rows.length;
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
