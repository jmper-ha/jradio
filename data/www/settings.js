(() => {
  'use strict';

  const socketState = document.querySelector('#socket-state');
  const form = document.querySelector('#wifi-form');
  const ssidInput = document.querySelector('#wifi-ssid');
  const passwordInput = document.querySelector('#wifi-password');
  const submitButton = document.querySelector('#wifi-submit');
  const wifiStatus = document.querySelector('#wifi-status');
  const wifiActive = document.querySelector('#wifi-active');
  const wifiIp = document.querySelector('#wifi-ip');
  const savedNetworks = document.querySelector('#saved-networks');
  const savedNetworksEmpty = document.querySelector('#saved-networks-empty');
  const wifiAdd = document.querySelector('#wifi-add');
  const wifiCancel = document.querySelector('#wifi-cancel');
  const wifiScan = document.querySelector('#wifi-scan');
  const wifiScanBlock = document.querySelector('#wifi-scan-block');
  const scanNetworks = document.querySelector('#scan-networks');
  const scanEmpty = document.querySelector('#scan-empty');
  const wifiChosen = document.querySelector('#wifi-chosen');
  const wifiChosenName = document.querySelector('#wifi-chosen-name');
  const wifiSsidRow = document.querySelector('#wifi-ssid-row');
  const yandexStatus = document.querySelector('#yandex-status');
  const yandexCodeBlock = document.querySelector('#yandex-code-block');
  const yandexUrl = document.querySelector('#yandex-url');
  const yandexCode = document.querySelector('#yandex-code');
  const yandexCountdown = document.querySelector('#yandex-countdown');
  const yandexLink = document.querySelector('#yandex-link');
  const yandexCancel = document.querySelector('#yandex-cancel');
  const yandexForget = document.querySelector('#yandex-forget');
  const yandexRefresh = document.querySelector('#yandex-refresh');
  const yandexStationsBlock = document.querySelector('#yandex-stations-block');
  const yandexStations = document.querySelector('#yandex-stations');
  const yandexStationsEmpty = document.querySelector('#yandex-stations-empty');
  const deviceStatus = document.querySelector('#device-status');
  const deviceBrightness = document.querySelector('#device-brightness');
  /* The device's own settings screen, field for field, in the order and with
     the wording it uses - so that "Скроллинг: Влево-вправо" means the same
     thing in both places. `row` and `gate` belong to the two fields the device
     itself can be without: a build with no Yandex Music has no switch for it,
     and a board with only one place to go has no home screen to choose. */
  const deviceFields = [
    {field: 'language', kind: 'choice', node: document.querySelector('#device-language')},
    {field: 'home_screen', kind: 'choice', node: document.querySelector('#device-home-screen'),
     row: document.querySelector('#device-home-screen-row'), gate: 'home_screen'},
    {field: 'scroll', kind: 'choice', node: document.querySelector('#device-scroll')},
    {field: 'buffer_view', kind: 'choice', node: document.querySelector('#device-buffer-view')},
    {field: 'autoplay', kind: 'switch', node: document.querySelector('#device-autoplay')},
    {field: 'yandex_music', kind: 'switch', node: document.querySelector('#device-yandex'),
     row: document.querySelector('#device-yandex-row'), gate: 'yandex_music'},
    {field: 'brightness', kind: 'number', node: deviceBrightness,
     output: document.querySelector('#device-brightness-value')},
    {field: 'flip_vertical', kind: 'switch', node: document.querySelector('#device-flip-vertical')},
    {field: 'flip_horizontal', kind: 'switch',
     node: document.querySelector('#device-flip-horizontal')},
  ];

  const reconnectDelays = Object.freeze([500, 1000, 2000, 4000, 8000]);
  const passwordErrors = new Set([2, 15, 202, 204]);
  let socket = null;
  let reconnectTimer = null;
  let reconnectAttempt = 0;
  let requestSequence = 0;
  let lastRevision = null;
  let haveSnapshot = false;
  let connected = false;
  let saveInFlight = false;
  let saveAccepted = false;
  let expectedSsid = '';
  let pendingRequestId = '';
  // Forgetting, reordering and switching a network off share one slot: the
  // device runs them one at a time, and two at once would only queue anyway.
  let editRequestId = '';
  /* What the edit was supposed to do, kept until the list proves it happened.
     The acknowledgement cannot: it says the command was queued, and the work
     runs on the device afterwards - a name that is no longer saved, or a busy
     device, is refused there with nothing sent back. So the answer is read off
     the list itself, and a timeout says so when it never changes. */
  let editPending = null;
  let editTimer = null;
  let apMode = false;
  // The name the form is about to send. Empty means it is typed in instead,
  // which is how a hidden network and "Другая сеть…" are reached.
  let chosenSsid = '';
  let scanTimer = null;
  let scanning = false;
  let scanRequested = false;
  // The last Wi-Fi state the device sent. Read before an edit is sent: the
  // device refuses one while a save is still waiting for its IP, and a refusal
  // it never hears about is worse than a button that says why it is not ready.
  let lastWifi = null;
  let yandexTimer = null;
  let yandexBusy = false;
  let deviceBusy = false;
  // The field whose handle is being held right now, if any. A push from the
  // device must not move a control the visitor has hold of.
  let deviceHeld = '';

  function isObject(value) {
    return value !== null && typeof value === 'object' && !Array.isArray(value);
  }

  function safeString(value, fallback = '') {
    return typeof value === 'string' ? value : fallback;
  }

  function normalizeWifi(value) {
    const wifi = isObject(value) ? value : {};
    return {
      mode: safeString(wifi.mode, 'unknown'),
      active_ssid: safeString(wifi.active_ssid),
      ip: safeString(wifi.ip),
      save_pending: wifi.save_pending === true,
      last_error: Number.isSafeInteger(wifi.last_error) ? wifi.last_error : 0,
      // The order is the order the device tries them in, so it is never sorted
      // here: the first row is the one that gets the first attempt.
      saved: Array.isArray(wifi.saved)
        ? wifi.saved
            .filter((item) => isObject(item) && typeof item.ssid === 'string' && item.ssid)
            .map((item) => ({ssid: item.ssid, blocked: item.blocked === true}))
        : [],
    };
  }

  function setConnected(value) {
    connected = value;
    socketState.textContent = value ? 'Подключено' : 'Нет связи';
    socketState.classList.toggle('is-online', value);
    socketState.classList.toggle('is-offline', !value);
    socketState.classList.remove('is-connecting');
    submitButton.disabled = !value || saveInFlight;
  }

  function networkButton(label, action, ssid) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = label;
    button.classList.add('secondary-button');
    button.dataset.action = action;
    button.dataset.ssid = ssid;
    button.addEventListener('click', () => editNetwork(action, ssid));
    return button;
  }

  function tag(label) {
    const node = document.createElement('span');
    node.textContent = label;
    node.classList.add('network-tag');
    return node;
  }

  function renderSavedNetworks(wifi) {
    const rows = wifi.saved.map((network, index) => {
      const row = document.createElement('li');
      const name = document.createElement('span');
      name.textContent = network.ssid;
      name.classList.add('network-name');
      row.append(name);
      // Only worth saying when there is a choice to be first among.
      if (index === 0 && wifi.saved.length > 1) row.append(tag('первая'));
      if (network.blocked) row.append(tag('выключена до перезагрузки'));
      if (index > 0) row.append(networkButton('Сделать первой', 'wifi.prioritize', network.ssid));
      if (wifi.mode === 'sta_connected' && network.ssid === wifi.active_ssid) {
        row.append(networkButton('Отключиться', 'wifi.disconnect', network.ssid));
      }
      row.append(networkButton('Забыть', 'wifi.forget', network.ssid));
      return row;
    });
    savedNetworks.replaceChildren(...rows);
    savedNetworks.hidden = rows.length === 0;
    savedNetworksEmpty.hidden = rows.length !== 0;
  }

  function failureText(error) {
    if (passwordErrors.has(error)) return 'Неверный пароль или ошибка авторизации';
    // ESP_ERR_NO_MEM as the device reports it: the list holds five networks and
    // this one would have been the sixth. Worth its own words - the visitor can
    // do something about it, unlike a write that failed.
    if (error === 257) return 'Сохранено максимум 5 сетей — сначала забудьте лишнюю';
    if (error === 258) return 'Некорректное название сети или пароль';
    if (error < 0 || error >= 256) return 'Не удалось записать настройки в память';
    if (error === 201) return 'Точка доступа не найдена';
    return `Не удалось подключиться (код ${error})`;
  }

  function setWifiStatus(message, error = false) {
    wifiStatus.textContent = message;
    wifiStatus.classList.toggle('is-error', error);
    wifiStatus.classList.remove('is-success');
  }

  /* Forgetting and switching off both take the connection down with them when
     they land on the network this very page is reachable over, so both ask
     first and say so. */
  function editConfirmText(action, ssid, isActive) {
    if (action === 'wifi.forget') {
      return isActive
        ? `Забыть сеть «${ssid}»? Устройство отключится от неё, и эта страница перестанет отвечать.`
        : `Забыть сеть «${ssid}»? Пароль придётся вводить заново.`;
    }
    return `Отключиться от сети «${ssid}»? Устройство не вернётся к ней до перезагрузки, ` +
      'и эта страница перестанет отвечать.';
  }

  function editSatisfied(wifi) {
    if (!editPending) return false;
    if (editPending.action === 'wifi.forget') {
      return !wifi.saved.some((network) => network.ssid === editPending.ssid);
    }
    if (editPending.action === 'wifi.prioritize') {
      return wifi.saved.length > 0 && wifi.saved[0].ssid === editPending.ssid;
    }
    return wifi.saved.some((network) => network.ssid === editPending.ssid && network.blocked);
  }

  function finishEdit(message, error) {
    if (editTimer !== null) {
      window.clearTimeout(editTimer);
      editTimer = null;
    }
    editPending = null;
    editRequestId = '';
    setWifiStatus(message, error);
  }

  function editNetwork(action, ssid) {
    if (!connected || editPending || !socket || socket.readyState !== WebSocket.OPEN) return;
    if (lastWifi && lastWifi.save_pending) {
      setWifiStatus('Идёт подключение — подождите', true);
      return;
    }
    const isActive = wifiActive.textContent === ssid;
    if (action !== 'wifi.prioritize' &&
        !window.confirm(editConfirmText(action, ssid, isActive))) {
      return;
    }
    requestSequence += 1;
    editRequestId = `settings-${requestSequence}`;
    setWifiStatus(action === 'wifi.prioritize' ? 'Меняем порядок…' : 'Выполняем…');
    const frame = action === 'wifi.disconnect'
      ? {type: 'command', id: editRequestId, action}
      : {type: 'command', id: editRequestId, action, ssid};
    editPending = {action, ssid};
    /* Switching off usually takes this page down with it - the device leaves
       the network the page arrived over - so a timeout here is expected and
       only spoken about while the socket is still up. */
    editTimer = window.setTimeout(() => {
      editTimer = null;
      if (editPending && connected) finishEdit('Устройство не выполнило команду', true);
    }, 5000);
    socket.send(JSON.stringify(frame));
  }

  function finishSave(message, error = false) {
    wifiStatus.textContent = message;
    wifiStatus.classList.toggle('is-error', error);
    wifiStatus.classList.toggle('is-success', !error && message === 'Сохранено');
    saveInFlight = false;
    saveAccepted = false;
    pendingRequestId = '';
    expectedSsid = '';
    submitButton.disabled = !connected;
    // The form has done its job; leaving it open invites a second attempt at
    // something that already worked.
    if (!error) hideForm();
  }

  function showForm(ssid) {
    chosenSsid = ssid;
    form.hidden = false;
    // Picked off the list of what is around, the name is shown rather than
    // typed: there is nothing to get wrong about it.
    wifiChosen.hidden = !ssid;
    wifiChosenName.textContent = ssid || '—';
    wifiSsidRow.hidden = Boolean(ssid);
    if (!ssid) ssidInput.value = '';
    passwordInput.value = '';
    submitButton.disabled = !connected || saveInFlight;
  }

  function hideForm() {
    chosenSsid = '';
    form.hidden = true;
    ssidInput.value = '';
    passwordInput.value = '';
  }

  function signalLabel(rssi) {
    const bars = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
    return '▮'.repeat(bars) + '▯'.repeat(4 - bars);
  }

  function renderScan(networks) {
    const rows = networks
      .filter((network) => isObject(network) && typeof network.ssid === 'string' && network.ssid)
      .map((network) => {
        const row = document.createElement('li');
        const pick = document.createElement('button');
        pick.type = 'button';
        pick.textContent = network.ssid;
        pick.classList.add('network-name');
        pick.addEventListener('click', () => showForm(network.ssid));
        row.append(pick);
        const rssi = Number.isSafeInteger(network.rssi) ? network.rssi : -100;
        row.append(tag(`${signalLabel(rssi)} ${rssi} dBm`));
        if (network.secure === false) row.append(tag('без пароля'));
        return row;
      });
    // A hidden network never announces itself, so there has to be a way in that
    // does not go through the list.
    const other = document.createElement('li');
    const otherPick = document.createElement('button');
    otherPick.type = 'button';
    otherPick.textContent = 'Другая сеть…';
    otherPick.classList.add('network-name');
    otherPick.addEventListener('click', () => showForm(''));
    other.append(otherPick);
    rows.push(other);
    scanNetworks.replaceChildren(...rows);
    scanNetworks.hidden = false;
    scanEmpty.hidden = true;
  }

  function pollScan(attempt) {
    scanTimer = window.setTimeout(() => {
      scanTimer = null;
      window.fetch('/api/wifi-scan')
        .then((response) => response.json())
        .then((body) => {
          const state = safeString(isObject(body) ? body.state : '', 'failed');
          if (state === 'scanning' && attempt < 15) {
            pollScan(attempt + 1);
            return;
          }
          scanning = false;
          if (state !== 'done') {
            scanEmpty.textContent = 'Не удалось найти сети';
            return;
          }
          renderScan(Array.isArray(body.networks) ? body.networks : []);
        })
        .catch(() => {
          scanning = false;
          scanEmpty.textContent = 'Не удалось найти сети';
        });
    }, 700);
  }

  function startScan() {
    if (scanning) return;
    if (scanTimer !== null) {
      window.clearTimeout(scanTimer);
      scanTimer = null;
    }
    scanning = true;
    scanNetworks.replaceChildren();
    scanNetworks.hidden = true;
    scanEmpty.textContent = 'Ищем сети…';
    scanEmpty.hidden = false;
    window.fetch('/api/wifi-scan', {method: 'POST'})
      .then((response) => {
        if (!response.ok) throw new Error('refused');
        pollScan(0);
      })
      .catch(() => {
        scanning = false;
        scanEmpty.textContent = 'Не удалось запустить поиск';
      });
  }

  function applyWifiMode(wifi) {
    const wasApMode = apMode;
    /* The setup AP exactly, not merely "not connected": while an attempt is
       still running the radio is busy with it, and a scan would interrupt the
       very connection it is meant to help set up. */
    apMode = wifi.mode === 'ap_setup';
    wifiScanBlock.hidden = !apMode;
    wifiScan.hidden = !apMode;
    // With no network the list of what is around is the way in, so there is
    // nothing for "Добавить сеть" to open that is not already open.
    wifiAdd.hidden = apMode;
    if (apMode && !wasApMode) hideForm();
    if (apMode && !scanRequested) {
      scanRequested = true;
      startScan();
    }
    if (!apMode) scanRequested = false;
  }

  function applyWifi(value) {
    const wifi = normalizeWifi(value);
    lastWifi = wifi;
    const editDone = editSatisfied(wifi);
    wifiActive.textContent = wifi.active_ssid || '—';
    wifiIp.textContent = wifi.ip || '—';
    renderSavedNetworks(wifi);
    applyWifiMode(wifi);
    if (editDone) {
      finishEdit('Готово', false);
      return;
    }

    if (!saveInFlight) {
      if (wifi.save_pending) {
        saveInFlight = true;
        wifiStatus.textContent = 'Подключение…';
        wifiStatus.classList.remove('is-error', 'is-success');
      } else if (wifi.last_error !== 0) {
        wifiStatus.textContent = failureText(wifi.last_error);
        wifiStatus.classList.add('is-error');
      } else {
        wifiStatus.textContent = wifi.mode === 'sta_connected' ? 'Подключено' : 'Готово к настройке';
        wifiStatus.classList.remove('is-error', 'is-success');
      }
      submitButton.disabled = !connected || saveInFlight;
      return;
    }

    if (wifi.save_pending) {
      wifiStatus.textContent = 'Подключение…';
      wifiStatus.classList.remove('is-error', 'is-success');
      submitButton.disabled = true;
      return;
    }
    if (wifi.last_error !== 0) {
      finishSave(failureText(wifi.last_error), true);
      return;
    }
    if (!expectedSsid) {
      finishSave(wifi.mode === 'sta_connected' ? 'Сохранено' : 'Готово к настройке');
      return;
    }
    if (saveAccepted && wifi.mode === 'sta_connected' &&
        wifi.active_ssid === expectedSsid &&
        wifi.saved.some((network) => network.ssid === expectedSsid)) {
      finishSave('Сохранено');
    }
  }

  function validRevision(message) {
    return Number.isSafeInteger(message.revision) && message.revision >= 0;
  }

  function handleCommandResult(message) {
    const id = safeString(message.id);
    if (editRequestId && id === editRequestId) {
      // A refusal is final; an acceptance only means it was queued, so that
      // one keeps waiting for the list to change.
      if (message.ok !== true) {
        finishEdit(safeString(message.error, 'Команда не выполнена'), true);
      } else if (lastWifi && editSatisfied(lastWifi)) {
        // Already the way it was asked to be - a reorder of a network that is
        // first anyway. Nothing will change, so nothing will arrive to prove it.
        finishEdit('Готово', false);
      }
      return;
    }
    if (!saveInFlight || id !== pendingRequestId) return;
    if (message.ok === true) {
      saveAccepted = true;
      return;
    }
    finishSave(safeString(message.error, 'Команда не выполнена'), true);
  }

  function handleMessage(event) {
    let message;
    try { message = JSON.parse(event.data); } catch (_error) { return; }
    if (!isObject(message) || typeof message.type !== 'string') return;
    if (message.type === 'command.result') {
      handleCommandResult(message);
      return;
    }
    if (message.type === 'snapshot') {
      if (!validRevision(message) || (haveSnapshot && message.revision <= lastRevision)) return;
      lastRevision = message.revision;
      haveSnapshot = true;
      applyWifi(message.wifi);
      // Absent until the device has published its own; the REST load on page
      // open is what fills the fields in the meantime.
      if (isObject(message.settings)) applyLiveSettings(message.settings);
      return;
    }
    if (message.type === 'wifi.update') {
      if (!haveSnapshot || !validRevision(message) || message.revision <= lastRevision) return;
      lastRevision = message.revision;
      applyWifi(message.wifi);
      return;
    }
    if (message.type === 'settings.update') {
      if (!haveSnapshot || !validRevision(message) || message.revision <= lastRevision) return;
      lastRevision = message.revision;
      applyLiveSettings(message.settings);
    }
  }

  function submitWifi(event) {
    event.preventDefault();
    // Picked off the list of what is around, or typed in when it was not there.
    const ssid = chosenSsid || ssidInput.value.trim();
    const password = passwordInput.value;
    passwordInput.value = '';
    if (!connected || saveInFlight || !socket || socket.readyState !== WebSocket.OPEN) return;
    if (!ssid) {
      wifiStatus.textContent = 'Введите название сети';
      wifiStatus.classList.add('is-error');
      return;
    }
    requestSequence += 1;
    pendingRequestId = `settings-${requestSequence}`;
    expectedSsid = ssid;
    saveInFlight = true;
    saveAccepted = false;
    wifiStatus.textContent = 'Проверка…';
    wifiStatus.classList.remove('is-error', 'is-success');
    submitButton.disabled = true;
    const frame = JSON.stringify({
      type: 'command', id: pendingRequestId, action: 'wifi.save', ssid, password,
    });
    socket.send(frame);
  }

  function scheduleReconnect() {
    if (reconnectTimer !== null) return;
    const delay = reconnectDelays[Math.min(reconnectAttempt, reconnectDelays.length - 1)];
    reconnectAttempt += 1;
    reconnectTimer = window.setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, delay);
  }

  function connect() {
    if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) return;
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    socketState.textContent = reconnectAttempt === 0 ? 'Подключение…' : 'Нет связи';
    socketState.classList.add('is-connecting');
    const currentSocket = new WebSocket(`${protocol}//${window.location.host}/ws`);
    socket = currentSocket;
    lastRevision = null;
    haveSnapshot = false;
    currentSocket.addEventListener('open', () => {
      if (socket !== currentSocket) return;
      reconnectAttempt = 0;
      setConnected(true);
    });
    currentSocket.addEventListener('message', (event) => {
      if (socket === currentSocket) handleMessage(event);
    });
    currentSocket.addEventListener('close', () => {
      if (socket !== currentSocket) return;
      socket = null;
      setConnected(false);
      scheduleReconnect();
    });
    currentSocket.addEventListener('error', () => {
      if (socket === currentSocket) currentSocket.close();
    });
  }

  /* The device settings run over REST for the same reason the Yandex section
     below does: they change when someone changes them and never on their own,
     so they have no business in the diff stream that carries the player.

     Neither this page nor the device screen owns them - both write settings.csv
     through the same setters, and the device is told to re-read after a write
     from here. That also means the values shown were read when the page loaded:
     turn the knob on the device and this page will not notice until it is
     reloaded. */

  /* A change made at the device - the volume knob, the encoder in the settings
     screen, a button on the front. It arrives over the socket rather than
     being polled, because settings.csv is eleven reads and nothing else would
     say when it had changed.

     Dropped outright while a control is held or a write of ours is in flight:
     the device pushes about four times a second, and the next one is 250 ms
     away. */
  function applyLiveSettings(payload) {
    if (deviceBusy || deviceHeld !== '') return;
    applyDeviceSettings(payload);
  }

  function setDeviceDisabled(disabled) {
    for (const entry of deviceFields) entry.node.disabled = disabled;
  }

  function applyDeviceSettings(payload) {
    if (!isObject(payload)) return false;
    const available = isObject(payload.available) ? payload.available : {};
    if (Number.isSafeInteger(payload.brightness_min) &&
        Number.isSafeInteger(payload.brightness_max)) {
      // The panel is unreadable below about ten and zero looks like a dead
      // device, so the slider stops where the encoder does - and the device is
      // what says where that is.
      deviceBrightness.min = String(payload.brightness_min);
      deviceBrightness.max = String(payload.brightness_max);
    }
    for (const entry of deviceFields) {
      const value = payload[entry.field];
      if (entry.kind === 'choice') {
        if (typeof value === 'string') entry.node.value = value;
      } else if (entry.kind === 'switch') {
        if (typeof value === 'boolean') entry.node.checked = value;
      } else if (Number.isSafeInteger(value)) {
        entry.node.value = String(value);
        entry.output.textContent = String(value);
      }
      // A field the build does not have is taken off the page rather than
      // disabled: there is nothing behind it to explain.
      if (entry.row) entry.row.hidden = available[entry.gate] !== true;
    }
    return true;
  }

  function refreshDeviceSettings() {
    return window.fetch('/api/settings', {cache: 'no-store'})
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        return response.json();
      })
      .then((payload) => {
        if (!applyDeviceSettings(payload)) throw new Error('unexpected payload');
        deviceStatus.textContent = 'Готово';
        deviceStatus.classList.remove('is-error', 'is-success');
      })
      .catch(() => {
        deviceStatus.textContent = 'Нет связи с устройством';
        deviceStatus.classList.add('is-error');
      });
  }

  function sendDeviceChange(field, value) {
    if (deviceBusy) return Promise.resolve();
    deviceBusy = true;
    setDeviceDisabled(true);
    deviceStatus.textContent = 'Сохранение…';
    deviceStatus.classList.remove('is-error', 'is-success');
    return window.fetch('/api/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({field, value}),
    })
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        return response.json();
      })
      .then((payload) => {
        // The answer is the whole document as the device now has it, so a value
        // it refused or adjusted is what ends up on screen.
        if (!applyDeviceSettings(payload)) throw new Error('unexpected payload');
        deviceStatus.textContent = 'Сохранено';
        deviceStatus.classList.add('is-success');
      })
      .catch(() => {
        // Put the controls back to what the device actually holds: a switch
        // left showing a change that never landed is worse than no answer.
        return refreshDeviceSettings().then(() => {
          deviceStatus.textContent = 'Не удалось сохранить';
          deviceStatus.classList.add('is-error');
          deviceStatus.classList.remove('is-success');
        });
      })
      .then(() => {
        deviceBusy = false;
        setDeviceDisabled(false);
      });
  }

  function bindDeviceFields() {
    for (const entry of deviceFields) {
      if (entry.kind === 'number') {
        // The readout follows the handle; the write waits for it to be let go,
        // or a drag across the range would post every step of the way.
        entry.node.addEventListener('input', () => {
          deviceHeld = entry.field;
          entry.output.textContent = String(entry.node.value);
        });
        entry.node.addEventListener('change', () => {
          deviceHeld = '';
          sendDeviceChange(entry.field, Number(entry.node.value));
        });
        continue;
      }
      entry.node.addEventListener('change', () => {
        sendDeviceChange(entry.field,
                         entry.kind === 'switch' ? entry.node.checked === true
                                                 : String(entry.node.value));
      });
    }
  }

  // Yandex Music runs over REST rather than the WebSocket: it changes a few
  // times per authorisation and never during playback, so it does not belong
  // in the live diff stream that carries the player state.
  const yandexStateText = Object.freeze({
    idle: 'Аккаунт не привязан',
    requesting: 'Запрашиваем код…',
    waiting: 'Ожидание подтверждения',
    authorized: 'Аккаунт привязан',
  });
  const yandexErrorText = Object.freeze({
    network: 'Не удалось связаться с Яндексом',
    timeout: 'Код истёк, попробуйте ещё раз',
    denied: 'Вход не подтверждён',
    server: 'Неожиданный ответ сервера',
    storage: 'Не удалось сохранить токен на устройстве',
  });

  function normalizeYandex(value) {
    const status = isObject(value) ? value : {};
    return {
      state: safeString(status.state, 'idle'),
      error: safeString(status.error, 'none'),
      userCode: safeString(status.user_code),
      verificationUrl: safeString(status.verification_url),
      secondsLeft: Number.isSafeInteger(status.seconds_left) ? status.seconds_left : 0,
      catalog: safeString(status.catalog, 'empty'),
      stations: Array.isArray(status.stations)
        ? status.stations
            .filter((item) => isObject(item) && typeof item.name === 'string')
            .map((item) => ({id: safeString(item.id), name: item.name}))
        : [],
    };
  }

  function renderYandexStations(stations) {
    const rows = stations.map((station) => {
      const row = document.createElement('li');
      row.textContent = station.name;
      return row;
    });
    yandexStations.replaceChildren(...rows);
    yandexStations.hidden = rows.length === 0;
    yandexStationsEmpty.hidden = rows.length !== 0;
  }

  function applyYandex(value) {
    const status = normalizeYandex(value);
    const failed = status.state === 'failed';
    yandexStatus.textContent = failed
      ? (yandexErrorText[status.error] || 'Не удалось привязать аккаунт')
      : (yandexStateText[status.state] || 'Аккаунт не привязан');
    yandexStatus.classList.toggle('is-error', failed);
    yandexStatus.classList.toggle('is-success', status.state === 'authorized');

    const waiting = status.state === 'waiting' && status.userCode !== '';
    yandexCodeBlock.hidden = !waiting;
    if (waiting) {
      yandexCode.textContent = status.userCode;
      yandexUrl.textContent = status.verificationUrl;
      // Only ever the address the device was told to show, and only as http(s):
      // it arrives from the network, and a javascript: URL here would run in
      // the visitor's browser.
      yandexUrl.href = /^https?:\/\//.test(status.verificationUrl)
        ? status.verificationUrl : '#';
      yandexCountdown.textContent = status.secondsLeft > 0
        ? `Код действителен ещё ${status.secondsLeft} с` : '';
    }

    const linked = status.state === 'authorized';
    const busyState = status.state === 'requesting' || status.state === 'waiting';
    yandexLink.hidden = busyState || linked;
    yandexCancel.hidden = !busyState;
    yandexForget.hidden = !linked;
    yandexRefresh.hidden = !linked;
    yandexLink.disabled = yandexBusy;
    yandexCancel.disabled = yandexBusy;
    yandexForget.disabled = yandexBusy;
    yandexRefresh.disabled = yandexBusy || status.catalog === 'loading';

    // Stations belong to the account that is linked right now; after an
    // unlink the block goes away rather than showing a stale list.
    yandexStationsBlock.hidden = !linked;
    if (linked) {
      renderYandexStations(status.stations);
      if (status.catalog === 'loading') {
        yandexStatus.textContent = 'Загрузка станций…';
      } else if (status.catalog === 'failed') {
        yandexStatus.textContent = 'Не удалось получить станции';
        yandexStatus.classList.add('is-error');
        yandexStatus.classList.remove('is-success');
      }
    }
    // Keep polling while a fetch is in the air, so the list appears on its own.
    return busyState || (linked && status.catalog === 'loading');
  }

  function scheduleYandexRefresh(fast) {
    if (yandexTimer !== null) return;
    // Two seconds while a code is on screen so the countdown and the moment of
    // confirmation are visible; rarely otherwise, since nothing changes.
    yandexTimer = window.setTimeout(() => {
      yandexTimer = null;
      refreshYandex();
    }, fast ? 2000 : 15000);
  }

  function refreshYandex() {
    return window.fetch('/api/yandex', {cache: 'no-store'})
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        return response.json();
      })
      .then((payload) => scheduleYandexRefresh(applyYandex(payload)))
      .catch(() => {
        yandexStatus.textContent = 'Нет связи с устройством';
        yandexStatus.classList.add('is-error');
        scheduleYandexRefresh(false);
      });
  }

  function sendYandexAction(action) {
    if (yandexBusy) return Promise.resolve();
    yandexBusy = true;
    yandexLink.disabled = true;
    yandexCancel.disabled = true;
    yandexForget.disabled = true;
    yandexRefresh.disabled = true;
    return window.fetch('/api/yandex', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({action}),
    })
      .then(() => {})
      .catch(() => {})
      .then(() => {
        yandexBusy = false;
        if (yandexTimer !== null) {
          window.clearTimeout(yandexTimer);
          yandexTimer = null;
        }
        return refreshYandex();
      });
  }

  form.addEventListener('submit', submitWifi);
  wifiAdd.addEventListener('click', () => showForm(''));
  wifiCancel.addEventListener('click', () => hideForm());
  wifiScan.addEventListener('click', () => startScan());
  bindDeviceFields();
  yandexLink.addEventListener('click', () => sendYandexAction('begin'));
  yandexCancel.addEventListener('click', () => sendYandexAction('cancel'));
  yandexForget.addEventListener('click', () => sendYandexAction('forget'));
  yandexRefresh.addEventListener('click', () => sendYandexAction('refresh'));
  setConnected(false);
  connect();
  refreshDeviceSettings();
  refreshYandex();
})();
