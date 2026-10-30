# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Keep manifest preferences within the browser instance that runs them."""

import os


def manifest_test_groups(tests, paths):
    settings = {}
    groups = {}
    selected = set(paths)
    for test in tests:
        manifest = test['manifest']
        # Like current Firefox, split on lines: a string pref may contain spaces.
        prefs = tuple(line.strip() for line in (test.get('prefs') or '').splitlines()
                      if line.strip())
        if manifest in settings and settings[manifest] != prefs:
            raise ValueError("The 'prefs' key must be set in DEFAULT: " + manifest)
        settings[manifest] = prefs
        if test['path'] not in selected:
            continue
        key = (os.path.dirname(test['path']), manifest)
        groups.setdefault(key, []).append(test['path'])
    return [(directory, manifest, settings[manifest], groups[(directory, manifest)])
            for directory, manifest in sorted(groups)]
