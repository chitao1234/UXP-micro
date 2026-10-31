# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Check the retained cross-architecture dump helper protocol without processes."""

import importlib
import os
import subprocess
import tempfile
import unittest
from unittest import mock

import mozinfo

crash = importlib.import_module('mozcrash.mozcrash')


@unittest.skipUnless(mozinfo.isWin, 'The helper protocol is Windows-only')
class TestMinidumpWriter(unittest.TestCase):
    def setUp(self):
        self.dump_dir = os.path.join(tempfile.gettempdir(), u'dump output \u6d4b\u8bd5')
        self.utility_dir = os.path.join(tempfile.gettempdir(), u'utility path \u6d4b\u8bd5')
        self.bits = 32 if crash.ctypes.sizeof(crash.ctypes.c_voidp) == 8 else 64

    def test_cross_arch_helper_preserves_unicode_argument_vector(self):
        with mock.patch.dict(crash.mozinfo.info, {'bits': self.bits}), \
             mock.patch.object(crash.uuid, 'uuid4', return_value='unit-dump'), \
             mock.patch.object(crash.os.path, 'exists', return_value=True), \
             mock.patch.object(crash, 'get_logger') as logger, \
             mock.patch.object(crash.subprocess, 'Popen') as popen:
            popen.return_value.wait.return_value = 0
            crash.write_minidump(1234, self.dump_dir, self.utility_dir)
            expected = [os.path.normpath(os.path.join(self.utility_dir,
                                                     'minidumpwriter.exe')),
                        '1234', os.path.join(self.dump_dir, 'unit-dump.dmp')]
            popen.assert_called_once_with(expected)
            self.assertTrue(all(isinstance(arg, str) for arg in expected))
            # Python 3.7's real Windows quoting rejects a bytes argument.
            command_line = subprocess.list2cmdline(popen.call_args[0][0])
            self.assertIn(u'\u6d4b\u8bd5', command_line)
            popen.return_value.wait.assert_called_once_with()
            logger.return_value.error.assert_not_called()

    def test_missing_helper_is_reported(self):
        with mock.patch.dict(crash.mozinfo.info, {'bits': self.bits}), \
             mock.patch.object(crash.os.path, 'exists', return_value=False), \
             mock.patch.object(crash, 'get_logger') as logger, \
             mock.patch.object(crash.subprocess, 'Popen') as popen:
            crash.write_minidump(1234, self.dump_dir, self.utility_dir)
            popen.assert_not_called()
            logger.return_value.error.assert_called_once_with(
                'minidumpwriter not found in %s' % self.utility_dir)

    def test_helper_failure_is_reported(self):
        with mock.patch.dict(crash.mozinfo.info, {'bits': self.bits}), \
             mock.patch.object(crash.os.path, 'exists', return_value=True), \
             mock.patch.object(crash, 'get_logger') as logger, \
             mock.patch.object(crash.subprocess, 'Popen') as popen:
            popen.return_value.wait.return_value = 7
            crash.write_minidump(1234, self.dump_dir, self.utility_dir)
            logger.return_value.error.assert_called_once_with(
                'minidumpwriter exited with status: 7')


if __name__ == '__main__':
    unittest.main()
