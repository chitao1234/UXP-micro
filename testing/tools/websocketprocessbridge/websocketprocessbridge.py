# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from twisted.internet import protocol, reactor
from twisted.internet.error import ProcessExitedAlready
from twisted.internet.task import LoopingCall
from autobahn.twisted.websocket import WebSocketServerFactory, WebSocketServerProtocol
import psutil

import argparse
import sys
import os

# maps a command issued via websocket to running an executable with args
commands = {
    'iceserver' : [sys.executable,
                   "-u",
                   os.path.join("iceserver", "iceserver.py")]
}

class ProcessSide(protocol.ProcessProtocol):
    """Handles the spawned process (I/O, process termination)"""

    def __init__(self, socketSide):
        self.socketSide = socketSide
        self.pending = b""

    def outReceived(self, data):
        if self.socketSide:
            # A pipe callback is not a line boundary. The client parses the
            # first complete ICE configuration as one JSON websocket message.
            self.pending += data
            while b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                self.socketSide.sendMessage(line.rstrip(b"\r"), isBinary=False)

    def errReceived(self, data):
        self.outReceived(data)

    def processEnded(self, reason):
        if self.socketSide:
            if self.pending:
                self.socketSide.sendMessage(self.pending, isBinary=False)
                self.pending = b""
            self.outReceived(reason.getTraceback().encode("utf-8") + b"\n")
            self.socketSide.processGone()

    def socketGone(self):
        self.socketSide = None
        self.transport.loseConnection()
        try:
            self.transport.signalProcess("KILL")
        except ProcessExitedAlready:
            pass


class SocketSide(WebSocketServerProtocol):
    """
    Handles the websocket (I/O, closed connection), and spawning the process
    """

    def __init__(self):
        super(SocketSide, self).__init__()
        self.processSide = None

    def onMessage(self, payload, isBinary):
        if not self.processSide:
            if isBinary:
                self.sendClose(code=1003, reason="Expected a text process command")
                return
            try:
                data = payload.decode("utf-8")
            except UnicodeDecodeError:
                self.sendClose(code=1007, reason="Invalid UTF-8 process command")
                return
            if data not in commands:
                self.sendClose(code=1008, reason="Unknown process command")
                return
            self.processSide = ProcessSide(self)
            try:
                reactor.spawnProcess(self.processSide,
                                     commands[data][0],
                                     commands[data],
                                     env=os.environ)
            except Exception as error:
                print("Unable to start {}: {}".format(data, error), flush=True)
                self.sendMessage(str(error).encode("utf-8"), isBinary=False)
                self.processGone()

    def onClose(self, wasClean, code, reason):
        if self.processSide:
            self.processSide.socketGone()

    def processGone(self):
        self.processSide = None
        self.transport.loseConnection()


# Parent process could have already exited, so this is slightly racy. Only
# alternative is to set up a pipe between parent and child, but that requires
# special cooperation from the parent.
parent_process = psutil.Process(os.getpid()).parent()

def check_parent():
    """ Checks if parent process is still alive, and exits if not """
    if not parent_process.is_running():
        print("websocket/process bridge exiting because parent process is gone")
        reactor.stop()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Starts websocket/process bridge.')
    parser.add_argument("--port", type=str, dest="port", default="8191",
                    help="Port for websocket/process bridge. Default 8191.")
    args = parser.parse_args()

    parent_checker = LoopingCall(check_parent)
    parent_checker.start(1)

    bridgeFactory = WebSocketServerFactory()
    bridgeFactory.protocol = SocketSide
    reactor.listenTCP(int(args.port), bridgeFactory)
    print(("websocket/process bridge listening on port %s" % args.port))
    reactor.run()


