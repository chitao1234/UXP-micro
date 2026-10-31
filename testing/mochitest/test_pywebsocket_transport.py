# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Exercise the real RFC 6455 wire path without browser or listening sockets.

Vectors follow RFC 6455 and Firefox 78's pywebsocket3 tests for handshake,
stream framing, masking and util.  The mock connection deliberately accepts
only bytes, like Python 3 socket files; reads may be shorter than requested.
"""

import os
import struct
import sys
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'pywebsocket'))

from mod_pywebsocket import common, stream, util
from mod_pywebsocket.handshake import hybi
from mod_pywebsocket.handshake._base import HandshakeException, VersionException


class ByteConnection(object):
    remote_addr = ('127.0.0.1', 50000)

    def __init__(self, incoming=b'', read_limit=None):
        self.incoming = incoming
        self.read_limit = read_limit
        self.written = []

    def read(self, size):
        if self.read_limit is not None:
            size = min(size, self.read_limit)
        result = self.incoming[:size]
        self.incoming = self.incoming[size:]
        return result

    def write(self, data):
        if not isinstance(data, bytes):
            raise TypeError('The network connection requires bytes')
        self.written.append(data)


class Request(object):
    method = 'GET'
    protocol = 'HTTP/1.1'
    uri = '/demo'

    def __init__(self, incoming=b'', read_limit=None):
        self.connection = ByteConnection(incoming, read_limit)
        self.headers_in = {
            'Host': 'server.example.com',
            'Upgrade': 'websocket',
            'Connection': 'Upgrade',
            'Sec-WebSocket-Key': 'dGhlIHNhbXBsZSBub25jZQ==',
            'Sec-WebSocket-Version': '13',
            'Origin': 'http://example.com',
        }
        self.ws_version = 13


class Dispatcher(object):
    def do_extra_handshake(self, request):
        pass


def masked_frame(payload, opcode=1, fin=True, compressed=False):
    """Independent fixed-key client encoder; never use the production builder."""
    key = bytearray(b'\x37\xfa\x21\x3d')
    payload = bytearray(payload)
    first = (0x80 if fin else 0) | (0x40 if compressed else 0) | opcode
    length = len(payload)
    if length < 126:
        header = struct.pack('!BB', first, 0x80 | length)
    elif length < 65536:
        header = struct.pack('!BBH', first, 0xfe, length)
    else:
        header = struct.pack('!BBQ', first, 0xff, length)
    masked = bytearray(value ^ key[index % 4]
                       for index, value in enumerate(payload))
    return header + bytes(key) + bytes(masked)


def connected_stream(incoming=b'', read_limit=2):
    request = Request(incoming, read_limit)
    return request, stream.Stream(request, stream.StreamOptions())


class WebSocketTransportTests(unittest.TestCase):
    def test_hexify_binary(self):
        self.assertEqual('61 7a 41 5a 30 39 20 09 0d 0a 00 ff',
                         util.hexify(b'azAZ09 \t\r\n\x00\xff'))

    def test_mask_binary_and_preserve_position(self):
        masker = util.RepeatedXorMasker(b'\x00\x7f\xff\x20')
        self.assertEqual(b'\x00\x7f\xff\x20\x00', masker.mask(b'\x00' * 5))
        self.assertEqual(b'\x7f\xff\x20\x00\x7f', masker.mask(b'\x00' * 5))
        self.assertRaises(TypeError, masker._mask_using_array, u'text')

    def test_rfc6455_handshake(self):
        request = Request()
        hybi.Handshaker(request, Dispatcher()).do_handshake()
        self.assertEqual(
            b'HTTP/1.1 101 Switching Protocols\r\n'
            b'Upgrade: websocket\r\n'
            b'Connection: Upgrade\r\n'
            b'Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n',
            b''.join(request.connection.written))
        self.assertEqual('http://example.com', request.ws_origin)
        self.assertIsInstance(request.ws_stream, stream.Stream)

    def test_invalid_nonce_is_rejected_without_response(self):
        for key in ('', 'not-base64', 'dGhlIHNhbXBsZSBub25jZQ==,other'):
            request = Request()
            request.headers_in['Sec-WebSocket-Key'] = key
            self.assertRaises(HandshakeException,
                              hybi.Handshaker(request, Dispatcher()).do_handshake)
            self.assertEqual([], request.connection.written)

    def test_invalid_version_is_rejected(self):
        request = Request()
        request.headers_in['Sec-WebSocket-Version'] = '12'
        self.assertRaises(VersionException,
                          hybi.Handshaker(request, Dispatcher()).do_handshake)
        self.assertEqual([], request.connection.written)

    def test_rfc_masked_hello_with_short_reads(self):
        # RFC 6455 section 5.7: masked text "Hello".
        wire = b'\x81\x85\x37\xfa\x21\x3d\x7f\x9f\x4d\x51\x58'
        request, connection = connected_stream(wire, 1)
        self.assertEqual(u'Hello', connection.receive_message())
        self.assertEqual(b'', request.connection.incoming)

    def test_header_length_encodings(self):
        self.assertEqual(b'\x00', stream.create_length_header(0, False))
        self.assertEqual(b'\xfd', stream.create_length_header(125, True))
        self.assertEqual(b'\x7e\x00\x7e', stream.create_length_header(126, False))
        self.assertEqual(b'\xfe\xff\xff', stream.create_length_header(65535, True))
        self.assertEqual(b'\x7f\x00\x00\x00\x00\x00\x01\x00\x00',
                         stream.create_length_header(65536, False))
        self.assertRaises(ValueError, stream.create_length_header, -1, False)
        self.assertRaises(ValueError, stream.create_length_header, 1 << 63, False)

    def test_extended_binary_lengths_with_short_reads(self):
        for length in (0, 126, 65536):
            payload = b'\xff' * length
            request, connection = connected_stream(masked_frame(payload, opcode=2), 97)
            self.assertEqual(payload, connection.receive_message())
            self.assertEqual(b'', request.connection.incoming)

    def test_send_text_binary_and_reject_unicode_binary(self):
        request, connection = connected_stream()
        connection.send_message(u'\u65e5')
        connection.send_message(b'\x00\xff', binary=True)
        self.assertEqual([b'\x81\x03\xe6\x97\xa5', b'\x82\x02\x00\xff'],
                         request.connection.written)
        self.assertRaises(stream.BadOperationException,
                          connection.send_message, u'\u65e5', binary=True)

    def test_receive_fragmented_utf8(self):
        wire = masked_frame(b'\xe6', fin=False)
        wire += masked_frame(b'\x97\xa5', opcode=0)
        request, connection = connected_stream(wire, 1)
        self.assertEqual(u'\u65e5', connection.receive_message())
        self.assertEqual(b'', request.connection.incoming)

    def test_invalid_utf8_and_unmasked_client_are_rejected(self):
        request, connection = connected_stream(masked_frame(b'\xff'))
        self.assertRaises(stream.InvalidUTF8Exception, connection.receive_message)
        request, connection = connected_stream(b'\x81\x02hi')
        self.assertRaises(stream.InvalidFrameException, connection.receive_message)

    def test_ping_and_client_close_acknowledgement(self):
        request, connection = connected_stream(masked_frame(b'\x03\xe8', opcode=8))
        connection.send_ping()
        connection.send_ping(u'\u65e5')
        self.assertEqual(None, connection.receive_message())
        self.assertEqual([b'\x89\x00', b'\x89\x03\xe6\x97\xa5', b'\x88\x02\x03\xe8'],
                         request.connection.written)
        self.assertEqual(1000, request.ws_close_code)
        self.assertTrue(request.client_terminated)
        self.assertTrue(request.server_terminated)

    def test_empty_close_frame_is_bytes(self):
        self.assertEqual(b'', stream.create_closing_handshake_body(None, u''))
        self.assertEqual(b'\x88\x00', stream.create_close_frame(b''))

    def test_unicode_close_reason(self):
        request, connection = connected_stream()
        connection.close_connection(reason=u'\u65e5', wait_response=False)
        self.assertEqual([b'\x88\x05\x03\xe8\xe6\x97\xa5'],
                         request.connection.written)

    def test_bytes_close_reason_and_invalid_utf8(self):
        self.assertEqual(b'\x03\xe8\xe6\x97\xa5',
                         stream.create_closing_handshake_body(1000, b'\xe6\x97\xa5'))
        self.assertRaises(stream.BadOperationException,
                          stream.create_closing_handshake_body, 1000, b'\xff')

    def test_rfc1979_compression_and_inflation(self):
        for no_context_takeover in (False, True):
            compressor = util._RFC1979Deflater(15, no_context_takeover)
            decompressor = util._RFC1979Inflater()
            for payload in (b'Hello', b'Hello Hello', b'\x00\xff' * 100):
                encoded = compressor.filter(payload)
                self.assertIsInstance(encoded, bytes)
                self.assertEqual(payload, decompressor.filter(encoded))
        # Canonical raw-DEFLATE encoding of "Hello" with the sync-flush tail removed.
        self.assertEqual(b'Hello', util._RFC1979Inflater().filter(b'\xf2H\xcd\xc9\xc9\x07\x00'))

    def test_final_deflate_block(self):
        encoded = util._RFC1979Deflater(15, False).filter(b'Hello', bfinal=True)
        inflater = zlib.decompressobj(-15)
        self.assertEqual(b'Hello', inflater.decompress(encoded))

    def test_negotiated_permessage_deflate(self):
        compressed_hello = b'\xf2H\xcd\xc9\xc9\x07\x00'
        request = Request(masked_frame(compressed_hello, compressed=True), 1)
        request.headers_in['Sec-WebSocket-Extensions'] = 'permessage-deflate; client_max_window_bits'
        hybi.Handshaker(request, Dispatcher()).do_handshake()
        self.assertIn(b'Sec-WebSocket-Extensions: permessage-deflate',
                      b''.join(request.connection.written))
        self.assertEqual(u'Hello', request.ws_stream.receive_message())
        request.connection.written = []
        request.ws_stream.send_message(u'Hello')
        wire = b''.join(request.connection.written)
        self.assertEqual(0xc1, bytearray(wire)[0])
        self.assertEqual(b'Hello', util._RFC1979Inflater().filter(wire[2:]))


if __name__ == '__main__':
    unittest.main()
