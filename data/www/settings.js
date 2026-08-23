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
  const yandexStatus = document.querySelector('#yandex-status');
  const yandexCodeBlock = document.querySelector('#yandex-code-block');
  const yandexUrl = document.querySelector('#yandex-url');
  const yandexCode = document.querySelector('#yandex-code');
  const yandexCountdown = document.querySelector('#yandex-countdown');
  const yandexLink = document.querySelector('#yandex-link');
  const yandexCancel = document.querySelector('#yandex-cancel');
  const yandexForget = document.querySelector('#yandex-forget');

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
  let yandexTimer = null;
  let yandexBusy = false;

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
    };
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

    const busyState = status.state === 'requesting' || status.state === 'waiting';
    yandexLink.hidden = busyState || status.state === 'authorized';
    yandexCancel.hidden = !busyState;
    yandexForget.hidden = status.state !== 'authorized';
    yandexLink.disabled = yandexBusy;
    yandexCancel.disabled = yandexBusy;
    yandexForget.disabled = yandexBusy;
    return busyState;
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
  yandexLink.addEventListener('click', () => sendYandexAction('begin'));
  yandexCancel.addEventListener('click', () => sendYandexAction('cancel'));
  yandexForget.addEventListener('click', () => sendYandexAction('forget'));
  setConnected(false);
  connect();
  refreshYandex();
})();
