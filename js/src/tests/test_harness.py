# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import unittest

from lib.manifest import NullXULInfoTester, _parse_one
from lib.results import TestOutput, TestResult
from lib.tests import RefTestCase


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


if __name__ == '__main__':
    unittest.main()
