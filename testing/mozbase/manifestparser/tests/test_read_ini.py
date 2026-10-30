#!/usr/bin/env python

"""
test .ini parsing

ensure our .ini parser is doing what we want; to be deprecated for
python's standard ConfigParser when 2.7 is reality so OrderedDict
is the default:

http://docs.python.org/2/library/configparser.html
"""

import unittest
import os
from manifestparser import read_ini
from configparser import ConfigParser
from io import StringIO


class IniParserTest(unittest.TestCase):
    def test_multiline_preferences_contain_assignments(self):
        parsed = read_ini(StringIO(
            '[DEFAULT]\nprefs =\n  test.bool=true\n'
            '  test.string=two words\n[test.html]\n'))
        values = parsed[0][1]
        self.assertEqual(values['prefs'].splitlines(),
                         ['', 'test.bool=true', 'test.string=two words'])
        self.assertNotIn('test.bool', values)

    def test_preference_defaults_are_preserved_when_combining_manifests(self):
        parsed = read_ini(StringIO(
            '[DEFAULT]\nprefs = default.pref=true\n'
            '[test.html]\nprefs = local.pref=false\n'))
        self.assertEqual(parsed[0][1]['prefs'].splitlines(),
                         ['default.pref=true', 'local.pref=false'])

    def test_indented_assignment_at_key_indentation_is_a_new_key(self):
        values = read_ini(StringIO(
            '[test.html]\n  first = one\n    continued=two\n'
            '  second = three\n'))[0][1]
        self.assertEqual(values['first'], 'one' + os.linesep + 'continued=two')
        self.assertEqual(values['second'], 'three')

    def test_inline_comments(self):
        """
        We have no inline comments; so we're testing to ensure we don't:
        https://bugzilla.mozilla.org/show_bug.cgi?id=855288
        """

        # test '#' inline comments (really, the lack thereof)
        string = """[test_felinicity.py]
kittens = true # This test requires kittens
"""
        buffer = StringIO()
        buffer.write(string)
        buffer.seek(0)
        result = read_ini(buffer)[0][1]['kittens']
        self.assertEqual(result, "true # This test requires kittens")

        # compare this to ConfigParser
        # python 2.7 ConfigParser does not support '#' as an
        # inline comment delimeter (for "backwards compatability"):
        # http://docs.python.org/2/library/configparser.html
        buffer.seek(0)
        parser = ConfigParser()
        parser.read_file(buffer)
        control = parser.get('test_felinicity.py', 'kittens')
        self.assertEqual(result, control)

        # test ';' inline comments (really, the lack thereof)
        string = string.replace('#', ';')
        buffer = StringIO()
        buffer.write(string)
        buffer.seek(0)
        result = read_ini(buffer)[0][1]['kittens']
        self.assertEqual(result, "true ; This test requires kittens")

        # compare this to ConfigParser
        # python 2.7 ConfigParser *does* support ';' as an
        # inline comment delimeter (ibid).
        # Python 3.x configparser, OTOH, does not support
        # inline-comments by default.  It does support their specification,
        # though they are weakly discouraged:
        # http://docs.python.org/dev/library/configparser.html
        buffer.seek(0)
        parser = ConfigParser(inline_comment_prefixes=(';',))
        parser.read_file(buffer)
        control = parser.get('test_felinicity.py', 'kittens')
        self.assertNotEqual(result, control)


if __name__ == '__main__':
    unittest.main()
