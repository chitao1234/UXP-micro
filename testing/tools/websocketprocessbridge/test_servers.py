# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Protocol regressions; run after installing the adjacent requirements file.

These tests use the real server classes with byte-only recording transports.
The matching media mochitests provide browser/process/socket integration.
"""

import importlib.util
import os
import struct
import tempfile
import unittest
from unittest import mock

from OpenSSL import crypto
from twisted.internet.address import IPv4Address
from twisted.internet.error import ProcessDone, ProcessExitedAlready
from twisted.python.failure import Failure

import websocketprocessbridge as bridge


def load_ice_server():
    source = os.path.join(os.path.dirname(__file__), '..', 'iceserver', 'iceserver.py')
    spec = importlib.util.spec_from_file_location('test_ice_server', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ice = load_ice_server()


class BridgeTests(unittest.TestCase):
    def test_fragmented_process_output_is_one_text_message_per_line(self):
        socket = mock.Mock()
        process = bridge.ProcessSide(socket)
        process.outReceived(b'[{"urls": ["stun:')
        socket.sendMessage.assert_not_called()
        process.outReceived(b'localhost"]}]\r\nsecond\n')
        self.assertEqual(socket.sendMessage.call_args_list, [
            mock.call(b'[{"urls": ["stun:localhost"]}]', isBinary=False),
            mock.call(b'second', isBinary=False),
        ])

    def test_unterminated_output_is_flushed_once_at_process_end(self):
        socket = mock.Mock()
        process = bridge.ProcessSide(socket)
        process.outReceived(b'last line')
        process.processEnded(Failure(ProcessDone(0)))
        self.assertEqual(socket.sendMessage.call_args_list[0],
                         mock.call(b'last line', isBinary=False))
        self.assertEqual(process.pending, b'')
        socket.processGone.assert_called_once_with()

    def test_connection_close_terminates_process(self):
        process = bridge.ProcessSide(mock.Mock())
        process.transport = mock.Mock()
        process.socketGone()
        process.transport.signalProcess.assert_called_once_with('KILL')
        self.assertIsNone(process.socketSide)

    def test_connection_close_accepts_already_exited_process(self):
        process = bridge.ProcessSide(mock.Mock())
        process.transport = mock.Mock()
        process.transport.signalProcess.side_effect = ProcessExitedAlready()
        process.socketGone()

    def test_text_command_starts_one_child_with_current_environment(self):
        socket = bridge.SocketSide()
        with mock.patch.object(bridge.reactor, 'spawnProcess') as spawn:
            socket.onMessage(b'iceserver', False)
            socket.onMessage(b'iceserver', False)
        spawn.assert_called_once_with(
            socket.processSide, bridge.commands['iceserver'][0],
            bridge.commands['iceserver'], env=os.environ)

    def test_invalid_commands_do_not_spawn(self):
        for payload, binary in [(b'unknown', False), (b'iceserver', True), (b'\xff', False)]:
            socket = bridge.SocketSide()
            socket.sendClose = mock.Mock()
            with mock.patch.object(bridge.reactor, 'spawnProcess') as spawn:
                socket.onMessage(payload, binary)
                spawn.assert_not_called()
            socket.sendClose.assert_called_once()


class ByteTransport(object):
    def __init__(self):
        self.messages = []

    def write(self, data, *address):
        if not isinstance(data, bytes):
            raise TypeError('Network writes must be bytes')
        self.messages.append((data, address))


class IceServerTests(unittest.TestCase):
    # Independent STUN binding wire request, not built by the code under test.
    request = struct.pack('!HHI12s', 1, 0, 0x2112a442, b'0123456789ab')

    def test_binding_request_parses_and_returns_binary_udp_response(self):
        server = ice.UdpStunHandler()
        server.transport = ByteTransport()
        server.datagramReceived(self.request, ('127.0.0.1', 12345))
        self.assertEqual(len(server.transport.messages), 1)
        response, address = server.transport.messages[0]
        self.assertEqual(response[:2], b'\x01\x01')
        self.assertEqual(response[4:20], self.request[4:20])
        self.assertEqual(address, (('127.0.0.1', 12345),))

    def test_tcp_reassembles_fragments_and_coalesced_messages(self):
        server = ice.TcpStunHandler(IPv4Address('TCP', '127.0.0.1', 12345))
        server.transport = ByteTransport()
        server.dataReceived(self.request[:7])
        self.assertEqual(server.transport.messages, [])
        server.dataReceived(self.request[7:] + self.request)
        self.assertEqual(len(server.transport.messages), 2)
        self.assertEqual(server.transport.messages[0][0], server.transport.messages[1][0])

    def test_turn_authentication_challenge_and_utf8_credentials(self):
        server = ice.StunHandler(mock.Mock())
        server.client_address = IPv4Address('UDP', '127.0.0.1', 12345)
        request = ice.StunMessage()
        request.method = ice.ALLOCATE
        challenge = server.check_long_term_auth(request)
        self.assertEqual(challenge.msg_class, ice.ERROR_RESPONSE)
        self.assertEqual(bytes(challenge.find(ice.REALM).data), b'mozilla.invalid')
        request.attributes.append(ice.StunAttribute(ice.USERNAME, bytearray(b'foo')))
        request.add_nonce('nonce')
        request.add_realm(ice.turn_realm)
        request.add_message_integrity(ice.turn_user, ice.turn_realm, ice.turn_pass)
        self.assertEqual(request.get_username(), 'foo')
        self.assertEqual(server.check_long_term_auth(request).msg_class, ice.SUCCESS_RESPONSE)
        request.find(ice.MESSAGE_INTEGRITY).data[0] ^= 1
        self.assertEqual(server.check_long_term_auth(request).msg_class, ice.ERROR_RESPONSE)

    def test_certificate_files_are_pem_bytes_and_loadable(self):
        with tempfile.TemporaryDirectory() as temporary:
            cert_file = os.path.join(temporary, 'cert.pem')
            key_file = os.path.join(temporary, 'key.pem')
            with mock.patch.object(ice, 'CERT_FILE', cert_file), \
                    mock.patch.object(ice, 'KEY_FILE', key_file):
                ice.create_self_signed_cert('localhost')
                with open(cert_file, 'rb') as stream:
                    cert = crypto.load_certificate(crypto.FILETYPE_PEM, stream.read())
                with open(key_file, 'rb') as stream:
                    key = crypto.load_privatekey(crypto.FILETYPE_PEM, stream.read())
                self.assertEqual(key.bits(), 2048)
                self.assertEqual(cert.get_signature_algorithm(), b'sha256WithRSAEncryption')


if __name__ == '__main__':
    unittest.main()
