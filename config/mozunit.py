# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from unittest import TextTestRunner as _TestRunner, TestResult as _TestResult
import unittest
import inspect
from io import BytesIO, StringIO
import os
import sys

'''Helper to make python unit tests report the way that the Mozilla
unit test infrastructure expects tests to report.

Usage:

import unittest
import mozunit

if __name__ == '__main__':
    mozunit.main()
'''

class _MozTestResult(_TestResult):
    def __init__(self, stream, descriptions):
        _TestResult.__init__(self)
        self.stream = stream
        self.descriptions = descriptions

    def getDescription(self, test):
        if self.descriptions:
            return test.shortDescription() or str(test)
        else:
            return str(test)

    def printStatus(self, status, test, message=''):
        line = "{status} | {file} | {klass}.{test}{sep}{message}".format(
            status=status,
            file=inspect.getfile(test.__class__),
            klass=test.__class__.__name__,
            test=test._testMethodName,
            sep=', ' if message else '',
            message=message,
        )
        self.stream.writeln(line)

    def addSuccess(self, test):
        _TestResult.addSuccess(self, test)
        self.printStatus('TEST-PASS', test)

    def addSkip(self, test, reason):
        _TestResult.addSkip(self, test, reason)
        self.printStatus('TEST-SKIP', test)

    def addExpectedFailure(self, test, err):
        _TestResult.addExpectedFailure(self, test, err)
        self.printStatus('TEST-KNOWN-FAIL', test)

    def addUnexpectedSuccess(self, test):
        _TestResult.addUnexpectedSuccess(self, test)
        self.printStatus('TEST-UNEXPECTED-PASS', test)

    def addError(self, test, err):
        _TestResult.addError(self, test, err)
        self.printFail(test, err)
        self.stream.writeln("ERROR: {0}".format(self.getDescription(test)))
        self.stream.writeln(self.errors[-1][1])

    def addFailure(self, test, err):
        _TestResult.addFailure(self, test, err)
        self.printFail(test,err)
        self.stream.writeln("FAIL: {0}".format(self.getDescription(test)))
        self.stream.writeln(self.failures[-1][1])

    def printFail(self, test, err):
        exctype, value, tb = err
        # Python 3 exceptions have no .message attribute. Match current
        # Firefox's fallback so a test error cannot crash result reporting.
        detail = getattr(value, 'message', str(value))
        message = detail.splitlines()[0] if detail else 'NO MESSAGE'
        # Skip test runner traceback levels
        while tb and self._is_relevant_tb_level(tb):
            tb = tb.tb_next
        if tb:
            _, ln, _ = inspect.getframeinfo(tb)[:3]
            message = 'line {0}: {1}'.format(ln, message)
        self.printStatus("TEST-UNEXPECTED-FAIL", test, message)


class MozTestRunner(_TestRunner):
    def _makeResult(self):
        return _MozTestResult(self.stream, self.descriptions)
    def run(self, test):
        result = self._makeResult()
        test(result)
        return result

def _mocked_file(base):
    # Match Firefox's separate binary/text mocks; real build code opens both.
    class MockedFile(base):
        def __init__(self, context, filename, content):
            self.context = context
            self.name = filename
            base.__init__(self, content)

        def close(self):
            if not self.closed:
                self.context.files[normcase(self.name)] = self.getvalue()
                base.close(self)

        def __enter__(self):
            return self

        def __exit__(self, type, value, traceback):
            self.close()
    return MockedFile

MockedFile = _mocked_file(StringIO)
MockedBytesFile = _mocked_file(BytesIO)

def normcase(path):
    '''
    Normalize the case of `path`.

    Don't use `os.path.normcase` because that also normalizes forward slashes
    to backslashes on Windows.
    '''
    if sys.platform.startswith('win'):
        return path.lower()
    return path

class MockedOpen(object):
    '''
    Context manager diverting the open builtin such that opening files
    can open "virtual" file instances given when creating a MockedOpen.

    with MockedOpen({'foo': 'foo', 'bar': 'bar'}):
        f = open('foo', 'r')

    will thus open the virtual file instance for the file 'foo' to f.

    MockedOpen also masks writes, so that creating or replacing files
    doesn't touch the file system, while subsequently opening the file
    will return the recorded content.

    with MockedOpen():
        f = open('foo', 'w')
        f.write('foo')
    self.assertRaises(Exception,f.open('foo', 'r'))
    '''
    def __init__(self, files = {}):
        self.files = {}
        for name, content in files.items():
            self.files[normcase(os.path.abspath(name))] = content

    def __call__(self, name, mode='r', buffering=-1, encoding=None,
                 errors=None, newline=None):
        if isinstance(name, int):
            return self.open(name, mode, buffering=buffering, encoding=encoding,
                             errors=errors, newline=newline)
        displayname = os.path.abspath(name)
        absname = normcase(displayname)
        if 'w' in mode:
            content = b'' if 'b' in mode else ''
        elif absname in self.files:
            content = self.files[absname]
            if content is None:
                raise IOError(2, 'No such file or directory')
        elif 'a' in mode:
            with self.open(name, 'rb' if 'b' in mode else 'r') as existing:
                content = existing.read()
        else:
            return self.open(name, mode, buffering=buffering, encoding=encoding,
                             errors=errors, newline=newline)
        if 'b' in mode:
            if isinstance(content, str):
                content = content.encode('utf-8')
            file = MockedBytesFile(self, displayname, content)
        else:
            if isinstance(content, bytes):
                content = content.decode(encoding or 'utf-8', errors or 'strict')
            file = MockedFile(self, displayname, content)
        if 'a' in mode:
            file.seek(0, os.SEEK_END)
        return file

    def __enter__(self):
        import builtins
        self.open = builtins.open
        self._orig_path_exists = os.path.exists
        self._orig_path_isdir = os.path.isdir
        self._orig_path_isfile = os.path.isfile
        builtins.open = self
        os.path.exists = self._wrapped_exists
        os.path.isdir = self._wrapped_isdir
        os.path.isfile = self._wrapped_isfile

    def __exit__(self, type, value, traceback):
        import builtins
        builtins.open = self.open
        os.path.exists = self._orig_path_exists
        os.path.isdir = self._orig_path_isdir
        os.path.isfile = self._orig_path_isfile

    def _wrapped_exists(self, p):
        return (self._wrapped_isfile(p) or
                self._wrapped_isdir(p) or
                self._orig_path_exists(p))

    def _wrapped_isfile(self, p):
        p = normcase(p)
        if p in self.files:
            return True

        abspath = normcase(os.path.abspath(p))
        if abspath in self.files:
            return True

        return self._orig_path_isfile(p)

    def _wrapped_isdir(self, p):
        p = normcase(p)
        p = p if p.endswith(('/', '\\')) else p + os.sep
        if any(f.startswith(p) for f in self.files):
            return True

        abspath = normcase(os.path.abspath(p) + os.sep)
        if any(f.startswith(abspath) for f in self.files):
            return True

        return self._orig_path_exists(p)

def main(*args, **kwargs):
    unittest.main(testRunner=MozTestRunner(), *args, **kwargs)
