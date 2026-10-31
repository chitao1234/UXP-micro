# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import os
from pathlib import Path
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

import mozbuild.virtualenv as virtualenv
from mozunit import main


class TestVirtualenv(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.environment = self.root / 'venv'
        self.environment.mkdir()
        self.manifest = self.root / 'packages.txt'
        self.manifest.write_text('', encoding='utf-8')
        self.manager = virtualenv.VirtualenvManager(
            str(self.root), str(self.root), str(self.environment),
            io.StringIO(), str(self.manifest))
        self.python = Path(self.manager.python_path)
        self.python.parent.mkdir(parents=True)
        self.python.write_bytes(b'controlled test launcher')
        self.marker = self.environment / 'env_built.stamp'
        self.marker.touch()
        self.old_time = time.time() - 3600
        os.utime(str(self.marker), (self.old_time, self.old_time))

    def test_successful_population_refreshes_existing_stamp(self):
        with mock.patch.object(self.manager, 'create'), \
             mock.patch.object(self.manager, '_log_process_output', return_value=0):
            self.manager.build(str(self.python))
        self.assertGreater(self.marker.stat().st_mtime, self.old_time + 600)
        self.assertTrue(self.manager.up_to_date(str(self.python)))

    def test_failed_population_does_not_refresh_stamp(self):
        before = self.marker.stat().st_mtime
        with mock.patch.object(self.manager, 'create'), \
             mock.patch.object(self.manager, '_log_process_output', return_value=1):
            with self.assertRaisesRegex(Exception, 'Error populating'):
                self.manager.build(str(self.python))
        self.assertEqual(self.marker.stat().st_mtime, before)

    @unittest.skipUnless(os.name == 'nt', 'Windows paths are case-insensitive')
    def test_active_interpreter_case_difference_is_current(self):
        future = time.time() + 10
        os.utime(str(self.marker), (future, future))
        with mock.patch.object(self.manager, 'get_exe_info', side_effect=
                               AssertionError('Active venv is its own interpreter')):
            self.assertTrue(self.manager.up_to_date(str(self.python).swapcase()))

    @unittest.skipUnless(os.name == 'nt', 'Windows paths are case-insensitive')
    def test_case_variant_active_venv_is_never_recreated(self):
        active = str(self.python).swapcase()
        with mock.patch.object(virtualenv, 'sys', SimpleNamespace(executable=active)), \
             mock.patch('venv.EnvBuilder') as builder:
            with self.assertRaisesRegex(RuntimeError, 'while it is active'):
                self.manager.create()
            builder.assert_not_called()

    def test_sibling_with_same_prefix_is_not_the_active_venv(self):
        active = str(self.environment) + '-other' + os.sep + 'python.exe'
        with mock.patch.object(virtualenv, 'sys', SimpleNamespace(executable=active)), \
             mock.patch('venv.EnvBuilder') as builder, \
             mock.patch.object(self.manager, 'write_exe_info'):
            self.manager.create()
        builder.return_value.create.assert_called_once_with(str(self.environment))


if __name__ == '__main__':
    main()
