# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest
import ast
import json
import os
import tempfile
from pathlib import Path
from types import SimpleNamespace

from manifestprefs import manifest_test_groups


class SchemeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(__file__).with_name('runtests.py')
        tree = ast.parse(source.read_text(encoding='utf8'))
        owner = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'MochitestDesktop')
        wanted = ('groupTestsByScheme', 'buildTestURL', 'buildTestPath', 'runMochitests')
        module = ast.parse('')
        module.body = [n for n in owner.body if isinstance(n, ast.FunctionDef) and n.name in wanted]
        cls.namespace = {'os': os, 'json': json}
        exec(compile(module, str(source), 'exec'), cls.namespace)

    def method(self, name):
        return self.namespace[name]

    def test_groups_default_http_and_explicit_https_without_dropping_tests(self):
        tests = [dict(path='a'), dict(path='b', scheme='https'), dict(path='c', scheme='http')]
        groups = self.method('groupTestsByScheme')(None, tests)
        self.assertEqual(groups, {'http': [tests[0], tests[2]], 'https': [tests[1]]})
        with self.assertRaises(ValueError):
            self.method('groupTestsByScheme')(None, [dict(path='bad', scheme='file')])

    def test_url_uses_upstream_secure_host_and_preserves_default_host(self):
        options = SimpleNamespace(test_paths=[], flavor='plain', nested_oop=False)
        harness = SimpleNamespace(TEST_PATH='tests')
        self.assertEqual(self.method('buildTestURL')(harness, options), 'http://mochi.test:8888/tests')
        harness._current_scheme = 'https'
        self.assertEqual(self.method('buildTestURL')(harness, options), 'https://example.com:443/tests')

    def test_secure_browser_selection_keeps_other_manifest_prefs_separate(self):
        tests = [dict(path='a', manifest='selected'), dict(path='b', scheme='https', manifest='selected'),
                 dict(path='c', scheme='https', manifest='other')]
        calls = []
        harness = SimpleNamespace(_current_manifest='selected', log=SimpleNamespace(info=lambda _: None),
                                  getActiveTests=lambda _: tests)
        harness.groupTestsByScheme = lambda tests: self.method('groupTestsByScheme')(harness, tests)
        harness._runMochitests = lambda options, paths: calls.append((harness._current_scheme, paths)) or 0
        options = SimpleNamespace(runByDir=True, bisectChunk=None)
        self.assertEqual(self.method('runMochitests')(harness, options, ['a', 'b', 'c']), 0)
        self.assertEqual(calls, [('http', ['a']), ('https', ['b'])])
        self.assertEqual(harness._current_scheme, 'http')

    def test_direct_harness_resets_launch_state_between_schemes(self):
        calls = []
        harness = SimpleNamespace(_current_manifest=None, log=SimpleNamespace(info=lambda _: None),
                                  getActiveTests=lambda _: [dict(path='a'), dict(path='b', scheme='https')])
        harness.groupTestsByScheme = lambda tests: self.method('groupTestsByScheme')(harness, tests)
        harness.initializeLooping = lambda _: calls.append('reset')
        harness._runMochitests = lambda options, paths: calls.append((harness._current_scheme, paths)) or 0
        options = SimpleNamespace(runByDir=False, bisectChunk=None)
        self.assertEqual(self.method('runMochitests')(harness, options, ['a', 'b']), 0)
        self.assertEqual(calls, [('http', ['a']), 'reset', ('https', ['b'])])

    def test_result_and_exception_do_not_leave_secure_state_enabled(self):
        harness = SimpleNamespace(_current_manifest=None, log=SimpleNamespace(info=lambda _: None),
                                  getActiveTests=lambda _: [dict(path='b', scheme='https')])
        harness.groupTestsByScheme = lambda tests: self.method('groupTestsByScheme')(harness, tests)
        harness._runMochitests = lambda *_: 7
        self.assertEqual(self.method('runMochitests')(harness, None, ['b']), 7)
        def fail(*_):
            raise RuntimeError('launch failure')
        harness._runMochitests = fail
        with self.assertRaises(RuntimeError):
            self.method('runMochitests')(harness, None, ['b'])
        self.assertEqual(harness._current_scheme, 'http')

    def test_written_manifest_contains_only_selected_scheme_and_manifest(self):
        tests = [dict(path='a', manifest='selected'), dict(path='b', scheme='https', manifest='selected'),
                 dict(path='c', scheme='https', manifest='other')]
        harness = SimpleNamespace(_current_manifest='selected', _current_scheme='https',
                                  getActiveTests=lambda *_: tests, buildTestURL=lambda _: 'secure-url')
        with tempfile.TemporaryDirectory() as directory:
            self.namespace['SCRIPT_DIR'] = directory
            options = SimpleNamespace(testRunManifestFile='selected.json')
            self.assertEqual(self.method('buildTestPath')(harness, options, ['a', 'b', 'c']), 'secure-url')
            result = json.loads((Path(directory) / 'selected.json').read_text())
            self.assertEqual(result['tests'], [tests[1]])


class ManifestPrefsTests(unittest.TestCase):
    def test_same_directory_different_manifests_do_not_share_prefs(self):
        tests = [dict(path='dir/a.html', manifest='a.ini', prefs='test.a=true'),
                 dict(path='dir/b.html', manifest='b.ini', prefs='test.a=false'),
                 dict(path='dir/c.html', manifest='a.ini', prefs='test.a=true')]
        groups = manifest_test_groups(tests, [test['path'] for test in tests])
        self.assertEqual(groups, [
            ('dir', 'a.ini', ('test.a=true',), ['dir/a.html', 'dir/c.html']),
            ('dir', 'b.ini', ('test.a=false',), ['dir/b.html'])])

    def test_conflicting_per_test_prefs_are_rejected(self):
        with self.assertRaises(ValueError):
            manifest_test_groups([
                dict(path='a.html', manifest='a.ini', prefs='test.a=true'),
                dict(path='b.html', manifest='a.ini', prefs='test.a=false')],
                ['a.html', 'b.html'])

    def test_paths_skips_and_string_spaces(self):
        groups = manifest_test_groups([
            dict(path='a.html', manifest='a.ini',
                 prefs='\n test.string=two words\n test.bool=false\n'),
            dict(path='disabled.html', manifest='b.ini')], ['a.html'])
        self.assertEqual(groups, [('', 'a.ini',
                                  ('test.string=two words', 'test.bool=false'),
                                  ['a.html'])])


if __name__ == '__main__':
    unittest.main()
