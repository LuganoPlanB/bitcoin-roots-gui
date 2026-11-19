# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Audit actual CMake target closures, compile inputs and linked binary boundaries."""

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

GUI = Path(__file__).resolve().parents[2]
FORBIDDEN = (
    "bitcoin_node",
    "bitcoin_wallet",
    "bitcoin_kernel",
    "bitcoin_common",
    "bitcoin_consensus",
    "bitcoin_util",
    "bitcoinqt",
    "bitcoin_ipc",
    "bitcoinkernel",
    "core_interface",
    "libbitcoin",
    "Qt6::",
    "Qt5::",
    "libQt6",
    "libQt5",
)


def permitted_libraries(libraries):
    return not any(token in library for library in libraries for token in FORBIDDEN)


def load_model(build):
    reply = build / ".cmake/api/v1/reply"
    index = json.loads(sorted(reply.glob("index-*.json"))[-1].read_text())
    model = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text())
    targets = {
        t["name"]: json.loads((reply / t["jsonFile"]).read_text())
        for t in model["configurations"][0]["targets"]
    }
    names = {t["id"]: name for name, t in targets.items()}
    return targets, names


def cache(build):
    result = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if (
            line
            and not line.startswith(("#", "//"))
            and "=" in line
            and ":" in line.split("=", 1)[0]
        ):
            key, value = line.split("=", 1)
            result[key.split(":", 1)[0]] = value
    return result


def permitted_include(path, build):
    path = path.resolve() if path.is_absolute() else (build / path).resolve()
    # Project sources, verified generated/bundled build inputs and native SDKs.
    return any(path.is_relative_to(root.resolve()) for root in (GUI, build, Path("/usr"), Path("/lib")))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--standalone", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    checks, details, unexecuted = [], {}, []

    def check(name, condition, observed=None):
        checks.append(dict(id=name, passed=bool(condition), observed=observed))

    for name in ["standalone"]:
        build = getattr(args, name)
        if build is None:
            continue
        targets, names = load_model(build)
        pending, closure = ["roots-gui"], set()
        while pending:
            target = pending.pop()
            if target in closure:
                continue
            closure.add(target)
            pending += [names[x["id"]] for x in targets[target].get("dependencies", [])]
        details[name] = dict(closure=sorted(closure))
        check(
            name + ".required-targets",
            {
                "roots-gui",
                "roots_gui_core",
                "roots_gui_ui",
                "roots_gui_platform",
                "roots_gui_provider_build",
            }
            <= closure,
        )
        for target in closure:
            allowed = (
                target == "roots-gui"
                or target.startswith("roots_gui_")
                or target in {"glfw", "libcurl_static"}
            )
            check(
                name + ".dependency." + target,
                allowed and not any(x in target for x in FORBIDDEN),
            )
            data = targets[target]
            for group in data.get("compileGroups", []):
                for include in group.get("includes", []):
                    path = Path(include["path"])
                    check(
                        name + ".include." + target + "." + str(path),
                        permitted_include(path, build),
                    )
                # libcurl legitimately uses HAVE_CONFIG_H for its own curl_config.h.
                forbidden_defines = ("ENABLE_WALLET", "QT_") + (
                    () if target == "libcurl_static" else ("HAVE_CONFIG_H",)
                )
                check(
                    name + ".definitions." + target,
                    not any(
                        d["define"].startswith(forbidden_defines)
                        for d in group.get("defines", [])
                    ),
                )
        libraries = [
            x["fragment"]
            for x in targets["roots-gui"]["link"]["commandFragments"]
            if x["role"] == "libraries"
        ]
        details[name]["linkLibraries"] = libraries
        check(name + ".linked-libraries", permitted_libraries(libraries))
        check(
            name + ".isolated-provider-secp",
            any("provider-build/src/secp256k1/" in library for library in libraries),
        )
        commands = json.loads((build / "compile_commands.json").read_text())
        # Audit the production executable closure, excluding independently built
        # test executables whose object dependencies may not exist in this build.
        source_root = Path(cache(build)["CMAKE_HOME_DIRECTORY"])
        production_sources = {
            (
                Path(s["path"])
                if Path(s["path"]).is_absolute()
                else source_root / s["path"]
            ).resolve()
            for target in closure
            for s in targets[target].get("sources", [])
            if "compileGroupIndex" in s
        }
        gui_commands = [
            x for x in commands if Path(x["file"]).resolve() in production_sources
        ]
        check(name + ".compile-coverage", len(gui_commands) >= 240, len(gui_commands))
        objects = []
        for command in gui_commands:
            argv = shlex.split(command["command"])
            objects.append(argv[argv.index("-o") + 1])
        provider_build = build / "provider-build"
        # Scope the parent dependency database to GUI objects: legacy targets may
        # also have been built in this directory and legitimately use Core headers.
        dependencies = subprocess.check_output(
            ["ninja", "-C", str(build), "-t", "deps", *objects], text=True
        )
        dependencies += subprocess.check_output(
            ["ninja", "-C", str(provider_build), "-t", "deps"], text=True
        )
        for path in sorted(
            {
                Path(line.strip())
                for line in dependencies.splitlines()
                if line.startswith("    ")
            }
        ):
            check(
                name + ".actual-header." + str(path),
                permitted_include(path, build),
            )
        for subdir in [
            build,
            build / "provider-build",
        ]:
            raw = subprocess.check_output(
                ["ninja", "-C", str(subdir), "-t", "commands"]
                + (["roots-gui"] if subdir == build else []),
                text=True,
            )
            for index, line in enumerate(raw.splitlines()):
                if " -c " in line:
                    check(
                        name + ".single-ccache." + str(subdir) + "." + str(index),
                        sum(Path(x).name == "ccache" for x in shlex.split(line)) <= 1,
                    )
        binary = build / targets["roots-gui"]["artifacts"][0]["path"]
        symbols = subprocess.check_output(["nm", "--demangle", str(binary)], text=True)
        dynamic = subprocess.check_output(["readelf", "-d", str(binary)], text=True)
        check(
            name + ".symbol-boundary",
            not re.search(
                r"\b(?:CWallet::|WalletContext::|QApplication::|QCoreApplication::|interfaces::Chain::|node::NodeContext::)",
                symbols,
            ),
        )
        check(
            name + ".dynamic-boundary",
            not any(x in dynamic for x in ["libQt", "libbitcoin"]),
        )
        check(
            name + ".native-not-initialized",
            json.loads(
                subprocess.check_output([str(binary), "--build-contract"], text=True)
            )["native_initialized"]
            is False,
        )
    report = dict(
        executed=len(checks),
        passed=sum(x["passed"] for x in checks),
        failed=sum(not x["passed"] for x in checks),
        skipped=0,
        checks=checks,
        details=details,
        unexecutedProfiles=unexecuted,
    )
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps({k: report[k] for k in ["executed", "passed", "failed", "skipped"]})
    )
    return bool(report["failed"] or not checks)


if __name__ == "__main__":
    raise SystemExit(main())
