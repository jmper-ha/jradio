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

  /* The one module the firmware is built for: the partition table wants its
     16 MB of flash and sdkconfig its octal PSRAM, which drives 35-37 itself.
     The quad-PSRAM N8R2 would need a build of its own before it can be
     offered here. */
  const MODULES = {
    esp32s3_n16r8: {psramOctal: true},
  };

  /* A pin value meaning "the module's RST pad", next to a GPIO number and NONE. */
  const RESET = 'rst';
  const RTC_GPIO_MAX = 21;
  const USB_PINS = {usb_dp: 20, usb_dm: 19};
  const CONSOLE_PINS = [43, 44];
  const STRAPPING_PINS = [0, 3, 45, 46];
  const JTAG_PINS = [39, 40, 41, 42];
  const OCTAL_PSRAM_PINS = [35, 36, 37];
  /* SPI2's IOMUX pins, the ones a 40 MHz panel bus is measured on; anything
     else goes through the GPIO matrix and may not hold that clock. SPI3 on the
     S3 has no IOMUX pins at all, so it is never warned about. */
  const SPI2_IOMUX = {sclk: 12, mosi: 11, cs: 10};

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
     `bus` on a device key names which bus field the device sits on;
     `define` is the same setting's name in board_options.h, shown on the
     label so a reader of the header finds the row and back. */
  const FIELDS = [
    {key: 'board_format', kind: 'text', device: 'board', fixed: FORMAT},
    {key: 'board_name', kind: 'text', device: 'board', dflt: '', hint: true},
    {key: 'module', kind: 'text', device: 'board', fixed: 'esp32s3_n16r8'},
    {key: 'display', kind: 'choice', device: 'board', options: DISPLAYS, dflt: 'st7796s_480_320', define: 'DISPLAY'},

    /* SPI2's pins are the chip's own (IOMUX) and are not chosen: the display
       bus runs at 40 MHz on them and nowhere else. They are shown, not
       edited; a file saying otherwise is read as these. No MISO: the display
       only listens, and the firmware brings the bus up without one - which
       is why the card cannot share it. */
    {key: 'spi2_sclk', kind: 'fixed', device: 'spi2', dflt: SPI2_IOMUX.sclk, locked: true, define: 'TFT_SCLK_GPIO'},
    {key: 'spi2_mosi', kind: 'fixed', device: 'spi2', dflt: SPI2_IOMUX.mosi, locked: true, define: 'TFT_MOSI_GPIO'},
    {key: 'spi3_sclk', kind: 'opt_pin', device: 'spi3', dflt: 41, define: 'SDC_SCK_GPIO'},
    {key: 'spi3_mosi', kind: 'opt_pin', device: 'spi3', dflt: 42, define: 'SDC_MOSI_GPIO'},
    {key: 'spi3_miso', kind: 'opt_pin', device: 'spi3', dflt: 40, define: 'SDC_MISO_GPIO'},
    {key: 'i2s0_bclk', kind: 'opt_pin', device: 'i2s0', dflt: 18, define: 'I2S_BCLK_GPIO'},
    {key: 'i2s0_lrck', kind: 'opt_pin', device: 'i2s0', dflt: 17, define: 'I2S_LRCK_GPIO'},
    {key: 'i2s0_dout', kind: 'opt_pin', device: 'i2s0', dflt: 16, define: 'I2S_DOUT_GPIO'},
    {key: 'uart1_tx', kind: 'opt_pin', device: 'uart1', dflt: NONE, define: 'BT_UART_TX_GPIO'},
    {key: 'uart1_rx', kind: 'opt_pin', device: 'uart1', dflt: NONE, define: 'BT_UART_RX_GPIO'},

    /* The display is always on SPI2: only that bus has IOMUX pins on the S3,
       and the 40 MHz the wide panels run at holds on those alone. The key
       stays in the file so the card can say "the display's bus" by number. */
    {key: 'tft_spi', kind: 'text', device: 'tft', fixed: '2', bus: 'spi', define: 'DISPLAY_SPI_PERIPHERAL'},
    /* The display has no switch: the firmware does not boot without one. */
    {key: 'tft_cs', kind: 'pin', device: 'tft', dflt: 10, define: 'TFT_CS_GPIO'},
    {key: 'tft_dc', kind: 'pin', device: 'tft', dflt: 47, define: 'TFT_DC_GPIO'},
    /* The display's RST is most often tied to the module's own RST pad - the
       README board does that - so the field takes RESET besides a GPIO or
       "none" (a module with no reset pin at all). */
    {key: 'tft_reset', kind: 'opt_pin', device: 'tft', dflt: RESET, resetOk: true, define: 'TFT_RESET_GPIO'},
    {key: 'tft_backlight', kind: 'pin', device: 'tft', dflt: 2, define: 'TFT_BACKLIGHT_GPIO'},

    /* The encoder has no switch either: the firmware does not run without
       one. A web-only board is for the day it does. */
    {key: 'encoder_right', kind: 'pin', device: 'encoder', dflt: 5, define: 'ENCODER_RIGHT_GPIO'},
    {key: 'encoder_left', kind: 'pin', device: 'encoder', dflt: 7, define: 'ENCODER_LEFT_GPIO'},
    {key: 'encoder_button', kind: 'pin', device: 'encoder', dflt: 6, define: 'ENCODER_BUTTON_GPIO'},
    {key: 'encoder_pullups', kind: 'bool', device: 'encoder', dflt: 0, define: 'ENCODER_USE_INTERNAL_PULLUPS'},

    {key: 'button_sleep', kind: 'opt_pin', device: 'buttons', dflt: 21, define: 'BUTTON_SLEEP_GPIO'},
    {key: 'button_quick_menu', kind: 'opt_pin', device: 'buttons', dflt: 45, define: 'BUTTON_QUICK_MENU_GPIO'},
    {key: 'button_prev', kind: 'opt_pin', device: 'buttons', dflt: 46, define: 'BUTTON_PREV_GPIO'},
    {key: 'button_next', kind: 'opt_pin', device: 'buttons', dflt: 9, define: 'BUTTON_NEXT_GPIO'},
    {key: 'buttons_pullups', kind: 'bool', device: 'buttons', dflt: 1, define: 'BUTTONS_USE_INTERNAL_PULLUPS'},

    /* The infrared receiver: one pin, off by default like the README board. */
    {key: 'ir_receiver', kind: 'opt_pin', device: 'ir', dflt: NONE, enables: true, define: 'IR_RECEIVER_GPIO'},

    /* The DAC is always there: a player without an output is no player. The
       chip stays a choice for the day a second one is supported. */
    {key: 'dac', kind: 'choice', device: 'dac', options: ['pcm5102'], dflt: 'pcm5102', define: 'AUDIO_DAC'},
    {key: 'dac_i2s', kind: 'choice', device: 'dac', options: ['0'], dflt: '0', bus: 'i2s'},
    {key: 'dac_mute', kind: 'opt_pin', device: 'dac', dflt: NONE, define: 'AUDIO_DAC_MUTE_GPIO'},
    {key: 'amp_enable', kind: 'opt_pin', device: 'amp', dflt: NONE, enables: true, define: 'AUDIO_AMP_GPIO'},
    {key: 'amp_on_level', kind: 'bool', device: 'amp', dflt: 1, define: 'AUDIO_AMP_ON_LEVEL'},

    {key: 'peripheral_power', kind: 'opt_pin', device: 'power', dflt: NONE, enables: true, define: 'PERIPHERAL_POWER_GPIO'},
    {key: 'peripheral_power_on_level', kind: 'bool', device: 'power', dflt: 1, define: 'PERIPHERAL_POWER_ON_LEVEL'},

    {key: 'usb_dp', kind: 'fixed', device: 'usb', dflt: USB_PINS.usb_dp, enables: true, define: 'USB_DP_GPIO'},
    {key: 'usb_dm', kind: 'fixed', device: 'usb', dflt: USB_PINS.usb_dm, define: 'USB_DM_GPIO'},

    /* The card's bus is SPI3 and no choice: the display's SPI2 has no MISO
       in the firmware, so a card there would build and never mount. */
    {key: 'sd_spi', kind: 'choice', device: 'sd', options: ['3'], dflt: '3', bus: 'spi', define: 'SDC_SPI_PERIPHERAL'},
    {key: 'sd_cs', kind: 'opt_pin', device: 'sd', dflt: 1, enables: true, define: 'SDC_CS_GPIO'},

    {key: 'bluetooth', kind: 'choice', device: 'bluetooth', options: [NONE, 'jradio_bt'], dflt: NONE, enables: true, define: 'BLUETOOTH'},
    {key: 'bt_uart', kind: 'choice', device: 'bluetooth', options: ['1'], dflt: '1', bus: 'uart'},
    /* The module's audio goes over the DAC's I2S bus - both directions - so
       the card names that bus as its "sound". */
    {key: 'bt_i2s', kind: 'choice', device: 'bluetooth', options: ['0'], dflt: '0', bus: 'i2s'},

    /* Software sources, built in or left out: no pins, but a line in the
       header each. Both are on in the README board. */
    {key: 'yandex_music', kind: 'bool', device: 'features', dflt: 1, define: 'YANDEX_MUSIC'},
    {key: 'dlna', kind: 'bool', device: 'features', dflt: 1, define: 'DLNA'},
  ];

  const FIELD_BY_KEY = Object.fromEntries(FIELDS.map((field) => [field.key, field]));

  /* The page's order: the board, then each part with its bus right under
     it - the display's SPI2, the DAC's I2S, the card's SPI3, the module's
     UART - so the wires sit next to what they serve. */
  const DEVICES = [
    'board', 'tft', 'spi2', 'encoder', 'buttons', 'ir', 'dac', 'i2s0', 'amp', 'power',
    'usb', 'sd', 'spi3', 'bluetooth', 'uart1', 'features',
  ];
  const BUSES = ['spi2', 'spi3', 'i2s0', 'uart1'];

  /* What each device needs of its bus, by the bus's pin names. */
  const BUS_NEEDS = {
    tft: {spi: ['sclk', 'mosi']},
    sd: {spi: ['sclk', 'mosi', 'miso']},
    dac: {i2s: ['bclk', 'lrck', 'dout']},
    bluetooth: {uart: ['tx', 'rx'], i2s: ['bclk', 'lrck', 'dout']},
  };

  /* When a device is switched on from "none", these are the values it comes
     up with: the README's own wiring where the README has one (the module on
     13/14, the card's select on 1), and otherwise a pin the README leaves
     free that carries no warning of its own - 4, 38 and 48 are the ones
     left once the strapping and JTAG pins are set aside; the receiver gets
     4 because it must be able to wake the chip, and only 0-21 can. */
  const ENABLE_VALUES = {
    ir: {ir_receiver: 4},
    amp: {amp_enable: 48},
    power: {peripheral_power: 38},
    usb: {usb_dp: USB_PINS.usb_dp, usb_dm: USB_PINS.usb_dm},
    sd: {sd_cs: 1},
    bluetooth: {bluetooth: 'jradio_bt', uart1_tx: 13, uart1_rx: 14},
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
     the chips, the defining pin for the rest. The buses, the display, the
     encoder, the DAC and the buttons are always there. */
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
      /* A bus pin already wired is left alone: switching a device on must
         not move a bus something else already sits on. */
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

  /* A bus is in use while a device that is on the board names it. A bus
     nobody is on - the module's UART with the module switched off, the
     card's SPI3 without the card - holds no pins: its numbers stay in the
     file for the day the device comes back, but on the picture and in the
     conflict check they are free. */
  function busUsed(values, bus) {
    return FIELDS.some((field) => field.bus && deviceEnabled(values, field.device) &&
                                  busOf(values, field.device, field.bus) === bus);
  }

  /* Every signal that owns a pin right now: the devices on the board and
     the buses they are on. */
  function signals(values) {
    const list = [];
    for (const field of FIELDS) {
      if (!['pin', 'opt_pin', 'fixed'].includes(field.kind)) continue;
      if (!deviceEnabled(values, field.device)) continue;
      if (BUSES.includes(field.device) && !busUsed(values, field.device)) continue;
      if (field.when && !field.when(values)) continue;
      const gpio = pinValue(values[field.key]);
      list.push({key: field.key, device: field.device, gpio, required: field.kind === 'pin',
                 reset: Boolean(field.resetOk) && values[field.key] === RESET});
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
    const ir = pinValue(values.ir_receiver);
    if (ir !== null && ir > RTC_GPIO_MAX) warnings.push({code: 'ir_not_rtc', key: 'ir_receiver', gpio: ir});

    /* The display's bus at 40 MHz wants SPI2's own pins; the clock and data
       cannot stray any more, the chip select still can. */
    {
      const gpio = pinValue(values.tft_cs);
      if (gpio !== null && gpio !== SPI2_IOMUX.cs) warnings.push({code: 'spi_not_iomux', key: 'tft_cs', gpio});
    }

    for (const field of FIELDS) {
      const value = values[field.key];
      if (field.kind === 'choice' && !field.options.includes(String(value))) {
        errors.push({code: 'bad_value', key: field.key, value});
      } else if (field.kind === 'bool' && !['0', '1'].includes(String(value))) {
        errors.push({code: 'bad_value', key: field.key, value});
      } else if (field.kind === 'int' && !Number.isInteger(Number(value))) {
        errors.push({code: 'bad_value', key: field.key, value});
      } else if (['pin', 'opt_pin'].includes(field.kind) && pinValue(value) === null &&
                 !isNone(value) && !(field.resetOk && value === RESET)) {
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

  /* ---- board_options.h -------------------------------------------------- */

  /* The same board as the header the firmware is built from: what a person
     downloads today, while the CSV above is what the flasher will write to
     the board partition once the firmware reads one. Every option the header
     in the repository carries is here, in its order and with its spelling,
     so the file drops into the project root as it is. A part that is not on
     the board leaves a comment where its block would be, not a #define. */
  const HEADER_DISPLAY = (values) => `DISPLAY_${String(values.display).toUpperCase()}`;

  function toHeader(values) {
    const pin = (key) => pinValue(values[key]);
    const gpio = (name, key) => (pin(key) === null ? [] : [`#define ${name} ${pin(key)}`]);
    const on = (key) => (isNone(values[key]) ? 0 : Number(values[key]) === 1 ? 1 : 0);
    const name = String(values.board_name || '').trim();
    const lines = [
      '#pragma once',
      '/* board_options.h - the wiring of one board, for the jRadio firmware.',
      ` * Written by the wiring editor${name ? ` for "${name}"` : ''}; module ESP32-S3-WROOM-1 N16R8.`,
      ' * Put it at the root of the project in place of the one there and build. */',
      '#include "board_parts.h"',
      '#include "board_options_guard.h"',
      '',
      '/* The display: which panel, how it stands, and its SPI2 pins. */',
      `#define DISPLAY ${HEADER_DISPLAY(values)}`,
      `#define DISPLAY_SPI_PERIPHERAL ${values.tft_spi}`,
      ...gpio('TFT_CS_GPIO', 'tft_cs'),
      ...gpio('TFT_DC_GPIO', 'tft_dc'),
      ...(pin('tft_reset') === null
        ? ["/* TFT_RESET_GPIO: the panel's RST is tied to the module's RST pad. */"]
        : gpio('TFT_RESET_GPIO', 'tft_reset')),
      ...gpio('TFT_MOSI_GPIO', 'spi2_mosi'),
      ...gpio('TFT_SCLK_GPIO', 'spi2_sclk'),
      ...gpio('TFT_BACKLIGHT_GPIO', 'tft_backlight'),
      '',
      '/* The encoder and the buttons. A button that is not wired has no line. */',
      ...gpio('ENCODER_RIGHT_GPIO', 'encoder_right'),
      ...gpio('ENCODER_LEFT_GPIO', 'encoder_left'),
      ...gpio('ENCODER_BUTTON_GPIO', 'encoder_button'),
      ...gpio('BUTTON_SLEEP_GPIO', 'button_sleep'),
      ...gpio('BUTTON_QUICK_MENU_GPIO', 'button_quick_menu'),
      ...gpio('BUTTON_PREV_GPIO', 'button_prev'),
      ...gpio('BUTTON_NEXT_GPIO', 'button_next'),
      `#define ENCODER_USE_INTERNAL_PULLUPS ${on('encoder_pullups')}`,
      `#define BUTTONS_USE_INTERNAL_PULLUPS ${on('buttons_pullups')}`,
      '',
      ...(deviceEnabled(values, 'ir')
        ? ['/* The infrared receiver, for the remote. */', ...gpio('IR_RECEIVER_GPIO', 'ir_receiver')]
        : ['/* No infrared receiver: IR_RECEIVER_GPIO would go here. */']),
      '',
      '/* The DAC on I2S0. */',
      `#define AUDIO_DAC DAC_${String(values.dac).toUpperCase()}`,
      ...gpio('I2S_DOUT_GPIO', 'i2s0_dout'),
      ...gpio('I2S_BCLK_GPIO', 'i2s0_bclk'),
      ...gpio('I2S_LRCK_GPIO', 'i2s0_lrck'),
      '#define AUDIO_DAC_HAS_MCLK 0',
      ...(pin('dac_mute') === null ? [] : gpio('AUDIO_DAC_MUTE_GPIO', 'dac_mute')),
      ...(deviceEnabled(values, 'amp')
        ? ['', "/* The amplifier's MUTE / SD input, and the level that lets it play. */",
           ...gpio('AUDIO_AMP_GPIO', 'amp_enable'), `#define AUDIO_AMP_ON_LEVEL ${on('amp_on_level')}`]
        : []),
      ...(deviceEnabled(values, 'power')
        ? ['', '/* The switch feeding everything outside the module, cut in deep sleep. */',
           ...gpio('PERIPHERAL_POWER_GPIO', 'peripheral_power'),
           `#define PERIPHERAL_POWER_ON_LEVEL ${on('peripheral_power_on_level')}`]
        : []),
      '',
      ...(deviceEnabled(values, 'usb')
        ? ['/* USB Host for a flash drive: the pins are the chip\'s own. */',
           `#define USB_DM_GPIO ${USB_PINS.usb_dm}`, `#define USB_DP_GPIO ${USB_PINS.usb_dp}`,
           '#define USB_VBUS_SWITCHED 0']
        : ['/* No USB port: USB_DM_GPIO and USB_DP_GPIO would go here. */']),
      '',
      ...(deviceEnabled(values, 'sd')
        ? ['/* microSD over SPI3. */',
           '#define SDC_SPI_PERIPHERAL 3',
           ...gpio('SDC_CS_GPIO', 'sd_cs'),
           ...gpio('SDC_SCK_GPIO', 'spi3_sclk'),
           ...gpio('SDC_MISO_GPIO', 'spi3_miso'),
           ...gpio('SDC_MOSI_GPIO', 'spi3_mosi'),
           '#define SDC_HAS_CARD_DETECT 0']
        : ['/* No microSD slot: the SDC_* lines would go here. */']),
      '',
      ...(deviceEnabled(values, 'bluetooth')
        ? ['/* The jradio-bt module: commands over UART1, sound over the same I2S. */',
           `#define BLUETOOTH BLUETOOTH_${String(values.bluetooth).toUpperCase()}`,
           ...gpio('BT_UART_TX_GPIO', 'uart1_tx'),
           ...gpio('BT_UART_RX_GPIO', 'uart1_rx')]
        : ['/* No Bluetooth module: BLUETOOTH and the BT_UART_* lines would go here. */']),
      '',
      '/* Sources that need no wiring; a built-in one also has a switch in the settings. */',
      `#define YANDEX_MUSIC ${on('yandex_music') ? 'FEATURE_ON' : 'FEATURE_OFF'}`,
      `#define DLNA ${on('dlna') ? 'FEATURE_ON' : 'FEATURE_OFF'}`,
    ];
    return lines.join('\n') + '\n';
  }

  /* The header back into a board, so a file downloaded earlier can be
     pasted in and carried on with. Only lines that begin with #define count:
     a commented-out one is a part that is not there. Names this editor does
     not know are reported, never dropped silently. */
  function parseHeader(text) {
    const defined = {};
    const unknown = [];
    for (const raw of String(text).split(/\r?\n/)) {
      const match = /^\s*#define\s+([A-Z0-9_]+)(?:\s+(\S+))?/.exec(raw);
      if (match) defined[match[1]] = match[2] === undefined ? '' : match[2];
    }
    const values = defaults();
    const named = /wiring editor for "([^"]*)"/.exec(String(text));
    if (named) values.board_name = named[1];
    const take = (name) => {
      const value = defined[name];
      delete defined[name];
      return value;
    };
    const asPin = (key, name) => {
      const value = take(name);
      if (value === undefined) values[key] = FIELD_BY_KEY[key].resetOk ? RESET : NONE;
      else if (Number.isInteger(Number(value))) values[key] = Number(value);
      else unknown.push({key: name, value});
    };
    const asBool = (key, name) => {
      const value = take(name);
      if (value !== undefined) values[key] = value === '1' ? 1 : 0;
    };
    const display = take('DISPLAY');
    if (display !== undefined) {
      const id = display.replace(/^DISPLAY_/, '').toLowerCase();
      if (DISPLAYS.includes(id)) values.display = id; else unknown.push({key: 'DISPLAY', value: display});
    }
    take('DISPLAY_SPI_PERIPHERAL');  /* always 2 here */
    take('TFT_MOSI_GPIO'); take('TFT_SCLK_GPIO');  /* SPI2's own pins */
    asPin('tft_cs', 'TFT_CS_GPIO'); asPin('tft_dc', 'TFT_DC_GPIO');
    asPin('tft_reset', 'TFT_RESET_GPIO'); asPin('tft_backlight', 'TFT_BACKLIGHT_GPIO');
    asPin('encoder_right', 'ENCODER_RIGHT_GPIO'); asPin('encoder_left', 'ENCODER_LEFT_GPIO');
    asPin('encoder_button', 'ENCODER_BUTTON_GPIO');
    asPin('button_sleep', 'BUTTON_SLEEP_GPIO'); asPin('button_quick_menu', 'BUTTON_QUICK_MENU_GPIO');
    asPin('button_prev', 'BUTTON_PREV_GPIO'); asPin('button_next', 'BUTTON_NEXT_GPIO');
    asBool('encoder_pullups', 'ENCODER_USE_INTERNAL_PULLUPS');
    asBool('buttons_pullups', 'BUTTONS_USE_INTERNAL_PULLUPS');
    asPin('ir_receiver', 'IR_RECEIVER_GPIO');
    const dac = take('AUDIO_DAC');
    if (dac !== undefined) {
      const id = dac.replace(/^DAC_/, '').toLowerCase();
      if (FIELD_BY_KEY.dac.options.includes(id)) values.dac = id; else unknown.push({key: 'AUDIO_DAC', value: dac});
    }
    asPin('i2s0_dout', 'I2S_DOUT_GPIO'); asPin('i2s0_bclk', 'I2S_BCLK_GPIO'); asPin('i2s0_lrck', 'I2S_LRCK_GPIO');
    take('AUDIO_DAC_HAS_MCLK');
    asPin('dac_mute', 'AUDIO_DAC_MUTE_GPIO');
    asPin('amp_enable', 'AUDIO_AMP_GPIO'); asBool('amp_on_level', 'AUDIO_AMP_ON_LEVEL');
    asPin('peripheral_power', 'PERIPHERAL_POWER_GPIO'); asBool('peripheral_power_on_level', 'PERIPHERAL_POWER_ON_LEVEL');
    /* USB is on or off by its pins being named; the numbers are fixed. */
    const usb = take('USB_DP_GPIO') !== undefined;
    take('USB_DM_GPIO'); take('USB_VBUS_SWITCHED');
    if (!usb) values.usb_dp = NONE;
    const sdBus = take('SDC_SPI_PERIPHERAL');
    if (sdBus !== undefined && sdBus !== '3') unknown.push({key: 'SDC_SPI_PERIPHERAL', value: sdBus});
    asPin('sd_cs', 'SDC_CS_GPIO');
    asPin('spi3_sclk', 'SDC_SCK_GPIO'); asPin('spi3_miso', 'SDC_MISO_GPIO'); asPin('spi3_mosi', 'SDC_MOSI_GPIO');
    take('SDC_HAS_CARD_DETECT');
    const bluetooth = take('BLUETOOTH');
    if (bluetooth === undefined) values.bluetooth = NONE;
    else {
      const id = bluetooth.replace(/^BLUETOOTH_/, '').toLowerCase();
      if (FIELD_BY_KEY.bluetooth.options.includes(id)) values.bluetooth = id;
      else unknown.push({key: 'BLUETOOTH', value: bluetooth});
    }
    asPin('uart1_tx', 'BT_UART_TX_GPIO'); asPin('uart1_rx', 'BT_UART_RX_GPIO');
    const asFeature = (key, name) => {
      const value = take(name);
      if (value !== undefined) values[key] = value === 'FEATURE_ON' ? 1 : 0;
    };
    asFeature('yandex_music', 'YANDEX_MUSIC'); asFeature('dlna', 'DLNA');
    for (const [key, value] of Object.entries(defined)) unknown.push({key, value});
    return {values, unknown};
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
      if (field.fixed !== undefined || field.locked) return;
      if (['pin', 'opt_pin', 'fixed'].includes(field.kind)) {
        if (isNone(value)) values[key] = NONE;
        else if (field.resetOk && value === RESET) values[key] = RESET;
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
    NONE, RESET, FORMAT, FIELDS, FIELD_BY_KEY, DEVICES, BUSES, HEADER, MODULES, DISPLAYS,
    RTC_GPIO_MAX, USB_PINS,
    defaults, headerPins, headerGpios, pinNote, deviceEnabled, setDeviceEnabled,
    busOf, busUsed, signals, pinMap, validate, toCsv, parseCsv, toHeader, parseHeader, isNone, pinValue,
  };
});
