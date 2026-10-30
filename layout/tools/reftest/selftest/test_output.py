# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
import unittest

import mozunit
from mozlog.structuredlog import StructuredLogger

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from output import OutputHandler


class TestOutputHandler(unittest.TestCase):
    def setUp(self):
        self.log = StructuredLogger(self.id())
        self.messages = []
        self.log.add_handler(self.messages.append)
        self.output = OutputHandler(self.log, os.path.dirname(__file__))

    def send(self, action, **kwargs):
        kwargs['action'] = action
        self.output(json.dumps(kwargs))

    def start(self):
        self.send('suite_start', tests=['example.html'])

    def end(self, **results):
        self.send('suite_end', extra={'results': results})

    def test_complete_run(self):
        self.start()
        self.send('test_start', test='example.html')
        self.send('test_end', test='example.html', status='PASS')
        self.end(Pass=1)
        self.assertTrue(self.output.suite_finished)
        self.assertEqual(self.output.get_exit_status(0), 0)

    def test_comparison_failure_keeps_historical_exit_status(self):
        self.start()
        self.send('test_start', test='example.html')
        self.send('test_end', test='example.html', status='FAIL', expected='PASS')
        self.end(UnexpectedFail=1)
        self.assertEqual(self.output.get_exit_status(0), 0)
        self.assertTrue(any(item['action'] == 'test_end' and
                            item.get('expected') == 'PASS'
                            for item in self.messages))

    def test_failed_load_remains_a_test_result(self):
        self.start()
        self.send('test_start', test='example.html')
        self.send('test_end', test='example.html', status='FAIL', expected='PASS',
                  message='load failed: timed out waiting for reftest-wait')
        self.end(FailedLoad=1)
        self.assertEqual(self.output.get_exit_status(0), 0)

    def test_missing_suite(self):
        self.assertEqual(self.output.get_exit_status(0), 1)

    def test_missing_suite_end(self):
        self.start()
        self.assertEqual(self.output.get_exit_status(0), 1)

    def test_startup_exception_before_suite_start(self):
        self.send('log', level='ERROR', message='EXCEPTION: malformed manifest URI')
        self.end(Exception=1)
        # mozlog rejects this suite_end, so inspect the unfiltered input too.
        self.assertFalse(any(item['action'] == 'suite_end' for item in self.messages))
        self.assertEqual(self.output.get_exit_status(0), 1)

    def test_suite_end_without_start_or_error_message(self):
        self.end()
        self.assertEqual(self.output.get_exit_status(0), 1)

    def test_exception_result_without_error_message(self):
        self.start()
        self.end(Exception=1)
        self.assertEqual(self.output.get_exit_status(0), 1)

    def test_error_in_completed_suite(self):
        self.start()
        self.send('log', level='ERROR', message='HTTP ERROR: 404')
        self.end()
        self.assertEqual(self.output.get_exit_status(0), 1)

    def test_warning_in_completed_suite(self):
        self.start()
        self.send('log', level='WARNING', message='Test plugin unavailable')
        self.end()
        self.assertEqual(self.output.get_exit_status(0), 0)

    def test_nonzero_process_status_preserved(self):
        self.assertEqual(self.output.get_exit_status(7), 7)


if __name__ == '__main__':
    mozunit.main()
