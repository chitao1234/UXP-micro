#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Import Firefox's maintained HSTS list at an explicit source revision.

No network access is required to build. Review and commit the resulting list
and source metadata together. --input supports an already downloaded snapshot;
it is optional and is never a build dependency.
"""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', required=True,
                        help='Full commit ID in mozilla-firefox/firefox')
    parser.add_argument('--input', type=Path, help='Optional offline source file')
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.revision):
        parser.error('--revision must be a full 40-digit lowercase commit ID')
    url = ('https://raw.githubusercontent.com/mozilla-firefox/firefox/' +
           args.revision + '/security/manager/ssl/nsSTSPreloadList.inc')
    if args.input:
        data = args.input.read_bytes()
    else:
        request = urllib.request.Request(url, headers={'User-Agent': 'MicroGecko-HSTS'})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read()
    text = data.decode('ascii')
    sections = text.split('%%')
    if len(sections) != 3 or sections[2].strip():
        parser.error('Unexpected Firefox preload list format')
    expiration = re.search(r'gPreloadListExpirationTime = INT64_C\((\d+)\)', sections[0])
    if not expiration:
        parser.error('Missing preload expiration')
    expires = int(expiration.group(1))
    now = int(datetime.datetime.now(datetime.timezone.utc).timestamp() * 1000000)
    if expires <= now + 7 * 86400 * 1000000:
        parser.error('The source list expires in less than seven days')
    entries = {}
    for line in sections[1].splitlines():
        if not line:
            continue
        match = re.fullmatch(r'([a-z0-9_.-]+), ([01])', line)
        if not match or match.group(1) in entries:
            parser.error('Invalid or duplicate preload entry: ' + line)
        entries[match.group(1)] = int(match.group(2))
    if len(entries) < 10000:
        parser.error('Refusing a truncated preload list')
    for name, flag in [('includesubdomains.preloaded.test', 1),
                       ('noincludesubdomains.preloaded.test', 0)]:
        if entries.get(name) != flag:
            parser.error('Missing upstream regression fixture: ' + name)
    directory = Path(__file__).resolve().parent.parent / 'ssl'
    metadata = dict(source_url=url, revision=args.revision,
                    sha256=hashlib.sha256(data).hexdigest(), entries=len(entries),
                    expiration_microseconds=expires)
    (directory / 'nsSTSPreloadList.inc').write_bytes(data)
    (directory / 'nsSTSPreloadList.source.json').write_text(
        json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(metadata, indent=2))


if __name__ == '__main__':
    main()
