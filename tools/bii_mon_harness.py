#!/usr/bin/env python3
"""Persistent PTY harness for the mon-enabled BasiliskII.

Runs BasiliskII on a pty (so cxmon's readline gets a tty), streaming all
output to LOG and taking commands on a control FIFO:
  - line "__SIGINT__" -> send SIGINT to BasiliskII (enters mon)
  - any other line    -> written to BasiliskII/mon stdin

Drive it from the shell:
  printf '__SIGINT__\n' > /tmp/bii.ctl     # break into mon (also prints A5)
  printf 'm 12340 12380\n' > /tmp/bii.ctl   # a mon command
  tail -40 /tmp/bii.log                      # read output
Child pid is in /tmp/bii.pid.
"""
import os, pty, select, signal, sys

BII = os.environ.get("BII", os.path.expanduser("~/macemu-mon/BasiliskII/src/Unix/BasiliskII"))
LOG, CTL, PIDF = "/tmp/bii.log", "/tmp/bii.ctl", "/tmp/bii.pid"

if not os.path.exists(CTL):
    os.mkfifo(CTL)
pid, fd = pty.fork()
if pid == 0:
    os.environ["SDL_VIDEODRIVER"] = "x11"
    os.environ.setdefault("DISPLAY", ":0")
    os.execv(BII, [BII])
    os._exit(127)

open(PIDF, "w").write(str(pid))
log = open(LOG, "wb", buffering=0)
# O_RDWR so the FIFO never sees EOF (harness holds a write end too)
ctl = os.open(CTL, os.O_RDWR)
cbuf = b""
while True:
    try:
        r, _, _ = select.select([fd, ctl], [], [], 1.0)
    except (OSError, InterruptedError):
        break
    if fd in r:
        try:
            d = os.read(fd, 8192)
        except OSError:
            break
        if not d:
            break
        log.write(d)
    if ctl in r:
        cbuf += os.read(ctl, 8192)
        while b"\n" in cbuf:
            line, cbuf = cbuf.split(b"\n", 1)
            if line == b"__SIGINT__":
                os.kill(pid, signal.SIGINT)
            else:
                os.write(fd, line + b"\n")
try:
    os.waitpid(pid, 0)
except OSError:
    pass
