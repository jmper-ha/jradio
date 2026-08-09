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
      saved_ssids: Array.isArray(wifi.saved_ssids)
        ? wifi.saved_ssids.filter((ssid) => typeof ssid === 'string')
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

  function renderSavedNetworks(items) {
    const rows = items.map((ssid) => {
      const row = document.createElement('li');
      row.textContent = ssid;
      return row;
    });
    savedNetworks.replaceChildren(...rows);
    savedNetworks.hidden = rows.length === 0;
    savedNetworksEmpty.hidden = rows.length !== 0;
  }

  function failureText(error) {
    if (passwordErrors.has(error)) return 'Неверный пароль или ошибка авторизации';
    if (error < 0 || error >= 256) return 'Не удалось записать настройки в память';
    if (error === 201) return 'Точка доступа не найдена';
    return `Не удалось подключиться (код ${error})`;
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
  }

  function applyWifi(value) {
    const wifi = normalizeWifi(value);
    wifiActive.textContent = wifi.active_ssid || '—';
    wifiIp.textContent = wifi.ip || '—';
    renderSavedNetworks(wifi.saved_ssids);

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
        wifi.active_ssid === expectedSsid && wifi.saved_ssids.includes(expectedSsid)) {
      finishSave('Сохранено');
    }
  }

  function validRevision(message) {
    return Number.isSafeInteger(message.revision) && message.revision >= 0;
  }

  function handleCommandResult(message) {
    if (!saveInFlight || safeString(message.id) !== pendingRequestId) return;
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
      return;
    }
    if (message.type === 'wifi.update') {
      if (!haveSnapshot || !validRevision(message) || message.revision <= lastRevision) return;
      lastRevision = message.revision;
      applyWifi(message.wifi);
    }
  }

  function submitWifi(event) {
    event.preventDefault();
    const ssid = ssidInput.value.trim();
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

  form.addEventListener('submit', submitWifi);
  setConnected(false);
  connect();
})();
