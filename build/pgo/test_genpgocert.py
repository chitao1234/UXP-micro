# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest
from collections import namedtuple

from genpgocert import server_spec


Location = namedtuple('Location', 'scheme host options')


class ServerCertificateSpecTests(unittest.TestCase):
    def test_sans_include_only_automatic_https_hosts(self):
        spec = server_spec([
            Location('http', 'plain.example.com', []),
            Location('https', 'example.com', ['privileged']),
            Location('https', 'example.com', []),
            Location('https', 'second.example.com', ['clientauth=require']),
            Location('https', 'expired.example.com', ['cert=expired']),
            Location('https', 'nocert.example.com', ['nocert']),
        ])
        self.assertIn('subject:example.com\n', spec)
        self.assertIn('subjectAlternativeName:example.com,second.example.com\n', spec)
        for excluded in ('plain.example.com', 'expired.example.com', 'nocert.example.com'):
            self.assertNotIn(excluded, spec)

    def test_no_automatic_hosts_is_an_error(self):
        with self.assertRaises(ValueError):
            server_spec([Location('https', 'expired.example.com', ['cert=expired'])])


if __name__ == '__main__':
    unittest.main()
