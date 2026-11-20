# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Check the packaged binary on Windows with a real native window and close event."""

import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def find_window(user32, pid):
    windows = []
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    @callback_type
    def visit(window, _data):
        owner = w.DWORD()
        user32.GetWindowThreadProcessId(window, c.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(window):
            name = c.create_unicode_buffer(256)
            user32.GetWindowTextW(window, name, len(name))
            if name.value == "Bitcoin Roots":
                windows.append(window)
        return True

    user32.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user32.EnumWindows(visit, 0)
    return windows[0] if windows else None


def main():
    stage, mesa = map(Path, sys.argv[1:3])
    user32 = c.WinDLL("user32", use_last_error=True)
    user32.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user32.IsWindowVisible.argtypes = [w.HWND]
    user32.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user32.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    with tempfile.TemporaryDirectory(prefix="roots-win-") as scratch:
        work = Path(scratch)
        package = work / "Roots \u0394"
        shutil.copytree(stage, package)
        binary = package / "bin" / "roots-gui.exe"
        cwd = work / "Unrelated working directory"
        cwd.mkdir()
        env = os.environ | {
            "LOCALAPPDATA": str(work / "profile"),
            "PATH": str(Path(os.environ["SystemRoot"]) / "System32"),
        }
        subprocess.run(
            [sys.executable, "-B", str(Path(__file__).parents[1] / "build" / "command_contract.py"), str(binary)],
            env=env,
            cwd=cwd,
            timeout=15,
            check=True,
        )
        # Windows CI has no GPU driver guarantee. Mesa is test-only and stays
        # in this disposable copy, outside the uploaded Windows package.
        shutil.copy2(mesa / "opengl32.dll", binary.parent)
        env["PATH"] += os.pathsep + str(mesa)
        env["GALLIUM_DRIVER"] = "llvmpipe"
        app = subprocess.Popen([str(binary)], env=env, cwd=cwd)
        try:
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline and app.poll() is None:
                window = find_window(user32, app.pid)
                if window:
                    assert user32.PostMessageW(window, 0x0010, 0, 0), "WM_CLOSE failed"
                    assert app.wait(timeout=8) == 0, "Windows application failed to close"
                    print("PASS relocated Unicode Windows installation launches and handles WM_CLOSE")
                    return
                time.sleep(0.05)
            raise RuntimeError(f"installed Windows desktop window did not appear, exit={app.poll()}")
        finally:
            if app.poll() is None:
                app.kill()
                app.wait(timeout=5)


if __name__ == "__main__":
    main()
