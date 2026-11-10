# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Source-tree entry point for the restored Marionette harness.

Based on Firefox's Marionette mach provider. The harness and driver remain
the owners of argument validation, browser startup and test execution.
"""

import argparse
import os
import sys

from mach.decorators import Command, CommandProvider
from mozbuild.base import MachCommandBase, MachCommandConditions as conditions


def create_parser():
    from marionette_harness.runtests import MarionetteArguments
    from mozlog import commandline
    parser = MarionetteArguments()
    commandline.add_logging_group(parser)
    return parser


@CommandProvider
class MarionetteTests(MachCommandBase):
    @Command('marionette-test', category='testing',
             description='Run Marionette browser automation tests.',
             conditions=[conditions.is_firefox], parser=create_parser)
    def marionette_test(self, tests, **kwargs):
        from marionette_harness.runtests import MarionetteHarness
        from mozlog import commandline

        if 'test_objects' in kwargs:
            tests = [obj['file_relpath'] for obj in kwargs.pop('test_objects')]
        if not tests:
            tests = [os.path.join(self.topsrcdir, 'testing', 'marionette',
                     'harness', 'marionette_harness', 'tests', 'unit', 'unit-tests.ini')]
        if not kwargs.get('binary') and not kwargs.get('address'):
            kwargs['binary'] = self.get_binary_path('app')
        if not kwargs.get('app'):
            kwargs['app'] = 'fxdesktop'

        args = argparse.Namespace(tests=tests, **kwargs)
        create_parser().verify_usage(args)
        logger = commandline.setup_logging(args.logger_name, args, {'mach': sys.stdout})
        options = vars(args)
        for key in list(options):
            if key.startswith('log_'):
                del options[key]
        options['logger'] = logger
        return int(MarionetteHarness(args=options).run() > 0)
