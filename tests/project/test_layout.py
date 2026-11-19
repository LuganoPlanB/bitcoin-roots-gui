# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Standalone root, sibling staging and source-boundary regressions."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tests/boundary"),
               str(ROOT / "tests/build"), str(ROOT / "third_party")]
from audit_build import permitted_include
from prepare_test_inputs import prepare
from run_boundary import permitted_path
from support.runtime import REPOSITORY, evidence_directory


class StandaloneLayoutTest(unittest.TestCase):
    def test_sibling_input_staging_is_outside_this_repository(self):
        with tempfile.TemporaryDirectory(prefix="gui-sibling-inputs-", dir=ROOT.parent) as directory:
            result = prepare(Path(directory) / "inputs")
            self.assertEqual(result["files"], 37)

    def test_every_preparer_rejects_source_staging_without_writing(self):
        destination = ROOT / ("forbidden-staging-" + uuid.uuid4().hex)
        for script in ("prepare_build_inputs.py", "prepare_ui.py", "prepare_provider.py", "prepare_test_inputs.py"):
            with self.subTest(script=script):
                result = subprocess.run(
                    [sys.executable, str(ROOT / "third_party" / script), "--destination", str(destination)],
                    capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertRegex(result.stderr, "outside the source repository|out of source")
                self.assertFalse(destination.exists())

    def test_evidence_and_header_boundaries_use_the_project_root(self):
        self.assertEqual(REPOSITORY, ROOT)
        with self.assertRaises(ValueError):
            evidence_directory(ROOT / "forbidden-evidence")
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            self.assertTrue(permitted_include(ROOT / "domain/model.h", build))
            self.assertTrue(permitted_include(build / "inputs/generated.h", build))
            self.assertTrue(permitted_include(Path("/usr/include/stdio.h"), build))
            self.assertFalse(permitted_include(ROOT.parent / "bitcoin-roots/src/wallet/wallet.h", build))

    def test_source_scope_accepts_standalone_files_and_rejects_node_or_escaped_paths(self):
        for path in ("README.md", "AGENTS.md", "COPYING", "main.cpp", "app/commands.cpp", ".github/workflows/ci.yml"):
            with self.subTest(path=path):
                self.assertTrue(permitted_path(path))
        for path in ("src/wallet/wallet.cpp", "../outside.cpp", "/tmp/outside.cpp", "app/../outside.cpp", "app//invalid.cpp", ".git/config"):
            with self.subTest(path=path):
                self.assertFalse(permitted_path(path))

    def test_cmake_rejects_an_in_source_build_before_dependency_setup(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "CMakeLists.txt").write_text((ROOT / "CMakeLists.txt").read_text(), encoding="utf-8")
            result = subprocess.run(
                ["cmake", "-S", str(source), "-B", str(source / "build")],
                capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("outside the source repository", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
