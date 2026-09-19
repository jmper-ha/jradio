/* The wiring editor's page: the parts list on the left, the module's header
   on the right, and board.csv underneath - all drawn from the model in
   hardware_core.js, which is where every rule lives. This file only puts the
   model on screen and puts the clicks back into it.

   Two ways to give a signal its pin, because they suit different hands: a
   select beside the signal for the keyboard and for anyone who knows the
   number, and "arm the signal, then click the pin on the picture" for anyone
   working from the board in front of them. Both end in the same assignment.

   The board being edited is kept in localStorage between visits, as a draft:
   the page has no device behind it yet, and losing an hour of wiring to a
   closed tab is the failure that matters here. */
(() => {
  'use strict';

  const hw = window.jradioHardware;
  const t = (key, values) => window.jradioI18n.t(key, values);
  const $ = (id) => document.getElementById(id);

  const DRAFT_KEY = 'jradio.board.csv';
  const SVG_NS = 'http://www.w3.org/2000/svg';

  /* Which device a pin's colour comes from. Bus pins take the bus's colour,
     so a shared bus reads as one thing on the picture. */
  const DEVICE_LABEL = (device) => t(`hw.dev.${device}`);
  const FIELD_LABEL = (key) => t(`hw.f.${key}`);
  const OPTION_LABEL = (value) => {
    const key = `hw.opt.${value}`;
    const text = t(key);
    return text === key ? String(value) : text;
  };

  let values = hw.defaults();
  let picking = null;  /* the signal key armed for a click on the picture */
  const controls = {};  /* key -> the input/select that edits it */
  const rows = {};      /* key -> its row, for the arming highlight */
  const groups = {};    /* device -> {section, toggle} */
  const pinNodes = {};  /* gpio -> the <g> on the picture */

  function loadDraft() {
    try {
      const text = window.localStorage.getItem(DRAFT_KEY);
      if (text) {
        const parsed = hw.parseCsv(text);
        if (parsed.bad.length === 0) values = parsed.values;
      }
    } catch (error) {
      /* Private windows and blocked storage: the defaults are fine. */
    }
  }

  function saveDraft() {
    try {
      window.localStorage.setItem(DRAFT_KEY, hw.toCsv(values));
    } catch (error) {
      /* Same as loadDraft(): nothing worth a message. */
    }
  }

  function setValue(key, value) {
    values = {...values, [key]: value};
    sync();
    saveDraft();
  }

  /* ---- the parts list --------------------------------------------------- */

  function pinOptions(field) {
    const options = [];
    if (field.kind !== 'pin') options.push({value: hw.NONE, label: '—'});
    for (const gpio of hw.headerGpios()) {
      options.push({value: String(gpio), label: `GPIO ${gpio}`});
    }
    return options;
  }

  function buildControl(field) {
    if (field.fixed !== undefined) {
      const span = document.createElement('span');
      span.className = 'row-value';
      span.textContent = String(field.fixed);
      return span;
    }
    if (field.kind === 'fixed') {
      const span = document.createElement('span');
      span.className = 'row-value';
      span.dataset.role = 'fixed';
      return span;
    }
    if (field.kind === 'pin' || field.kind === 'opt_pin') {
      const select = document.createElement('select');
      select.id = `hw-${field.key}`;
      select.dataset.key = field.key;
      for (const option of pinOptions(field)) {
        const element = document.createElement('option');
        element.value = option.value;
        element.textContent = option.label;
        select.append(element);
      }
      select.addEventListener('change', () => {
        const raw = select.value;
        setValue(field.key, raw === hw.NONE ? hw.NONE : Number(raw));
      });
      return select;
    }
    if (field.kind === 'choice') {
      const select = document.createElement('select');
      select.id = `hw-${field.key}`;
      select.dataset.key = field.key;
      for (const value of field.options) {
        const element = document.createElement('option');
        element.value = value;
        element.textContent = OPTION_LABEL(value);
        select.append(element);
      }
      select.addEventListener('change', () => setValue(field.key, select.value));
      return select;
    }
    if (field.kind === 'bool') {
      const input = document.createElement('input');
      input.type = 'checkbox';
      input.id = `hw-${field.key}`;
      input.dataset.key = field.key;
      input.addEventListener('change', () => setValue(field.key, input.checked ? 1 : 0));
      return input;
    }
    const input = document.createElement('input');
    input.type = field.kind === 'int' ? 'number' : 'text';
    input.id = `hw-${field.key}`;
    input.dataset.key = field.key;
    input.addEventListener('change', () => {
      setValue(field.key, field.kind === 'int' ? Number(input.value) : input.value);
    });
    return input;
  }

  function buildRow(field) {
    const row = document.createElement('div');
    row.className = 'device-row hw-row';
    row.dataset.key = field.key;
    const label = document.createElement('label');
    label.setAttribute('for', `hw-${field.key}`);
    label.textContent = FIELD_LABEL(field.key);
    row.append(label);
    const control = buildControl(field);
    controls[field.key] = control;
    row.append(control);
    if (field.kind === 'pin' || field.kind === 'opt_pin') {
      /* The arming button: "this one next". A second press disarms. */
      const arm = document.createElement('button');
      arm.type = 'button';
      arm.className = 'hw-arm';
      arm.dataset.key = field.key;
      arm.setAttribute('aria-label', t('hw.arm', {signal: FIELD_LABEL(field.key)}));
      arm.title = t('hw.arm', {signal: FIELD_LABEL(field.key)});
      arm.textContent = '⌖';
      arm.addEventListener('click', () => {
        picking = picking === field.key ? null : field.key;
        sync();
      });
      row.append(arm);
    }
    rows[field.key] = row;
    return row;
  }

  function buildGroups() {
    const root = $('hw-parts');
    root.replaceChildren();
    for (const device of hw.DEVICES) {
      const fields = hw.FIELDS.filter((field) => field.device === device && field.fixed === undefined);
      if (fields.length === 0) continue;
      const section = document.createElement('div');
      section.className = `device-group hw-group hw-dev-${device}`;
      section.dataset.device = device;
      const heading = document.createElement('h3');
      const swatch = document.createElement('span');
      swatch.className = 'hw-swatch';
      heading.append(swatch);
      const name = document.createElement('span');
      name.textContent = DEVICE_LABEL(device);
      heading.append(name);
      const enabling = fields.find((field) => field.enables);
      let toggle = null;
      if (enabling) {
        toggle = document.createElement('input');
        toggle.type = 'checkbox';
        toggle.className = 'hw-group-toggle';
        toggle.id = `hw-enable-${device}`;
        toggle.setAttribute('aria-label', t('hw.enable', {device: DEVICE_LABEL(device)}));
        toggle.addEventListener('change', () => {
          values = hw.setDeviceEnabled(values, device, toggle.checked);
          if (!toggle.checked && picking && hw.FIELD_BY_KEY[picking].device === device) picking = null;
          sync();
          saveDraft();
        });
        heading.append(toggle);
      }
      section.append(heading);
      const body = document.createElement('div');
      body.className = 'hw-group-body';
      for (const field of fields) body.append(buildRow(field));
      section.append(body);
      groups[device] = {section, toggle, body};
      root.append(section);
    }
  }

  /* ---- the picture ------------------------------------------------------ */

  const PITCH = 22;
  const TOP = 40;
  const BODY_X = 128;
  const BODY_W = 144;

  function svgElement(name, attributes) {
    const element = document.createElementNS(SVG_NS, name);
    for (const [key, value] of Object.entries(attributes || {})) element.setAttribute(key, value);
    return element;
  }

  function buildSvg() {
    const svg = $('hw-svg');
    svg.replaceChildren();
    const rowsCount = Math.max(hw.HEADER.left.length, hw.HEADER.right.length);
    const height = TOP + rowsCount * PITCH + 30;
    svg.setAttribute('viewBox', `0 0 400 ${height}`);

    const body = svgElement('rect', {
      x: BODY_X, y: TOP - 18, width: BODY_W, height: rowsCount * PITCH + 26, rx: 10, class: 'hw-body',
    });
    svg.append(body);
    const antenna = svgElement('rect', {
      x: BODY_X + 20, y: TOP - 30, width: BODY_W - 40, height: 14, rx: 3, class: 'hw-antenna',
    });
    svg.append(antenna);
    const title = svgElement('text', {x: 200, y: TOP + rowsCount * PITCH / 2, class: 'hw-body-label', 'text-anchor': 'middle'});
    title.textContent = 'ESP32-S3';
    svg.append(title);
    const usbNote = svgElement('text', {x: 200, y: TOP + rowsCount * PITCH + 4, class: 'hw-body-note', 'text-anchor': 'middle'});
    usbNote.textContent = 'USB';
    svg.append(usbNote);

    for (const pin of hw.headerPins()) {
      const left = pin.side === 'left';
      const y = TOP + pin.index * PITCH;
      const group = svgElement('g', {class: 'hw-pin', transform: `translate(0 ${y})`});
      const pad = svgElement('rect', {
        x: left ? BODY_X - 14 : BODY_X + BODY_W, y: -7, width: 14, height: 14, rx: 2, class: 'hw-pad',
      });
      group.append(pad);
      const name = svgElement('text', {
        x: left ? BODY_X + 6 : BODY_X + BODY_W - 6, y: 4, class: 'hw-pin-name',
        'text-anchor': left ? 'start' : 'end',
      });
      name.textContent = pin.gpio !== undefined ? (pin.label ? `${pin.gpio} ${pin.label}` : String(pin.gpio)) : pin.label;
      group.append(name);
      if (pin.gpio === undefined) {
        group.classList.add(`hw-pin-${pin.kind}`);
      } else {
        group.dataset.gpio = String(pin.gpio);
        group.classList.add('hw-gpio');
        const assignment = svgElement('text', {
          x: left ? BODY_X - 20 : BODY_X + BODY_W + 20, y: 4, class: 'hw-pin-signal',
          'text-anchor': left ? 'end' : 'start',
        });
        group.append(assignment);
        const note = svgElement('title');
        group.append(note);
        group.addEventListener('click', () => onPinClick(pin.gpio));
        pinNodes[pin.gpio] = {group, assignment, note};
      }
      svg.append(group);
    }
  }

  function onPinClick(gpio) {
    if (picking === null) {
      /* No signal armed: a click on a used pin arms that signal instead, so
         moving something is "click it, click where it goes". */
      const users = hw.pinMap(values)[gpio] || [];
      if (users.length > 0) {
        picking = users[0];
        sync();
      }
      return;
    }
    const note = hw.pinNote(gpio, values.module);
    const device = hw.FIELD_BY_KEY[picking].device;
    if (note && note.hard && !(note.usbOnly && device === 'usb')) {
      flash(t(`hw.note.${note.code}`), true);
      return;
    }
    setValue(picking, gpio);
    picking = null;
    sync();
  }

  /* ---- keeping the screen equal to the model ---------------------------- */

  function flash(text, error) {
    const status = $('hw-status');
    status.textContent = text;
    status.classList.toggle('is-error', Boolean(error));
    status.classList.toggle('is-success', !error && text !== '');
  }

  /* What the picture writes beside a pin: the short form where the dictionary
     has one ("TFT CS" rather than "Выбор кристалла экрана"), the row's label
     otherwise. */
  function shortSignal(key) {
    const short = t(`hw.s.${key}`);
    return short === `hw.s.${key}` ? FIELD_LABEL(key) : short;
  }

  function syncControls() {
    for (const field of hw.FIELDS) {
      const control = controls[field.key];
      if (!control) continue;
      const value = values[field.key];
      if (field.kind === 'bool') control.checked = String(value) === '1';
      else if (field.kind === 'fixed') control.textContent = hw.isNone(value) ? '—' : `GPIO ${value}`;
      else if (field.kind === 'pin' || field.kind === 'opt_pin') control.value = hw.isNone(value) ? hw.NONE : String(value);
      else if (control.value !== undefined) control.value = value === undefined || value === null ? '' : String(value);
      const row = rows[field.key];
      if (row) row.classList.toggle('is-picking', picking === field.key);
    }
    for (const device of Object.keys(groups)) {
      const {section, toggle} = groups[device];
      const enabled = hw.deviceEnabled(values, device);
      if (toggle) toggle.checked = enabled;
      section.classList.toggle('is-off', !enabled);
      for (const row of section.querySelectorAll('.hw-row')) {
        const field = hw.FIELD_BY_KEY[row.dataset.key];
        /* The enabling field itself stays live, so the type can be chosen
           before the rest of the rows appear. */
        row.hidden = !enabled && !field.enables;
      }
    }
  }

  function syncSvg() {
    const map = hw.pinMap(values);
    const conflicts = new Set();
    for (const [gpio, keys] of Object.entries(map)) if (keys.length > 1) conflicts.add(Number(gpio));
    const pickingDevice = picking ? hw.FIELD_BY_KEY[picking].device : null;
    for (const [gpioText, node] of Object.entries(pinNodes)) {
      const gpio = Number(gpioText);
      const users = map[gpio] || [];
      const note = hw.pinNote(gpio, values.module);
      const hard = Boolean(note && note.hard && !(note.usbOnly && (users.some((key) => hw.FIELD_BY_KEY[key].device === 'usb') || pickingDevice === 'usb')));
      node.group.setAttribute('class', 'hw-pin hw-gpio');
      if (users.length > 0) {
        node.group.classList.add('is-used', `hw-dev-${hw.FIELD_BY_KEY[users[0]].device}`);
      }
      if (hard) node.group.classList.add('is-reserved');
      else if (note) node.group.classList.add('is-noted');
      if (conflicts.has(gpio)) node.group.classList.add('is-conflict');
      if (picking) node.group.classList.add(hard ? 'is-blocked' : 'is-target');
      if (picking && users.includes(picking)) node.group.classList.add('is-picking');
      node.assignment.textContent = users.map(shortSignal).join(' / ');
      const notes = [];
      if (note) notes.push(t(`hw.note.${note.code}`));
      if (users.length > 0) notes.push(users.map(FIELD_LABEL).join(', '));
      node.note.textContent = notes.join('\n');
    }
    const hint = $('hw-hint');
    hint.textContent = picking
      ? t('hw.hint.picking', {signal: FIELD_LABEL(picking)})
      : t('hw.hint.idle');
  }

  function syncLegend() {
    const legend = $('hw-legend');
    legend.replaceChildren();
    const used = new Set();
    for (const signal of hw.signals(values)) if (signal.gpio !== null) used.add(signal.device);
    for (const device of hw.DEVICES) {
      if (!used.has(device)) continue;
      const item = document.createElement('li');
      item.className = `hw-dev-${device}`;
      const swatch = document.createElement('span');
      swatch.className = 'hw-swatch';
      item.append(swatch);
      const name = document.createElement('span');
      name.textContent = DEVICE_LABEL(device);
      item.append(name);
      legend.append(item);
    }
  }

  function describe(problem) {
    const params = {
      key: problem.key ? FIELD_LABEL(problem.key) : '',
      gpio: problem.gpio === undefined ? '' : problem.gpio,
      device: problem.device ? DEVICE_LABEL(problem.device) : '',
      bus: problem.bus ? problem.bus.toUpperCase() : '',
      pin: problem.pin ? problem.pin.toUpperCase() : '',
      keys: problem.keys ? problem.keys.map(FIELD_LABEL).join(', ') : '',
      value: problem.value === undefined ? '' : String(problem.value),
    };
    return t(`hw.err.${problem.code}`, params);
  }

  function syncReport() {
    const report = hw.validate(values);
    const list = $('hw-report');
    list.replaceChildren();
    for (const problem of report.errors) {
      const item = document.createElement('li');
      item.className = 'hw-problem is-error';
      item.textContent = describe(problem);
      list.append(item);
    }
    for (const problem of report.warnings) {
      const item = document.createElement('li');
      item.className = 'hw-problem is-warning';
      item.textContent = describe(problem);
      list.append(item);
    }
    if (report.errors.length === 0 && report.warnings.length === 0) {
      const item = document.createElement('li');
      item.className = 'hw-problem is-ok';
      item.textContent = t('hw.report_ok');
      list.append(item);
    }
    $('hw-download').disabled = report.errors.length > 0;
    return report;
  }

  function sync() {
    syncControls();
    syncSvg();
    syncLegend();
    syncReport();
    $('hw-csv').textContent = hw.toCsv(values);
  }

  /* ---- the file --------------------------------------------------------- */

  function download() {
    const text = hw.toCsv(values);
    const blob = new Blob([text], {type: 'text/csv;charset=utf-8'});
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = 'board.csv';
    document.body.append(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
    flash(t('hw.downloaded'), false);
  }

  function copy() {
    const text = hw.toCsv(values);
    if (!navigator.clipboard) {
      flash(t('hw.copy_failed'), true);
      return;
    }
    navigator.clipboard.writeText(text).then(
      () => flash(t('hw.copied'), false),
      () => flash(t('hw.copy_failed'), true),
    );
  }

  function applyImport() {
    const status = $('hw-import-status');
    const parsed = hw.parseCsv($('hw-import').value);
    if (parsed.bad.length > 0) {
      status.textContent = t('hw.import_bad', {line: parsed.bad[0].line, text: parsed.bad[0].text});
      status.classList.add('is-error');
      return;
    }
    values = parsed.values;
    picking = null;
    sync();
    saveDraft();
    status.classList.remove('is-error');
    status.textContent = parsed.unknown.length > 0
      ? t('hw.import_unknown', {keys: parsed.unknown.map((entry) => entry.key).join(', ')})
      : t('hw.import_ok');
  }

  function reset() {
    values = hw.defaults();
    picking = null;
    sync();
    saveDraft();
    flash(t('hw.reset_done'), false);
  }

  /* ---- start ------------------------------------------------------------ */

  function build() {
    buildGroups();
    buildSvg();
    window.jradioI18n.apply(document);
    sync();
  }

  loadDraft();
  build();
  $('hw-download').addEventListener('click', download);
  $('hw-copy').addEventListener('click', copy);
  $('hw-import-apply').addEventListener('click', applyImport);
  $('hw-reset').addEventListener('click', reset);
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && picking !== null) {
      picking = null;
      sync();
    }
  });
  /* The labels are built here, not in the markup, so a language change has
     to rebuild them. */
  window.jradioI18n.onChange(() => build());
})();
