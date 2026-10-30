# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest

from manifestprefs import manifest_test_groups


class ManifestPrefsTests(unittest.TestCase):
    def test_same_directory_different_manifests_do_not_share_prefs(self):
        tests = [dict(path='dir/a.html', manifest='a.ini', prefs='test.a=true'),
                 dict(path='dir/b.html', manifest='b.ini', prefs='test.a=false'),
                 dict(path='dir/c.html', manifest='a.ini', prefs='test.a=true')]
        groups = manifest_test_groups(tests, [test['path'] for test in tests])
        self.assertEqual(groups, [
            ('dir', 'a.ini', ('test.a=true',), ['dir/a.html', 'dir/c.html']),
            ('dir', 'b.ini', ('test.a=false',), ['dir/b.html'])])

    def test_conflicting_per_test_prefs_are_rejected(self):
        with self.assertRaises(ValueError):
            manifest_test_groups([
                dict(path='a.html', manifest='a.ini', prefs='test.a=true'),
                dict(path='b.html', manifest='a.ini', prefs='test.a=false')],
                ['a.html', 'b.html'])

    def test_paths_skips_and_string_spaces(self):
        groups = manifest_test_groups([
            dict(path='a.html', manifest='a.ini',
                 prefs='\n test.string=two words\n test.bool=false\n'),
            dict(path='disabled.html', manifest='b.ini')], ['a.html'])
        self.assertEqual(groups, [('', 'a.ini',
                                  ('test.string=two words', 'test.bool=false'),
                                  ['a.html'])])


if __name__ == '__main__':
    unittest.main()
