# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sys
import unittest

from marionette_driver.addons import AddonInstallException
from marionette_driver.errors import MarionetteException


class ErrorTests(unittest.TestCase):
    def test_message_is_available_on_python_three(self):
        error = MarionetteException('missing file')
        self.assertEqual(error.message, 'missing file')
        self.assertEqual(str(error), 'missing file')

    def test_wrapped_addon_error_remains_printable(self):
        error = AddonInstallException(MarionetteException('corrupt archive'))
        self.assertIn('corrupt archive', str(error))

    def test_cause_and_remote_stack_remain_printable(self):
        try:
            raise ValueError('root cause')
        except ValueError:
            error = MarionetteException('install failed', cause=sys.exc_info(),
                                        stacktrace='first\nsecond')
        rendered = str(error)
        self.assertIn('install failed', rendered)
        self.assertIn('ValueError', rendered)
        self.assertIn('stacktrace:\n\tfirst\n\tsecond', rendered)


if __name__ == '__main__':
    unittest.main()
