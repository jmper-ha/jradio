/* The flasher page's model: which files go where for each of its two
   buttons, and the board partition's image, built here from the wiring the
   way the firmware reads it back. No DOM and no serial port, so the whole of
   it runs under Node for tests/test_web_flasher.js.

   The partition image is the same bytes tools/board_bin.py writes for a
   build and board_config_blob_read() checks at boot: "JRBD", a version, the
   text's length and its CRC-32, little-endian, then board.csv. */
(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.jradioFlasher = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const BLOB_MAGIC = [0x4a, 0x52, 0x42, 0x44];  // "JRBD"
  const BLOB_VERSION = 1;
  const BLOB_HEADER = 16;
  const BLOB_MAX = 4096;

  let crcTable = null;

  /* IEEE CRC-32, zlib's - what board_config_crc32() computes. */
  function crc32(bytes) {
    if (crcTable === null) {
      crcTable = new Uint32Array(256);
      for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
        crcTable[n] = c >>> 0;
      }
    }
    let crc = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) crc = crcTable[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
    return (crc ^ 0xffffffff) >>> 0;
  }

  function putU32(out, at, value) {
    out[at] = value & 0xff;
    out[at + 1] = (value >>> 8) & 0xff;
    out[at + 2] = (value >>> 16) & 0xff;
    out[at + 3] = (value >>> 24) & 0xff;
  }

  /* board.csv behind its header, as the partition holds it. Throws when the
     text does not fit: a cut file would be refused by the firmware anyway,
     and saying so here says it before anything is written. */
  function boardBlob(csvText) {
    const text = new TextEncoder().encode(csvText);
    if (BLOB_HEADER + text.length > BLOB_MAX) throw new Error('board.csv does not fit in the board partition');
    const out = new Uint8Array(BLOB_HEADER + text.length);
    out.set(BLOB_MAGIC, 0);
    out[4] = BLOB_VERSION & 0xff;
    out[5] = (BLOB_VERSION >>> 8) & 0xff;
    putU32(out, 8, text.length);
    putU32(out, 12, crc32(text));
    out.set(text, BLOB_HEADER);
    return out;
  }

  /* Whether this browser is one to flash from. Having navigator.serial is not
     enough: Firefox has grown one, the page let it through, and a user's
     board came out of it written wrong - the splash blinked once and the
     radio never came up; the same files from Chrome worked. Until its port
     behaves like Chrome's and Edge's, Firefox gets the same notice as a
     browser with no port at all. */
  function serialSupported(nav) {
    if (!nav || !('serial' in nav)) return false;
    return !/Firefox\//.test(String(nav.userAgent || ''));
  }

  /* The build for a display, or null when the site has none for it. */
  function buildFor(manifest, display) {
    if (!manifest || !Array.isArray(manifest.displays)) return null;
    return manifest.displays.includes(display) ? display : null;
  }

  /* What the firmware button writes: everything but the data partition. The
     board partition gets the page's wiring, so the build's own board.bin is
     never among the files - it would be the README board, not this one. */
  function firmwareParts(manifest, display, wiringBlob) {
    const build = buildFor(manifest, display);
    if (build === null) throw new Error(`no firmware for the display ${display}`);
    const offsets = manifest.offsets;
    return [
      {path: 'firmware/bootloader.bin', address: offsets.bootloader},
      {path: 'firmware/partition-table.bin', address: offsets.partition_table},
      {path: 'firmware/ota-data.bin', address: offsets.ota_data},
      {data: wiringBlob, address: offsets.board},
      {path: `firmware/${build}/jradio.bin`, address: offsets.app},
    ];
  }

  /* What the LittleFS button writes: the data partition, and only it. */
  function littlefsParts(manifest) {
    return [{path: 'firmware/littlefs.bin', address: manifest.offsets.littlefs}];
  }

  /* A file cut into pieces written one after another, each at its own
     offset. The loader sends compressed data faster than the chip writes
     it, then waits for the chip to finish with a fixed 3 s timeout - the
     Python esptool scales that wait with the size, this loader does not. The
     10 MB file system, almost all of it empty, went over in 28 s while the
     chip was still writing, and the finish timed out on a write that had
     worked. Half a megabyte fixed it for our boards; a user's N16R8 with a
     Macronix flash (id c2 2018) still timed out on the file system's first
     piece - erasing 512 KB of empty pages takes it about 3.2 s. 64 KB is one
     erase block, at most 2 s on that chip by its datasheet, so the finish
     always fits; the cost is a few seconds more on the file system. */
  const PIECE = 64 * 1024;

  function pieces(file, size = PIECE) {
    const out = [];
    for (let at = 0; at < file.data.length; at += size) {
      out.push({data: file.data.subarray(at, Math.min(at + size, file.data.length)),
                address: file.address + at});
    }
    return out;
  }

  /* Where the wiring comes from: the editor's draft on this site when there
     is one, the README board otherwise. The draft is the editor's own
     localStorage entry, read as it is. */
  const DRAFT_KEY = 'jradio.board.csv';

  return {BLOB_HEADER, BLOB_MAX, DRAFT_KEY, PIECE, crc32, boardBlob, buildFor, firmwareParts,
    serialSupported,
          littlefsParts, pieces};
});
