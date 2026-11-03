# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import subprocess
import time
from types import SimpleNamespace
import unittest
import venv
import zipfile
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

    def test_requirements_are_source_relative_and_hash_checked_by_default(self):
        with mock.patch.object(self.manager, '_run_pip') as install:
            self.manager.install_pip_requirements('requirements.txt')
        install.assert_called_once_with([
            'install', '--requirement', str(self.root / 'requirements.txt'),
            '--require-hashes'])

    def test_absolute_requirements_and_explicit_legacy_hash_opt_out(self):
        requirements = str(self.root / 'other directory' / 'requirements.txt')
        with mock.patch.object(self.manager, '_run_pip') as install:
            self.manager.install_pip_requirements(requirements, require_hashes=False)
        install.assert_called_once_with(['install', '--requirement', requirements])

    def test_pip_failure_is_not_converted_to_success(self):
        failure = subprocess.CalledProcessError(1, 'pip install')
        with mock.patch.object(virtualenv.subprocess, 'check_call',
                               side_effect=[0, failure]) as call:
            with self.assertRaises(subprocess.CalledProcessError):
                self.manager.install_pip_package('a-nonexistent-test-package')
        self.assertEqual(call.call_count, 2)
        self.assertEqual(call.call_args[0][0][:3], [str(self.python), '-m', 'pip'])

    def test_bootstrap_failure_stops_before_install(self):
        failure = subprocess.CalledProcessError(1, 'ensurepip')
        with mock.patch.object(virtualenv.subprocess, 'check_call',
                               side_effect=[failure, failure]) as call:
            with self.assertRaises(subprocess.CalledProcessError):
                self.manager.install_pip_package('unused')
        self.assertEqual(call.call_count, 2)
        self.assertEqual(call.call_args[0][0], [str(self.python), '-m', 'ensurepip', '--upgrade'])


class TestVirtualenvPipInstallation(unittest.TestCase):
    def test_offline_hashed_wheel_bootstraps_pip_only_inside_target_venv(self):
        # Exercise the removed API against a real pip-less stdlib venv. No
        # network, global package install, or preinstalled-pip assumption.
        package = 'microgecko_venv_probe'
        self.assertIsNone(importlib.util.find_spec(package))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            environment = root / 'target venv'
            venv.EnvBuilder(with_pip=False).create(str(environment))
            manifest = root / 'packages.txt'
            manifest.write_text('', encoding='utf-8')
            manager = virtualenv.VirtualenvManager(
                str(root), str(root), str(environment), io.StringIO(), str(manifest))
            wheel = root / (package + '-1.0-py3-none-any.whl')
            info = package + '-1.0.dist-info/'
            files = {
                package + '.py': 'VALUE = 42\n',
                info + 'METADATA': 'Metadata-Version: 2.1\nName: microgecko-venv-probe\nVersion: 1.0\n',
                info + 'WHEEL': 'Wheel-Version: 1.0\nGenerator: test\nRoot-Is-Purelib: true\nTag: py3-none-any\n',
            }
            files[info + 'RECORD'] = ''.join(name + ',,\n' for name in list(files) + [info + 'RECORD'])
            with zipfile.ZipFile(str(wheel), 'w') as archive:
                for name, data in files.items():
                    archive.writestr(name, data)
            digest = hashlib.sha256(wheel.read_bytes()).hexdigest()
            (root / 'requirements.txt').write_text(
                wheel.as_uri() + ' --hash=sha256:' + digest + '\n', encoding='utf-8')
            probe = [manager.python_path, '-I', '-c',
                     'import json,sys,microgecko_venv_probe as p; '
                     'print(json.dumps([sys.prefix,p.__file__,p.VALUE]))']
            with mock.patch.dict(os.environ, {'PIP_NO_INDEX': '1', 'PIP_DISABLE_PIP_VERSION_CHECK': '1'}):
                manager.install_pip_requirements('requirements.txt')
                # Already-installed pip and requirements must also work.
                manager.install_pip_requirements('requirements.txt')
                output = subprocess.check_output(probe, universal_newlines=True)
            prefix, location, value = json.loads(output)
            self.assertEqual(os.path.normcase(prefix), os.path.normcase(str(environment)))
            self.assertTrue(os.path.normcase(location).startswith(os.path.normcase(str(environment)) + os.sep))
            self.assertEqual(value, 42)
        self.assertIsNone(importlib.util.find_spec(package))


if __name__ == '__main__':
    main()
