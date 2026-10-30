# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Resume interrupted local automation runs without hiding failed invocations.

Callers must use the same binary, environment and test sources. Only complete
invocation records are reusable; assertion lines and timeout records are not.
"""
import io
import os
import re
import shlex


def invocation_key(path, flags):
    return os.path.normcase(os.path.normpath(path.strip())), tuple(sorted(flags))


def completed_invocations(logs, suite):
    completed = set()
    for log in logs:
        with io.open(log, encoding='utf-8', errors='replace') as stream:
            for line in stream:
                match = re.match(r'^(TEST-[A-Z-]+)(?: \(EXPECTED RANDOM\))? \| ([^|]+) \| (.*)$',
                                 line.rstrip('\r\n'))
                if not match:
                    continue
                status, path, detail = match.groups()
                if suite == 'jstests':
                    args = re.fullmatch(r'\(args: "([^"\r\n]*)"\)(?: \| \((SKIP|TIMEOUT)\))?',
                                        detail)
                    if not args or args.group(2) == 'TIMEOUT':
                        continue
                    success = status in ('TEST-PASS', 'TEST-KNOWN-FAIL')
                else:
                    args = re.search(r'\(code [^,]+, args "([^"\r\n]*)"\)$', detail)
                    if status == 'TEST-SKIP':
                        # Legacy JIT skip lines don't identify their flags. Run
                        # these again rather than guessing which variants ran.
                        continue
                    success = status == 'TEST-PASS' and detail.startswith('Success ')
                if args and success:
                    completed.add(invocation_key(path, shlex.split(args.group(1))))
    return completed


def remaining_invocations(tests, logs, suite, shell_args=''):
    completed = completed_invocations(logs, suite)
    prefix = shlex.split(shell_args) if suite == 'jstests' else []
    pending = []
    reused = 0
    for test in tests:
        path = test.relpath_top if suite == 'jit' else test.path
        if invocation_key(path, prefix + test.jitflags) in completed:
            reused += 1
        else:
            pending.append(test)
    print('RESUME | {} completed invocations retained; {} pending'.format(reused, len(pending)))
    return pending
