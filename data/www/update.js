(() => {
  'use strict';

  /* The firmware update from a file. The page only carries the file and reads
     the answers: whether the file is a firmware for this radio is decided on
     the device (components/ota/ota_image.c), and whether it is installed is
     decided by whoever presses the encoder. */

  const t = (key, values) => window.jradioI18n.t(key, values);

  const statusLine = document.querySelector('#update-status');
  const current = document.querySelector('#update-current');
  const fileInput = document.querySelector('#update-file');
  const sendButton = document.querySelector('#update-send');
  const progress = document.querySelector('#update-progress');

  /* While the device waits for the press, and then while it restarts. The
     restart takes a few seconds and the page sees only failed requests until
     the device is back. */
  const POLL_MS = 1000;
  const RETURN_POLL_MS = 2000;
  /* A restart that has not answered in this long has gone wrong in a way the
     page cannot see - the panel is the place to look. */
  const RETURN_GIVE_UP_MS = 90000;

  const errorText = Object.freeze({
    short: 'update.err_not_firmware',
    not_firmware: 'update.err_not_firmware',
    wrong_chip: 'update.err_wrong_chip',
    not_jradio: 'update.err_not_jradio',
    no_mark: 'update.err_no_mark',
    wrong_display: 'update.err_wrong_display',
    too_big: 'update.err_too_big',
    memory: 'update.err_memory',
    upload: 'update.err_upload',
    flash: 'update.err_flash',
    verify: 'update.err_verify',
    mismatch: 'update.err_mismatch',
  });
  /* The archive's own refusals: the same words for a different file mean
     something else - "too big" there is one page in it, not the archive. */
  const webErrorText = Object.freeze({
    not_web: 'update.err_not_web',
    bad_name: 'update.err_not_web',
    too_big: 'update.err_not_web',
    upload: 'update.err_upload',
    write: 'update.err_write',
    memory: 'update.err_memory',
  });

  let busy = false;
  let expected = '';

  function setStatus(text, kind) {
    statusLine.textContent = text;
    statusLine.classList.toggle('is-error', kind === 'error');
    statusLine.classList.toggle('is-success', kind === 'success');
  }

  /* The firmware and its web files, picked together or either alone; a
     choice that is neither, or two of one kind, is null. */
  function chosenFiles() {
    const files = Array.from(fileInput.files || []);
    if (files.length === 0 || files.length > 2) return null;
    const app = files.filter((file) => /\.bin$/i.test(file.name));
    const web = files.filter((file) => /\.tar$/i.test(file.name));
    if (app.length > 1 || web.length > 1 || app.length + web.length !== files.length) return null;
    return {app: app[0] || null, web: web[0] || null};
  }

  function refreshButton() {
    sendButton.disabled = busy || chosenFiles() === null;
  }

  function errorFor(code, table = errorText) {
    const key = table[code];
    return key ? t(key) : t('update.err_refused');
  }

  function getJson(url) {
    return window.fetch(url, {cache: 'no-store'}).then((response) => {
      if (!response || !response.ok) throw new Error('no answer');
      return response.json();
    });
  }

  function showRunning(status) {
    current.textContent = typeof status.running === 'string' && status.running !== ''
      ? status.running : '—';
  }

  function finish() {
    busy = false;
    progress.hidden = true;
    fileInput.value = '';
    refreshButton();
  }

  /* After the press: the device answers "restarting", then nothing, then the
     new firmware's About. Matching the version is what tells an installed
     update from a device that came back with the old one. */
  function waitForReturn(startedAt) {
    window.setTimeout(() => {
      getJson('/api/about')
        .then((about) => {
          const running = about && about.firmware ? about.firmware.version : '';
          current.textContent = running || '—';
          if (running === expected) {
            setStatus(t('update.installed', {version: running}), 'success');
          } else {
            setStatus(t('update.came_back_old', {version: running}), 'error');
          }
          finish();
        })
        .catch(() => {
          if (Date.now() - startedAt > RETURN_GIVE_UP_MS) {
            setStatus(t('update.no_return'), 'error');
            finish();
            return;
          }
          waitForReturn(startedAt);
        });
    }, RETURN_POLL_MS);
  }

  function watchConfirm() {
    window.setTimeout(() => {
      getJson('/api/ota')
        .then((status) => {
          if (status.state === 'confirm') {
            watchConfirm();
          } else if (status.state === 'restarting') {
            setStatus(t('update.restarting'));
            waitForReturn(Date.now());
          } else if (status.error === 'declined') {
            setStatus(t('update.declined'), 'error');
            finish();
          } else {
            setStatus(errorFor(status.error), 'error');
            finish();
          }
        })
        /* The device can drop off between two polls only by restarting -
           which is what the press does. */
        .catch(() => {
          setStatus(t('update.restarting'));
          waitForReturn(Date.now());
        });
    }, POLL_MS);
  }

  /* XMLHttpRequest rather than fetch: fetch has no upload progress, and the
     device cannot report its own while the upload occupies the only worker
     the web server has. Resolves with the status and the answer. */
  function upload(url, file, before, total) {
    return new Promise((resolve) => {
      const request = new window.XMLHttpRequest();
      request.open('POST', url);
      request.setRequestHeader('Content-Type', 'application/octet-stream');
      request.upload.addEventListener('progress', (event) => {
        if (!event.lengthComputable) return;
        const percent = Math.floor((before + event.loaded) * 100 / total);
        progress.value = percent;
        setStatus(t('update.sending', {percent}));
      });
      request.addEventListener('load', () => {
        let payload = {};
        try {
          payload = JSON.parse(request.responseText || '{}');
        } catch (error) {
          payload = {};
        }
        resolve({status: request.status, payload});
      });
      request.addEventListener('error', () => resolve({status: 0, payload: {error: 'upload'}}));
      request.send(file);
    });
  }

  /* The web files first, then the firmware: the press installs whatever is
     waiting, so both have to be on the device before it is asked. */
  function send() {
    const chosen = chosenFiles();
    if (chosen === null || busy) return;
    busy = true;
    refreshButton();
    progress.value = 0;
    progress.hidden = false;
    setStatus(t('update.sending', {percent: 0}));
    const total = (chosen.web ? chosen.web.size || 0 : 0) + (chosen.app ? chosen.app.size || 0 : 0);
    const webDone = chosen.web ? chosen.web.size || 0 : 0;
    const sendWeb = chosen.web
      ? upload(`/api/ota/www?app=${chosen.app ? 1 : 0}`, chosen.web, 0, total || 1)
        .then((answer) => {
          if (answer.status !== 200) throw errorFor(answer.payload.error, webErrorText);
          return answer.payload;
        })
      : Promise.resolve(null);
    sendWeb
      .then((webAnswer) => {
        if (!chosen.app) return webAnswer;
        return upload('/api/ota/app', chosen.app, webDone, total || 1).then((answer) => {
          if (answer.status !== 200) throw errorFor(answer.payload.error);
          return answer.payload;
        });
      })
      .then((payload) => {
        expected = payload && typeof payload.version === 'string' ? payload.version : '';
        progress.value = 100;
        setStatus(t('update.press', {version: expected}));
        watchConfirm();
      })
      .catch((text) => {
        setStatus(typeof text === 'string' ? text : t('update.err_upload'), 'error');
        finish();
      });
  }

  fileInput.addEventListener('change', () => {
    const files = Array.from(fileInput.files || []);
    refreshButton();
    if (files.length === 0) {
      setStatus('');
    } else if (chosenFiles() === null) {
      setStatus(t('update.err_choice'), 'error');
    } else {
      setStatus(t('backup.chosen', {name: files.map((file) => file.name).join(', ')}));
    }
  });
  sendButton.addEventListener('click', send);
  refreshButton();

  /* A page opened while the radio is already waiting for the press - the
     upload came from another tab, or this one was reloaded - says so instead
     of offering a second upload over the first. */
  getJson('/api/ota')
    .then((status) => {
      showRunning(status);
      if (status.state === 'confirm') {
        expected = status.version;
        busy = true;
        refreshButton();
        setStatus(t('update.press', {version: status.version}));
        watchConfirm();
      }
    })
    .catch(() => {});
})();
