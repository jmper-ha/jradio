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
  /* `icon` is the name of a picture the device already holds; `picture` is one
     chosen or imported here that it does not, and the two are never both set.
     A pending picture is uploaded when the playlist is saved - see
     uploadPendingPictures() for why not sooner. */
  function makeRow(name, url, flag, icon) {
    return {id: nextRowId++, name, url, flag: flag ? 1 : 0, icon: icon || '', picture: null};
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

  /* Mirrors station_catalog_parse_line() in
     components/internet_radio/station_catalog.c: the third column says which
     dialect the line is written in. `S` or `L` is ours - which name to show -
     and a picture may follow it. A number there is a volume correction from a
     playlist written for another device, which this one has nowhere to put:
     those lines are read for their name and address alone. */
  function parseCatalogLine(line) {
    const trimmed = line.replace(/[\r\n]+$/, '');
    if (trimmed.length === 0) return null;
    const parts = trimmed.split('\t');
    if (parts.length < 2 || parts.length > 4) return null;
    const [name, url] = parts;
    if (name.length === 0 || url.length === 0) return null;
    const kind = parts.length > 2 ? parts[2] : '';
    if (kind === 'S' || kind === 'L') {
      // The picture is the optional fourth column; an empty one is no picture.
      const icon = parts.length === 4 ? parts[3] : '';
      if (!isIconName(icon)) return null;
      return {name, url, flag: kind === 'L' ? 1 : 0, icon};
    }
    // Not ours: at most the two columns every list has plus that correction.
    if (parts.length > 3) return null;
    if (kind !== '' && !/^[+-]?\d{1,12}$/.test(kind)) return null;
    return {name, url, flag: 0, icon: ''};
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
      const line = `${row.name}\t${row.url}\t${row.flag ? 'L' : 'S'}`;
      return row.icon ? `${line}\t${row.icon}` : line;
    }).join('\n') + (rows.length > 0 ? '\n' : '');
  }

  /* Zip, both ways, because a playlist with pictures is more than one file and
     a browser hands over one. Everything is stored rather than deflated: a PNG
     or a JPEG is already compressed, and storing keeps the writer down to a
     header, a directory and a CRC. Reading has to cope with deflate anyway -
     an archive assembled by any other tool will use it - and that is what
     DecompressionStream is for, and why there is no library here. */
  const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let index = 0; index < 256; index += 1) {
      let value = index;
      for (let bit = 0; bit < 8; bit += 1) {
        value = (value & 1) !== 0 ? (0xedb88320 ^ (value >>> 1)) : (value >>> 1);
      }
      table[index] = value >>> 0;
    }
    return table;
  })();

  function crc32(bytes) {
    let crc = 0xffffffff;
    for (let index = 0; index < bytes.length; index += 1) {
      crc = CRC_TABLE[(crc ^ bytes[index]) & 0xff] ^ (crc >>> 8);
    }
    return (crc ^ 0xffffffff) >>> 0;
  }

  function buildZip(files) {
    const entries = files.map((file) => ({
      name: encoder.encode(file.name),
      bytes: file.bytes,
      crc: crc32(file.bytes),
    }));
    // Local header and central directory entry are 30 and 46 bytes plus the
    // name; the end record is 22.
    const total = entries.reduce(
      (size, entry) => size + 76 + entry.name.length * 2 + entry.bytes.length, 22);
    const out = new Uint8Array(total);
    const view = new DataView(out.buffer);
    let at = 0;
    const u16 = (value) => { view.setUint16(at, value, true); at += 2; };
    const u32 = (value) => { view.setUint32(at, value, true); at += 4; };
    const raw = (bytes) => { out.set(bytes, at); at += bytes.length; };

    const offsets = [];
    entries.forEach((entry) => {
      offsets.push(at);
      // Flag 0x0800 says the name is UTF-8, which station names need.
      u32(0x04034b50); u16(20); u16(0x0800); u16(0); u16(0); u16(0);
      u32(entry.crc); u32(entry.bytes.length); u32(entry.bytes.length);
      u16(entry.name.length); u16(0);
      raw(entry.name); raw(entry.bytes);
    });
    const directoryAt = at;
    entries.forEach((entry, index) => {
      u32(0x02014b50); u16(20); u16(20); u16(0x0800); u16(0); u16(0); u16(0);
      u32(entry.crc); u32(entry.bytes.length); u32(entry.bytes.length);
      u16(entry.name.length); u16(0); u16(0); u16(0); u16(0); u32(0);
      u32(offsets[index]);
      raw(entry.name);
    });
    const directoryBytes = at - directoryAt;
    u32(0x06054b50); u16(0); u16(0); u16(entries.length); u16(entries.length);
    u32(directoryBytes); u32(directoryAt); u16(0);
    return out;
  }

  function inflate(data) {
    const stream = new DecompressionStream('deflate-raw');
    const writer = stream.writable.getWriter();
    writer.write(data);
    writer.close();
    const reader = stream.readable.getReader();
    const parts = [];
    let total = 0;
    const pump = () => reader.read().then(({value, done}) => {
      if (done) {
        const out = new Uint8Array(total);
        let at = 0;
        parts.forEach((part) => { out.set(part, at); at += part.length; });
        return out;
      }
      parts.push(value);
      total += value.length;
      return pump();
    });
    return pump();
  }

  /* Read through the central directory rather than by walking local headers:
     a writer that does not know a file's size in advance leaves the sizes in
     the local header at zero and puts them after the data, and the directory
     is the copy that is always filled in. */
  function readZip(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let end = -1;
    // The comment that may follow the end record is at most 64 KB.
    const first = Math.max(0, bytes.length - 66000);
    for (let at = bytes.length - 22; at >= first && end < 0; at -= 1) {
      if (view.getUint32(at, true) === 0x06054b50) end = at;
    }
    if (end < 0) throw new Error('not a zip');

    const decoder = new TextDecoder();
    const count = view.getUint16(end + 10, true);
    let at = view.getUint32(end + 16, true);
    const files = [];
    const readOne = (index) => {
      if (index >= count) return Promise.resolve(files);
      if (view.getUint32(at, true) !== 0x02014b50) throw new Error('bad directory');
      const method = view.getUint16(at + 10, true);
      const compressed = view.getUint32(at + 20, true);
      const nameLength = view.getUint16(at + 28, true);
      const extraLength = view.getUint16(at + 30, true);
      const commentLength = view.getUint16(at + 32, true);
      const localAt = view.getUint32(at + 42, true);
      const name = decoder.decode(bytes.subarray(at + 46, at + 46 + nameLength));
      at += 46 + nameLength + extraLength + commentLength;
      if (name.endsWith('/')) return readOne(index + 1);
      /* The local header repeats the name and the extra field, and its own
         lengths are the ones that place the data: writers pad the two
         differently. */
      const dataAt = localAt + 30 + view.getUint16(localAt + 26, true) +
        view.getUint16(localAt + 28, true);
      const data = bytes.subarray(dataAt, dataAt + compressed);
      if (method === 0) {
        files.push({name, bytes: data});
        return readOne(index + 1);
      }
      if (method !== 8) throw new Error('unsupported compression');
      return inflate(data).then((inflated) => {
        files.push({name, bytes: inflated});
        return readOne(index + 1);
      });
    };
    return Promise.resolve().then(() => readOne(0));
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
    return serializeCatalog(state.rows) !== state.lastSyncedText ||
      state.rows.some((row) => row.picture !== null);
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

  /* A picture that is not on the device yet. It carries a name of its own
     because an export has to call it something before the device has named
     it. */
  let nextPictureId = 1;
  function makePicture(blob) {
    const type = blob.type === 'image/jpeg' ? 'image/jpeg' : 'image/png';
    return {
      blob,
      type,
      name: `p${nextPictureId++}.${type === 'image/jpeg' ? 'jpg' : 'png'}`,
      url: null,
    };
  }

  function dropPicture(row) {
    if (row.picture !== null && row.picture.url !== null) {
      URL.revokeObjectURL(row.picture.url);
    }
    row.picture = null;
  }

  function pictureUrl(picture) {
    if (picture.url === null) picture.url = URL.createObjectURL(picture.blob);
    return picture.url;
  }

  /* Straight through when it already fits: a picture out of an archive was
     scaled by whoever exported it, and decoding and re-encoding ninety-nine of
     them would cost quality to arrive where it started. Anything the device
     will not take goes through the same canvas a hand-picked file does. */
  function fitPicture(blob) {
    const known = blob.type === 'image/png' || blob.type === 'image/jpeg';
    if (known && blob.size <= ICON_MAX_BYTES) return Promise.resolve(makePicture(blob));
    return scaleImage(blob).then((scaled) => {
      if (scaled.size > ICON_MAX_BYTES) throw new Error('too large');
      return makePicture(scaled);
    });
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

  /* Pictures go up when the playlist is saved, not when they are chosen: an
     import that is then abandoned would otherwise leave up to ninety-nine
     files behind, and the only thing that ever deletes an unused one is the
     next save. One at a time, because esp_http_server runs a single worker -
     parallel uploads only queue behind each other while holding sockets the
     save itself needs. */
  async function uploadPendingPictures() {
    for (const row of state.rows) {
      if (row.picture === null) continue;
      const name = await uploadPicture(row.picture);
      dropPicture(row);
      row.icon = name;
    }
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

    /* The picture: a file chooser, a preview of what the station will have,
       and a way to take it off again. The file is scaled here and goes to the
       device with the playlist, so what the preview shows before saving is the
       local copy. */
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
      let source = '';
      if (row.picture !== null) source = pictureUrl(row.picture);
      else if (row.icon !== '') source = `/api/station-icon?file=${encodeURIComponent(row.icon)}`;
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
      iconCaption.textContent = source === '' ? 'Иконка станции' : 'Заменить иконку';
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
      setRowNotice(item, 'Готовим иконку…', false);
      scaleImage(file).then((blob) => {
        if (blob.size > ICON_MAX_BYTES) throw new Error('too large');
        dropPicture(row);
        row.picture = makePicture(blob);
        row.icon = '';
        paintIcon();
        updateSaveAvailability();
        setRowNotice(item,
          'Иконка готова. Нажмите «Сохранить на устройстве», чтобы отправить её.', false);
      }).catch(() => {
        setRowNotice(item, 'Не удалось подготовить иконку', true);
      }).then(() => {
        // Cleared so that choosing the same file again is a change.
        iconInput.value = '';
      });
    });
    iconClear.addEventListener('click', () => {
      dropPicture(row);
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
      dropPicture(row);
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

  function download(blob, filename) {
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  }

  // A picture the device holds against one that is still only here.
  function iconNameOf(row) {
    return row.picture !== null ? row.picture.name : row.icon;
  }

  function pictureBytes(row) {
    if (row.picture !== null) {
      return row.picture.blob.arrayBuffer().then((buffer) => new Uint8Array(buffer));
    }
    return window.fetch(`/api/station-icon?file=${encodeURIComponent(row.icon)}`)
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('missing');
        return response.arrayBuffer();
      })
      .then((buffer) => new Uint8Array(buffer));
  }

  /* A plain file while there are no pictures, so a list stays something any
     other player can read; an archive once there is one, because a browser
     downloads a file and a playlist with pictures is a folder. */
  async function exportPlaylist() {
    const named = state.rows.filter((row) => iconNameOf(row) !== '');
    const text = serializeCatalog(
      state.rows.map((row) => ({...row, icon: iconNameOf(row)})));
    if (named.length === 0) {
      download(new Blob([text], {type: 'text/plain;charset=utf-8'}), 'playlist.csv');
      setStatus(`Экспортировано ${state.rows.length} станций`);
      return;
    }
    setStatus('Собираем архив…');
    try {
      const files = [{name: 'playlist.csv', bytes: encoder.encode(text)}];
      const taken = new Set();
      for (const row of named) {
        const name = iconNameOf(row);
        // Two stations may share a picture; the archive keeps one copy.
        if (taken.has(name)) continue;
        taken.add(name);
        files.push({name: `radio_img/${name}`, bytes: await pictureBytes(row)});
      }
      download(new Blob([buildZip(files)], {type: 'application/zip'}), 'playlist.zip');
      setStatus(`Экспортировано ${state.rows.length} станций и ${files.length - 1} картинок`);
    } catch (error) {
      setStatus('Не удалось собрать архив: картинка не отдалась', true);
    }
  }

  function readFileBytes(file) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.addEventListener('load', () => resolve(new Uint8Array(reader.result)));
      reader.addEventListener('error', () => reject(new Error('read failed')));
      reader.readAsArrayBuffer(file);
    });
  }

  function pictureTypeOf(name) {
    if (/\.jpe?g$/i.test(name)) return 'image/jpeg';
    if (/\.png$/i.test(name)) return 'image/png';
    return '';
  }

  /* Pictures are indexed by their bare name and the playlist is whatever is
     not a picture: an archive written here puts them under radio_img/, but one
     assembled by hand may nest them anywhere or not at all. */
  function readArchive(bytes) {
    return readZip(bytes).then((files) => {
      const pictures = new Map();
      let playlist = null;
      files.forEach((file) => {
        const type = pictureTypeOf(file.name);
        if (type !== '') {
          pictures.set(file.name.slice(file.name.lastIndexOf('/') + 1),
                       new Blob([file.bytes], {type}));
        } else if (playlist === null) {
          playlist = file;
        }
      });
      if (playlist === null) throw new Error('no playlist');
      return {text: new TextDecoder().decode(playlist.bytes), pictures};
    });
  }

  const ZIP_SIGNATURE = [0x50, 0x4b, 0x03, 0x04];

  function importPlaylist(file) {
    if (state.loaded && isDirty() &&
        !window.confirm('Несохранённые изменения будут потеряны. Импортировать файл?')) {
      return;
    }
    setStatus('Читаем файл…');
    readFileBytes(file)
      .then((bytes) => {
        const archive = bytes.length > ZIP_SIGNATURE.length &&
          ZIP_SIGNATURE.every((value, index) => bytes[index] === value);
        return archive
          ? readArchive(bytes)
          : {text: new TextDecoder().decode(bytes), pictures: new Map()};
      })
      .then(async ({text, pictures}) => {
        const {rows, skipped, truncated} = parseCatalogText(text);
        let missing = 0;
        for (const row of rows) {
          /* The name in the file names a picture in the archive, not one on
             the device: it is cleared either way, and what is found takes its
             place until the playlist is saved. */
          const wanted = row.icon;
          row.icon = '';
          if (wanted === '') continue;
          const blob = pictures.get(wanted);
          if (blob === undefined) {
            missing += 1;
            continue;
          }
          try {
            row.picture = await fitPicture(blob);
          } catch (error) {
            missing += 1;
          }
        }
        state.rows.forEach(dropPicture);
        state.rows = rows;
        renderRows();
        updateSaveAvailability();
        const notes = [];
        if (skipped > 0) notes.push(`${skipped} строк пропущено`);
        if (truncated) notes.push(`оставлены первые ${STATION_MAX_ENTRIES} станций`);
        if (missing > 0) notes.push(`${missing} картинок не нашлось`);
        setStatus(notes.length > 0
          ? `Импортировано ${rows.length} станций (${notes.join(', ')})`
          : `Импортировано ${rows.length} станций`);
      })
      .catch(() => {
        setStatus('Не удалось прочитать файл', true);
      });
  }

  async function savePlaylist() {
    if (hasErrors() || state.saving) return;
    state.saving = true;
    updateSaveAvailability();
    try {
      /* The pictures go first: the playlist names them, and a line naming one
         the device does not have yet is a line it will refuse. */
      const pending = state.rows.filter((row) => row.picture !== null).length;
      if (pending > 0) {
        setSaveStatus(`Отправляем картинки: ${pending}…`);
        await uploadPendingPictures();
        renderRows();
      }
      setSaveStatus('Сохранение…');
      const text = serializeCatalog(state.rows);
      const response = await fetch('/api/playlist', {
        method: 'POST',
        headers: {'Content-Type': 'text/plain;charset=utf-8'},
        body: text,
      });
      if (!response.ok) {
        const message = await response.text().catch(() => '');
        throw new Error(message || `HTTP ${response.status}`);
      }
      const payload = await response.json();
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
      } else {
        state.lastSyncedText = text;
        setSaveStatus(`Сохранено: ${count} станций`);
      }
    } catch (error) {
      setSaveStatus(`Не удалось сохранить: ${error.message}`, true);
    } finally {
      state.saving = false;
      updateSaveAvailability();
    }
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
    module.exports = {parseCatalogText, serializeCatalog, rowError, buildZip, readZip};
  }
})();
