#!/usr/bin/env python3
"""Real TCP and ncurses PTY regressions, using only Python's standard library."""
import contextlib
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import socket
import struct
import subprocess
import termios
import time

ROOT = Path(__file__).resolve().parent.parent
MAGIC, VERSION = 0x534E4B32, 2
FRAME_SIZE = 48 + 8 * 76 + 24 * 80 * 4 + 32


def exact(sock, size):
    result = bytearray()
    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise EOFError("server closed connection")
        result.extend(chunk)
    return bytes(result)


def frame(sock):
    raw = exact(sock, FRAME_SIZE)
    keys = "magic version self state mode host winner paused tps countdown tick target".split()
    result = dict(zip(keys, struct.unpack_from("!12I", raw)))
    assert result["magic"] == MAGIC and result["version"] == VERSION
    players = []
    for i in range(8):
        values = struct.unpack_from("!11I32s", raw, 48 + 76 * i)
        player_keys = "active alive participating color length score boost pacman respawn x y name".split()
        player = dict(zip(player_keys, values))
        player["name"] = player["name"].split(b"\0")[0].decode("ascii")
        players.append(player)
    result["players"] = players
    result["map"] = struct.unpack_from("!1920i", raw, 48 + 8 * 76)
    return result


def until(sock, predicate, seconds=3):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = frame(sock)
        if predicate(result):
            return result
    raise AssertionError("expected server state was not reached")


def connect(port, name="Tester", fragments=False, commands=b""):
    sock = socket.create_connection(("127.0.0.1", port), timeout=2)
    hello = struct.pack("!II32s", MAGIC, VERSION, name.encode()[:31])
    if fragments:
        for chunk in (hello[:1], hello[1:5], hello[5:17], hello[17:]):
            sock.sendall(chunk)
            time.sleep(0.01)
        if commands:
            sock.sendall(commands)
    else:
        sock.sendall(hello + commands)
    return sock


@contextlib.contextmanager
def server():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    proc = subprocess.Popen([str(ROOT / "server"), str(port)], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        ready, _, _ = select.select([proc.stdout], [], [], 3)
        assert ready and "listening" in proc.stdout.readline(), "server failed to start"
        yield port
    finally:
        proc.terminate()
        stdout, stderr = proc.communicate(timeout=3)
        assert proc.returncode == 0, (proc.returncode, stdout, stderr)
        assert "Sanitizer" not in stderr and "runtime error" not in stderr, stderr


def test_lobby_and_game():
    with server() as port, contextlib.ExitStack() as stack:
        host = stack.enter_context(connect(port, "Host", fragments=True))
        initial = frame(host)
        assert initial["host"] == initial["self"] == 1
        guest = stack.enter_context(connect(port, "Guest", commands=b"6GMP+"))
        state = until(guest, lambda f: f["players"][1]["color"] == 6)
        assert state["state"] == 0 and state["mode"] == 0 and state["tps"] == 6
        host.sendall(b"M")
        until(guest, lambda f: f["mode"] == 1)
        host.sendall(b"M")
        until(guest, lambda f: f["mode"] == 0)
        host.sendall(b"G")
        state = until(guest, lambda f: f["state"] == 1)
        assert state["players"][0]["length"] == state["players"][1]["length"] == 8
        assert state["players"][1]["color"] == 6
        assert state["map"].count(1) == state["map"].count(2) == 8
        guest.sendall(b"P")
        paused = until(guest, lambda f: f["paused"])
        time.sleep(0.25)
        still = until(guest, lambda f: f["paused"])
        assert still["tick"] == paused["tick"] and still["countdown"] == paused["countdown"]
        assert still["map"] == paused["map"]
        guest.sendall(b"++++++++++++++++++++")
        until(guest, lambda f: f["tps"] == 20 and f["paused"])
        guest.sendall(b"P")
        state = until(guest, lambda f: f["tick"] >= 2)
        # Host disconnect transfers host ownership without resetting the round.
        host.close()
        state = until(guest, lambda f: f["host"] == 2)
        assert state["state"] == 1 and not state["players"][0]["active"]
        # Slot reuse cannot make the former host's cleanup remove a new player.
        replacement = stack.enter_context(connect(port, "Replacement"))
        state = until(guest, lambda f: f["players"][0]["active"])
        assert state["players"][0]["name"] == "Replacement" and state["host"] == 2
        assert frame(replacement)["self"] == 1
        replacement.sendall(b".")
        until(guest, lambda f: not f["players"][0]["active"])
        # Max-score deaths automatically respawn while the score round continues.
        guest.sendall(b"P")
        state = until(guest, lambda f: f["paused"])
        guest.sendall(b"P")
        until(guest, lambda f: not f["players"][1]["alive"], seconds=5)
        state = until(guest, lambda f: f["players"][1]["alive"], seconds=3)
        assert state["players"][1]["length"] == 8 and state["state"] == 1


def test_full_bad_and_slow_connections():
    with server() as port, contextlib.ExitStack() as stack:
        # A partial handshake does not reserve a player or block anyone.
        pending = stack.enter_context(socket.create_connection(("127.0.0.1", port), timeout=2))
        pending.sendall(b"S")
        bad = stack.enter_context(socket.create_connection(("127.0.0.1", port), timeout=2))
        bad.sendall(b"x" * 40)
        assert bad.recv(1) == b""
        peers = [stack.enter_context(connect(port, f"P{i}")) for i in range(8)]
        for i, peer in enumerate(peers):
            assert frame(peer)["self"] == i + 1
        extra = stack.enter_context(connect(port, "Full"))
        assert extra.recv(1) == b""
        peers[3].close()
        until(peers[0], lambda f: not f["players"][3]["active"])
        new = stack.enter_context(connect(port, "New"))
        assert frame(new)["self"] == 4
    with server() as port, contextlib.ExitStack() as stack:
        host = stack.enter_context(connect(port, "Reader"))
        frame(host)
        slow = stack.enter_context(socket.socket())
        slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        slow.connect(("127.0.0.1", port))
        slow.sendall(struct.pack("!II32s", MAGIC, VERSION, b"Slow"))
        until(host, lambda f: f["players"][1]["active"])
        host.sendall(b"G++++++++++++++++++++")
        state = until(host, lambda f: f["tick"] >= 5, seconds=3)
        assert state["players"][1]["active"]
        until(host, lambda f: not f["players"][1]["active"], seconds=9)
        host.sendall(b"P")
        until(host, lambda f: f["paused"])


def test_last_worm_standing():
    with server() as port, contextlib.ExitStack() as stack:
        host = stack.enter_context(connect(port, "Host"))
        frame(host)
        guest = stack.enter_context(connect(port, "Guest"))
        frame(guest)
        host.sendall(b"MG++++++++++++++++++++")
        until(host, lambda f: f["state"] == 1 and f["mode"] == 1)
        late = stack.enter_context(connect(port, "Spectator"))
        state = frame(late)
        assert not state["players"][2]["participating"] and not state["players"][2]["alive"]
        state = until(host, lambda f: f["state"] == 2, seconds=6)
        assert sum(p["alive"] for p in state["players"]) <= 1
        assert all(p["respawn"] == 0 for p in state["players"])
        old_tick = state["tick"]
        time.sleep(0.2)
        state = until(host, lambda f: f["state"] == 2)
        assert state["tick"] == old_tick
        host.sendall(b"MG")
        state = until(host, lambda f: f["state"] == 1 and f["mode"] == 0)
        assert all(p["alive"] and p["score"] == 0 for p in state["players"][:3])


def pty_read(fd, needle, seconds=3):
    output = bytearray()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            output.extend(os.read(fd, 65536))
            if needle in output:
                return output
    raise AssertionError(f"terminal did not show {needle!r}: {output[-500:]!r}")


def test_terminal_client():
    with server() as port:
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 100, 0, 0))
        proc = subprocess.Popen([str(ROOT / "client"), "127.0.0.1", str(port)],
                                stdin=slave, stdout=slave, stderr=slave, cwd=ROOT,
                                env={**os.environ, "TERM": "xterm-256color"})
        os.close(slave)
        try:
            pty_read(master, b"Enter Username")
            os.write(master, b"TerminalPlayer\n")
            pty_read(master, b"LOBBY")
            os.write(master, b"6")
            pty_read(master, b"Cyan")
            os.write(master, b"g")
            pty_read(master, b"WASD/arrows")
            os.write(master, b"p")
            pty_read(master, b"PAUSED")
            # Resize uses the same UI thread and preserves the shared controls.
            fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
            proc.send_signal(signal.SIGWINCH)
            pty_read(master, b"scrolling view")
            os.write(master, b".")
            proc.wait(timeout=3)
            assert proc.returncode == 0
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=3)
            os.close(master)


if __name__ == "__main__":
    test_lobby_and_game()
    test_full_bad_and_slow_connections()
    test_last_worm_standing()
    test_terminal_client()
    print("TCP framing, shared controls, respawn, host transfer, slow clients and ncurses PTY passed.")
