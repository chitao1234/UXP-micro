# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest
import sys
import time
import traceback

try:
    from unittest import TextTestResult
except ImportError:
    # bug 971243 - python 2.6 compatibilty
    from unittest import _TextTestResult as TextTestResult

"""Adapter used to output structuredlog messages from unittest
testsuites"""


def get_test_class_name(test):
    """
    This method is used to return the full class name from a
    :class:`unittest.TestCase` instance.

    It is used as a default to define the "class_name" extra value
    passed in structured loggers. You can override the default by
    implementing a "get_test_class_name" method on you TestCase subclass.
    """
    return "%s.%s" % (test.__class__.__module__,
                      test.__class__.__name__)


def get_test_method_name(test):
    """
    This method is used to return the full method name from a
    :class:`unittest.TestCase` instance.

    It is used as a default to define the "method_name" extra value
    passed in structured loggers. You can override the default by
    implementing a "get_test_method_name" method on you TestCase subclass.
    """
    return test._testMethodName


class StructuredTestResult(TextTestResult):

    def __init__(self, *args, **kwargs):
        self.logger = kwargs.pop('logger')
        self.test_list = kwargs.pop("test_list", [])
        self.result_callbacks = kwargs.pop('result_callbacks', [])
        self.passed = 0
        self._tests_passed = []
        self.testsRun = 0
        TextTestResult.__init__(self, *args, **kwargs)

    @property
    def tests_passed(self):
        return self._tests_passed

    def call_callbacks(self, test, status):
        debug_info = {}
        for callback in self.result_callbacks:
            info = callback(test, status)
            if info is not None:
                debug_info.update(info)
        return debug_info

    def startTestRun(self):
        # This would be an opportunity to call the logger's suite_start action,
        # however some users may use multiple suites, and per the structured
        # logging protocol, this action should only be called once.
        pass

    def startTest(self, test):
        self.testsRun += 1
        self._test_results = []
        self.logger.test_start(test.id())

    def stopTest(self, test):
        if not self._test_results:
            return
        # unittest can report both a method failure and a teardown/cleanup error.
        # Retain every diagnostic, but the structured protocol permits one end.
        priority = {'PASS': 0, 'SKIP': 1, 'FAIL': 2, 'ERROR': 3}
        status, data = max(self._test_results, key=lambda item: priority[item[0]])
        data = dict(data)
        for field in ('message', 'stack'):
            values = [result[field] for _, result in self._test_results
                      if result.get(field)]
            if values:
                data[field] = '\n'.join(values)
        self.logger.test_end(test.id(), status, **data)

    def _record_test_end(self, test, status, **data):
        self._test_results.append((status, data))

    def stopTestRun(self):
        # This would be an opportunity to call the logger's suite_end action,
        # however some users may use multiple suites, and per the structured
        # logging protocol, this action should only be called once.
        pass

    def _extract_err_message(self, err):
        # Format an exception message in the style of unittest's _exc_info_to_string
        # while maintaining a division between a traceback and a message.
        exc_ty, val, _ = err
        exc_msg = "".join(traceback.format_exception_only(exc_ty, val))
        if self.buffer:
            output_msg = "\n".join([sys.stdout.getvalue(), sys.stderr.getvalue()])
            return "".join([exc_msg, output_msg])
        return exc_msg.rstrip()

    def _extract_stacktrace(self, err, test):
        # Format an exception stack in the style of unittest's _exc_info_to_string
        # while maintaining a division between a traceback and a message.
        # This is mostly borrowed from unittest.result._exc_info_to_string.

        exctype, value, tb = err
        while tb and self._is_relevant_tb_level(tb):
            tb = tb.tb_next
        # Header usually included by print_exception
        lines = ["Traceback (most recent call last):\n"]
        if exctype is test.failureException:
            length = self._count_relevant_tb_levels(tb)
            lines += traceback.format_tb(tb, length)
        else:
            lines += traceback.format_tb(tb)
        return "".join(lines)

    def _get_class_method_name(self, test):
        if hasattr(test, 'get_test_class_name'):
            class_name = test.get_test_class_name()
        else:
            class_name = get_test_class_name(test)

        if hasattr(test, 'get_test_method_name'):
            method_name = test.get_test_method_name()
        else:
            method_name = get_test_method_name(test)

        return {
            'class_name': class_name,
            'method_name': method_name
        }

    def addError(self, test, err):
        unittest.TestResult.addError(self, test, err)
        extra = self.call_callbacks(test, "ERROR")
        extra.update(self._get_class_method_name(test))
        self._record_test_end(test,
                             "ERROR",
                             message=self._extract_err_message(err),
                             expected="PASS",
                             stack=self._extract_stacktrace(err, test),
                             extra=extra)

    def addFailure(self, test, err):
        unittest.TestResult.addFailure(self, test, err)
        extra = self.call_callbacks(test, "FAIL")
        extra.update(self._get_class_method_name(test))
        self._record_test_end(test,
                             "FAIL",
                             message=self._extract_err_message(err),
                             expected="PASS",
                             stack=self._extract_stacktrace(err, test),
                             extra=extra)

    def addSuccess(self, test):
        self.passed += 1
        self._tests_passed.append(test)
        extra = self._get_class_method_name(test)
        self._record_test_end(test,
                             "PASS",
                             expected="PASS",
                             extra=extra)

    def addExpectedFailure(self, test, err):
        unittest.TestResult.addExpectedFailure(self, test, err)
        extra = self.call_callbacks(test, "FAIL")
        extra.update(self._get_class_method_name(test))
        self._record_test_end(test,
                             "FAIL",
                             message=self._extract_err_message(err),
                             expected="FAIL",
                             stack=self._extract_stacktrace(err, test),
                             extra=extra)

    def addUnexpectedSuccess(self, test):
        unittest.TestResult.addUnexpectedSuccess(self, test)
        extra = self.call_callbacks(test, "PASS")
        extra.update(self._get_class_method_name(test))
        self._record_test_end(test,
                             "PASS",
                             expected="FAIL",
                             extra=extra)

    def addSkip(self, test, reason):
        unittest.TestResult.addSkip(self, test, reason)
        extra = self.call_callbacks(test, "SKIP")
        extra.update(self._get_class_method_name(test))
        self._record_test_end(test,
                             "SKIP",
                             message=reason,
                             expected="PASS",
                             extra=extra)


class StructuredTestRunner(unittest.TextTestRunner):

    resultclass = StructuredTestResult

    def __init__(self, **kwargs):
        """TestRunner subclass designed for structured logging.

        :params logger: A ``StructuredLogger`` to use for logging the test run.
        :params test_list: An optional list of tests that will be passed along
            the `suite_start` message.

        """

        self.logger = kwargs.pop("logger")
        self.test_list = kwargs.pop("test_list", [])
        self.result_callbacks = kwargs.pop("result_callbacks", [])
        unittest.TextTestRunner.__init__(self, **kwargs)

    def _makeResult(self):
        return self.resultclass(self.stream,
                                self.descriptions,
                                self.verbosity,
                                logger=self.logger,
                                test_list=self.test_list)

    def run(self, test):
        """Run the given test case or test suite."""
        result = self._makeResult()
        result.failfast = self.failfast
        result.buffer = self.buffer
        startTime = time.time()
        startTestRun = getattr(result, 'startTestRun', None)
        if startTestRun is not None:
            startTestRun()
        try:
            test(result)
        finally:
            stopTestRun = getattr(result, 'stopTestRun', None)
            if stopTestRun is not None:
                stopTestRun()
        stopTime = time.time()
        if hasattr(result, 'time_taken'):
            result.time_taken = stopTime - startTime

        return result
