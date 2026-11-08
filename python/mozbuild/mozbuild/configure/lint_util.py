# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

from dis import Bytecode


def disassemble_as_iter(co):
    """Yield resolved bytecode operands using the running Python's format."""
    for instruction in Bytecode(co):
        yield instruction.opname, instruction.argval
