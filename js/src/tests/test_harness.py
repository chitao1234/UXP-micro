# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import unittest
from types import SimpleNamespace
from unittest import mock

import jstests

from lib.manifest import NullXULInfoTester, _parse_one
from lib.results import TestOutput, TestResult
from lib.tests import RefTestCase
from lib import jittests


class NegativeTestResults(unittest.TestCase):
    def result(self, err='', rc=3, out='', timed_out=False):
        test = RefTestCase(os.path.join('negative', 'fixture.js'))
        test.terms = 'error:SyntaxError'
        _parse_one(test, NullXULInfoTester())
        output = TestOutput(test, [], out, err, rc, 0.01, timed_out)
        return TestResult.from_output(output).result

    def diagnostic(self, name='SyntaxError', path=None):
        path = path or os.path.join('negative', 'fixture.js')
        return '{}:12:4 {}: invalid optional chain\n'.format(path, name)

    def test_expected_syntax_error_passes(self):
        self.assertEqual(self.result(self.diagnostic()), TestResult.PASS)

    def test_absolute_path_matches(self):
        diagnostic = self.diagnostic(path=os.path.abspath('negative/fixture.js'))
        self.assertEqual(self.result(diagnostic), TestResult.PASS)

    def test_diagnostic_without_column_matches(self):
        diagnostic = self.diagnostic().replace(':12:4 ', ':12 ')
        self.assertEqual(self.result(diagnostic), TestResult.PASS)

    def test_wrong_error_fails(self):
        self.assertEqual(self.result(self.diagnostic('TypeError')), TestResult.FAIL)

    def test_error_in_prelude_fails(self):
        self.assertEqual(self.result(self.diagnostic(path='shell.js')), TestResult.FAIL)

    def test_no_exception_fails_even_with_passing_assertions(self):
        self.assertEqual(self.result(rc=0, out=' PASSED! example\n'), TestResult.FAIL)

    def test_native_crash_fails(self):
        self.assertEqual(self.result(self.diagnostic(), rc=0xc0000005), TestResult.CRASH)

    def test_success_exit_with_diagnostic_fails(self):
        self.assertEqual(self.result(self.diagnostic(), rc=0), TestResult.FAIL)

    def test_missing_diagnostic_fails(self):
        self.assertEqual(self.result(), TestResult.FAIL)

    def test_timeout_fails(self):
        self.assertEqual(self.result(self.diagnostic(), timed_out=True), TestResult.FAIL)

    def test_failed_assertion_still_fails(self):
        self.assertEqual(self.result(self.diagnostic(), out=' FAILED! example\n'),
                         TestResult.FAIL)

    def test_malformed_error_directives_fail(self):
        for directive in ('error:', 'error:SyntaxError)', 'error:123Error'):
            test = RefTestCase('fixture.js')
            test.terms = directive
            with self.assertRaises(ValueError):
                _parse_one(test, NullXULInfoTester())


class JitExitStatusResults(unittest.TestCase):
    def result(self, rc, err='', crash=False, error='', status=0):
        test = SimpleNamespace(skip_if_cond=None, expect_crash=crash,
                               expect_error=error, expect_status=status,
                               allow_oom=False, allow_unhandlable_oom=False,
                               allow_overrecursed=False)
        with mock.patch.object(jittests.sys, 'platform', 'win32'):
            return jittests.check_output('', err, rc, False, test, SimpleNamespace())

    def test_windows_crash_statuses(self):
        self.assertTrue(self.result(0xc0000005, crash=True))
        self.assertTrue(self.result(-1073741819, crash=True))
        self.assertTrue(self.result(0x80000003, crash=True))

    def test_crash_required(self):
        self.assertFalse(self.result(0, crash=True))
        self.assertFalse(self.result(3, err='Error: unrelated', crash=True))
        self.assertFalse(self.result(0xc0000005))

    def test_expected_status_required(self):
        self.assertTrue(self.result(6, status=6))
        self.assertFalse(self.result(0, status=6))

    def test_expected_error_requires_exception_exit(self):
        self.assertTrue(self.result(3, err='TypeError: expected', error='TypeError'))
        self.assertFalse(self.result(0, err='TypeError: expected', error='TypeError'))
        self.assertFalse(self.result(3, err='Error: different', error='TypeError'))


class TestFileSelection(unittest.TestCase):
    def test_forward_slashes_are_normalized_in_exact_file_filter(self):
        wanted = RefTestCase(os.path.normpath('negative/fixture.js'))
        other = RefTestCase(os.path.normpath('negative/other.js'))
        options = SimpleNamespace(
            js_shell=None, test_reflect_stringify=None, make_manifests=None,
            tbpl=False, tbpl_debug=False, jitflags='none', test_file=['tests.list'],
            no_extensions=False, random=True, run_only_skipped=False,
            run_slow_tests=True, repeat=1)
        with mock.patch('lib.manifest.count_tests', return_value=2), \
             mock.patch('lib.manifest.load_reftests', return_value=iter([wanted, other])), \
             mock.patch('builtins.open', mock.mock_open(read_data='negative/fixture.js\n')):
            _, tests = jstests.load_tests(options, {wanted.path}, set())
            self.assertEqual(list(tests), [wanted])


if __name__ == '__main__':
    unittest.main()
