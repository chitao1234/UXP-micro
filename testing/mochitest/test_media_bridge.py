# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest

from media_bridge import needs_websocket_process_bridge


class MediaBridgeTests(unittest.TestCase):
    def test_playback_and_webaudio_need_no_process_bridge(self):
        self.assertFalse(needs_websocket_process_bridge([
            dict(path='test_can_play_type_wave.html'),
            dict(path='test_delayNode.html', tags='msg'),
        ], 'media'))

    def test_mixed_selection_retains_webrtc_process_bridge(self):
        self.assertTrue(needs_websocket_process_bridge([
            dict(path='test_can_play_type_wave.html'),
            dict(path='test_peerConnection_basicAudio.html', tags='msg webrtc'),
        ], 'media'))

    def test_tag_is_a_token_not_a_substring(self):
        self.assertFalse(needs_websocket_process_bridge([dict(tags='notwebrtc')], 'media'))
        self.assertTrue(needs_websocket_process_bridge([dict(tags='msg\nwebrtc')], 'media'))

    def test_empty_and_disabled_selections_do_not_launch_bridge(self):
        self.assertFalse(needs_websocket_process_bridge([], 'media'))
        self.assertFalse(needs_websocket_process_bridge([
            dict(tags='webrtc', disabled='unsupported platform'),
        ], 'media'))

    def test_other_subsuites_do_not_launch_bridge(self):
        for subsuite in (None, '', 'default', 'gpu'):
            self.assertFalse(needs_websocket_process_bridge([dict(tags='webrtc')], subsuite))


if __name__ == '__main__':
    unittest.main()
