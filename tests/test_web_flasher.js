'use strict';

/* The flasher page's model: the board partition's image and what each of
   the two buttons writes. The page itself needs a serial port and is tried
   by hand; everything it decides is here. */

const assert = require('assert');
const path = require('path');
const zlib = require('zlib');

const FLASHER = path.join(__dirname, '..', 'flasher');
const fl = require(path.join(FLASHER, 'flasher_core.js'));
const hw = require(path.join(FLASHER, 'hardware_core.js'));

const MANIFEST = {
  version: 'v1.4.0',
  displays: ['st7796s_480_320', 'ili9488_480_320'],
  offsets: {bootloader: 0x0, partition_table: 0x8000, ota_data: 0xf000, board: 0x12000,
            app: 0x20000, littlefs: 0x620000},
};

function test_the_crc_is_zlibs() {
  const text = new TextEncoder().encode('123456789');
  assert.strictEqual(fl.crc32(text), 0xcbf43926);
  assert.strictEqual(fl.crc32(new Uint8Array(0)), 0);
  if (typeof zlib.crc32 === 'function') {
    const board = new TextEncoder().encode(hw.toCsv(hw.defaults()));
    assert.strictEqual(fl.crc32(board), zlib.crc32(board) >>> 0);
  }
}

/* The layout board_config_blob_read() checks: magic, version, length, CRC,
   all little-endian, then the text. */
function test_the_board_image_is_what_the_firmware_reads() {
  const csv = 'tft_cs,10\n';
  const blob = fl.boardBlob(csv);
  assert.strictEqual(blob.length, 16 + csv.length);
  assert.deepStrictEqual(Array.from(blob.slice(0, 4)), [0x4a, 0x52, 0x42, 0x44]);
  assert.deepStrictEqual(Array.from(blob.slice(4, 8)), [1, 0, 0, 0]);
  const view = new DataView(blob.buffer);
  assert.strictEqual(view.getUint32(8, true), csv.length);
  assert.strictEqual(view.getUint32(12, true), fl.crc32(new TextEncoder().encode(csv)));
  assert.strictEqual(new TextDecoder().decode(blob.slice(16)), csv);

  // The README board fits with room to spare; a text past 4 KB is refused.
  assert.ok(fl.boardBlob(hw.toCsv(hw.defaults())).length < fl.BLOB_MAX);
  assert.throws(() => fl.boardBlob('x'.repeat(fl.BLOB_MAX)));
}

function test_the_firmware_button_never_touches_the_data() {
  const blob = fl.boardBlob('tft_cs,10\n');
  const parts = fl.firmwareParts(MANIFEST, 'ili9488_480_320', blob);
  assert.deepStrictEqual(parts.map((part) => part.address),
                         [0x0, 0x8000, 0xf000, 0x12000, 0x20000]);
  assert.strictEqual(parts[3].data, blob);
  assert.strictEqual(parts[4].path, 'firmware/ili9488_480_320/jradio.bin');
  assert.ok(!parts.some((part) => part.address === MANIFEST.offsets.littlefs));
  // The build's own board.bin is the README board; the page writes its own.
  assert.ok(!parts.some((part) => /board\.bin$/.test(part.path || '')));
  // A display the site has no build for is refused, not flashed with another.
  assert.strictEqual(fl.buildFor(MANIFEST, 'st7789_320_170'), null);
  assert.throws(() => fl.firmwareParts(MANIFEST, 'st7789_320_170', blob));
}

function test_the_littlefs_button_writes_the_data_alone() {
  const parts = fl.littlefsParts(MANIFEST);
  assert.deepStrictEqual(parts, [{path: 'firmware/littlefs.bin', address: 0x620000}]);
}

function test_the_draft_is_the_editors() {
  // One key, read by both pages: the editor writes it, the flasher reads it.
  const fs = require('fs');
  const editor = fs.readFileSync(path.join(FLASHER, 'hardware.js'), 'utf8');
  assert.ok(editor.includes(`'${fl.DRAFT_KEY}'`));
}

/* Every key the page's script asks for is in the dictionary, both languages:
   the script builds its messages at run time, so the markup check in
   test_web_i18n.js never sees them. */
function test_every_message_is_in_the_dictionary() {
  const fs = require('fs');
  const vm = require('vm');
  const context = {window: {}, document: {documentElement: {}, addEventListener() {}},
                   navigator: {language: 'ru'}};
  context.window.localStorage = {getItem: () => null, setItem() {}};
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'data', 'www', 'i18n.js'), 'utf8'), context);
  const i18n = context.window.jradioI18n;
  const script = fs.readFileSync(path.join(FLASHER, 'flasher.js'), 'utf8');
  const keys = new Set();
  for (const match of script.matchAll(/\bt\('([a-z_]+\.[a-z_.]+)'/g)) keys.add(match[1]);
  assert.ok(keys.size > 15, `found ${keys.size} keys`);
  for (const key of keys) {
    assert.notStrictEqual(i18n.t(key), key, `no Russian for ${key}`);
    i18n.setLanguage('en');
    assert.notStrictEqual(i18n.t(key), key, `no English for ${key}`);
    i18n.setLanguage('ru');
  }
}

/* A file goes over in pieces, each at its own offset, together covering it
   exactly - see pieces() for why. */
function test_a_file_is_written_in_pieces() {
  const data = new Uint8Array(fl.PIECE * 2 + 100).map((_, i) => i & 0xff);
  const parts = fl.pieces({data, address: 0x620000});
  assert.deepStrictEqual(parts.map((p) => p.address),
                         [0x620000, 0x620000 + fl.PIECE, 0x620000 + 2 * fl.PIECE]);
  assert.deepStrictEqual(parts.map((p) => p.data.length), [fl.PIECE, fl.PIECE, 100]);
  assert.strictEqual(parts[2].data[99], data[data.length - 1]);
  // A small file is one piece, as it was.
  const small = fl.pieces({data: new Uint8Array(676), address: 0x12000});
  assert.strictEqual(small.length, 1);
  assert.strictEqual(small[0].address, 0x12000);
}

test_every_message_is_in_the_dictionary();
test_a_file_is_written_in_pieces();
/* Firefox has a navigator.serial of its own now, and a board flashed through
   it came out broken; it is turned away like a browser with no port. */
function test_only_chrome_and_edge_flash() {
  const chrome = {serial: {}, userAgent: 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 ' +
    '(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36'};
  const edge = {serial: {}, userAgent: chrome.userAgent + ' Edg/141.0.0.0'};
  const firefox = {serial: {}, userAgent: 'Mozilla/5.0 (X11; Linux x86_64; rv:144.0) ' +
    'Gecko/20100101 Firefox/144.0'};
  const safari = {userAgent: 'Mozilla/5.0 (Macintosh) AppleWebKit/605.1.15 Version/18.0 Safari/605.1.15'};
  assert.strictEqual(fl.serialSupported(chrome), true);
  assert.strictEqual(fl.serialSupported(edge), true);
  assert.strictEqual(fl.serialSupported(firefox), false);
  assert.strictEqual(fl.serialSupported(safari), false);
  assert.strictEqual(fl.serialSupported(undefined), false);
}

/* A table as esptool writes it: 32-byte entries, the MD5 entry after them. */
function table(rows) {
  const bytes = new Uint8Array(0xC00).fill(0xFF);
  rows.forEach(([type, subtype, offset, size, label], index) => {
    const at = index * 32;
    bytes[at] = 0xAA; bytes[at + 1] = 0x50; bytes[at + 2] = type; bytes[at + 3] = subtype;
    for (let byte = 0; byte < 4; ++byte) {
      bytes[at + 4 + byte] = (offset >>> (8 * byte)) & 0xFF;
      bytes[at + 8 + byte] = (size >>> (8 * byte)) & 0xFF;
    }
    for (let char = 0; char < 16; ++char) bytes[at + 12 + char] = char < label.length ? label.charCodeAt(char) : 0;
    for (let flag = 28; flag < 32; ++flag) bytes[at + flag] = 0;
  });
  const md5 = rows.length * 32;
  bytes[md5] = 0xEB; bytes[md5 + 1] = 0xEB;
  return bytes;
}

/* Before v1.6.1 the data partition sat at 0x620000; since, at 0x820000. A
   board on the old table loses what is on it when the firmware is written,
   and the page has to know before it writes. */
function test_the_page_knows_when_the_data_would_be_lost() {
  const before = table([[1, 2, 0x9000, 0x6000, 'nvs'], [0, 0, 0x20000, 0x300000, 'factory'],
                        [0, 0x10, 0x320000, 0x300000, 'ota_0'], [1, 0x83, 0x620000, 0x9e0000, 'littlefs']]);
  const since = table([[1, 2, 0x9000, 0x6000, 'nvs'], [0, 0x10, 0x20000, 0x400000, 'ota_0'],
                       [0, 0x11, 0x420000, 0x400000, 'ota_1'], [1, 0x83, 0x820000, 0x7e0000, 'littlefs']]);
  const offsets = {littlefs: 0x820000};
  const entries = fl.partitionTable(before);
  assert.strictEqual(entries.length, 4);
  assert.deepStrictEqual(entries[3], {type: 1, subtype: 0x83, offset: 0x620000, size: 0x9e0000,
                                      label: 'littlefs'});
  assert.strictEqual(fl.layoutChange(entries, offsets), 'moved');
  assert.strictEqual(fl.layoutChange(fl.partitionTable(since), offsets), 'same');
  // A new or erased board has no table: nothing on it to lose.
  assert.strictEqual(fl.layoutChange(fl.partitionTable(new Uint8Array(0xC00).fill(0xFF)), offsets),
                     'fresh');
}

test_the_crc_is_zlibs();
test_the_board_image_is_what_the_firmware_reads();
test_the_firmware_button_never_touches_the_data();
test_the_littlefs_button_writes_the_data_alone();
test_the_draft_is_the_editors();
test_only_chrome_and_edge_flash();
test_the_page_knows_when_the_data_would_be_lost();
console.log('web flasher tests passed');
