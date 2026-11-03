# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


def needs_websocket_process_bridge(tests, subsuite):
    """Only selected WebRTC media tests use the process/ICE server bridge.

    Share this decision between Mach dependency setup and the packaged runner.
    Ordinary media playback and WebAudio still use the normal WebSocket server.
    """
    return subsuite == 'media' and any(
        'disabled' not in test and 'webrtc' in test.get('tags', '').split()
        for test in tests)
