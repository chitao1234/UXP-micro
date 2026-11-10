# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import math
import io
import importlib.util
import os
import sys
import tempfile
import time
import unittest

from moztest.results import TestContext, TestResult, TestResultCollection
from moztest.adapters.unit import StructuredTestRunner, StructuredTestResult


class StructuredResults(unittest.TestCase):
    def test_subclass_can_supply_readonly_passed_collection(self):
        class Derived(StructuredTestResult):
            @property
            def tests_passed(self):
                return ['subclass-owned-results']

        class Logger:
            def test_end(self, *args, **kwargs):
                pass

        result = Derived(io.StringIO(), True, 1, logger=Logger())
        result._test_results = []
        result.addSuccess(unittest.FunctionTestCase(lambda: None))
        self.assertEqual(result.passed, 1)
        self.assertEqual(result.tests_passed, ['subclass-owned-results'])
        self.assertEqual(len(result._tests_passed), 1)

    def run_cases(self, cases):
        class Logger(object):
            def __init__(self):
                self.events = []

            def test_start(self, name):
                self.events.append(('start', name, {}))

            def test_end(self, name, status, **data):
                self.events.append(('end', name, dict(data, status=status)))

        logger = Logger()
        result = StructuredTestRunner(logger=logger, stream=io.StringIO()).run(
            unittest.TestSuite(cases))
        return result, logger.events

    def test_assertion_is_a_recorded_failure(self):
        class Case(unittest.TestCase):
            def runTest(self):
                self.fail('assertion diagnostic')

        result, events = self.run_cases([Case()])
        self.assertFalse(result.wasSuccessful())
        self.assertEqual(len(result.failures), 1)
        self.assertEqual(TestResultCollection.from_unittest_results(None, result).num_failures, 1)
        self.assertEqual([event[0] for event in events], ['start', 'end'])
        self.assertEqual(events[-1][2]['status'], 'FAIL')

    def test_cleanup_error_has_one_end_and_both_diagnostics(self):
        class Case(unittest.TestCase):
            def runTest(self):
                self.fail('assertion diagnostic')

            def tearDown(self):
                raise RuntimeError('cleanup diagnostic')

        result, events = self.run_cases([Case()])
        self.assertEqual((len(result.failures), len(result.errors)), (1, 1))
        self.assertEqual([event[0] for event in events], ['start', 'end'])
        terminal = events[-1][2]
        self.assertEqual(terminal['status'], 'ERROR')
        for diagnostic in ('assertion diagnostic', 'cleanup diagnostic'):
            self.assertIn(diagnostic, terminal['message'])
            self.assertIn(diagnostic, terminal['stack'])

    def test_expected_and_skipped_results_are_retained(self):
        class Cases(unittest.TestCase):
            def test_pass(self):
                pass

            @unittest.skip('capability')
            def test_skip(self):
                pass

            @unittest.expectedFailure
            def test_expected(self):
                self.fail('expected')

            @unittest.expectedFailure
            def test_unexpected(self):
                pass

        result, events = self.run_cases(unittest.defaultTestLoader.loadTestsFromTestCase(Cases))
        self.assertFalse(result.wasSuccessful())
        self.assertEqual((result.passed, len(result.skipped), len(result.expectedFailures),
                          len(result.unexpectedSuccesses)), (1, 1, 1, 1))
        self.assertEqual(len([event for event in events if event[0] == 'end']), 4)


class SuiteLoading(unittest.TestCase):
    def test_same_basename_has_independent_classes_and_globals(self):
        path = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', 'test.py'))
        spec = importlib.util.spec_from_file_location('mozbase_driver', path)
        driver = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(driver)
        name = 'colliding_test'
        previous = sys.modules.get(name)
        try:
            with tempfile.TemporaryDirectory() as directory:
                suites = []
                for index in range(2):
                    folder = os.path.join(directory, str(index))
                    os.mkdir(folder)
                    fixture = os.path.join(folder, name + '.py')
                    with open(fixture, 'w') as stream:
                        stream.write('import unittest\nVALUE = %d\n'
                                     'class Case%d(unittest.TestCase):\n'
                                     '    def test_value(self):\n'
                                     '        self.assertEqual(VALUE, %d)\n'
                                     % (index, index, index))
                    suites.extend(driver.unittests(fixture))
                result = unittest.TestResult()
                unittest.TestSuite(suites).run(result)
                self.assertEqual(result.testsRun, 2)
                self.assertTrue(result.wasSuccessful(), result.errors + result.failures)
        finally:
            if previous is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = previous


class Result(unittest.TestCase):

    def test_results(self):
        self.assertRaises(AssertionError,
                          lambda: TestResult('test', result_expected='hello'))
        t = TestResult('test')
        self.assertRaises(ValueError, lambda: t.finish(result='good bye'))

    def test_time(self):
        now = time.time()
        t = TestResult('test')
        time.sleep(1)
        t.finish('PASS')
        duration = time.time() - now
        self.assertTrue(math.fabs(duration - t.duration) < 1)

    def test_custom_time(self):
        t = TestResult('test', time_start=0)
        t.finish(result='PASS', time_end=1000)
        self.assertEqual(t.duration, 1000)


class Collection(unittest.TestCase):

    def setUp(self):
        c1 = TestContext('host1')
        c2 = TestContext('host2')
        c3 = TestContext('host2')
        c3.os = 'B2G'
        c4 = TestContext('host1')

        t1 = TestResult('t1', context=c1)
        t2 = TestResult('t2', context=c2)
        t3 = TestResult('t3', context=c3)
        t4 = TestResult('t4', context=c4)

        self.collection = TestResultCollection('tests')
        self.collection.extend([t1, t2, t3, t4])

    def test_unique_contexts(self):
        self.assertEqual(len(self.collection.contexts), 3)

if __name__ == '__main__':
    unittest.main()
