# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import unittest

from marionette_driver.transport import Command, Response, TcpTransport


class FragmentedSocket(object):
    def __init__(self, incoming=b''):
        self.incoming = incoming
        self.sent = b''

    def recv(self, size):
        size = min(size, 3)
        chunk, self.incoming = self.incoming[:size], self.incoming[size:]
        return chunk

    def send(self, data):
        chunk = data[:3]
        self.sent += chunk
        return len(chunk)


class TransportTests(unittest.TestCase):
    def transport(self, incoming=b''):
        transport = TcpTransport('localhost', 2828)
        transport.protocol = 3
        transport.sock = FragmentedSocket(incoming)
        # The fake socket has no OS resources to close.
        self.addCleanup(setattr, transport, 'sock', None)
        return transport

    def test_protocol_three_response_uses_decoded_type(self):
        response = self.transport()._unmarshal(b'[1, 7, null, {"value": 42}]')
        self.assertIsInstance(response, Response)
        self.assertEqual(response.id, 7)
        self.assertEqual(response.result, {'value': 42})

    def test_protocol_three_command(self):
        command = self.transport()._unmarshal(b'[0, 8, "executeScript", {}]')
        self.assertIsInstance(command, Command)
        self.assertEqual(command.name, 'executeScript')

    def test_fragmented_response(self):
        body = json.dumps([1, 9, None, {'value': '\u00e9\u4e2d'}]).encode('utf-8')
        transport = self.transport(str(len(body)).encode('ascii') + b':' + body)
        transport.expected_response = Command(9, 'executeScript', {})
        self.assertEqual(transport.receive().result, {'value': '\u00e9\u4e2d'})

    def test_partial_send_frames_bytes(self):
        command = Command(10, 'executeScript', {'script': '\u00e9\u4e2d'})
        transport = self.transport()
        transport.send(command)
        body = command.to_msg().encode('utf-8')
        self.assertEqual(transport.sock.sent, str(len(body)).encode('ascii') + b':' + body)


if __name__ == '__main__':
    unittest.main()
