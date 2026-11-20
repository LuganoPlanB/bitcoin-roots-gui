# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Exercise default-path discovery with disposable homes and owned loopback ports."""

import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile


def main():
    binary = sys.argv[1]
    with (
        tempfile.TemporaryDirectory(prefix="roots-rpc-") as directory,
        socket.socket() as listener,
        socket.socket() as offline,
    ):
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        offline.bind(("127.0.0.1", 0))
        open_port = listener.getsockname()[1]
        closed_port = offline.getsockname()[1]
        home = Path(directory) / "Local profile \u0394"
        roaming = home / "Roaming"
        local = home / "Local"
        env = os.environ | {
            "HOME": str(home),
            "APPDATA": str(roaming),
            "LOCALAPPDATA": str(local),
            "http_proxy": f"http://127.0.0.1:{closed_port}",
            "all_proxy": f"http://127.0.0.1:{closed_port}",
            "no_proxy": "",
            "NO_PROXY": "",
        }
        if os.name == "nt":
            cookie = local / "Bitcoin" / ".cookie"
        elif sys.platform == "darwin":
            cookie = home / "Library" / "Application Support" / "Bitcoin" / ".cookie"
        else:
            cookie = home / ".bitcoin" / ".cookie"

        def discover(port):
            return subprocess.run(
                [binary, str(port)], env=env, capture_output=True, encoding="utf-8", timeout=5
            )

        def expect_cookie(path):
            result = discover(open_port)
            assert result.returncode == 0, result.stderr
            assert Path(result.stdout.strip()) == path
            listener.settimeout(2)
            peer, _ = listener.accept()
            with peer:
                peer.settimeout(2)
                assert peer.recv(1) == b"", "discovery sent application data"

        assert discover(open_port).returncode == 1, "missing cookie"
        cookie.mkdir(parents=True)
        assert discover(open_port).returncode == 1, "cookie path was a directory"
        listener.setblocking(False)
        try:
            unexpected, _ = listener.accept()
        except BlockingIOError:
            pass
        else:
            unexpected.close()
            raise AssertionError("discovery contacted the listener without a regular cookie file")
        cookie.rmdir()
        # Detection uses file metadata. This fixture deliberately contains no valid cookie.
        cookie.write_bytes(b"")
        assert discover(closed_port).returncode == 1, "cookie without a listener"
        expect_cookie(cookie)
        if os.name == "nt":
            legacy = roaming / "Bitcoin" / ".cookie"
            legacy.parent.mkdir(parents=True)
            legacy.write_bytes(b"")
            expect_cookie(legacy)


if __name__ == "__main__":
    main()
