# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest

from marionette_harness import MarionetteTestResult


def test_common_case_parent_counts_do_not_duplicate_teardown_errors():
    from types import SimpleNamespace
    records = [SimpleNamespace(test_name='a', result='UNEXPECTED-FAIL'),
               SimpleNamespace(test_name='a', result='ERROR'),
               SimpleNamespace(test_name='b', result='PASS')]
    assert MarionetteTestResult.parent_summary(records) == {
        'completed': 2, 'counts': {'ERROR': 1, 'PASS': 1}}


def test_common_case_honors_python3_expected_failure():
    import unittest
    from marionette_harness import CommonTestCase

    class Case(CommonTestCase):
        def setUp(self):
            pass

        def cleanTest(self):
            pass

        @unittest.expectedFailure
        def test_expected(self):
            self.fail('intentional failure')

        @unittest.expectedFailure
        def test_unexpected(self):
            pass

        @unittest.expectedFailure
        def test_skipped(self):
            self.skipTest('unavailable capability')

    result = unittest.TestResult()
    for method in ('test_expected', 'test_unexpected', 'test_skipped'):
        Case(method, lambda: None, {}).run(result)
    assert result.testsRun == 3
    assert len(result.expectedFailures) == 1
    assert len(result.unexpectedSuccesses) == 1
    assert len(result.skipped) == 1
    assert not result.errors
    assert not result.failures


def test_common_case_preserves_setup_error_with_expected_failure():
    import unittest
    from marionette_harness import CommonTestCase

    class Case(CommonTestCase):
        def setUp(self):
            raise RuntimeError('setup failed')

        def cleanTest(self):
            pass

        @unittest.expectedFailure
        def test_expected(self):
            self.fail('body must not execute')

    result = unittest.TestResult()
    Case('test_expected', lambda: None, {}).run(result)
    assert result.testsRun == 1
    assert len(result.errors) == 1
    assert not result.expectedFailures


@pytest.fixture
def empty_marionette_testcase():
    """ Testable MarionetteTestCase class """
    from marionette_harness import MarionetteTestCase

    class EmptyTestCase(MarionetteTestCase):
        def test_nothing(self):
            pass

    return EmptyTestCase


@pytest.fixture
def empty_marionette_test(mock_marionette, empty_marionette_testcase):
    return empty_marionette_testcase(lambda: mock_marionette, lambda: mock_httpd,
                                     'test_nothing')


@pytest.mark.parametrize("has_crashed", [True, False])
def test_crash_is_recorded_as_error(empty_marionette_test,
                                    logger,
                                    has_crashed):
    """ Number of errors is incremented by stopTest iff has_crashed is true """
    # collect results from the empty test
    result = MarionetteTestResult(
        marionette=empty_marionette_test._marionette_weakref(),
        logger=logger, verbosity=0,
        stream=None, descriptions=None,
    )
    result.startTest(empty_marionette_test)
    assert len(result.errors) == 0
    assert len(result.failures) == 0
    assert result.testsRun == 1
    assert result.shouldStop is False
    result.stopTest(empty_marionette_test)
    assert result.shouldStop == has_crashed
    if has_crashed:
        assert len(result.errors) == 1
    else:
        assert len(result.errors) == 0


if __name__ == '__main__':
    import sys
    sys.exit(pytest.main(['--verbose', __file__]))
