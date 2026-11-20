# Bitcoin Roots GUI

[![Build and tests](https://github.com/LuganoPlanB/bitcoin-roots-gui/actions/workflows/ci.yml/badge.svg)](https://github.com/LuganoPlanB/bitcoin-roots-gui/actions/workflows/ci.yml)

A standalone desktop wallet interface for Bitcoin Roots and compatible Bitcoin
Core nodes. It connects to an existing node through authenticated RPC. This
repository builds the GUI itself; it does not build or embed a Bitcoin daemon,
the Qt GUI, or a node wallet database.

The interface includes wallet setup, receiving, transaction history, coin
controls, transaction composition, PSBT exchange, signing review, broadcast
tracking, local metadata, and protected workspace recovery. Features depend on
the connected node and available providers. The [capability ledger](docs/capabilities.md)
describes implemented and conditional surfaces; unavailable integrations remain
disabled. Private keys belong to the connected wallet or external signer.

## Build

Linux with GLFW and OpenGL over X11 is the default build path. Use CMake 3.22 or newer,
GCC 12.1+ or Clang 17+, Python 3.10+, Ninja, Git, and pkg-config. On Debian or
Ubuntu, install the native development prerequisites:

```sh
sudo apt-get install build-essential cmake ninja-build git pkg-config \
  python3 libssl-dev libsodium-dev libgl1-mesa-dev libx11-dev \
  libxext-dev libxrender-dev libxcb1-dev libglib2.0-dev
```

Clone and build from the repository root:

```sh
git clone https://github.com/LuganoPlanB/bitcoin-roots-gui.git
cd bitcoin-roots-gui
cmake -S . -B ../roots-gui-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../roots-gui-build --target roots-gui -j 2
../roots-gui-build/roots-gui
```

Build directories must be **outside this source repository**. The same commands
work from any directory with an absolute `-S /path/to/bitcoin-roots-gui` and
`-B /path/to/roots-gui-build`. No `bitcoin-roots` checkout is needed.

Reviewed dependency sources are bundled and hash-checked during configuration;
CMake does not download application dependencies. Native development libraries
must already be installed. `ccache` is used when available; pass
`-DWITH_CCACHE=OFF` to disable automatic selection. Developer tests default off.

Keep `roots-gui-assets/` beside the executable when copying a build. For a normal
installation, including runtime fonts and license notices:

```sh
cmake --install ../roots-gui-build --prefix "$HOME/.local"
```

The installed binary uses the system's OpenSSL, libsodium, and desktop libraries.
See [build details](docs/build.md) and [Linux distribution packaging](docs/linux-package.md)
for the separate relocatable package workflow.

Set `-DROOTS_GUI_SDL3=ON` for the fully static Linux SDL3 build. Use an external
musl dependency tree and the Dyne musl toolchain as shown in the
[static build instructions](docs/build.md#static-sdl3-build). Keep GLFW for
Debug developer builds and the AT-SPI2 automation.

## Test

Use a separate developer build and the bounded model suite:

```sh
cmake -S . -B ../roots-gui-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DROOTS_GUI_BUILD_TESTS=ON
cmake --build ../roots-gui-tests --target roots-gui roots_gui_model_tests -j 2
python3 -m unittest discover -s tests/project
```

`roots_gui_model_tests` builds its prerequisites and **runs** the model tests.
CI runs this suite, standalone project regressions, Python lint, and an install
smoke check. Models do not need a running node, a display, or a Bitcoin source
checkout. Optional GnuPG fixtures require the exact supported provider version.

The broader harness includes RPC fixtures, CPU interaction tests, boundary
audits, native accessibility/visual checks, and optional owned regtest nodes.
Those checks have explicit prerequisites and are not all part of routine CI.
See [the test guide](docs/test-harness.md). Use disposable regtest profiles for
node-backed tests; never point them at a personal wallet or live network.

## Releases

The [Release workflow](https://github.com/LuganoPlanB/bitcoin-roots-gui/actions/workflows/release.yml)
previews a semantic version and optionally publishes a source release from a
tested `main` commit. See [release instructions](docs/releasing.md).

## Contribute

Start with [AGENTS.md](AGENTS.md) for architecture, build/test commands, and
ownership boundaries. Useful references include the
[workspace behavior](docs/workspace-shell.md),
[connection/RPC contract](docs/server-connection.md), and
[dependency inputs](docs/build-inputs.md).

This project was extracted from the GUI developed in
[`LuganoPlanB/bitcoin-roots`](https://github.com/LuganoPlanB/bitcoin-roots).
[Provenance and qualifications](docs/provenance.md) distinguish inherited results
from validation of this standalone repository. Some design documents retain
historical development context; use this README, the build/test guides, the
current capability ledger, and executable tests for current behavior.

## License

MIT; see [COPYING](COPYING). Bundled libraries, fonts, and artwork retain their
own notices in [third_party/NOTICES.md](third_party/NOTICES.md),
[packaging/embedded-notices.txt](packaging/embedded-notices.txt), and
[assets/brand/NOTICES.md](assets/brand/NOTICES.md). Preserve those notices and the
original source attributions.
