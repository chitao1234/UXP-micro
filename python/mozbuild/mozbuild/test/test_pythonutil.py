# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from mozbuild.pythonutil import iter_modules_in_path
from mozunit import main
import os
import builtins
import importlib.util
import sys
from types import ModuleType
import unittest


class TestIterModules(unittest.TestCase):
    def test_iter_modules_in_path(self):
        mozbuild_path = os.path.normcase(os.path.dirname(os.path.dirname(__file__)))
        paths = list(iter_modules_in_path(mozbuild_path))
        self.assertEqual(sorted(paths), [
            os.path.join(os.path.abspath(mozbuild_path), '__init__.py'),
            os.path.join(os.path.abspath(mozbuild_path), 'pythonutil.py'),
            os.path.join(os.path.abspath(mozbuild_path), 'test', 'test_pythonutil.py'),
        ])

    def test_namespace_package_has_no_source_file(self):
        name = '_mozbuild_namespace_package_test'
        namespace = ModuleType(name)
        namespace.__file__ = None
        sys.modules[name] = namespace
        try:
            paths = list(iter_modules_in_path(os.path.dirname(__file__)))
            self.assertTrue(all(isinstance(path, str) for path in paths))
        finally:
            del sys.modules[name]

    def test_import_hook_accepts_namespace_and_builtin_modules(self):
        source = os.path.abspath(os.path.join(
            os.path.dirname(__file__), '..', '..', '..', '..',
            'build', 'mach_bootstrap.py'))
        spec = importlib.util.spec_from_file_location('_test_mach_bootstrap', source)
        bootstrap = importlib.util.module_from_spec(spec)
        original_import = builtins.__import__
        try:
            spec.loader.exec_module(bootstrap)
        finally:
            builtins.__import__ = original_import
        for name, has_file in [('namespace_package', True), ('builtin_module', False)]:
            module = ModuleType(name)
            if has_file:
                module.__file__ = None
            hook = bootstrap.ImportHook(lambda *args: module)
            self.assertIs(hook(name), module)


if __name__ == '__main__':
    main()
