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
  });

  let busy = false;
  let expected = '';

  function setStatus(text, kind) {
    statusLine.textContent = text;
    statusLine.classList.toggle('is-error', kind === 'error');
    statusLine.classList.toggle('is-success', kind === 'success');
  }

  function chosenFile() {
    const files = fileInput.files;
    return files && files.length > 0 ? files[0] : null;
  }

  function refreshButton() {
    sendButton.disabled = busy || chosenFile() === null;
  }

  function errorFor(code) {
    const key = errorText[code];
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
     the web server has. */
  function send() {
    const file = chosenFile();
    if (file === null || busy) return;
    busy = true;
    refreshButton();
    progress.value = 0;
    progress.hidden = false;
    setStatus(t('update.sending', {percent: 0}));
    const request = new window.XMLHttpRequest();
    request.open('POST', '/api/ota/app');
    request.setRequestHeader('Content-Type', 'application/octet-stream');
    request.upload.addEventListener('progress', (event) => {
      if (!event.lengthComputable || event.total <= 0) return;
      const percent = Math.floor(event.loaded * 100 / event.total);
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
      if (request.status !== 200) {
        setStatus(errorFor(payload.error), 'error');
        finish();
        return;
      }
      expected = typeof payload.version === 'string' ? payload.version : '';
      progress.value = 100;
      setStatus(t('update.press', {version: expected}));
      watchConfirm();
    });
    request.addEventListener('error', () => {
      setStatus(t('update.err_upload'), 'error');
      finish();
    });
    request.send(file);
  }

  fileInput.addEventListener('change', () => {
    const file = chosenFile();
    refreshButton();
    setStatus(file === null ? '' : t('backup.chosen', {name: file.name}));
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
