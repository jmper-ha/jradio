(() => {
  'use strict';

  /* Firmware updates, on two pages. The notice of a new release sits at the
     top of the player and the settings page; the settings page also has the
     card - the release check, and the update from a file.

     The page only carries files and reads answers. Whether a file is a
     firmware for this radio is decided on the device (components/ota), a
     file from the browser is installed only after the encoder is pressed,
     and the release is checked by the radio itself (ota_check.c). */

  const t = (key, values) => window.jradioI18n.t(key, values);
  const $ = (selector) => document.querySelector(selector);

  /* While the device works or waits for the press, and then while it
     restarts: a few seconds in which every request fails. */
  const POLL_MS = 1000;
  const RETURN_POLL_MS = 2000;
  /* A restart that has not answered in this long has gone wrong in a way the
     page cannot see - the panel is the place to look. */
  const RETURN_GIVE_UP_MS = 90000;
  /* A check is one small download; longer than this and it is not coming. */
  const CHECK_GIVE_UP_MS = 45000;

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
  /* What the radio says when it fetched from the release itself. */
  const releaseErrorText = Object.freeze({
    network: 'update.err_network',
    download: 'update.err_network',
    malformed: 'update.err_release',
    missing: 'update.err_release',
    format: 'update.err_format',
    no_display: 'update.err_no_display',
    checksum: 'update.err_checksum',
    verify: 'update.err_verify',
    flash: 'update.err_flash',
    write: 'update.err_write',
    memory: 'update.err_memory',
    busy: 'update.err_busy',
  });

  function errorFor(code, table = errorText) {
    const key = table[code];
    return key ? t(key) : t('update.err_refused');
  }

  function setLine(line, text, kind) {
    if (!line) return;
    line.textContent = text;
    line.classList.toggle('is-error', kind === 'error');
    line.classList.toggle('is-success', kind === 'success');
  }

  function getJson(url) {
    return window.fetch(url, {cache: 'no-store'}).then((response) => {
      if (!response || !response.ok) throw new Error('no answer');
      return response.json();
    });
  }

  /* The release buttons: one handler on the device, the action in the
     query. Resolves with the refusal's code, or '' when it went through. */
  function action(name, on) {
    const query = on === undefined ? `do=${name}` : `do=${name}&on=${on ? 1 : 0}`;
    return window.fetch(`/api/ota/action?${query}`, {method: 'POST'})
      .then((response) => {
        if (response && response.ok) return '';
        return response.json().then((payload) => payload.error || 'refused', () => 'refused');
      })
      .catch(() => 'network');
  }

  /* After the press, or the install from the release: the device answers
     "restarting", then nothing, then the new firmware's About. Matching the
     version is what tells an installed update from a device that came back
     with the old one. */
  function waitForReturn(expected, say, done) {
    const startedAt = Date.now();
    const poll = () => {
      window.setTimeout(() => {
        getJson('/api/about')
          .then((about) => {
            const running = about && about.firmware ? about.firmware.version : '';
            if (running === expected) {
              say(t('update.installed', {version: running}), 'success');
            } else {
              say(t('update.came_back_old', {version: running}), 'error');
            }
            done(running);
          })
          .catch(() => {
            if (Date.now() - startedAt > RETURN_GIVE_UP_MS) {
              say(t('update.no_return'), 'error');
              done('');
              return;
            }
            poll();
          });
      }, RETURN_POLL_MS);
    };
    poll();
  }

  let last = null;

  /* The notice. */
  const banner = $('#update-banner');
  const bannerTitle = $('#update-banner-title');
  const bannerNotes = $('#update-banner-notes');
  const bannerStatus = $('#update-banner-status');
  const bannerInstall = $('#update-banner-install');
  const bannerSkip = $('#update-banner-skip');
  let installing = false;

  /* "New:" and "- an item" lines, as tools/ota_manifest.py hands them over:
     the items become list items, the rest headings among them. */
  function renderNotes(text) {
    if (!bannerNotes) return;
    const items = String(text || '').split('\n').filter((line) => line.trim() !== '')
      .map((line) => {
        const item = document.createElement('li');
        if (line.startsWith('- ')) {
          item.textContent = line.slice(2);
        } else {
          item.textContent = line;
          item.classList.add('is-heading');
        }
        return item;
      });
    bannerNotes.replaceChildren(...items);
  }

  function renderBanner(status) {
    if (!banner || !status) return;
    const check = status.check || {};
    banner.hidden = !(check.available || installing);
    if (banner.hidden) return;
    bannerTitle.textContent = t('update.available', {version: check.latest});
    const english = window.jradioI18n.language && window.jradioI18n.language() === 'en';
    renderNotes(english ? check.notes_en : check.notes_ru);
    bannerInstall.disabled = installing;
    bannerSkip.disabled = installing;
  }

  /* After a restart everything on the page describes the firmware before
     it: asked again, so the card says what runs now - it went on showing the
     old version beside the new one when only the notice was redrawn. */
  function refresh() {
    getJson('/api/ota')
      .then((status) => {
        last = status;
        renderBanner(status);
        renderCard(status);
      })
      .catch(() => {});
  }

  function installDone() {
    installing = false;
    if (bannerInstall) bannerInstall.disabled = false;
    if (bannerSkip) bannerSkip.disabled = false;
    refresh();
  }

  function watchInstall(version) {
    const sayBanner = (text, kind) => setLine(bannerStatus, text, kind);
    window.setTimeout(() => {
      getJson('/api/ota')
        .then((status) => {
          const check = status.check || {};
          if (status.state === 'restarting') {
            sayBanner(t('update.restarting'));
            waitForReturn(version, sayBanner, installDone);
            return;
          }
          if (!check.installing) {
            installDone();
            sayBanner(errorFor(check.error || status.error, releaseErrorText), 'error');
            renderBanner(status);
            return;
          }
          if (status.state === 'receiving' && status.total > 0) {
            const percent = Math.floor(status.done * 100 / status.total);
            sayBanner(t('update.downloading', {percent}));
          }
          watchInstall(version);
        })
        /* The device can drop off between two polls only by restarting. */
        .catch(() => {
          sayBanner(t('update.restarting'));
          waitForReturn(version, sayBanner, installDone);
        });
    }, POLL_MS);
  }

  /* The card on the settings page. */
  const statusLine = $('#update-status');
  const current = $('#update-current');
  const latestLine = $('#update-latest');
  const autoCheck = $('#update-auto');
  const checkButton = $('#update-check');
  const fileInput = $('#update-file');
  const sendButton = $('#update-send');
  const progress = $('#update-progress');
  const filesLine = $('#update-files');
  const say = (text, kind) => setLine(statusLine, text, kind);

  let busy = false;
  let expected = '';
  let checking = false;

  function renderCard(status) {
    if (!statusLine || !status) return;
    const check = status.check || {};
    current.textContent = status.running || '—';
    if (check.latest) {
      latestLine.textContent = check.checked_at > 0
        ? `${check.latest} · ${new Date(check.checked_at * 1000).toLocaleString()}`
        : check.latest;
    } else {
      latestLine.textContent = '—';
    }
    autoCheck.checked = check.enabled !== false;
    checkButton.disabled = checking;
    /* Nine firmwares in a release, one per display, and nothing on the radio
       says which display it has: the names are spelled out. The version is
       the latest one when it is known. */
    if (filesLine && status.display) {
      const version = check.latest || t('update.version_placeholder');
      filesLine.textContent = t('update.files', {
        app: `jradio-${version}-${status.display}.bin`,
        web: `jradio-${version}-www.tar`,
      });
    }
  }

  if (banner) {
    bannerInstall.addEventListener('click', () => {
      if (installing || !last) return;
      const version = last.check.latest;
      installing = true;
      renderBanner(last);
      setLine(bannerStatus, t('update.starting'));
      action('install').then((code) => {
        if (code !== '') {
          installDone();
          renderBanner(last);
          setLine(bannerStatus, errorFor(code, releaseErrorText), 'error');
          return;
        }
        watchInstall(version);
      });
    });
    bannerSkip.addEventListener('click', () => {
      if (installing) return;
      action('skip').then((code) => {
        if (code !== '') {
          setLine(bannerStatus, errorFor(code, releaseErrorText), 'error');
          return;
        }
        if (last) last.check = {...last.check, available: false, skipped: last.check.latest};
        banner.hidden = true;
        renderCard(last);
      });
    });
    if (window.jradioI18n.onChange) {
      window.jradioI18n.onChange(() => renderBanner(last));
    }
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

  function finish(running) {
    busy = false;
    progress.hidden = true;
    fileInput.value = '';
    if (running) {
      current.textContent = running;
      refresh();
    }
    refreshButton();
  }

  function watchConfirm() {
    window.setTimeout(() => {
      getJson('/api/ota')
        .then((status) => {
          if (status.state === 'confirm') {
            watchConfirm();
          } else if (status.state === 'restarting') {
            say(t('update.restarting'));
            waitForReturn(expected, say, finish);
          } else if (status.error === 'declined') {
            say(t('update.declined'), 'error');
            finish();
          } else {
            say(errorFor(status.error), 'error');
            finish();
          }
        })
        .catch(() => {
          say(t('update.restarting'));
          waitForReturn(expected, say, finish);
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
        say(t('update.sending', {percent}));
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
    say(t('update.sending', {percent: 0}));
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
        say(t('update.press', {version: expected}));
        watchConfirm();
      })
      .catch((text) => {
        say(typeof text === 'string' ? text : t('update.err_upload'), 'error');
        finish();
      });
  }

  /* "Check now": the radio asks the release, the page waits for the answer
     and shows it the way a check of the radio's own would have been. */
  function checkNow() {
    if (checking) return;
    checking = true;
    checkButton.disabled = true;
    say(t('update.checking'));
    const startedAt = Date.now();
    const stop = (text, kind) => {
      checking = false;
      checkButton.disabled = false;
      say(text, kind);
    };
    const poll = () => {
      window.setTimeout(() => {
        getJson('/api/ota')
          .then((status) => {
            const check = status.check || {};
            if (check.state === 'checking' && Date.now() - startedAt < CHECK_GIVE_UP_MS) {
              poll();
              return;
            }
            last = status;
            checking = false;
            renderCard(status);
            renderBanner(status);
            if (check.state === 'failed') {
              stop(errorFor(check.error, releaseErrorText), 'error');
            } else if (check.available) {
              stop(t('update.available', {version: check.latest}), 'success');
            } else {
              stop(t('update.up_to_date'), 'success');
            }
          })
          .catch(() => stop(t('update.err_network'), 'error'));
      }, POLL_MS);
    };
    action('check').then((code) => {
      if (code !== '') {
        stop(errorFor(code, releaseErrorText), 'error');
        return;
      }
      poll();
    });
  }

  if (statusLine) {
    fileInput.addEventListener('change', () => {
      const files = Array.from(fileInput.files || []);
      refreshButton();
      if (files.length === 0) {
        say('');
      } else if (chosenFiles() === null) {
        say(t('update.err_choice'), 'error');
      } else {
        say(t('backup.chosen', {name: files.map((file) => file.name).join(', ')}));
      }
    });
    sendButton.addEventListener('click', send);
    checkButton.addEventListener('click', checkNow);
    autoCheck.addEventListener('change', () => {
      const wanted = autoCheck.checked === true;
      action('auto', wanted).then((code) => {
        if (code === '') return;
        autoCheck.checked = !wanted;
        say(errorFor(code, releaseErrorText), 'error');
      });
    });
    refreshButton();
  }

  /* Once per page. A page opened while the radio already waits for the
     press - the upload came from another tab, or this one was reloaded -
     says so instead of offering a second upload over the first. */
  getJson('/api/ota')
    .then((status) => {
      last = status;
      renderBanner(status);
      renderCard(status);
      if (statusLine && status.state === 'confirm') {
        expected = status.version;
        busy = true;
        refreshButton();
        say(t('update.press', {version: status.version}));
        watchConfirm();
      }
    })
    .catch(() => {});
})();
