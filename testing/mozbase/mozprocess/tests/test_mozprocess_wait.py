#!/usr/bin/env python

import os
import subprocess
import sys
import threading
import unittest
from unittest import mock
import proctest
import mozinfo
from mozprocess import processhandler

here = os.path.dirname(os.path.abspath(__file__))


class ProcTestWait(proctest.ProcTest):
    """ Class to test process waits and timeouts """

    def test_normal_finish(self):
        """Process is started, runs to completion while we wait for it"""

        p = processhandler.ProcessHandler([self.python, self.proclaunch,
                                           "process_normal_finish_python.ini"],
                                          cwd=here)
        p.run()
        p.wait()

        self.determine_status(p)

    def test_wait(self):
        """Process is started runs to completion while we wait indefinitely"""

        p = processhandler.ProcessHandler([self.python, self.proclaunch,
                                           "process_waittimeout_10s_python.ini"],
                                          cwd=here)
        p.run()
        p.wait()

        self.determine_status(p)

    def test_timeout(self):
        """ Process is started, runs but we time out waiting on it
            to complete
        """
        p = processhandler.ProcessHandler([self.python, self.proclaunch,
                                           "process_waittimeout_python.ini"],
                                          cwd=here)
        p.run(timeout=10)
        p.wait()

        if mozinfo.isUnix:
            # process was killed, so returncode should be negative
            self.assertLess(p.proc.returncode, 0)

        self.determine_status(p, False, ['returncode', 'didtimeout'])

    def test_waittimeout(self):
        """
        Process is started, then wait is called and times out.
        Process is still running and didn't timeout
        """
        p = processhandler.ProcessHandler([self.python, self.proclaunch,
                                           "process_waittimeout_10s_python.ini"],
                                          cwd=here)

        p.run()
        p.wait(timeout=5)

        self.determine_status(p, True, ())

    def test_waitnotimeout(self):
        """ Process is started, runs to completion before our wait times out
        """
        p = processhandler.ProcessHandler([self.python, self.proclaunch,
                                           "process_waittimeout_10s_python.ini"],
                                          cwd=here)
        p.run(timeout=30)
        p.wait()

        self.determine_status(p)

    def test_wait_twice_after_kill(self):
        """Bug 968718: Process is started and stopped. wait() twice afterward."""
        p = processhandler.ProcessHandler([self.python, self.proclaunch,
                                           "process_waittimeout_python.ini"],
                                          cwd=here)
        p.run()
        p.kill()
        returncode1 = p.wait()
        returncode2 = p.wait()

        self.determine_status(p)

        self.assertLess(returncode2, 0,
                        'Negative returncode expected, got "%s"' % returncode2)
        self.assertEqual(returncode1, returncode2,
                         'Expected both returncodes of wait() to be equal')

@unittest.skipUnless(os.name == 'nt', 'Windows job completion-port regression')
class WindowsConcurrentWait(unittest.TestCase):
    def test_all_waiters_observe_job_completion(self):
        """One job completion must release every concurrent wait caller."""
        process = processhandler.ProcessHandlerMixin.Process(
            [sys.executable, '-c', 'import time; time.sleep(60)'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        barrier = threading.Barrier(6)
        results, errors = [], []
        workers = []
        original_get = process._process_events.get

        def synchronized_get(*args, **kwargs):
            # Make all five callers enter the job-notification branch before
            # terminating. Keep a regressed implementation bounded to2seconds.
            barrier.wait(timeout=10)
            return original_get(timeout=2)

        def wait():
            try:
                results.append(process.wait())
            except Exception as error:
                errors.append(error)

        try:
            self.assertTrue(process._job, 'Exercise real Windows job objects')
            with mock.patch.object(process._process_events, 'get', synchronized_get):
                for _ in range(5):
                    worker = threading.Thread(target=wait)
                    worker.daemon = True
                    workers.append(worker)
                    worker.start()
                barrier.wait(timeout=10)
                processhandler.winprocess.TerminateJobObject(
                    process._job, processhandler.winprocess.ERROR_CONTROL_C_EXIT)
                for worker in workers:
                    worker.join(timeout=5)
                self.assertFalse(any(worker.is_alive() for worker in workers))
            self.assertEqual(errors, [])
            self.assertEqual(len(results), 5)
            self.assertEqual(len(set(results)), 1)
            self.assertNotIn(results[0], (None, 0, processhandler.winprocess.STILL_ACTIVE))
            self.assertEqual(process.wait(), results[0])
        finally:
            barrier.abort()
            process.kill()
            for stream in (process.stdin, process.stdout, process.stderr):
                stream.close()


if __name__ == '__main__':
    unittest.main()
