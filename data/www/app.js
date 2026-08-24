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
  const nextTrack = document.querySelector('#next-track');
  const likeTrack = document.querySelector('#like-track');
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
    list: {kind: '', active_index: null, items: [], path: '', revision: null, has_parent: false},
  };

  // The file listing does not travel over the socket: a directory of long
  // names dwarfs the frame budget, so the socket carries only a revision and
  // the entries come from GET /api/files. Tracked here so a reply for a
  // directory the user has already left can be discarded - opening two folders
  // quickly is enough to make the replies arrive out of order.
  let listFetchRevision = null;
  let listShownRevision = null;

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
    /* Absent unless the track belongs to an account's library, which is the
       device's way of saying there is no mark to draw - so it stays absent
       here rather than becoming a false "not liked". */
    if (typeof player.liked === 'boolean') {
      normalized.liked = player.liked;
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

    /* Only the rotor has a next track. A station has none, and a file list
       advances on its own when a track ends. */
    const skippable = state.activeSource === 'yandex';
    nextTrack.hidden = !skippable;
    nextTrack.disabled = !skippable || (player.state !== 'playing' && player.state !== 'paused');

    /* The mark is shown only where the device sends one, for the same reason:
       a station and a file are in nobody's library, and an empty heart there
       would offer a button that can only be refused. */
    const likeable = typeof player.liked === 'boolean';
    likeTrack.hidden = !likeable;
    likeTrack.disabled = !likeable || (player.state !== 'playing' && player.state !== 'paused');
    likeTrack.classList.toggle('is-liked', likeable && player.liked);
    likeTrack.setAttribute('aria-pressed', String(likeable && player.liked === true));

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
    return items
      .map((item) => `${item.index}\u0000${item.label}\u0000${item.isDirectory ? 'd' : 'f'}`)
      .join('\u0001');
  }

  function normalizeList(value) {
    if (!isObject(value)) return null;
    const active_index = value.active_index === null || Number.isSafeInteger(value.active_index)
      ? value.active_index
      : null;
    const kind = safeString(value.kind);
    if (kind === 'files') {
      // A file listing arrives as a header only; the entries are fetched
      // separately, so the ones already on screen are kept until they do.
      return {
        kind,
        active_index,
        items: state.list.kind === 'files' ? state.list.items : [],
        path: safeString(value.path),
        revision: Number.isSafeInteger(value.revision) ? value.revision : null,
        has_parent: value.has_parent === true,
      };
    }
    if (!Array.isArray(value.items)) return null;
    const items = value.items
      .filter((item) => isObject(item) && Number.isSafeInteger(item.index) && typeof item.label === 'string')
      .map((item) => ({index: item.index, label: item.label}));
    return {kind, active_index, items, path: '', revision: null, has_parent: false};
  }

  function fileEntryLabel(entry) {
    if (entry.kind === 'dir') return `${entry.name}/`;
    return entry.format ? `${entry.name} · ${entry.format}` : entry.name;
  }

  function normalizeFileEntries(payload) {
    if (!isObject(payload) || !Array.isArray(payload.items)) return null;
    return payload.items
      .filter((item) => isObject(item) && Number.isSafeInteger(item.index) && typeof item.name === 'string')
      .map((item) => ({
        index: item.index,
        label: fileEntryLabel(item),
        isDirectory: item.kind === 'dir',
      }));
  }

  async function loadFileListing(revision) {
    // Guard against the same revision being fetched twice: player updates
    // arrive far more often than the listing changes.
    if (listFetchRevision === revision || listShownRevision === revision) return;
    listFetchRevision = revision;
    try {
      const response = await fetch('/api/files', {cache: 'no-store'});
      if (!response.ok) throw new Error(`status ${response.status}`);
      const payload = await response.json();
      const entries = normalizeFileEntries(payload);
      if (!entries) throw new Error('unexpected payload');
      // The device may have moved on while this was in flight. Trust the
      // revision the response carries, not the one that triggered the fetch.
      if (state.list.kind !== 'files') return;
      if (listShownRevision !== null && Number.isSafeInteger(payload.revision) &&
          payload.revision < listShownRevision) {
        return;
      }
      const previousList = state.list;
      state.list = {
        ...state.list,
        items: entries,
        path: safeString(payload.path, state.list.path),
        has_parent: payload.has_parent === true,
      };
      listShownRevision = Number.isSafeInteger(payload.revision) ? payload.revision : revision;
      renderList(previousList);
    } catch (error) {
      commandStatus.textContent = 'Не удалось прочитать флешку';
      commandStatus.classList.add('is-error');
    } finally {
      if (listFetchRevision === revision) listFetchRevision = null;
    }
  }

  function syncFileListing() {
    if (state.list.kind !== 'files') {
      listShownRevision = null;
      return;
    }
    if (state.list.revision === null) return;
    if (state.list.revision !== listShownRevision) loadFileListing(state.list.revision);
  }

  function focusedListIndex() {
    const focused = document.activeElement;
    if (!(focused instanceof HTMLElement) || !listItems.contains(focused)) return null;
    const value = Number(focused.dataset.index);
    return Number.isSafeInteger(value) ? value : null;
  }

  function buildParentRow() {
    const row = document.createElement('li');
    const button = document.createElement('button');
    const label = document.createElement('span');

    button.type = 'button';
    button.className = 'list-item';
    button.classList.add('is-directory');
    button.dataset.command = 'browse.up';
    label.className = 'list-item-label';
    label.textContent = '.. (наверх)';
    button.append(label);
    button.addEventListener('click', () => {
      sendCommand('browse.up');
    });
    button.disabled = !state.connected;
    row.append(button);
    return row;
  }

  function rebuildListIfNeeded(previousList) {
    const previousSignature = listSignature(previousList.items);
    const nextSignature = listSignature(state.list.items);
    const parentChanged = Boolean(previousList.has_parent) !== Boolean(state.list.has_parent);
    if (previousSignature === nextSignature && !parentChanged) return;

    const scrollTop = listItems.scrollTop;
    const focusIndex = focusedListIndex();
    const entries = state.list.items.map((item) => {
      const row = document.createElement('li');
      const button = document.createElement('button');
      const label = document.createElement('span');
      const marker = document.createElement('span');

      button.type = 'button';
      button.className = 'list-item';
      // Modifier through classList, matching how is-active is applied below.
      button.classList.toggle('is-directory', Boolean(item.isDirectory));
      button.dataset.command = 'list.select';
      button.dataset.index = String(item.index);
      label.className = 'list-item-label';
      label.textContent = item.label || 'Без названия';
      marker.className = 'active-marker';
      // A directory is opened, never played, so the playing marker would be
      // meaningless on one.
      marker.textContent = item.isDirectory ? 'Открыть' : 'Играет';
      marker.setAttribute('aria-hidden', 'true');
      button.append(label, marker);
      button.addEventListener('click', () => {
        sendCommand('list.select', {index: item.index});
      });
      row.append(button);
      return row;
    });

    // The parent row belongs to the listing rather than to any entry, so it is
    // prepended here instead of being carried in the items array - that keeps
    // every item index equal to the device's own index.
    if (state.list.has_parent) entries.unshift(buildParentRow());
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

    // The current directory is the useful heading for a file listing: "Файлы"
    // alone leaves the user with no idea where they are.
    listTitle.textContent = kind === 'files' && state.list.path
      ? state.list.path
      : label.title;
    listItems.setAttribute('aria-label', label.aria);
    rebuildListIfNeeded(previousList);
    listCount.textContent = String(state.list.items.length);
    // A subdirectory with no playable files still offers the way back, so the
    // list is only truly empty when there is nothing to click at all.
    const rowCount = state.list.items.length + (state.list.has_parent ? 1 : 0);
    listEmpty.hidden = rowCount !== 0;
    listItems.hidden = rowCount === 0;

    listItems.querySelectorAll('button').forEach((button) => {
      button.disabled = !state.connected;
      if (button.dataset.index === undefined) return;
      const active = Number(button.dataset.index) === state.list.active_index;
      button.classList.toggle('is-active', active);
      if (active) {
        button.setAttribute('aria-current', 'true');
      } else {
        button.removeAttribute('aria-current');
      }
    });

    // Every path that changes the list ends here, so this is the single place
    // the REST fetch needs to be triggered from.
    syncFileListing();
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
  nextTrack.addEventListener('click', () => sendCommand('player.next'));
  likeTrack.addEventListener('click', () => sendCommand('player.like'));
  renderSources();
  updatePlaylistLink();
  renderPlayer();
  renderList();
  updateControlAvailability();
  connect();
})();
