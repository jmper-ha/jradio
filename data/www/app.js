(() => {
  'use strict';

  const socketState = document.querySelector('#socket-state');
  const playlistLink = document.querySelector('#playlist-link');
  const sourceTabs = document.querySelector('#source-tabs');
  const modeLabel = document.querySelector('#mode-label');
  const playbackState = document.querySelector('#playback-state');
  const trackTitle = document.querySelector('#track-title');
  const trackArtist = document.querySelector('#track-artist');
  const trackContext = document.querySelector('#track-context');
  const playToggle = document.querySelector('#play-toggle');
  const streamMeta = document.querySelector('#stream-meta');
  const playerError = document.querySelector('#player-error');
  const commandStatus = document.querySelector('#command-status');
  const mediaList = document.querySelector('#media-list');
  const listTitle = document.querySelector('#list-title');
  const listCount = document.querySelector('#list-count');
  const listItems = document.querySelector('#list-items');
  const listEmpty = document.querySelector('#list-empty');

  const playbackLabels = Object.freeze({
    stopped: 'Остановлено',
    connecting: 'Подключение',
    playing: 'Воспроизведение',
    paused: 'Пауза',
    reconnecting: 'Переподключение',
    error: 'Ошибка',
    unknown: 'Неизвестное состояние',
  });
  const listLabels = Object.freeze({
    stations: {title: 'Станции', aria: 'Список станций'},
    folders: {title: 'Папки', aria: 'Список папок'},
    files: {title: 'Файлы', aria: 'Список файлов'},
  });
  const reconnectDelays = Object.freeze([500, 1000, 2000, 4000, 8000]);

  const state = {
    connected: false,
    capabilities: [],
    activeSource: 'none',
    player: {
      state: 'stopped',
      mode: 'Нет источника',
      artist: '',
      title: '',
      context: '',
      codec: '',
      bitrate_kbps: 0,
      sample_rate_hz: 0,
      error: '',
    },
    list: {kind: '', active_index: null, items: []},
  };

  let socket = null;
  let reconnectTimer = null;
  let reconnectAttempt = 0;
  let requestSequence = 0;
  let lastRevision = null;
  let haveSnapshot = false;
  const pendingCommands = new Map();

  function isObject(value) {
    return value !== null && typeof value === 'object' && !Array.isArray(value);
  }

  function safeString(value, fallback = '') {
    return typeof value === 'string' ? value : fallback;
  }

  function safeInteger(value, fallback = 0) {
    return Number.isSafeInteger(value) ? value : fallback;
  }

  function normalizePlayer(value) {
    const player = isObject(value) ? value : {};
    const normalized = {
      state: safeString(player.state, 'unknown'),
      mode: safeString(player.mode, 'Нет источника'),
      artist: safeString(player.artist),
      title: safeString(player.title),
      context: safeString(player.context),
      codec: safeString(player.codec),
      bitrate_kbps: safeInteger(player.bitrate_kbps),
      sample_rate_hz: safeInteger(player.sample_rate_hz),
      error: safeString(player.error),
    };
    if (Number.isInteger(player.wifi_rssi_dbm)) {
      normalized.wifi_rssi_dbm = player.wifi_rssi_dbm;
    }
    return normalized;
  }

  function setConnected(connected) {
    state.connected = connected;
    socketState.textContent = connected ? 'Подключено' : 'Нет связи';
    socketState.classList.toggle('is-online', connected);
    socketState.classList.toggle('is-offline', !connected);
    socketState.classList.remove('is-connecting');
    updateControlAvailability();
  }

  function updateControlAvailability() {
    document.querySelectorAll('button[data-command], #play-toggle').forEach((button) => {
      button.disabled = !state.connected;
    });
  }

  function sourceSignature(capabilities) {
    return capabilities.map((source) => `${source.id}\u0000${source.label}\u0000${source.list_kind || ''}`).join('\u0001');
  }

  function normalizeCapabilities(value) {
    if (!Array.isArray(value)) return state.capabilities;
    return value
      .filter((item) => isObject(item) && typeof item.id === 'string' && typeof item.label === 'string')
      .map((item) => ({
        id: item.id,
        label: item.label,
        list_kind: safeString(item.list_kind),
      }));
  }

  function renderSources() {
    const previousSignature = sourceTabs.dataset.signature || '';
    const nextSignature = sourceSignature(state.capabilities);
    if (previousSignature !== nextSignature) {
      const buttons = state.capabilities.map((source) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'source-tab';
        button.dataset.command = 'source.select';
        button.dataset.source = source.id;
        button.textContent = source.label;
        button.addEventListener('click', () => {
          sendCommand('source.select', {source: source.id});
        });
        return button;
      });
      sourceTabs.replaceChildren(...buttons);
      sourceTabs.dataset.signature = nextSignature;
    }

    sourceTabs.querySelectorAll('button[data-source]').forEach((button) => {
      const active = button.dataset.source === state.activeSource;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-pressed', String(active));
      button.disabled = !state.connected;
    });
    sourceTabs.hidden = state.capabilities.length === 0;
  }

  function updatePlaylistLink() {
    playlistLink.hidden = state.activeSource !== 'internet_radio';
  }

  function stateLabel(value) {
    return playbackLabels[value] || playbackLabels.unknown;
  }

  function renderPlayer() {
    const player = state.player;
    const playing = player.state === 'playing';

    modeLabel.textContent = safeString(player.mode, 'Нет источника');
    playbackState.textContent = stateLabel(player.state);
    playbackState.dataset.state = safeString(player.state, 'unknown');
    trackTitle.textContent = safeString(player.title) || safeString(player.context) || 'Нет воспроизведения';
    trackArtist.textContent = safeString(player.artist);
    trackArtist.hidden = trackArtist.textContent.length === 0;
    trackContext.textContent = safeString(player.context);
    trackContext.hidden = trackContext.textContent.length === 0 ||
      trackContext.textContent === trackTitle.textContent;

    playToggle.classList.toggle('is-playing', playing);
    playToggle.setAttribute('aria-label', playing ? 'Пауза' : 'Воспроизвести');
    playToggle.setAttribute('aria-pressed', String(playing));

    const metadata = [];
    const codec = safeString(player.codec).trim();
    const bitrate = safeInteger(player.bitrate_kbps);
    if (codec) metadata.push(codec.toUpperCase());
    if (bitrate > 0) metadata.push(`${bitrate} кбит/с`);
    const sampleRate = safeInteger(player.sample_rate_hz);
    if (sampleRate > 0) metadata.push(`${sampleRate} Гц`);
    if (Number.isInteger(player.wifi_rssi_dbm)) metadata.push(`Wi-Fi ${player.wifi_rssi_dbm} дБм`);
    streamMeta.textContent = metadata.length > 0
      ? metadata.join(' · ')
      : 'Параметры потока появятся после подключения';

    playerError.textContent = safeString(player.error);
    playerError.hidden = playerError.textContent.length === 0;
    updateControlAvailability();
  }

  function listSignature(items) {
    return items.map((item) => `${item.index}\u0000${item.label}`).join('\u0001');
  }

  function normalizeList(value) {
    if (!isObject(value) || !Array.isArray(value.items)) return null;
    const items = value.items
      .filter((item) => isObject(item) && Number.isSafeInteger(item.index) && typeof item.label === 'string')
      .map((item) => ({index: item.index, label: item.label}));
    return {
      kind: safeString(value.kind),
      active_index: value.active_index === null || Number.isSafeInteger(value.active_index)
        ? value.active_index
        : null,
      items,
    };
  }

  function focusedListIndex() {
    const focused = document.activeElement;
    if (!(focused instanceof HTMLElement) || !listItems.contains(focused)) return null;
    const value = Number(focused.dataset.index);
    return Number.isSafeInteger(value) ? value : null;
  }

  function rebuildListIfNeeded(previousList) {
    const previousSignature = listSignature(previousList.items);
    const nextSignature = listSignature(state.list.items);
    if (previousSignature === nextSignature) return;

    const scrollTop = listItems.scrollTop;
    const focusIndex = focusedListIndex();
    const entries = state.list.items.map((item) => {
      const row = document.createElement('li');
      const button = document.createElement('button');
      const label = document.createElement('span');
      const marker = document.createElement('span');

      button.type = 'button';
      button.className = 'list-item';
      button.dataset.command = 'list.select';
      button.dataset.index = String(item.index);
      label.className = 'list-item-label';
      label.textContent = item.label || 'Без названия';
      marker.className = 'active-marker';
      marker.textContent = 'Играет';
      marker.setAttribute('aria-hidden', 'true');
      button.append(label, marker);
      button.addEventListener('click', () => {
        sendCommand('list.select', {index: item.index});
      });
      row.append(button);
      return row;
    });

    listItems.replaceChildren(...entries);
    listItems.scrollTop = scrollTop;
    if (focusIndex !== null) {
      const target = Array.from(listItems.querySelectorAll('button[data-index]'))
        .find((button) => Number(button.dataset.index) === focusIndex);
      if (target) target.focus({preventScroll: true});
      listItems.scrollTop = scrollTop;
    }
  }

  function renderList(previousList = {items: []}) {
    const activeCapability = state.capabilities.find((source) => source.id === state.activeSource);
    const kind = state.list.kind || (activeCapability && activeCapability.list_kind) || '';
    const label = listLabels[kind] || null;
    const applicable = Boolean(label);

    mediaList.hidden = !applicable;
    if (!applicable) return;

    listTitle.textContent = label.title;
    listItems.setAttribute('aria-label', label.aria);
    rebuildListIfNeeded(previousList);
    listCount.textContent = String(state.list.items.length);
    listEmpty.hidden = state.list.items.length !== 0;
    listItems.hidden = state.list.items.length === 0;

    listItems.querySelectorAll('button[data-index]').forEach((button) => {
      const active = Number(button.dataset.index) === state.list.active_index;
      button.classList.toggle('is-active', active);
      if (active) {
        button.setAttribute('aria-current', 'true');
      } else {
        button.removeAttribute('aria-current');
      }
      button.disabled = !state.connected;
    });
  }

  function applySnapshot(message) {
    state.capabilities = normalizeCapabilities(message.capabilities);
    state.activeSource = safeString(message.active_source, 'none');
    if (isObject(message.player)) state.player = normalizePlayer(message.player);
    const nextList = normalizeList(message.list);
    const previousList = state.list;
    if (nextList) state.list = nextList;
    renderSources();
    updatePlaylistLink();
    renderPlayer();
    renderList(previousList);
  }

  function applyCapabilities(message) {
    state.capabilities = normalizeCapabilities(message.capabilities);
    renderSources();
    updatePlaylistLink();
    renderList(state.list);
  }

  function applyPlayer(message) {
    state.activeSource = safeString(message.active_source, state.activeSource);
    if (isObject(message.player)) state.player = normalizePlayer(message.player);
    renderSources();
    updatePlaylistLink();
    renderPlayer();
    renderList(state.list);
  }

  function applyList(message) {
    const nextList = normalizeList(message.list);
    if (!nextList) return;
    const previousList = state.list;
    state.list = nextList;
    renderList(previousList);
  }

  function handleCommandResult(message) {
    const id = safeString(message.id);
    if (!pendingCommands.has(id)) return;
    pendingCommands.delete(id);
    if (message.ok === false) {
      commandStatus.textContent = safeString(message.error, 'Команда не выполнена');
      commandStatus.classList.add('is-error');
    } else if (message.ok === true) {
      commandStatus.textContent = '';
      commandStatus.classList.remove('is-error');
    }
  }

  function validRevision(message) {
    const revision = message.revision;
    return Number.isSafeInteger(revision) && revision >= 0;
  }

  function acceptSnapshotRevision(message) {
    if (!validRevision(message) ||
        (haveSnapshot && message.revision <= lastRevision)) {
      return false;
    }
    lastRevision = message.revision;
    haveSnapshot = true;
    return true;
  }

  function acceptSectionRevision(message) {
    if (!haveSnapshot || !validRevision(message) ||
        message.revision <= lastRevision) {
      return false;
    }
    lastRevision = message.revision;
    return true;
  }

  function handleMessage(event) {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch (_error) {
      return;
    }
    if (!isObject(message) || typeof message.type !== 'string') return;

    switch (message.type) {
      case 'snapshot':
        if (!acceptSnapshotRevision(message)) break;
        applySnapshot(message);
        break;
      case 'capabilities.update':
        if (!acceptSectionRevision(message)) break;
        applyCapabilities(message);
        break;
      case 'player.update':
        if (!acceptSectionRevision(message)) break;
        applyPlayer(message);
        break;
      case 'list.update':
        if (!acceptSectionRevision(message)) break;
        applyList(message);
        break;
      case 'wifi.update':
        acceptSectionRevision(message);
        break;
      case 'command.result':
        handleCommandResult(message);
        break;
      default:
        break;
    }
  }

  function sendCommand(action, fields = {}) {
    if (!state.connected || !socket || socket.readyState !== WebSocket.OPEN) return;
    requestSequence += 1;
    const id = `web-${requestSequence}`;
    const command = {type: 'command', id, action, ...fields};
    const frame = JSON.stringify(command);
    if (frame.length > 512) {
      commandStatus.textContent = 'Команда слишком длинная';
      commandStatus.classList.add('is-error');
      return;
    }
    commandStatus.textContent = '';
    commandStatus.classList.remove('is-error');
    pendingCommands.set(id, action);
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
    socketState.classList.remove('is-online');

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
      if (socket !== currentSocket) return;
      handleMessage(event);
    });
    currentSocket.addEventListener('close', () => {
      if (socket !== currentSocket) return;
      socket = null;
      pendingCommands.clear();
      setConnected(false);
      scheduleReconnect();
    });
    currentSocket.addEventListener('error', () => {
      if (socket === currentSocket) currentSocket.close();
    });
  }

  playToggle.addEventListener('click', () => sendCommand('player.toggle'));
  renderSources();
  updatePlaylistLink();
  renderPlayer();
  renderList();
  updateControlAvailability();
  connect();
})();
