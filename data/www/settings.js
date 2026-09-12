(() => {
  'use strict';

  const t = (key, values) => window.jradioI18n.t(key, values);

  const socketState = document.querySelector('#socket-state');
  const form = document.querySelector('#wifi-form');
  const ssidInput = document.querySelector('#wifi-ssid');
  const passwordInput = document.querySelector('#wifi-password');
  const passwordReveal = document.querySelector('#wifi-password-reveal');
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
  const aboutFirmware = document.querySelector('#about-firmware');
  const aboutBuilt = document.querySelector('#about-built');
  const aboutWeb = document.querySelector('#about-web');
  const aboutIdf = document.querySelector('#about-idf');
  const aboutNotice = document.querySelector('#about-notice');
  const aboutAuthor = document.querySelector('#about-author');
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
  const deviceTimezone = document.querySelector('#device-timezone');
  const backupStatus = document.querySelector('#backup-status');
  const backupFile = document.querySelector('#backup-file');
  const backupRestore = document.querySelector('#backup-restore');
  const deviceBrightness = document.querySelector('#device-brightness');
  const deviceIdleBrightness = document.querySelector('#device-idle-brightness');
  const deviceScreensaverAfter = document.querySelector('#device-screensaver-after');
  const weatherNowRow = document.querySelector('#device-weather-now-row');
  const weatherNow = document.querySelector('#device-weather-now');
  /* The device's own settings screen, field for field, in the order and with
     the wording it uses - so that "Скроллинг: Влево-вправо" means the same
     thing in both places. `row` and `gate` belong to the fields the device
     itself can be without: a build with no Yandex Music or no media server has
     no switch for it, and a board with only one place to go has no home screen
     to choose. */
  const deviceFields = [
    {field: 'language', kind: 'choice', node: document.querySelector('#device-language')},
    {field: 'home_screen', kind: 'choice', node: document.querySelector('#device-home-screen'),
     row: document.querySelector('#device-home-screen-row'), gate: 'home_screen'},
    {field: 'scroll', kind: 'choice', node: document.querySelector('#device-scroll')},
    {field: 'buffer_view', kind: 'choice', node: document.querySelector('#device-buffer-view')},
    {field: 'autoplay', kind: 'switch', node: document.querySelector('#device-autoplay')},
    {field: 'yandex_music', kind: 'switch', node: document.querySelector('#device-yandex'),
     row: document.querySelector('#device-yandex-row'), gate: 'yandex_music'},
    {field: 'dlna', kind: 'switch', node: document.querySelector('#device-dlna'),
     row: document.querySelector('#device-dlna-row'), gate: 'dlna'},
    {field: 'timezone', kind: 'choice', node: deviceTimezone},
    {field: 'ntp_server', kind: 'text', node: document.querySelector('#device-ntp')},
    {field: 'weather', kind: 'choice', node: document.querySelector('#device-weather')},
    {field: 'weather_latitude', kind: 'text',
     node: document.querySelector('#device-weather-latitude')},
    {field: 'weather_longitude', kind: 'text',
     node: document.querySelector('#device-weather-longitude')},
    /* A secret: sent like a text field, never sent back. The row exists only
       while the service that wants it is the chosen one. */
    {field: 'openweathermap_key', kind: 'secret',
     node: document.querySelector('#device-weather-key'),
     row: document.querySelector('#device-weather-key-row')},
    {field: 'brightness', kind: 'number', node: deviceBrightness,
     output: document.querySelector('#device-brightness-value')},
    {field: 'screensaver', kind: 'choice', node: document.querySelector('#device-screensaver')},
    /* A number picked off a list rather than a slider: the wait is one of six
       steps the device names, and a slider would offer every second between. */
    {field: 'screensaver_seconds', kind: 'number',
     node: document.querySelector('#device-screensaver-after')},
    {field: 'screensaver_brightness', kind: 'number', node: deviceIdleBrightness,
     output: document.querySelector('#device-idle-brightness-value')},
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
    socketState.textContent = value ? t('wifi.connected') : t('wifi.no_link');
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
      if (index === 0 && wifi.saved.length > 1) row.append(tag(t('wifi.first')));
      if (network.blocked) row.append(tag(t('wifi.disabled_until_reboot')));
      if (index > 0) row.append(networkButton(t('wifi.make_first'), 'wifi.prioritize', network.ssid));
      if (wifi.mode === 'sta_connected' && network.ssid === wifi.active_ssid) {
        row.append(networkButton(t('wifi.disconnect'), 'wifi.disconnect', network.ssid));
      }
      row.append(networkButton(t('wifi.forget'), 'wifi.forget', network.ssid));
      return row;
    });
    savedNetworks.replaceChildren(...rows);
    savedNetworks.hidden = rows.length === 0;
    savedNetworksEmpty.hidden = rows.length !== 0;
  }

  function failureText(error) {
    if (passwordErrors.has(error)) return t('wifi.bad_password');
    // ESP_ERR_NO_MEM as the device reports it: the list holds five networks and
    // this one would have been the sixth. Worth its own words - the visitor can
    // do something about it, unlike a write that failed.
    if (error === 257) return t('wifi.full');
    if (error === 258) return t('wifi.bad_name');
    if (error < 0 || error >= 256) return t('wifi.write_failed');
    if (error === 201) return t('wifi.ap_not_found');
    return t('wifi.connect_failed', {code: error});
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
        ? t('wifi.forget_active_confirm', {ssid}) + t('wifi.page_stops')
        : t('wifi.forget_confirm', {ssid});
    }
    return t('wifi.disconnect_confirm', {ssid}) + t('wifi.page_stops');
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
      setWifiStatus(t('wifi.busy'), true);
      return;
    }
    const isActive = wifiActive.textContent === ssid;
    if (action !== 'wifi.prioritize' &&
        !window.confirm(editConfirmText(action, ssid, isActive))) {
      return;
    }
    requestSequence += 1;
    editRequestId = `settings-${requestSequence}`;
    setWifiStatus(action === 'wifi.prioritize' ? t('settings.reorder') : t('common.working'));
    const frame = action === 'wifi.disconnect'
      ? {type: 'command', id: editRequestId, action}
      : {type: 'command', id: editRequestId, action, ssid};
    editPending = {action, ssid};
    /* Switching off usually takes this page down with it - the device leaves
       the network the page arrived over - so a timeout here is expected and
       only spoken about while the socket is still up. */
    editTimer = window.setTimeout(() => {
      editTimer = null;
      if (editPending && connected) finishEdit(t('common.device_refused'), true);
    }, 5000);
    socket.send(JSON.stringify(frame));
  }

  function finishSave(message, error = false) {
    wifiStatus.textContent = message;
    wifiStatus.classList.toggle('is-error', error);
    wifiStatus.classList.toggle('is-success', !error && message === t('common.saved'));
    saveInFlight = false;
    saveAccepted = false;
    pendingRequestId = '';
    expectedSsid = '';
    submitButton.disabled = !connected;
    // The form has done its job; leaving it open invites a second attempt at
    // something that already worked.
    if (!error) hideForm();
  }

  /* Shows the password while it is being typed. A field that hides what is in
     it is where a wrong character goes unnoticed, and on a phone keyboard that
     is most of them - so the reveal exists. Kept in a variable rather than read
     back off the input: the state belongs to this page, and reading a DOM
     attribute to decide what to do to it is how the two drift apart.

     Every path that empties the field turns it off again. The field is cleared
     the moment a password is sent, and a form left in "text" would then show
     the *next* password before anyone asked it to. */
  let passwordVisible = false;

  function setPasswordVisible(visible) {
    passwordVisible = visible;
    passwordInput.type = visible ? 'text' : 'password';
    passwordReveal.textContent = visible ? t('common.hide') : t('common.show');
    passwordReveal.setAttribute('aria-pressed', visible ? 'true' : 'false');
  }

  function clearPassword() {
    passwordInput.value = '';
    setPasswordVisible(false);
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
    clearPassword();
    submitButton.disabled = !connected || saveInFlight;
  }

  function hideForm() {
    chosenSsid = '';
    form.hidden = true;
    ssidInput.value = '';
    clearPassword();
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
        if (network.secure === false) row.append(tag(t('wifi.open_network')));
        return row;
      });
    // A hidden network never announces itself, so there has to be a way in that
    // does not go through the list.
    const other = document.createElement('li');
    const otherPick = document.createElement('button');
    otherPick.type = 'button';
    otherPick.textContent = t('wifi.other_network');
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
            scanEmpty.textContent = t('wifi.scan_failed');
            return;
          }
          renderScan(Array.isArray(body.networks) ? body.networks : []);
        })
        .catch(() => {
          scanning = false;
          scanEmpty.textContent = t('wifi.scan_failed');
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
    scanEmpty.textContent = t('wifi.scanning');
    scanEmpty.hidden = false;
    window.fetch('/api/wifi-scan', {method: 'POST'})
      .then((response) => {
        if (!response.ok) throw new Error('refused');
        pollScan(0);
      })
      .catch(() => {
        scanning = false;
        scanEmpty.textContent = t('wifi.scan_start_failed');
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
      finishEdit(t('common.ready'), false);
      return;
    }

    if (!saveInFlight) {
      if (wifi.save_pending) {
        saveInFlight = true;
        wifiStatus.textContent = t('wifi.connecting');
        wifiStatus.classList.remove('is-error', 'is-success');
      } else if (wifi.last_error !== 0) {
        wifiStatus.textContent = failureText(wifi.last_error);
        wifiStatus.classList.add('is-error');
      } else {
        wifiStatus.textContent = wifi.mode === 'sta_connected' ? t('wifi.connected') : t('wifi.setup_ready');
        wifiStatus.classList.remove('is-error', 'is-success');
      }
      submitButton.disabled = !connected || saveInFlight;
      return;
    }

    if (wifi.save_pending) {
      wifiStatus.textContent = t('wifi.connecting');
      wifiStatus.classList.remove('is-error', 'is-success');
      submitButton.disabled = true;
      return;
    }
    if (wifi.last_error !== 0) {
      finishSave(failureText(wifi.last_error), true);
      return;
    }
    if (!expectedSsid) {
      finishSave(wifi.mode === 'sta_connected' ? t('common.saved') : t('wifi.setup_ready'));
      return;
    }
    if (saveAccepted && wifi.mode === 'sta_connected' &&
        wifi.active_ssid === expectedSsid &&
        wifi.saved.some((network) => network.ssid === expectedSsid)) {
      finishSave(t('common.saved'));
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
        finishEdit(safeString(message.error, t('common.command_failed')), true);
      } else if (lastWifi && editSatisfied(lastWifi)) {
        // Already the way it was asked to be - a reorder of a network that is
        // first anyway. Nothing will change, so nothing will arrive to prove it.
        finishEdit(t('common.ready'), false);
      }
      return;
    }
    if (!saveInFlight || id !== pendingRequestId) return;
    if (message.ok === true) {
      saveAccepted = true;
      return;
    }
    finishSave(safeString(message.error, t('common.command_failed')), true);
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
    clearPassword();
    if (!connected || saveInFlight || !socket || socket.readyState !== WebSocket.OPEN) return;
    if (!ssid) {
      wifiStatus.textContent = t('wifi.need_ssid');
      wifiStatus.classList.add('is-error');
      return;
    }
    requestSequence += 1;
    pendingRequestId = `settings-${requestSequence}`;
    expectedSsid = ssid;
    saveInFlight = true;
    saveAccepted = false;
    wifiStatus.textContent = t('common.checking');
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
    socketState.textContent = reconnectAttempt === 0 ? t('wifi.connecting') : t('wifi.no_link');
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

  /* The zones come from the device rather than sitting in the markup: the list
     lives in the firmware, and a page carrying its own copy would offer a zone
     the device cannot translate the day one is added. Only the REST document
     carries it - a live update leaves the options alone. */
  function fillTimezones(zones) {
    if (!Array.isArray(zones) || zones.length === 0) return;
    const ids = zones.map((zone) => zone && zone.id).join('\n');
    if (deviceTimezone.dataset.zones === ids) return;
    deviceTimezone.dataset.zones = ids;
    deviceTimezone.replaceChildren(...zones.map((zone) => {
      const option = document.createElement('option');
      option.value = String(zone.id);
      option.textContent = String(zone.label === undefined ? zone.id : zone.label);
      return option;
    }));
  }

  /* The waits the device offers, as it names them. The page carries a
     default list so it is usable before the answer arrives; the device's
     replaces it, so a step added in the firmware needs no page change. */
  function fillSecondsChoices(choices) {
    if (!Array.isArray(choices) || choices.length === 0) return;
    if (!choices.every((value) => Number.isSafeInteger(value) && value > 0)) return;
    const key = choices.join(',');
    if (deviceScreensaverAfter.dataset.choices === key) return;
    deviceScreensaverAfter.dataset.choices = key;
    deviceScreensaverAfter.replaceChildren(...choices.map((value) => {
      const option = document.createElement('option');
      option.value = String(value);
      option.textContent = String(value);
      return option;
    }));
  }

  function applyDeviceSettings(payload) {
    if (!isObject(payload)) return false;
    applyLanguage(payload);
    const available = isObject(payload.available) ? payload.available : {};
    // Before the values below, or the zone would be set on an empty list.
    fillTimezones(payload.timezones);
    fillSecondsChoices(payload.screensaver_seconds_choices);
    if (Number.isSafeInteger(payload.idle_brightness_min) &&
        Number.isSafeInteger(payload.idle_brightness_max)) {
      deviceIdleBrightness.min = String(payload.idle_brightness_min);
      deviceIdleBrightness.max = String(payload.idle_brightness_max);
    }
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
      } else if (entry.kind === 'text') {
        /* Left alone while it is being typed in, and left alone when the
           answer does not carry it at all - a live update over the socket
           never does, because a time server changes once in a device's
           life. */
        if (typeof value === 'string' && deviceHeld !== entry.field) {
          entry.node.value = value;
        }
      } else if (entry.kind === 'switch') {
        if (typeof value === 'boolean') entry.node.checked = value;
      } else if (entry.kind === 'secret') {
        /* The device says whether it has one and never what it is. The
           field is emptied once an answer arrives - what was typed is on the
           card now or was refused - and the placeholder says which state the
           device is in. Only the REST document carries the flag. */
        if (typeof payload.openweathermap_key_set === 'boolean' && deviceHeld !== entry.field) {
          entry.node.value = '';
          entry.node.placeholder =
            t(payload.openweathermap_key_set ? 'settings.key_set' : 'settings.key_unset');
        }
        if (typeof payload.weather === 'string') {
          entry.row.hidden = payload.weather !== 'openweathermap';
        }
        continue;
      } else if (Number.isSafeInteger(value)) {
        entry.node.value = String(value);
        if (entry.output) entry.output.textContent = String(value);
      }
      // A field the build does not have is taken off the page rather than
      // disabled: there is nothing behind it to explain.
      if (entry.row) entry.row.hidden = available[entry.gate] !== true;
    }
    applyWeatherState(payload);
    return true;
  }

  /* What the device's weather task last said, under the picker: the reading
     when there is one, and otherwise why there is not. Only the REST document
     carries it; a live update leaves the line alone. */
  const weatherIconText = Object.freeze({
    clear_day: 'weather.clear',
    clear_night: 'weather.clear',
    partly_cloudy_day: 'weather.partly_cloudy',
    partly_cloudy_night: 'weather.partly_cloudy',
    cloudy: 'weather.cloudy',
    fog: 'weather.fog',
    rain: 'weather.rain',
    snow: 'weather.snow',
    sleet: 'weather.sleet',
    thunderstorm: 'weather.thunderstorm',
  });
  let weatherRefreshTimer = null;

  function weatherStateText(payload) {
    const report = payload.weather_report;
    if (isObject(report) && Number.isSafeInteger(report.temperature)) {
      const degrees = report.temperature > 0 ? `+${report.temperature}°` : `${report.temperature}°`;
      const key = weatherIconText[report.icon];
      return key ? `${degrees}, ${t(key)}` : degrees;
    }
    const status = Number.isSafeInteger(payload.weather_http_status) ? payload.weather_http_status : 0;
    switch (payload.weather_state) {
    case 'no_key': return t('weather.no_key');
    case 'waiting': return t('weather.waiting');
    case 'failed':
      if (status === 401 || status === 403) return t('weather.key_refused');
      return status > 0 ? t('weather.failed_status', {status}) : t('weather.failed');
    default: return '';
    }
  }

  function applyWeatherState(payload) {
    if (typeof payload.weather_state !== 'string') return;
    /* A service that is on with a task that still says off has not been
       told yet: that is a wait, not a blank. */
    if (payload.weather !== 'off' && payload.weather_state === 'off') {
      payload = {...payload, weather_state: 'waiting'};
    }
    const text = payload.weather === 'off' ? '' : weatherStateText(payload);
    weatherNowRow.hidden = text === '';
    weatherNow.textContent = text;
    weatherNow.classList.toggle('is-ok', payload.weather_state === 'ok');
    /* A service just switched on answers within a few seconds; the page asks
       again once so the line does not say "waiting" until it is reloaded. */
    if (weatherRefreshTimer !== null) {
      window.clearTimeout(weatherRefreshTimer);
      weatherRefreshTimer = null;
    }
    if (payload.weather_state === 'waiting') {
      weatherRefreshTimer = window.setTimeout(() => {
        weatherRefreshTimer = null;
        if (!deviceBusy && deviceHeld === '') refreshDeviceSettings();
      }, 5000);
    }
  }

  /* The language is a device setting like any other, so it arrives with the
     rest of them - which is what lets the row on the device's own screen
     relabel this page, and the picker on this page relabel the screen. The
     markup is relabelled by i18n itself; these are the parts the page draws,
     which have to be drawn again. */
  function applyLanguage(payload) {
    if (!isObject(payload) || typeof payload.language !== 'string') return;
    if (!window.jradioI18n.setLanguage(payload.language)) return;
    /* The Wi-Fi rows, the account card and the version card carry words this
       page builds rather than words the markup holds, so they are asked for
       again. Waiting for their own polls would leave half the page in the
       language it was in a moment ago. */
    if (lastWifi !== null) renderSavedNetworks(lastWifi);
    refreshAbout();
    refreshYandex();
  }

  function refreshDeviceSettings() {
    return window.fetch('/api/settings', {cache: 'no-store'})
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        return response.json();
      })
      .then((payload) => {
        if (!applyDeviceSettings(payload)) throw new Error('unexpected payload');
        deviceStatus.textContent = t('common.ready');
        deviceStatus.classList.remove('is-error', 'is-success');
      })
      .catch(() => {
        deviceStatus.textContent = t('common.no_device');
        deviceStatus.classList.add('is-error');
      });
  }

  function sendDeviceChange(field, value) {
    if (deviceBusy) return Promise.resolve();
    deviceBusy = true;
    setDeviceDisabled(true);
    deviceStatus.textContent = t('settings.saving');
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
        deviceStatus.textContent = t('common.saved');
        deviceStatus.classList.add('is-success');
      })
      .catch(() => {
        // Put the controls back to what the device actually holds: a switch
        // left showing a change that never landed is worse than no answer.
        return refreshDeviceSettings().then(() => {
          deviceStatus.textContent = t('common.save_failed');
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
        // or a drag across the range would post every step of the way. A list
        // has no handle and no readout - only the change.
        if (entry.output) {
          entry.node.addEventListener('input', () => {
            deviceHeld = entry.field;
            entry.output.textContent = String(entry.node.value);
          });
        }
        entry.node.addEventListener('change', () => {
          deviceHeld = '';
          sendDeviceChange(entry.field, Number(entry.node.value));
        });
        continue;
      }
      if (entry.kind === 'text' || entry.kind === 'secret') {
        /* Typing is not saving: the write goes out when the field is left or
           Enter is pressed, which is what `change` means for a text input. A
           per-keystroke write would put a dozen half-typed host names on the
           card, each one a flash erase. */
        entry.node.addEventListener('input', () => { deviceHeld = entry.field; });
        entry.node.addEventListener('change', () => {
          deviceHeld = '';
          sendDeviceChange(entry.field, String(entry.node.value).trim());
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

  /* Backup and restore. The archive is built and unpacked on the device, not
     here: the browser would need the same rules about which files count and
     what a restored file has to look like, and two copies of that is how the
     page and the device end up disagreeing about what a valid backup is. */
  /* Keys, not words: this is built once when the page loads, and the language
     changes afterwards. Frozen with the text in it, the first language the page
     ever saw was the one these lines kept for ever. */
  const restoreErrorText = Object.freeze({
    size: 'backup.err_size',
    incomplete: 'backup.err_incomplete',
    memory: 'backup.err_memory',
    compressed: 'backup.err_compressed',
    damaged: 'backup.err_damaged',
    malformed: 'backup.err_malformed',
    empty: 'backup.err_empty',
    'unknown-file': 'backup.err_unknown_file',
    contents: 'backup.err_contents',
    write: 'backup.err_write',
  });
  let restoreInFlight = false;

  function setBackupStatus(text, kind) {
    backupStatus.textContent = text;
    backupStatus.classList.toggle('is-error', kind === 'error');
    backupStatus.classList.toggle('is-success', kind === 'success');
  }

  function chosenFile() {
    const files = backupFile.files;
    return files && files.length > 0 ? files[0] : null;
  }

  function restoreChosenFile() {
    const file = chosenFile();
    if (file === null || restoreInFlight) return Promise.resolve();
    if (window.confirm(t('backup.confirm', {name: file.name}) +
                       t('backup.confirm_tail')) !== true) {
      return Promise.resolve();
    }
    restoreInFlight = true;
    backupRestore.disabled = true;
    setBackupStatus(t('common.sending'));
    /* The name travels in the query, because a single file is recognised by it
       - an archive says what is in it, but wifi.json on its own does not. */
    return window.fetch(`/api/restore?name=${encodeURIComponent(file.name)}`,
                        {method: 'POST', body: file})
      .then((response) => {
        if (!response) throw new Error('no answer');
        return response.json().then(
          (payload) => ({ok: response.ok === true, payload}),
          () => ({ok: response.ok === true, payload: {}}));
      })
      .then((result) => {
        if (!result.ok) {
          const code = typeof result.payload.error === 'string' ? result.payload.error : '';
          /* Two of these carry a tail the dictionary keeps separately - a
             list of file names, and a sentence about what to upload instead -
             because neither belongs inside a translated phrase. */
          const key = restoreErrorText[code];
          let text = key ? t(key) : t('backup.refused');
          if (code === 'compressed') text += t('backup.err_compressed_tail');
          if (code === 'unknown-file') text += 'wifi.json, settings.csv, yandex.json';
          setBackupStatus(text, 'error');
          return;
        }
        const restored = Array.isArray(result.payload.restored) ? result.payload.restored : [];
        const warnings = Array.isArray(result.payload.warnings) ? result.payload.warnings : [];
        if (warnings.length > 0) {
          /* Written, but the device could not read it back - the case that
             would otherwise be discovered as a device on the setup access
             point with nothing said about why. */
          setBackupStatus(t('backup.restored_warn', {files: restored.join(', ')}) +
                          t('backup.unreadable', {files: warnings.join(', ')}) +
                          t('backup.rebooting'), 'error');
        } else {
          setBackupStatus(t('backup.restored', {files: restored.join(', ')}),
                          'success');
        }
        /* The file is cleared either way: the same upload sent twice after a
           reboot would restore over settings the user may have changed since. */
        backupFile.value = '';
      })
      .catch(() => {
        setBackupStatus(t('backup.send_failed'), 'error');
      })
      .then(() => {
        restoreInFlight = false;
        backupRestore.disabled = chosenFile() === null;
      });
  }

  function bindBackup() {
    /* Set from the file input rather than trusted to the markup, the way the
       password button's label is: the two are written in different files and
       a Restore button that starts live sends an empty body. */
    backupRestore.disabled = chosenFile() === null;
    backupFile.addEventListener('change', () => {
      const file = chosenFile();
      backupRestore.disabled = file === null;
      setBackupStatus(file === null ? t('common.ready') : t('backup.chosen', {name: file.name}));
    });
    backupRestore.addEventListener('click', () => restoreChosenFile());
  }

  // Yandex Music runs over REST rather than the WebSocket: it changes a few
  // times per authorisation and never during playback, so it does not belong
  // in the live diff stream that carries the player state.
  // Keys for the same reason as restoreErrorText above.
  const yandexStateText = Object.freeze({
    idle: 'yandex.not_linked',
    requesting: 'yandex.requesting',
    waiting: 'yandex.waiting',
    authorized: 'yandex.linked',
  });
  const yandexErrorText = Object.freeze({
    network: 'yandex.no_connection',
    timeout: 'yandex.expired',
    denied: 'yandex.denied',
    server: 'yandex.server_error',
    storage: 'yandex.storage_failed',
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
      ? t(yandexErrorText[status.error] || 'yandex.link_failed')
      : t(yandexStateText[status.state] || 'yandex.not_linked');
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
        ? t('yandex.seconds_left', {n: status.secondsLeft}) : '';
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
        yandexStatus.textContent = t('yandex.loading');
      } else if (status.catalog === 'failed') {
        yandexStatus.textContent = t('yandex.list_failed');
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

  /* Versions never change while the device is running, so this is fetched
     once when the page loads and never polled. */
  function applyAbout(payload) {
    if (!isObject(payload)) return;
    const firmware = isObject(payload.firmware) ? payload.firmware : {};
    const web = isObject(payload.web) ? payload.web : {};
    const named = (value) => (typeof value === 'string' && value ? value : t('common.unknown'));
    aboutFirmware.textContent = named(firmware.version);
    aboutBuilt.textContent = named(firmware.built);
    aboutWeb.textContent = named(web.version);
    aboutIdf.textContent = named(payload.idf);
    /* Only when the two are known and differ. A web half that could not be
       read is old, not mismatched, and the "неизвестно" beside it has already
       said so - the same rule the device's own screen follows. */
    const mismatched = firmware.present === true && web.present === true &&
                       payload.matched === false;
    aboutNotice.hidden = !mismatched;
    aboutNotice.textContent = mismatched
      ? t('about.mismatch')
      : '';
    if (typeof payload.author === 'string' && payload.author) {
      aboutAuthor.textContent = payload.author;
      aboutAuthor.href = `mailto:${payload.author}`;
    }
  }

  function refreshAbout() {
    return window.fetch('/api/about', {cache: 'no-store'})
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        return response.json();
      })
      .then(applyAbout)
      /* A device that cannot answer leaves the dashes that are already there:
         this card is informational, and a failure notice for it would sit
         beside the connection state the page already shows. */
      .catch(() => {});
  }

  function refreshYandex() {
    return window.fetch('/api/yandex', {cache: 'no-store'})
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        return response.json();
      })
      .then((payload) => scheduleYandexRefresh(applyYandex(payload)))
      .catch(() => {
        yandexStatus.textContent = t('common.no_device');
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

  /* The page folds: one section stands open and the rest are a heading each.
     Unfolded it is five screens of scrolling on a phone, four of them past
     sections nobody came for.

     The status stays outside the button on purpose - saved, no connection, the
     Yandex countdown: all worth reading with the section folded, and a live
     region inside a button is read out as part of the button's own name.

     Which one was open is the page's own shape rather than a device setting,
     so it is remembered here and never written to settings.csv. Storage that
     refuses to answer - a private window, site data switched off - costs the
     memory and nothing else. */
  const SECTION_KEY = 'jradio.settings.section';

  const sections = Array.from(document.querySelectorAll('.card[data-section]'))
    .map((card) => ({
      name: card.dataset.section,
      card,
      toggle: card.querySelector('.card-toggle'),
      body: card.querySelector('.card-body'),
    }))
    .filter((section) => section.toggle !== null && section.body !== null);

  function rememberedSection() {
    try {
      const stored = window.localStorage.getItem(SECTION_KEY);
      // The empty string is a real answer: everything folded, chosen by hand.
      if (stored === '' || sections.some((section) => section.name === stored)) {
        return stored;
      }
    } catch (error) {
      // See above: nothing here is worth a message.
    }
    return sections.length === 0 ? '' : sections[0].name;
  }

  let openSection = rememberedSection();

  /* Folding is a phone's answer to a long page. A screen wide enough for the
     two columns has room for the lot at once and has always shown it, so the
     fold stops at the width the stylesheet changes shape at - the number lives
     in both places because a media query cannot be read from here. */
  const narrow = typeof window.matchMedia === 'function'
    ? window.matchMedia('(max-width: 779px)')
    : null;

  function folding() {
    return narrow === null ? true : narrow.matches === true;
  }

  function applySections() {
    const fold = folding();
    for (const section of sections) {
      const open = !fold || section.name === openSection;
      section.body.hidden = !open;
      section.card.classList.toggle('is-collapsed', !open);
      section.toggle.setAttribute('aria-expanded', open ? 'true' : 'false');
      /* Nothing to disclose on a wide screen, so the heading is not something
         to land on with the keyboard either. The stylesheet takes the arrow
         and the pointer away at the same width. */
      section.toggle.setAttribute('tabindex', fold ? '0' : '-1');
    }
  }

  /* Tapping the open section folds it away: the alternative is a page where
     something is always taking up half the screen and there is no way to say
     "none of these". */
  function toggleSection(name) {
    if (!folding()) return;
    openSection = name === openSection ? '' : name;
    try {
      window.localStorage.setItem(SECTION_KEY, openSection);
    } catch (error) {
      // See rememberedSection().
    }
    applySections();
  }

  function bindSections() {
    for (const section of sections) {
      section.toggle.addEventListener('click', () => toggleSection(section.name));
    }
    /* A phone that is turned on its side crosses the width where the fold
       stops mattering, and the page has to be put right both ways. */
    if (narrow !== null && typeof narrow.addEventListener === 'function') {
      narrow.addEventListener('change', applySections);
    }
    /* The markup comes folded so a phone never paints the whole page first,
       which also means the state it carries has to be put right here - when
       the reader left another section open, and on every wide screen. */
    applySections();
  }

  form.addEventListener('submit', submitWifi);
  wifiAdd.addEventListener('click', () => showForm(''));
  wifiCancel.addEventListener('click', () => hideForm());
  passwordReveal.addEventListener('click', () => setPasswordVisible(!passwordVisible));
  /* Puts the button's label and aria-pressed where the markup says the field
     is, rather than trusting the two to have been written to agree. */
  setPasswordVisible(false);
  wifiScan.addEventListener('click', () => startScan());
  bindSections();
  bindDeviceFields();
  bindBackup();
  yandexLink.addEventListener('click', () => sendYandexAction('begin'));
  yandexCancel.addEventListener('click', () => sendYandexAction('cancel'));
  yandexForget.addEventListener('click', () => sendYandexAction('forget'));
  yandexRefresh.addEventListener('click', () => sendYandexAction('refresh'));
  setConnected(false);
  connect();
  refreshDeviceSettings();
  refreshYandex();
  refreshAbout();
})();
