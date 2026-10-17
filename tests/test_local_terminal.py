#!/usr/bin/env python3
"""Exercise local play with one terminal, no server and no sockets."""
import fcntl
import os
from pathlib import Path
import pty
import signal
import struct
import subprocess
import termios
from test_network import ROOT, pty_read

master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 100, 0, 0))
proc = subprocess.Popen([str(ROOT / "client"), "--local"], stdin=slave, stdout=slave,
                        stderr=slave, cwd=ROOT, env={**os.environ, "TERM": "xterm-256color"})
os.close(slave)
try:
    output = pty_read(master, b"LOCAL TWO PLAYER")
    assert b"Enter Username" not in output and b"Connecting" not in output
    os.write(master, b"d\x1bOC")  # P1 D and P2 right arrow (xterm application mode).
    pty_read(master, b"White")
    os.write(master, b"m")
    pty_read(master, b"Last worm standing")
    os.write(master, b"g")
    pty_read(master, b"P pause/resume")
    os.write(master, b"p")
    pty_read(master, b"PAUSED")
    os.write(master, b"w\x1bOB+")  # Both players can queue a turn while paused.
    # The local process must never connect, listen, or spawn a server.
    for entry in Path(f"/proc/{proc.pid}/fd").iterdir():
        assert not os.readlink(entry).startswith("socket:")
    children = f"/proc/{proc.pid}/task/{proc.pid}/children"
    assert not Path(children).read_text().strip()
    # Shrinking a shared display pauses play instead of hiding either snake.
    os.write(master, b"p")
    pty_read(master, b"playing")
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    proc.send_signal(signal.SIGWINCH)
    pty_read(master, b"Both players need to see the full arena")
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 100, 0, 0))
    proc.send_signal(signal.SIGWINCH)
    output = pty_read(master, b"PAUSED")
    assert b"6 ticks/s" in output
    os.write(master, b".")
    proc.wait(timeout=3)
    assert proc.returncode == 0
finally:
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=3)
    os.close(master)
print("Local ncurses play, arrow/WASD input, resize safety and socket-free execution passed.")
