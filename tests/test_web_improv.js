'use strict';

/* The flasher's Improv half against the packets the device builds - the same
   bytes components/improv/improv_proto.c is tested to produce. */

const assert = require('assert');
const path = require('path');

const improv = require(path.join(__dirname, '..', 'flasher', 'improv_core.js'));

function bytes(text) {
  return Uint8Array.from(Buffer.from(text, 'latin1'));
}

function test_a_request_is_a_whole_checksummed_frame() {
  const packet = improv.rpc(improv.COMMAND.info);
  assert.deepStrictEqual(Array.from(packet.slice(0, 11)),
                         [0x49, 0x4d, 0x50, 0x52, 0x4f, 0x56, 1, 0x03, 2, 0x03, 0]);
  const sum = packet.slice(0, -1).reduce((a, b) => (a + b) & 0xff, 0);
  assert.strictEqual(packet[packet.length - 1], sum);
}

function test_the_wifi_request_carries_both_in_utf8() {
  const packet = improv.wifiRequest('Дом', 'пароль1');
  const data = packet.slice(9, -1);
  assert.strictEqual(data[0], improv.COMMAND.wifi);
  assert.strictEqual(data[1], data.length - 2);
  const name = Buffer.from('Дом', 'utf8');
  const secret = Buffer.from('пароль1', 'utf8');
  assert.strictEqual(data[2], name.length);
  assert.deepStrictEqual(Buffer.from(data.slice(3, 3 + name.length)), name);
  assert.strictEqual(data[3 + name.length], secret.length);
  assert.deepStrictEqual(Buffer.from(data.slice(4 + name.length)), secret);
  // An open network has an empty password, not a missing one.
  const open = improv.wifiRequest('Cafe', '').slice(9, -1);
  assert.strictEqual(open[open.length - 1], 0);
  // What the device would refuse is refused here, before anything is sent.
  assert.throws(() => improv.wifiRequest('', 'x'), /ssid/);
  assert.throws(() => improv.wifiRequest('x'.repeat(33), 'x'), /ssid/);
  assert.throws(() => improv.wifiRequest('Home', 'x'.repeat(65)), /password/);
}

function test_the_reader_picks_packets_out_of_the_log_in_pieces() {
  const state = improv.frame(improv.TYPE.state, Uint8Array.of(improv.STATE.provisioned));
  const result = improv.frame(improv.TYPE.result,
                              Uint8Array.of(improv.COMMAND.state, 20, 19,
                                            ...bytes('http://192.168.1.82')));
  const log = bytes('I (1234) wifi: connected\nIMPRO');
  const stream = new Uint8Array([...log, ...state, 0x0a, ...result, 0x0a]);
  const read = improv.createReader();
  const packets = [];
  // Three bytes at a time: the header and the length arrive cut.
  for (let at = 0; at < stream.length; at += 3) {
    packets.push(...read(stream.slice(at, at + 3)));
  }
  assert.strictEqual(packets.length, 2);
  assert.strictEqual(packets[0].type, improv.TYPE.state);
  assert.strictEqual(packets[0].data[0], improv.STATE.provisioned);
  const answer = improv.resultStrings(packets[1].data);
  assert.strictEqual(answer.command, improv.COMMAND.state);
  assert.deepStrictEqual(answer.strings, ['http://192.168.1.82']);
}

function test_a_bad_checksum_is_dropped_and_the_next_gets_through() {
  const good = improv.frame(improv.TYPE.error, Uint8Array.of(improv.ERROR.unableToConnect));
  const bad = good.slice();
  bad[bad.length - 1] ^= 1;
  const read = improv.createReader();
  assert.strictEqual(read(bad).length, 0);
  const packets = read(good);
  assert.strictEqual(packets.length, 1);
  assert.strictEqual(packets[0].data[0], improv.ERROR.unableToConnect);
}

function test_networks_come_strongest_first_and_once() {
  const result = (ssid, rssi, secure) => improv.network(
    {command: improv.COMMAND.networks, strings: [ssid, String(rssi), secure ? 'YES' : 'NO']});
  const list = improv.networkList([result('Home', -60, true), result('Cafe', -80, false),
                                   result('Home', -45, true), null,
                                   improv.network({command: 4, strings: []})]);
  assert.deepStrictEqual(list.map((entry) => entry.ssid), ['Home', 'Cafe']);
  assert.strictEqual(list[0].rssi, -45);
  assert.strictEqual(list[1].secure, false);
}

test_a_request_is_a_whole_checksummed_frame();
test_the_wifi_request_carries_both_in_utf8();
test_the_reader_picks_packets_out_of_the_log_in_pieces();
test_a_bad_checksum_is_dropped_and_the_next_gets_through();
test_networks_come_strongest_first_and_once();
console.log('web improv tests passed');
