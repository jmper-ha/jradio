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
  const previousItem = document.querySelector('#previous-item');
  const nextItem = document.querySelector('#next-item');
  const nextTrack = document.querySelector('#next-track');
  const likeTrack = document.querySelector('#like-track');
  const dislikeTrack = document.querySelector('#dislike-track');
  const trackCover = document.querySelector('#track-cover');
  const trackProgress = document.querySelector('#track-progress');
  const trackElapsed = document.querySelector('#track-elapsed');
  const trackTotal = document.querySelector('#track-total');
  const progressRail = document.querySelector('#progress-rail');
  const progressFill = document.querySelector('#progress-fill');
  const progressSeek = document.querySelector('#progress-seek');
  const volumeControl = document.querySelector('#volume-control');
  const volumeInput = document.querySelector('#volume-input');
  const volumeValue = document.querySelector('#volume-value');
  const streamMeta = document.querySelector('#stream-meta');
  const playerError = document.querySelector('#player-error');
  const commandStatus = document.querySelector('#command-status');
  const mediaList = document.querySelector('#media-list');
  const listTitle = document.querySelector('#list-title');
  const listCount = document.querySelector('#list-count');
  const listItems = document.querySelector('#list-items');
  const listEmpty = document.querySelector('#list-empty');
  const listLoading = document.querySelector('#list-loading');
  const listSearch = document.querySelector('#list-search');
  const playerBar = document.querySelector('#player-bar');
  const playerExpand = document.querySelector('#player-expand');

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
  /* Once a second while a track is running, and rarely otherwise. The device
     serves this from the single HTTP worker that also carries the WebSocket
     broadcasts, so an idle browser left open overnight must not keep asking at
     playing speed. */
  const progressActiveDelay = 1000;
  const progressIdleDelay = 5000;

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
    /* Null until the device says. It is a device setting rather than anything
       to do with the track, so it arrives in the settings section of the
       snapshot and is pushed again whenever the knob moves. */
    volume: null,
  };

  const volume = {holding: false, busy: false, queued: null};

  // Neither listing travels over the socket: a directory of long names, or a
  // catalogue of up to 99 stations, dwarfs the frame budget, so the socket
  // carries a revision and a count and the entries come from GET /api/files or
  // GET /api/stations. Tracked here so a reply for a listing the user has
  // already left can be discarded - opening two folders quickly is enough to
  // make the replies arrive out of order.
  //
  // The kind is tracked beside the revision because the two lists share one
  // counter: switching between them without it looks like the revision the
  // browser already has, and the new list would never be fetched.
  // The source is tracked beside the kind: internet radio and the rotor share
  // one kind ("stations"), so on the kind alone a move from one catalogue to
  // the other is indistinguishable from "the same list" - the previous
  // source's stations stayed on screen, and a click on a row selected that
  // index in the new catalogue.
  let listFetchKind = null;
  let listFetchSource = null;
  let listFetchRevision = null;
  let listShownKind = null;
  let listShownSource = null;
  let listShownRevision = null;

  /* Position, buffer fill and the cover do not travel over the socket. The
     device rebuilds its snapshot on every change and sends a diff, so a
     counter that ticks would push a frame a second to every open browser for
     something one card shows. They come over REST instead, polled only while
     there is something to poll for. */
  const progress = {
    timer: null, wanted: false, source: '', coverUrl: '',
    elapsed: null, total: null,
    /* True from the moment the handle is touched until the device answers the
       jump. While it is set the poll leaves the handle alone, or the position
       would be dragged back out from under the pointer once a second. */
    seeking: false, seekRequest: '',
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
    /* Absent unless the track belongs to an account's library, which is the
       device's way of saying there is no mark to draw - so it stays absent
       here rather than becoming a false "not liked". */
    if (typeof player.liked === 'boolean') {
      normalized.liked = player.liked;
      /* Sent together with the like and never on its own, so one test covers
         both marks - and an old device that sends only `liked` still draws a
         heart rather than nothing. */
      normalized.disliked = player.disliked === true;
    }
    return normalized;
  }

  function setConnected(connected) {
    state.connected = connected;
    socketState.textContent = connected ? 'Подключено' : 'Нет связи';
    socketState.classList.toggle('is-online', connected);
    socketState.classList.toggle('is-offline', !connected);
    socketState.classList.remove('is-connecting');
    // Through renderPlayer(), not straight to updateControlAvailability(): the
    // baseline it applies is "connected", and several buttons are disabled for
    // reasons of their own on top of that.
    renderPlayer();
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

  /* The same marks the device's home-screen carousel uses, redrawn as SVG:
     the antenna for internet radio, the flash drive standing up, the card and
     the rotor's spark. The spark's outline is the device's own geometry -
     twelve graded rays measured off the service's icon, the numbers copied
     from tools/gen_feed_icons.py - so the two faces show one shape.

     They replace the labels on a phone, where four words of Russian do not fit
     across the header. Ours, never the device's strings, so they are safe to
     hand to innerHTML. */
  const sourceIcons = Object.freeze({
    internet_radio:
      '<svg class="source-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false">' +
      '<circle cx="12" cy="8" r="2.2"/>' +
      '<path class="stroke" d="M9.1 11.4a4.4 4.4 0 0 1 0-6.8M14.9 4.6a4.4 4.4 0 0 1 0 6.8' +
      'M6.6 13.9a7.9 7.9 0 0 1 0-11.8M17.4 2.1a7.9 7.9 0 0 1 0 11.8"/>' +
      '<path class="stroke thick" d="M12 12.8v8.4"/></svg>',
    usb:
      '<svg class="source-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false">' +
      '<path fill-rule="evenodd" d="M9.5 2.2h5a.8.8 0 0 1 .8.8v5.4H8.7V3a.8.8 0 0 1 .8-.8Z' +
      'M9.89 3.94h1.72v2.11H9.89Zm2.5 0h1.72v2.11h-1.72Z"/>' +
      '<path class="stroke" d="M7 8.2h10v11a2.2 2.2 0 0 1-2.2 2.2H9.2A2.2 2.2 0 0 1 7 19.2Z"/>' +
      '<rect x="8.6" y="12.2" width="6.8" height="2" rx="0.9"/></svg>',
    sd:
      '<svg class="source-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false">' +
      '<path class="stroke" d="M16.2 2.4 18.6 4.8V19a2.4 2.4 0 0 1-2.4 2.4H7.8A2.4 2.4 0 0 1 5.4 19' +
      'V4.8a2.4 2.4 0 0 1 2.4-2.4Z"/>' +
      '<path class="stroke" d="M8.6 6.2v2.6M11.2 6.2v2.6M13.8 6.2v2.6"/></svg>',
    yandex:
      '<svg class="source-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false">' +
      '<path d="M20.43 12.93 16.86 12.18 19.55 16.11 15.83 13.15 17.02 18.79 14.26 13.63' +
      ' 11.74 20.37 12.21 13.07 4.94 16.88 10.86 11.29 3.57 10.24 10.79 9.42 5.90 5.89' +
      ' 11.52 8.02 9.50 3.93 12.84 7.09 13.27 3.62 14.64 6.90 16.71 4.86 16.30 7.68' +
      ' 19.05 6.89 17.28 9.11 20.27 9.70 17.43 10.77Z"/></svg>',
  });

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
        const icon = sourceIcons[source.id];
        if (icon) {
          button.innerHTML = icon;
          // Only where there is a mark to show instead: an unknown source
          // keeps its words rather than becoming a blank square.
          button.dataset.icon = '1';
        }
        /* The label stays in the button even when the icon replaces it - on a
           phone it is clipped, not removed, so the button still has a name to
           be read out. */
        const label = document.createElement('span');
        label.className = 'source-tab-label';
        label.textContent = source.label;
        button.append(label);
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

  function formatTime(seconds) {
    const whole = Math.max(0, Math.trunc(seconds));
    const minutes = Math.trunc((whole % 3600) / 60);
    const rest = String(whole % 60).padStart(2, '0');
    const hours = Math.trunc(whole / 3600);
    // Minutes are padded only once there are hours in front of them, which is
    // how every player writes it - and how the device's own screen does.
    return hours > 0
      ? `${hours}:${String(minutes).padStart(2, '0')}:${rest}`
      : `${minutes}:${rest}`;
  }

  function renderVolume() {
    const known = Number.isSafeInteger(state.volume);
    volumeControl.hidden = !known;
    if (!known) return;
    volumeInput.disabled = !state.connected;
    /* Left alone while the handle is held and while a change is in flight: the
       device pushes its own level about four times a second, and it would drag
       the handle back out from under the pointer. */
    if (volume.holding || volume.busy) return;
    volumeInput.value = String(state.volume);
    volumeValue.textContent = String(state.volume);
  }

  function holdVolume() {
    volume.holding = true;
    volumeValue.textContent = String(volumeInput.value);
  }

  /* Over REST rather than as a socket command: the volume is a stored device
     setting, not a piece of playback state, and this is the same endpoint the
     settings page writes - so the device applies it by exactly one path. Each
     write is a rewrite of settings.csv, which is why only one is ever in the
     air and the rest are coalesced. */
  function sendVolume(target) {
    volume.busy = true;
    window.fetch('/api/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({field: 'volume', value: target}),
    })
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        /* What the device now has. Without this the handle would flick back to
           the level of the last push - the device sends its own about four
           times a second, and one of them lands during the write. */
        state.volume = target;
      })
      .catch(() => {
        commandStatus.textContent = 'Не удалось изменить громкость';
        commandStatus.classList.add('is-error');
      })
      .then(() => {
        volume.busy = false;
        const queued = volume.queued;
        volume.queued = null;
        if (queued !== null && queued !== state.volume) {
          sendVolume(queued);
          return;
        }
        // Back to whatever the device last said, which after a refused write
        // is the level it still has.
        renderVolume();
      });
  }

  function commitVolume() {
    volume.holding = false;
    const level = Number(volumeInput.value);
    if (!Number.isFinite(level)) return;
    const target = Math.round(level);
    if (volume.busy) {
      /* Held down, an arrow key fires a change per repeat. Remembering the
         last one instead of dropping it is what keeps the device from ending
         up a step behind where the handle was left. */
      volume.queued = target;
      return;
    }
    sendVolume(target);
  }

  function applySettings(value) {
    if (!isObject(value)) return;
    const level = value.volume;
    if (!Number.isSafeInteger(level) || level < 0 || level > 100) return;
    state.volume = level;
    renderVolume();
  }

  function paintProgress(elapsed, total) {
    // Pinned at the end rather than allowed past it: a variable-bitrate file
    // routinely outlives the device's estimate of its length.
    const percent = total > 0 ? Math.min(100, Math.round((elapsed / total) * 100)) : 0;
    trackElapsed.textContent = formatTime(elapsed);
    trackTotal.textContent = formatTime(total);
    progressFill.style.width = `${percent}%`;
    // The handle's own value is a count of seconds, which read aloud is a
    // number nobody wants; this is the same time the label shows.
    progressSeek.setAttribute('aria-valuetext', formatTime(elapsed));
  }

  function renderProgress() {
    const known = Number.isFinite(progress.total) && progress.total > 0 &&
      Number.isFinite(progress.elapsed);
    trackProgress.hidden = !known;
    if (!known) {
      progressSeek.disabled = true;
      progressRail.classList.remove('is-seekable');
      return;
    }
    /* Only a file can be jumped: a stream has no position to move to, and the
       device refuses the command for anything else. Offering the handle there
       would be a control that can only be told no. */
    const seekable = state.connected &&
      (state.activeSource === 'usb' || state.activeSource === 'sd') &&
      (state.player.state === 'playing' || state.player.state === 'paused');
    progressSeek.disabled = !seekable;
    progressRail.classList.toggle('is-seekable', seekable);
    progressSeek.max = String(progress.total);
    if (progress.seeking) return;
    progressSeek.value = String(progress.elapsed);
    paintProgress(progress.elapsed, progress.total);
  }

  function beginSeek() {
    if (progressSeek.disabled) return;
    const target = Number(progressSeek.value);
    if (!Number.isFinite(target)) return;
    progress.seeking = true;
    paintProgress(target, progress.total);
  }

  function commitSeek() {
    const target = Number(progressSeek.value);
    const id = progressSeek.disabled || !Number.isFinite(target)
      ? null
      : sendCommand('player.seek', {position: Math.round(target)});
    if (id === null) {
      // Nothing was sent, so the handle has to go back to where the track is.
      progress.seeking = false;
      renderProgress();
      return;
    }
    progress.seekRequest = id;
  }

  function renderCover(cover) {
    const present = isObject(cover) && cover.present === true;
    /* Where the device hands over an address - the rotor's tracks, whose
       pictures the service serves at any size - the browser fetches it
       itself and gets one drawn for this screen rather than the 96 px the
       panel decoded. Everything else comes from the device.

       The device's own pictures are named in the query string by their
       signature - a checksum of the bytes they were decoded from - so the
       browser fetches one exactly when it changes and takes it from its cache
       the rest of the time; 27 KB once a second would be the alternative.
       The generation is only the fallback: it counts from zero at every boot,
       so the same ?g= meant a different picture after a restart, and the
       day-long cache handed back whichever one that browser saw first. */
    const remote = present && typeof cover.url === 'string' &&
                   cover.url.indexOf('http://') === 0 ? cover.url : '';
    let url = '';
    if (present) {
      const signature = safeInteger(cover.signature);
      url = remote || (signature > 0
        ? `/api/cover?s=${signature}`
        : `/api/cover?g=${safeInteger(cover.generation)}`);
    }
    if (url !== progress.coverUrl) {
      progress.coverUrl = url;
      if (present) trackCover.src = url;
      else trackCover.removeAttribute('src');
    }
    trackCover.hidden = !present;
  }

  function clearProgress() {
    progress.elapsed = null;
    progress.total = null;
    progress.seeking = false;
    progress.seekRequest = '';
    renderProgress();
    renderCover(null);
  }

  function applyProgress(payload) {
    const value = isObject(payload) ? payload : {};
    progress.elapsed = Number.isSafeInteger(value.elapsed_seconds)
      ? value.elapsed_seconds : null;
    progress.total = Number.isSafeInteger(value.total_seconds)
      ? value.total_seconds : null;
    renderProgress();
    renderCover(value.cover);
  }

  // Nothing to ask about while the socket is down or the source is stopped:
  // neither a position nor a cover exists then.
  function progressWanted() {
    return state.connected && state.activeSource !== 'none' &&
      (state.player.state === 'playing' || state.player.state === 'paused' ||
       state.player.state === 'connecting');
  }

  function scheduleProgress(delay) {
    if (progress.timer !== null) window.clearTimeout(progress.timer);
    progress.timer = window.setTimeout(() => {
      progress.timer = null;
      pollProgress();
    }, delay);
  }

  function pollProgress() {
    if (!progressWanted()) {
      scheduleProgress(progressIdleDelay);
      return;
    }
    window.fetch('/api/progress', {cache: 'no-store'})
      .then((response) => {
        if (!response || response.ok !== true) throw new Error('request failed');
        return response.json();
      })
      .then((payload) => {
        applyProgress(payload);
        scheduleProgress(progressActiveDelay);
      })
      // Quietly: the socket already says when the device is unreachable, and a
      // second message about it would only cover the command status line.
      .catch(() => scheduleProgress(progressIdleDelay));
  }

  /* Restarted only when the answer to "is there anything to poll for" changes,
     or when the source does. Restarting on every player update would reset the
     one-second cadence each time a station's ICY title changed. */
  function syncProgressPolling() {
    const wanted = progressWanted();
    if (wanted === progress.wanted && state.activeSource === progress.source) return;
    progress.wanted = wanted;
    progress.source = state.activeSource;
    if (!wanted) clearProgress();
    scheduleProgress(0);
  }

  function renderPlayer() {
    const player = state.player;
    const playing = player.state === 'playing';
    /* First, so the refinements below survive: this sets every command button
       to whether the socket is up, and running it afterwards would enable the
       ones that have a second reason to be disabled. */
    updateControlAvailability();

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
    nextTrack.disabled = !skippable || !state.connected ||
      (player.state !== 'playing' && player.state !== 'paused');

    /* Shown for the rotor and nowhere else: a station and a file are in
       nobody's library, and an empty heart there would offer a button that can
       only be refused.

       Tied to the source rather than to the mark the device sends, because
       between two rotor tracks it sends none - and hiding the pair there made
       them vanish on every skip, taking the buttons either side of them along
       for the ride. They stay, greyed, until the next track says what it is. */
    const likeable = state.activeSource === 'yandex';
    const markable = likeable && typeof player.liked === 'boolean' && state.connected &&
      (player.state === 'playing' || player.state === 'paused');
    likeTrack.hidden = !likeable;
    likeTrack.disabled = !markable;
    likeTrack.classList.toggle('is-liked', markable && player.liked === true);
    likeTrack.setAttribute('aria-pressed', String(markable && player.liked === true));

    /* Its own button rather than a second meaning of the heart: the device has
       one key and has to overload it, the browser has room for both. The two
       marks exclude each other, so pressing one takes the other off without
       the browser having to ask for that. */
    dislikeTrack.hidden = !likeable;
    dislikeTrack.disabled = !markable;
    dislikeTrack.classList.toggle('is-disliked', markable && player.disliked === true);
    dislikeTrack.setAttribute('aria-pressed', String(markable && player.disliked === true));

    /* The two track keys, the browser's copy of the buttons on the front of
       the device. They move what is playing along the list it came from - the
       neighbouring file in the directory, the neighbouring station in the
       catalog - so they belong to the sources that are a list. The rotor is
       not one: its next track is the skip button above, and it has no previous
       one at all. Nothing wraps, so at either end of the list the key does
       nothing; the device refuses it rather than rolling over. */
    const steppable = state.activeSource === 'usb' || state.activeSource === 'sd' ||
      state.activeSource === 'internet_radio';
    const stepReady = steppable && state.connected &&
      player.state !== 'stopped' && player.state !== 'error';
    previousItem.hidden = !steppable;
    nextItem.hidden = !steppable;
    previousItem.disabled = !stepReady;
    nextItem.disabled = !stepReady;

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
    renderVolume();
    syncProgressPolling();
  }

  function listSignature(items) {
    return items
      .map((item) => `${item.index}\u0000${item.label}\u0000${item.meta || ''}` +
                     `\u0000${item.isDirectory ? 'd' : 'f'}`)
      .join('\u0001');
  }

  function normalizeList(value) {
    if (!isObject(value)) return null;
    const active_index = value.active_index === null || Number.isSafeInteger(value.active_index)
      ? value.active_index
      : null;
    const kind = safeString(value.kind);
    if (kind !== 'files' && kind !== 'stations') return null;
    // Both listings arrive as a header only; the entries are fetched
    // separately, so the ones already on screen are kept until they do - but
    // only when they belong to the same source: rows of a catalogue that has
    // been left cannot stay, their indices no longer mean what they did.
    const files = kind === 'files';
    const keep = state.list.kind === kind && listShownSource === state.activeSource;
    return {
      kind,
      active_index,
      items: keep ? state.list.items : [],
      path: files ? safeString(value.path) : '',
      revision: Number.isSafeInteger(value.revision) ? value.revision : null,
      has_parent: files && value.has_parent === true,
    };
  }

  /* The format used to be appended to the name and a directory marked with a
     slash. The name is now just the name: the format moves to its own column
     on the right, a directory is known by its icon, and a long name is
     truncated on itself rather than on a trailing file type. */
  function fileEntryMeta(entry) {
    return entry.kind === 'dir' ? '' : safeString(entry.format);
  }

  function normalizeFileEntries(payload) {
    if (!isObject(payload) || !Array.isArray(payload.items)) return null;
    return payload.items
      .filter((item) => isObject(item) && Number.isSafeInteger(item.index) && typeof item.name === 'string')
      .map((item) => ({
        index: item.index,
        label: item.name,
        meta: fileEntryMeta(item),
        // A playlist opens a listing the way a folder does, so the row gets
        // the same marker and the same "Открыть" action.
        isDirectory: item.kind === 'dir' || item.kind === 'playlist',
      }));
  }

  function normalizeStationEntries(payload) {
    if (!isObject(payload) || !Array.isArray(payload.items)) return null;
    return payload.items
      .filter((item) => isObject(item) && Number.isSafeInteger(item.index) &&
                        typeof item.label === 'string')
      .map((item) => ({index: item.index, label: item.label, meta: ''}));
  }

  const listingSources = {
    files: {
      url: '/api/files',
      parse: normalizeFileEntries,
      error: 'Не удалось прочитать флешку',
    },
    stations: {
      url: '/api/stations',
      parse: normalizeStationEntries,
      error: 'Не удалось прочитать список станций',
    },
  };

  async function loadListing(kind, revision) {
    const source = listingSources[kind];
    if (!source) return;
    const activeSource = state.activeSource;
    // Guard against the same listing being fetched twice: player updates
    // arrive far more often than a listing changes.
    if (listFetchKind === kind && listFetchSource === activeSource &&
        listFetchRevision === revision) return;
    if (listShownKind === kind && listShownSource === activeSource &&
        listShownRevision === revision) return;
    listFetchKind = kind;
    listFetchSource = activeSource;
    listFetchRevision = revision;
    try {
      const response = await fetch(source.url, {cache: 'no-store'});
      if (!response.ok) throw new Error(`status ${response.status}`);
      const payload = await response.json();
      const entries = source.parse(payload);
      if (!entries) throw new Error('unexpected payload');
      // The device may have moved on while this was in flight. Trust the
      // revision the response carries, not the one that triggered the fetch.
      if (state.list.kind !== kind) return;
      /* And the source too: the device fetches the rotor's catalogue over the
         network, so a reply to a request sent before the switch arrives after
         it. The reply names the source it belongs to.

         Until a source is selected there is nothing to compare against: the
         device serves the radio catalogue under its own name while the page
         knows only "none". The reply is accepted, and still tracked as
         "none" - the first selection of a source refetches the list. */
      if (state.activeSource !== activeSource) return;
      if (activeSource !== 'none' && typeof payload.source === 'string' &&
          payload.source !== activeSource) return;
      if (listShownKind === kind && listShownRevision !== null &&
          Number.isSafeInteger(payload.revision) && payload.revision < listShownRevision) {
        return;
      }
      const previousList = state.list;
      const files = kind === 'files';
      state.list = {
        ...state.list,
        items: entries,
        path: files ? safeString(payload.path, state.list.path) : '',
        has_parent: files && payload.has_parent === true,
      };
      listShownKind = kind;
      listShownSource = activeSource;
      listShownRevision = Number.isSafeInteger(payload.revision) ? payload.revision : revision;
      renderList(previousList);
    } catch (error) {
      commandStatus.textContent = source.error;
      commandStatus.classList.add('is-error');
    } finally {
      if (listFetchKind === kind && listFetchSource === activeSource &&
          listFetchRevision === revision) {
        listFetchKind = null;
        listFetchSource = null;
        listFetchRevision = null;
      }
    }
  }

  function syncListing() {
    if (!listingSources[state.list.kind]) {
      listShownKind = null;
      listShownSource = null;
      listShownRevision = null;
      return;
    }
    if (state.list.revision === null) return;
    if (state.list.kind !== listShownKind || listShownSource !== state.activeSource ||
        state.list.revision !== listShownRevision) {
      loadListing(state.list.kind, state.list.revision);
    }
  }

  /* The search lives in the browser alone: all 99 stations have already
     arrived, and asking the device for a filtered list would pay another
     request for what is on the page. The "up" row is never hidden - it is
     navigation, not an entry. */
  function applyListFilter() {
    const query = listSearch.value.trim().toLowerCase();
    let shown = 0;
    Array.from(listItems.children).forEach((row) => {
      const button = row.children[0];
      if (!button || button.dataset.index === undefined) return;
      const label = button.children[0];
      const text = label ? String(label.textContent).toLowerCase() : '';
      const match = query === '' || text.includes(query);
      row.hidden = !match;
      if (match) shown += 1;
    });
    const total = state.list.items.length;
    listCount.textContent = query === '' ? String(total) : `${shown}/${total}`;
  }

  /* The list is on screen before a source is selected: with none active the
     device still reports the "stations" kind and serves the station catalogue.
     The device itself only opens that list through the menu, which has already
     selected the source. So the browser names the owner of the list it is
     showing: the first source with the same list kind, which for the station
     catalogue shown in this state is internet radio. */
  function listOwnerSource() {
    const kind = state.list.kind;
    if (!kind) return null;
    const active = state.capabilities.find((source) => source.id === state.activeSource);
    if (active && active.list_kind === kind) return null;
    const owner = state.capabilities.find((source) => source.list_kind === kind);
    return owner ? owner.id : null;
  }

  function selectListItem(index) {
    const owner = listOwnerSource();
    // Two commands rather than one: the device takes them off its queue in
    // order, and by the time the second is read the source is already set.
    if (owner !== null) sendCommand('source.select', {source: owner});
    sendCommand('list.select', {index});
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
      const meta = document.createElement('span');

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
      /* CSS draws two of the bars as pseudo-elements; the third has to be a
         node, because one element has only two of them. */
      marker.append(document.createElement('i'));
      meta.className = 'list-item-meta';
      meta.textContent = safeString(item.meta);
      meta.hidden = meta.textContent.length === 0;
      button.append(label, marker, meta);
      button.addEventListener('click', () => {
        selectListItem(item.index);
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
    mediaList.dataset.kind = kind;
    rebuildListIfNeeded(previousList);
    listCount.textContent = String(state.list.items.length);
    // A subdirectory with no playable files still offers the way back, so the
    // list is only truly empty when there is nothing to click at all.
    const rowCount = state.list.items.length + (state.list.has_parent ? 1 : 0);
    /* The rotor's stations are fetched from the service after the source is
       selected, so an empty list right after the switch means the device is
       still asking - not that there is nothing. The other sources are read
       from the device itself and are empty only when they really are. */
    const waiting = rowCount === 0 && state.connected && state.activeSource === 'yandex';
    listLoading.hidden = !waiting;
    listEmpty.hidden = rowCount !== 0 || waiting;
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

    // The filter is applied last: the rows may have just been rebuilt, and
    // they come back visible.
    applyListFilter();

    // Every path that changes the list ends here, so this is the single place
    // the REST fetch needs to be triggered from.
    syncListing();
  }

  function applySnapshot(message) {
    state.capabilities = normalizeCapabilities(message.capabilities);
    state.activeSource = safeString(message.active_source, 'none');
    if (isObject(message.player)) state.player = normalizePlayer(message.player);
    const nextList = normalizeList(message.list);
    const previousList = state.list;
    if (nextList) state.list = nextList;
    applySettings(message.settings);
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
    if (id === progress.seekRequest) {
      /* Answered, so the handle belongs to the poll again - including when the
         device refused the jump, which is how it snaps back to where the track
         actually is. Asked straight away rather than at the next tick: the
         position has just moved by however far the jump went. */
      progress.seekRequest = '';
      progress.seeking = false;
      scheduleProgress(0);
    }
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
      /* The knob on the device moves this, and nothing else would tell the
         browser: it is why the settings travel over the socket at all. */
      case 'settings.update':
        if (!acceptSectionRevision(message)) break;
        applySettings(message.settings);
        break;
      case 'command.result':
        handleCommandResult(message);
        break;
      default:
        break;
    }
  }

  // Returns the request id, or null when nothing was sent. The jump needs it:
  // it holds the handle until the device answers that exact command.
  function sendCommand(action, fields = {}) {
    if (!state.connected || !socket || socket.readyState !== WebSocket.OPEN) return null;
    requestSequence += 1;
    const id = `web-${requestSequence}`;
    const command = {type: 'command', id, action, ...fields};
    const frame = JSON.stringify(command);
    if (frame.length > 512) {
      commandStatus.textContent = 'Команда слишком длинная';
      commandStatus.classList.add('is-error');
      return null;
    }
    commandStatus.textContent = '';
    commandStatus.classList.remove('is-error');
    pendingCommands.set(id, action);
    socket.send(frame);
    return id;
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
  previousItem.addEventListener('click', () => sendCommand('player.previous_item'));
  nextItem.addEventListener('click', () => sendCommand('player.next_item'));
  nextTrack.addEventListener('click', () => sendCommand('player.next'));
  // input fires all the way through a drag, change once it is let go: the bar
  // follows the handle, and only the release is worth a command.
  progressSeek.addEventListener('input', beginSeek);
  progressSeek.addEventListener('change', commitSeek);
  volumeInput.addEventListener('input', holdVolume);
  volumeInput.addEventListener('change', commitVolume);
  listSearch.addEventListener('input', applyListFilter);
  /* Folded away on a phone the bar shows only the cover, the title and pause;
     the position, the volume and the stream's numbers live in the expanded
     card. On a large screen the button is hidden - everything fits there. */
  playerExpand.addEventListener('click', () => {
    const expanded = playerBar.classList.toggle('is-expanded');
    playerExpand.setAttribute('aria-expanded', String(expanded));
    playerExpand.setAttribute('aria-label',
      expanded ? 'Свернуть карточку трека' : 'Развернуть карточку трека');
  });
  likeTrack.addEventListener('click', () => sendCommand('player.like'));
  dislikeTrack.addEventListener('click', () => sendCommand('player.dislike'));
  renderSources();
  updatePlaylistLink();
  renderPlayer();
  renderList();
  scheduleProgress(progressIdleDelay);
  connect();
})();
