(() => {
  'use strict';

  const t = (key, values) => window.jradioI18n.t(key, values);

  const status = document.querySelector('#remote-status');
  const missing = document.querySelector('#remote-missing');
  const groups = document.querySelector('#remote-groups');

  /* The functions in the order a person looks for them, not the enum's: the
     keys everybody has first, the digits and the sources - which only some
     remotes have keys to spare for - last. */
  const GROUPS = [
    ['sound', ['volume_up', 'volume_down', 'mute', 'play_pause', 'prev', 'next']],
    ['navigate', ['up', 'down', 'ok', 'back', 'menu', 'quick', 'list']],
    ['extras', ['power', 'sleep', 'like', 'dislike']],
    ['digits', ['digit_0', 'digit_1', 'digit_2', 'digit_3', 'digit_4',
                'digit_5', 'digit_6', 'digit_7', 'digit_8', 'digit_9']],
    ['sources', ['source_radio', 'source_usb', 'source_sd', 'source_bluetooth',
                 'source_yandex', 'source_dlna']],
  ];

  /* The page asks the device four times a second while it is in front of
     someone - a small document: the key just pressed, whose row lights up so
     the table can be checked from the sofa, and the revision that says when
     the whole table is worth fetching again. A socket would be a slot held
     for a page open a few minutes a year. */
  const POLL_MS = 250;
  /* A pressed row stays lit this long after the key's last frame: a held key
     sends one every 108 ms, so it stays lit as long as the key is down and
     goes out about when the finger does. */
  const PRESS_FRESH_MS = 350;
  let pollTimer = null;
  let revision = null;
  let learning = null;
  let lastLearning = null;
  let lastLearningCode = null;
  let pressed = null;
  const rows = new Map();

  function isObject(value) {
    return value !== null && typeof value === 'object' && !Array.isArray(value);
  }

  function setStatus(text, error) {
    status.textContent = text;
    status.classList.toggle('is-error', error === true);
  }

  function buildRows() {
    groups.textContent = '';
    rows.clear();
    for (const [group, names] of GROUPS) {
      const block = document.createElement('div');
      block.className = 'device-group';
      const title = document.createElement('h3');
      title.setAttribute('data-i18n', `remote.group.${group}`);
      title.textContent = t(`remote.group.${group}`);
      block.appendChild(title);
      for (const name of names) {
        const row = document.createElement('div');
        row.className = 'device-row remote-row';
        row.dataset.function = name;
        const label = document.createElement('span');
        label.className = 'row-label';
        label.setAttribute('data-i18n', `remote.f.${name}`);
        label.textContent = t(`remote.f.${name}`);
        const code = document.createElement('span');
        code.className = 'row-value remote-code';
        code.textContent = '—';
        const learn = document.createElement('button');
        learn.type = 'button';
        learn.className = 'secondary-button';
        learn.setAttribute('data-i18n', 'remote.learn');
        learn.textContent = t('remote.learn');
        learn.addEventListener('click', () => learnFunction(name));
        const forget = document.createElement('button');
        forget.type = 'button';
        forget.className = 'secondary-button';
        forget.setAttribute('data-i18n', 'remote.forget');
        forget.textContent = t('remote.forget');
        forget.hidden = true;
        forget.addEventListener('click', () => forgetFunction(name));
        row.append(label, code, learn, forget);
        block.appendChild(row);
        rows.set(name, row);
      }
      groups.appendChild(block);
    }
  }

  function render(body) {
    const keys = isObject(body.keys) ? body.keys : {};
    learning = typeof body.learning === 'string' ? body.learning : null;
    for (const [name, row] of rows) {
      const bound = typeof keys[name] === 'string' && keys[name] !== '';
      const armed = learning === name;
      row.classList.toggle('is-learning', armed);
      row.children[1].textContent = armed ? t('remote.waiting') : (bound ? keys[name] : '—');
      row.children[2].disabled = learning !== null;
      row.children[3].hidden = !bound || armed;
    }
    if (learning !== null) {
      setStatus(t('remote.press', {function: t(`remote.f.${learning}`)}), false);
      lastLearningCode = typeof keys[learning] === 'string' ? keys[learning] : null;
    } else if (lastLearning !== null) {
      /* The arming ended: with a key, which moved the row's code, or with the
         thirty seconds, which moved nothing. */
      const code = typeof keys[lastLearning] === 'string' ? keys[lastLearning] : null;
      setStatus(code !== null && code !== lastLearningCode ? t('remote.learned') : t('remote.timeout'),
                code === null || code === lastLearningCode);
    }
    lastLearning = learning;
  }

  /* The last key: its row lit while fresh, and a key nobody has learned named
     in the status line - that is how one finds out which code a remote's
     odd key sends. */
  function showPressed(last) {
    const fresh = isObject(last) && Number.isSafeInteger(last.age_ms) && last.age_ms < PRESS_FRESH_MS;
    const name = fresh && typeof last.function === 'string' ? last.function : null;
    if (pressed !== null && pressed !== name) rows.get(pressed).classList.remove('is-pressed');
    if (name !== null && rows.has(name)) rows.get(name).classList.add('is-pressed');
    pressed = name !== null && rows.has(name) ? name : null;
    if (fresh && name === null && learning === null && typeof last.code === 'string') {
      setStatus(t('remote.unknown', {code: last.code}), false);
    }
  }

  function schedulePoll() {
    if (pollTimer !== null) {
      clearTimeout(pollTimer);
      pollTimer = null;
    }
    if (document.hidden === true) return;
    pollTimer = setTimeout(load, POLL_MS);
  }

  const getJson = (path) => fetch(path, {cache: 'no-store'})
    .then((response) => (response.ok ? response.json() : Promise.reject(new Error('bad reply'))))
    .then((body) => (isObject(body) ? body : Promise.reject(new Error('bad body'))));

  /* The whole table. */
  function loadTable() {
    return getJson('/api/remote').then((body) => {
      missing.hidden = body.available === true;
      groups.hidden = body.available !== true;
      if (body.available !== true) return;
      revision = body.revision;
      render(body);
      showPressed(body.last);
    });
  }

  /* The poll: the small document, and the table behind it only when the
     device says it moved. Called by the timer and by the buttons alike, so a
     poll already waiting is dropped first, or every Learn would add a second
     chain of polls. */
  function load() {
    if (pollTimer !== null) clearTimeout(pollTimer);
    pollTimer = null;
    return getJson('/api/remote/last')
      .then((body) => {
        if (revision !== body.revision) return loadTable();
        showPressed(body.last);
        return undefined;
      })
      .catch(() => setStatus(t('remote.failed'), true))
      .then(schedulePoll);
  }

  function post(path, name) {
    return fetch(path, {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({function: name}),
    }).then((response) => (response.ok ? response.json() : Promise.reject(new Error('bad reply'))));
  }

  function learnFunction(name) {
    setStatus(t('remote.press', {function: t(`remote.f.${name}`)}), false);
    return post('/api/remote/learn', name)
      .then(() => load())
      .catch(() => setStatus(t('remote.failed'), true));
  }

  function forgetFunction(name) {
    return post('/api/remote/forget', name)
      .then(() => load())
      .catch(() => setStatus(t('remote.failed'), true));
  }

  window.jradioI18n.onChange(() => {
    /* The rows carry their keys, so the dictionary re-translates them; only
       the status line and the armed row's text are built by hand. */
    if (learning !== null) {
      setStatus(t('remote.press', {function: t(`remote.f.${learning}`)}), false);
      rows.get(learning).children[1].textContent = t('remote.waiting');
    }
  });

  /* A tab in the background asks nothing; brought back, it asks at once. */
  document.addEventListener('visibilitychange', () => {
    if (document.hidden === true) schedulePoll();
    else if (pollTimer === null) load();
  });

  buildRows();
  loadTable().catch(() => setStatus(t('remote.failed'), true)).then(schedulePoll);
})();
