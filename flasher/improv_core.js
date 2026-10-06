/* Improv Wi-Fi over serial, the page's half: building the packets the
   device's components/improv answers and reading what comes back.
   https://www.improv-wifi.com/serial/

   A packet is "IMPROV", the version, a type, a length, the data and a
   checksum - the low byte of the sum of everything before it. The device
   shares the line with its log, so the reader skips whatever is not a packet.
   No DOM and no serial port: tests/test_web_improv.js runs all of it. */
(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.jradioImprov = factory();
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const HEADER = [0x49, 0x4d, 0x50, 0x52, 0x4f, 0x56]; // "IMPROV"
  const VERSION = 1;
  const TYPE = {state: 0x01, error: 0x02, rpc: 0x03, result: 0x04};
  const STATE = {ready: 0x02, provisioning: 0x03, provisioned: 0x04};
  const ERROR = {none: 0x00, invalidRpc: 0x01, unknownRpc: 0x02, unableToConnect: 0x03,
                 unknown: 0xff};
  const COMMAND = {wifi: 0x01, state: 0x02, info: 0x03, networks: 0x04};
  // What Wi-Fi allows, and what the device's parser refuses past.
  const SSID_MAX = 32;
  const PASSWORD_MAX = 64;

  function frame(type, data) {
    const out = new Uint8Array(HEADER.length + 3 + data.length + 1);
    out.set(HEADER, 0);
    out[6] = VERSION;
    out[7] = type;
    out[8] = data.length;
    out.set(data, 9);
    let sum = 0;
    for (let index = 0; index < out.length - 1; index += 1) sum = (sum + out[index]) & 0xff;
    out[out.length - 1] = sum;
    return out;
  }

  function rpc(command, payload) {
    const body = payload || new Uint8Array(0);
    const data = new Uint8Array(2 + body.length);
    data[0] = command;
    data[1] = body.length;
    data.set(body, 2);
    return frame(TYPE.rpc, data);
  }

  /* The network as the device takes it: the name and the password, each in
     UTF-8 behind its length in bytes. Throws on what the device would refuse,
     so the page can say why before anything is sent. */
  function wifiRequest(ssid, password) {
    const encoder = new TextEncoder();
    const name = encoder.encode(String(ssid));
    const secret = encoder.encode(String(password || ''));
    if (name.length === 0 || name.length > SSID_MAX) throw new Error('ssid');
    if (secret.length > PASSWORD_MAX) throw new Error('password');
    const payload = new Uint8Array(2 + name.length + secret.length);
    payload[0] = name.length;
    payload.set(name, 1);
    payload[1 + name.length] = secret.length;
    payload.set(secret, 2 + name.length);
    const packet = rpc(COMMAND.wifi, payload);
    payload.fill(0);
    secret.fill(0);
    return packet;
  }

  /* A reader over whatever arrives, in pieces: feed it bytes, get back the
     packets they complete. Bytes before a header are the log and are
     dropped; a packet with a bad checksum is dropped too - the page asks
     again. */
  function createReader() {
    let buffer = new Uint8Array(0);
    return function read(chunk) {
      const joined = new Uint8Array(buffer.length + chunk.length);
      joined.set(buffer, 0);
      joined.set(chunk, buffer.length);
      const packets = [];
      let at = 0;
      while (true) {
        let start = -1;
        for (let index = at; index + HEADER.length <= joined.length; index += 1) {
          if (HEADER.every((value, offset) => joined[index + offset] === value)) {
            start = index;
            break;
          }
        }
        if (start < 0) {
          // Keep a tail that may be the start of a header cut in two.
          at = Math.max(at, joined.length - (HEADER.length - 1));
          break;
        }
        if (start + 9 > joined.length) { at = start; break; }
        const length = joined[start + 8];
        const end = start + 9 + length;
        if (end >= joined.length) { at = start; break; }
        let sum = 0;
        for (let index = start; index < end; index += 1) sum = (sum + joined[index]) & 0xff;
        if (sum === joined[end] && joined[start + 6] === VERSION) {
          packets.push({type: joined[start + 7], data: joined.slice(start + 9, end)});
        }
        at = end + 1;
      }
      buffer = joined.slice(at);
      return packets;
    };
  }

  /* An RPC result: the command it answers and its strings. */
  function resultStrings(data) {
    const decoder = new TextDecoder();
    const strings = [];
    const total = Math.min(data[1] || 0, data.length - 2);
    let at = 2;
    while (at < 2 + total) {
      const length = data[at];
      strings.push(decoder.decode(data.slice(at + 1, at + 1 + length)));
      at += 1 + length;
    }
    return {command: data[0], strings};
  }

  /* One network out of a list result - name, signal, whether it wants a
     password - or null for the empty result that ends the list. Sorted and
     deduplicated by the caller with networkList(). */
  function network(result) {
    if (result.strings.length < 3) return null;
    return {ssid: result.strings[0], rssi: Number.parseInt(result.strings[1], 10) || 0,
            secure: result.strings[2] === 'YES'};
  }

  // Strongest first, each name once - a mesh answers from every access point.
  function networkList(networks) {
    const best = new Map();
    for (const entry of networks) {
      if (!entry || entry.ssid === '') continue;
      const seen = best.get(entry.ssid);
      if (!seen || entry.rssi > seen.rssi) best.set(entry.ssid, entry);
    }
    return Array.from(best.values()).sort((a, b) => b.rssi - a.rssi);
  }

  return {TYPE, STATE, ERROR, COMMAND, SSID_MAX, PASSWORD_MAX, frame, rpc, wifiRequest,
          createReader, resultStrings, network, networkList};
}));
