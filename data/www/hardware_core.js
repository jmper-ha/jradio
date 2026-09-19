/* The wiring editor's model: what a board is made of, which pin carries which
   signal, what the rules are, and how that becomes board.csv - with no DOM in
   it, so the same file runs under Node for the tests and, later, on the web
   flasher's page and beside the device's own "hardware" page.

   The shape of the model is the shape of the file. A board is a flat map of
   key -> value, keyed exactly as board.csv is written, so the editor, the
   file and (later) the firmware's parser all speak one vocabulary and there is
   nothing to translate between them. Buses come first and own their pins;
   devices name the bus they sit on and add only what is theirs - a chip
   select, a mute line, an interrupt. That is what lets a second I2C device be
   a line in the table rather than two more pins.

   Every rule in validate() is one the firmware will apply to the same file;
   the two are kept the same by testing them on the same cases. */
(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.jradioHardware = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const NONE = 'none';
  const FORMAT = '1';

  /* The module as it sits on the board: the ESP32-S3-DevKitC-1's two headers,
     top to bottom, left then right. A pin is either a GPIO or one of the
     supply, ground and reset pins, which are drawn but never assigned. The
     flash's GPIO 26-32 and the PSRAM's 33-34 are not on the header at all. */
  const HEADER = {
    left: [
      {label: '3V3', kind: 'power'}, {label: '3V3', kind: 'power'}, {label: 'RST', kind: 'reset'},
      {gpio: 4}, {gpio: 5}, {gpio: 6}, {gpio: 7}, {gpio: 15}, {gpio: 16}, {gpio: 17}, {gpio: 18},
      {gpio: 8}, {gpio: 3}, {gpio: 46}, {gpio: 9}, {gpio: 10}, {gpio: 11}, {gpio: 12}, {gpio: 13},
      {gpio: 14}, {label: '5V', kind: 'power'}, {label: 'GND', kind: 'ground'},
    ],
    right: [
      {label: 'GND', kind: 'ground'}, {gpio: 43, label: 'TX'}, {gpio: 44, label: 'RX'},
      {gpio: 1}, {gpio: 2}, {gpio: 42}, {gpio: 41}, {gpio: 40}, {gpio: 39}, {gpio: 38},
      {gpio: 37}, {gpio: 36}, {gpio: 35}, {gpio: 0}, {gpio: 45}, {gpio: 48}, {gpio: 47},
      {gpio: 21}, {gpio: 20}, {gpio: 19}, {label: 'GND', kind: 'ground'}, {label: 'GND', kind: 'ground'},
    ],
  };

  /* The two module variants that differ in what the header can carry: with
     octal PSRAM the chip drives 35-37 itself. */
  const MODULES = {
    esp32s3_n16r8: {psramOctal: true},
    esp32s3_n8r2: {psramOctal: false},
  };

  const RTC_GPIO_MAX = 21;
  const USB_PINS = {usb_dp: 20, usb_dm: 19};
  const CONSOLE_PINS = [43, 44];
  const STRAPPING_PINS = [0, 3, 45, 46];
  const JTAG_PINS = [39, 40, 41, 42];
  const OCTAL_PSRAM_PINS = [35, 36, 37];
  /* SPI2's IOMUX pins, the ones a 40 MHz panel bus is measured on; anything
     else goes through the GPIO matrix and may not hold that clock. SPI3 on the
     S3 has no IOMUX pins at all, so it is never warned about. */
  const SPI2_IOMUX = {sclk: 12, mosi: 11, miso: 13, cs: 10};

  const DISPLAYS = [
    'st7796s_480_320', 'st7796s_320_480', 'ili9488_480_320', 'ili9488_320_480',
    'ili9341_320_240', 'ili9341_240_320', 'st7789_320_240', 'st7789_240_320',
    'st7789_320_170',
  ];

  /* Every key of the file, in the order it is written, with what it holds:
       pin      a GPIO number, required
       opt_pin  a GPIO number or "none"
       fixed    a pin the chip decides (the USB pair): the number, or "none"
       choice   one of `options`
       bool     0 or 1
       int      a number
       text     free text
     `device` groups the keys for the page and for the enable/disable rule;
     `bus` on a device key names which bus field the device sits on. */
  const FIELDS = [
    {key: 'board_format', kind: 'text', device: 'board', fixed: FORMAT},
    {key: 'board_name', kind: 'text', device: 'board', dflt: ''},
    {key: 'module', kind: 'choice', device: 'board', options: Object.keys(MODULES), dflt: 'esp32s3_n16r8'},
    {key: 'display', kind: 'choice', device: 'board', options: DISPLAYS, dflt: 'st7796s_480_320'},

    {key: 'spi2_sclk', kind: 'opt_pin', device: 'spi2', dflt: 12},
    {key: 'spi2_mosi', kind: 'opt_pin', device: 'spi2', dflt: 11},
    {key: 'spi2_miso', kind: 'opt_pin', device: 'spi2', dflt: NONE},
    {key: 'spi3_sclk', kind: 'opt_pin', device: 'spi3', dflt: 41},
    {key: 'spi3_mosi', kind: 'opt_pin', device: 'spi3', dflt: 42},
    {key: 'spi3_miso', kind: 'opt_pin', device: 'spi3', dflt: 40},
    {key: 'i2c0_sda', kind: 'opt_pin', device: 'i2c0', dflt: NONE},
    {key: 'i2c0_scl', kind: 'opt_pin', device: 'i2c0', dflt: NONE},
    {key: 'i2c0_speed', kind: 'int', device: 'i2c0', dflt: 400000},
    {key: 'i2s0_bclk', kind: 'opt_pin', device: 'i2s0', dflt: 18},
    {key: 'i2s0_lrck', kind: 'opt_pin', device: 'i2s0', dflt: 17},
    {key: 'i2s0_dout', kind: 'opt_pin', device: 'i2s0', dflt: 16},
    {key: 'i2s0_din', kind: 'opt_pin', device: 'i2s0', dflt: NONE},
    {key: 'i2s0_mclk', kind: 'opt_pin', device: 'i2s0', dflt: NONE},
    {key: 'uart1_tx', kind: 'opt_pin', device: 'uart1', dflt: NONE},
    {key: 'uart1_rx', kind: 'opt_pin', device: 'uart1', dflt: NONE},

    {key: 'tft_spi', kind: 'choice', device: 'tft', options: ['2', '3'], dflt: '2', bus: 'spi'},
    {key: 'tft_cs', kind: 'pin', device: 'tft', dflt: 10},
    {key: 'tft_dc', kind: 'pin', device: 'tft', dflt: 47},
    {key: 'tft_reset', kind: 'opt_pin', device: 'tft', dflt: NONE},
    {key: 'tft_backlight', kind: 'pin', device: 'tft', dflt: 2},

    {key: 'encoder_right', kind: 'pin', device: 'encoder', dflt: 5},
    {key: 'encoder_left', kind: 'pin', device: 'encoder', dflt: 7},
    {key: 'encoder_button', kind: 'pin', device: 'encoder', dflt: 6},
    {key: 'encoder_pullups', kind: 'bool', device: 'encoder', dflt: 0},

    {key: 'button_sleep', kind: 'opt_pin', device: 'buttons', dflt: 21},
    {key: 'button_quick_menu', kind: 'opt_pin', device: 'buttons', dflt: 45},
    {key: 'button_prev', kind: 'opt_pin', device: 'buttons', dflt: 46},
    {key: 'button_next', kind: 'opt_pin', device: 'buttons', dflt: 9},
    {key: 'buttons_pullups', kind: 'bool', device: 'buttons', dflt: 1},

    {key: 'dac', kind: 'choice', device: 'dac', options: [NONE, 'pcm5102'], dflt: 'pcm5102', enables: true},
    {key: 'dac_i2s', kind: 'choice', device: 'dac', options: ['0'], dflt: '0', bus: 'i2s'},
    {key: 'dac_mute', kind: 'opt_pin', device: 'dac', dflt: NONE},
    {key: 'amp_enable', kind: 'opt_pin', device: 'amp', dflt: NONE, enables: true},
    {key: 'amp_on_level', kind: 'bool', device: 'amp', dflt: 1},

    {key: 'peripheral_power', kind: 'opt_pin', device: 'power', dflt: NONE, enables: true},
    {key: 'peripheral_power_on_level', kind: 'bool', device: 'power', dflt: 1},

    {key: 'usb_dp', kind: 'fixed', device: 'usb', dflt: USB_PINS.usb_dp, enables: true},
    {key: 'usb_dm', kind: 'fixed', device: 'usb', dflt: USB_PINS.usb_dm},
    {key: 'usb_vbus_switched', kind: 'bool', device: 'usb', dflt: 0},

    {key: 'sd_spi', kind: 'choice', device: 'sd', options: ['2', '3'], dflt: '3', bus: 'spi'},
    {key: 'sd_cs', kind: 'opt_pin', device: 'sd', dflt: 1, enables: true},
    {key: 'sd_card_detect', kind: 'opt_pin', device: 'sd', dflt: NONE},

    {key: 'bluetooth', kind: 'choice', device: 'bluetooth', options: [NONE, 'jradio_bt'], dflt: NONE, enables: true},
    {key: 'bt_uart', kind: 'choice', device: 'bluetooth', options: ['1'], dflt: '1', bus: 'uart'},

    {key: 'fm_tuner', kind: 'choice', device: 'fm', options: [NONE, 'rda5807'], dflt: NONE, enables: true},
    {key: 'fm_i2c', kind: 'choice', device: 'fm', options: ['0'], dflt: '0', bus: 'i2c'},
    {key: 'fm_i2s', kind: 'choice', device: 'fm', options: [NONE, '0'], dflt: NONE, bus: 'i2s'},

    {key: 'rtc', kind: 'choice', device: 'rtc', options: [NONE, 'ds3231', 'pcf8563'], dflt: NONE, enables: true},
    {key: 'rtc_i2c', kind: 'choice', device: 'rtc', options: ['0'], dflt: '0', bus: 'i2c'},
    {key: 'rtc_int', kind: 'opt_pin', device: 'rtc', dflt: NONE},
  ];

  const FIELD_BY_KEY = Object.fromEntries(FIELDS.map((field) => [field.key, field]));

  /* The page's order: the board, then what is on it, then the buses it all
     hangs on - a reader starts from the parts, not from the wires. */
  const DEVICES = [
    'board', 'tft', 'encoder', 'buttons', 'dac', 'amp', 'power', 'usb', 'sd',
    'bluetooth', 'fm', 'rtc', 'spi2', 'spi3', 'i2c0', 'i2s0', 'uart1',
  ];
  const BUSES = ['spi2', 'spi3', 'i2c0', 'i2s0', 'uart1'];

  /* What each device needs of its bus, by the bus's pin names. */
  const BUS_NEEDS = {
    tft: {spi: ['sclk', 'mosi']},
    sd: {spi: ['sclk', 'mosi', 'miso']},
    dac: {i2s: ['bclk', 'lrck', 'dout']},
    bluetooth: {uart: ['tx', 'rx']},
    fm: {i2c: ['sda', 'scl'], i2s: ['din']},
    rtc: {i2c: ['sda', 'scl']},
  };

  /* When a device is switched on from "none", these are the values it comes
     up with: the README's own wiring where the README has one (the module on
     13/14, the card's select on 1), and otherwise a pin the README leaves
     free that carries no warning of its own - 4, 8, 38 and 48 are the ones
     left once the strapping and JTAG pins are set aside. */
  const ENABLE_VALUES = {
    dac: {dac: 'pcm5102'},
    amp: {amp_enable: 48},
    power: {peripheral_power: 38},
    usb: {usb_dp: USB_PINS.usb_dp, usb_dm: USB_PINS.usb_dm},
    sd: {sd_cs: 1},
    bluetooth: {bluetooth: 'jradio_bt', uart1_tx: 13, uart1_rx: 14},
    fm: {fm_tuner: 'rda5807', i2c0_sda: 8, i2c0_scl: 4},
    rtc: {rtc: 'ds3231', i2c0_sda: 8, i2c0_scl: 4},
  };

  function isNone(value) {
    return value === NONE || value === '' || value === undefined || value === null;
  }

  function pinValue(value) {
    if (isNone(value)) return null;
    const number = Number(value);
    return Number.isInteger(number) ? number : null;
  }

  function defaults() {
    const values = {};
    for (const field of FIELDS) {
      values[field.key] = field.fixed !== undefined ? field.fixed : field.dflt;
    }
    return values;
  }

  function headerPins() {
    const pins = [];
    for (const side of ['left', 'right']) {
      HEADER[side].forEach((pin, index) => {
        pins.push({...pin, side, index});
      });
    }
    return pins;
  }

  function headerGpios() {
    return headerPins().filter((pin) => pin.gpio !== undefined).map((pin) => pin.gpio);
  }

  /* Why a pin cannot, or should not, take an ordinary signal. `hard` is an
     error; the rest is a note the page shows on the pin and nothing more - the
     README's own wiring puts buttons on strapping pins and the card on the
     JTAG pins, and a report that flagged the default board would teach the
     reader to ignore it. */
  function pinNote(gpio, moduleName) {
    const module = MODULES[moduleName] || MODULES.esp32s3_n16r8;
    if (module.psramOctal && OCTAL_PSRAM_PINS.includes(gpio)) return {code: 'pin_psram', hard: true};
    if (CONSOLE_PINS.includes(gpio)) return {code: 'pin_console', hard: true};
    if (gpio === USB_PINS.usb_dp || gpio === USB_PINS.usb_dm) return {code: 'pin_usb', hard: true, usbOnly: true};
    if (STRAPPING_PINS.includes(gpio)) return {code: 'pin_strapping', hard: false};
    if (JTAG_PINS.includes(gpio)) return {code: 'pin_jtag', hard: false};
    return null;
  }

  /* A device is on the board when its enabling key is not "none" - a type for
     the chips, the defining pin for the rest. The buses, the panel, the knob
     and the buttons are always there. */
  function deviceEnabled(values, device) {
    const field = FIELDS.find((item) => item.device === device && item.enables);
    if (!field) return true;
    return !isNone(values[field.key]);
  }

  function setDeviceEnabled(values, device, enabled) {
    const next = {...values};
    const field = FIELDS.find((item) => item.device === device && item.enables);
    if (!field) return next;
    if (!enabled) {
      next[field.key] = NONE;
      return next;
    }
    for (const [key, value] of Object.entries(ENABLE_VALUES[device] || {})) {
      /* A bus pin already wired is left alone: switching the RTC on must not
         move an I2C bus the tuner is already on. */
      if (FIELD_BY_KEY[key].device === device || isNone(next[key])) next[key] = value;
    }
    return next;
  }

  /* The bus a device sits on, as the bus's field prefix: tft_spi=2 -> spi2. */
  function busOf(values, device, busKind) {
    const field = FIELDS.find((item) => item.device === device && item.bus === busKind);
    if (!field) return null;
    const value = values[field.key];
    if (isNone(value)) return null;
    return busKind + value;
  }

  /* Every signal that owns a pin right now: the buses in use, the devices on
     the board. Bus pins that no device uses are reported too, so a wired but
     idle bus shows on the picture - it is still soldered. */
  function signals(values) {
    const list = [];
    for (const field of FIELDS) {
      if (!['pin', 'opt_pin', 'fixed'].includes(field.kind)) continue;
      if (!deviceEnabled(values, field.device)) continue;
      const gpio = pinValue(values[field.key]);
      list.push({key: field.key, device: field.device, gpio, required: field.kind === 'pin'});
    }
    return list;
  }

  function pinMap(values) {
    const map = {};
    for (const signal of signals(values)) {
      if (signal.gpio === null) continue;
      (map[signal.gpio] ||= []).push(signal.key);
    }
    return map;
  }

  function validate(values) {
    const errors = [];
    const warnings = [];
    const moduleName = values.module;
    const onHeader = new Set(headerGpios());

    /* Bus pins are one signal even when three devices share them, so a shared
       bus is never a conflict - and a bus pin reused as a chip select is. */
    const map = pinMap(values);
    for (const [gpio, keys] of Object.entries(map)) {
      if (keys.length > 1) errors.push({code: 'pin_conflict', gpio: Number(gpio), keys});
    }

    for (const signal of signals(values)) {
      const {key, gpio} = signal;
      if (gpio === null) {
        if (signal.required) errors.push({code: 'pin_missing', key});
        continue;
      }
      if (!onHeader.has(gpio)) {
        errors.push({code: 'pin_not_on_header', key, gpio});
        continue;
      }
      const note = pinNote(gpio, moduleName);
      if (note && note.usbOnly && FIELD_BY_KEY[key].device === 'usb') continue;
      if (note && note.hard) errors.push({code: note.code, key, gpio});
    }

    /* The USB pair is the chip's, not a choice. */
    for (const [key, fixed] of Object.entries(USB_PINS)) {
      const gpio = pinValue(values[key]);
      if (gpio !== null && gpio !== fixed) errors.push({code: 'usb_pin_fixed', key, gpio});
    }

    /* Every device on the board needs the pins of the bus it names. */
    for (const [device, needs] of Object.entries(BUS_NEEDS)) {
      if (!deviceEnabled(values, device)) continue;
      for (const [busKind, pins] of Object.entries(needs)) {
        const bus = busOf(values, device, busKind);
        if (bus === null) continue;  /* a bus the device may go without, such as the tuner's I2S */
        for (const pin of pins) {
          if (pinValue(values[`${bus}_${pin}`]) === null) {
            errors.push({code: 'bus_unwired', device, bus, pin});
          }
        }
      }
    }

    const sleep = pinValue(values.button_sleep);
    if (sleep !== null && sleep > RTC_GPIO_MAX) warnings.push({code: 'sleep_not_rtc', key: 'button_sleep', gpio: sleep});
    const rtcInt = pinValue(values.rtc_int);
    if (deviceEnabled(values, 'rtc') && rtcInt !== null && rtcInt > RTC_GPIO_MAX) {
      warnings.push({code: 'rtc_int_not_rtc', key: 'rtc_int', gpio: rtcInt});
    }

    /* The panel's bus at 40 MHz wants SPI2's own pins. */
    if (busOf(values, 'tft', 'spi') === 'spi2') {
      const checks = {spi2_sclk: SPI2_IOMUX.sclk, spi2_mosi: SPI2_IOMUX.mosi, tft_cs: SPI2_IOMUX.cs};
      for (const [key, iomux] of Object.entries(checks)) {
        const gpio = pinValue(values[key]);
        if (gpio !== null && gpio !== iomux) warnings.push({code: 'spi_not_iomux', key, gpio});
      }
    }

    for (const field of FIELDS) {
      const value = values[field.key];
      if (field.kind === 'choice' && !field.options.includes(String(value))) {
        errors.push({code: 'bad_value', key: field.key, value});
      } else if (field.kind === 'bool' && !['0', '1'].includes(String(value))) {
        errors.push({code: 'bad_value', key: field.key, value});
      } else if (field.kind === 'int' && !Number.isInteger(Number(value))) {
        errors.push({code: 'bad_value', key: field.key, value});
      }
    }

    return {errors, warnings};
  }

  function toCsv(values) {
    const lines = ['# key,value'];
    for (const field of FIELDS) {
      const value = values[field.key];
      lines.push(`${field.key},${isNone(value) && field.kind !== 'text' ? NONE : value}`);
    }
    return lines.join('\n') + '\n';
  }

  /* The file back into a board. Keys the editor does not know are kept aside
     rather than dropped, so a file from a newer firmware survives a round trip
     through an older page; lines that are not "key,value" are reported. */
  function parseCsv(text) {
    const values = defaults();
    const unknown = [];
    const bad = [];
    String(text).split(/\r?\n/).forEach((raw, index) => {
      const line = raw.trim();
      if (line === '' || line.startsWith('#')) return;
      const comma = line.indexOf(',');
      if (comma < 0) { bad.push({line: index + 1, text: line}); return; }
      const key = line.slice(0, comma).trim();
      const value = line.slice(comma + 1).trim();
      const field = FIELD_BY_KEY[key];
      if (!field) { unknown.push({key, value}); return; }
      if (field.fixed !== undefined) return;
      if (['pin', 'opt_pin', 'fixed'].includes(field.kind)) {
        if (isNone(value)) values[key] = NONE;
        else if (Number.isInteger(Number(value))) values[key] = Number(value);
        else bad.push({line: index + 1, text: line});
      } else if (field.kind === 'int' || field.kind === 'bool') {
        if (Number.isInteger(Number(value))) values[key] = Number(value);
        else bad.push({line: index + 1, text: line});
      } else {
        values[key] = value;
      }
    });
    return {values, unknown, bad};
  }

  return {
    NONE, FORMAT, FIELDS, FIELD_BY_KEY, DEVICES, BUSES, HEADER, MODULES, DISPLAYS,
    RTC_GPIO_MAX, USB_PINS,
    defaults, headerPins, headerGpios, pinNote, deviceEnabled, setDeviceEnabled,
    busOf, signals, pinMap, validate, toCsv, parseCsv, isNone, pinValue,
  };
});
