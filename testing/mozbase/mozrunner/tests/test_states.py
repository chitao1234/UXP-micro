#!/usr/bin/env python

import os

import mozrunner
import mozrunnertest


class MozrunnerStatesTestCase(mozrunnertest.MozrunnerTestCase):

    def test_cleanup_restores_profile(self):
        """Explicit cleanup must release a retained test case's profile."""
        path = self.profile.profile
        self.assertTrue(os.path.isdir(path))
        self.runner.cleanup()
        self.assertFalse(os.path.exists(path))

    def test_cleanup_preserves_profile_when_requested(self):
        self.runner.clean_profile = False
        path = self.profile.profile
        try:
            self.runner.cleanup()
            self.assertTrue(os.path.isdir(path))
        finally:
            self.profile.cleanup()

    def test_errors_before_start(self):
        """Bug 965714: Not started errors before start() is called"""

        def test_returncode():
            return self.runner.returncode

        self.assertRaises(mozrunner.RunnerNotStartedError, self.runner.is_running)
        self.assertRaises(mozrunner.RunnerNotStartedError, test_returncode)
        self.assertRaises(mozrunner.RunnerNotStartedError, self.runner.wait)
