import unittest

from io import StringIO
import os
import sys
import os.path
import mozunit
from tempfile import mkstemp

from mozbuild.preprocessor import Preprocessor

class TestLineEndings(unittest.TestCase):
  """
  Unit tests for the Context class
  """

  def setUp(self):
    self.pp = Preprocessor()
    self.pp.out = StringIO()
    fd, self.tempnam = mkstemp()
    os.close(fd)

  def tearDown(self):
    os.remove(self.tempnam)

  def createFile(self, lineendings):
    f = open(self.tempnam, 'wb')
    for line, ending in zip([b'a', b'#literal b', b'c'], lineendings):
      f.write(line+ending)
    f.close()

  def testMac(self):
    self.createFile([b'\x0D']*3)
    self.pp.do_include(self.tempnam)
    self.assertEqual(self.pp.out.getvalue(), 'a\nb\nc\n')

  def testUnix(self):
    self.createFile([b'\x0A']*3)
    self.pp.do_include(self.tempnam)
    self.assertEqual(self.pp.out.getvalue(), 'a\nb\nc\n')

  def testWindows(self):
    self.createFile([b'\x0D\x0A']*3)
    self.pp.do_include(self.tempnam)
    self.assertEqual(self.pp.out.getvalue(), 'a\nb\nc\n')

if __name__ == '__main__':
  mozunit.main()
