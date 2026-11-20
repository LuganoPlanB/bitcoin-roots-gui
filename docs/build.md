# Building the standalone GUI

The repository root is the CMake source directory. No Bitcoin source tree,
Core libraries, Boost or Qt are part of this build. Linux X11/OpenGL is the
default desktop build. See the native guides for [Windows x64](windows-build.md)
and [Apple Silicon](macos-build.md).

## Prerequisites and normal build

Use CMake >=3.22, Ninja, GCC >=12.1 or Clang >=17 with C++20, Python >=3.10,
Git and pkg-config. Native SDKs are OpenSSL, libsodium >=1.0.18, GLib/GIO >=2.66,
Xlib, XCB, X Shape, XRender and OpenGL. See the Debian/Ubuntu package command in
[README](../README.md). A desktop launch needs the corresponding runtime
libraries, an X11 session and a working OpenGL driver.

```sh
cmake -S . -B ../roots-gui-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../roots-gui-build --target roots-gui -j 2
../roots-gui-build/roots-gui --version
../roots-gui-build/roots-gui --build-contract
../roots-gui-build/roots-gui
```

Both sibling and arbitrary external build directories work. From another
working directory, use `cmake -S /absolute/bitcoin-roots-gui -B /absolute/build`.
Builds within the source repository are rejected with an actionable diagnostic.
Dependencies and evidence must not become source files.

Configuration verifies and extracts the pinned archives under `third_party/`
into the build directory. It does not download sources or read sibling
checkouts. Existing staging is reverified; modified content fails instead of
being adopted. See [the input inventory](build-inputs.md) for pins and licenses.
The public-data provider is built in an isolated CMake subprocess with the
selected compiler/toolchain and at most two parallel jobs.

ccache is selected when present unless explicitly disabled with
`-DWITH_CCACHE=OFF` or a compiler launcher is supplied. Linking is not cached.
Keep incompatible compiler, instrumentation and configuration variants in
separate build directories.

## Static SDL3 build

The optional `ROOTS_GUI_SDL3=ON` build uses SDL3 for the window, input events
and software rendering over X11. Software rendering allows the static musl
executable to run without loading GPU driver libraries. It still needs an X11
server, the adjacent font assets, and a system CA store for HTTPS connections.
Desktop settings or providers that depend on loadable GIO modules may be
unavailable in the static variant. Use the GLFW build for the existing Debug
and AT-SPI2 test harness.

The **sdl3-static** CI job provisions
[Dyne musl v2.7.0](https://github.com/dyne/musl/releases/tag/v2.7.0) for x86-64
through [dyne/musl-action](https://github.com/dyne/musl-action). The build links
the toolchain's GLib, GIO, LibreSSL and libsodium archives. SDL3 is built from the
project's pinned sources with X11 enabled. The SDL3 archive in Dyne v2.7.0
omits X11.

To reproduce the build locally, set `CC` and `CXX` to the absolute paths to
`x86_64-linux-musl-gcc` and `x86_64-linux-musl-g++` in the extracted release.
Use a fresh build directory when switching from another toolchain.

```sh
x11_work="$(realpath -m ../roots-gui-musl-x11)"
bash .github/scripts/prepare-musl-x11.sh "$x11_work"
cmake -S . -B ../roots-gui-sdl3 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DROOTS_GUI_SDL3=ON -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/linux-musl-x86_64.cmake" \
  -DROOTS_GUI_MUSL_X11_ROOT="$x11_work/sysroot"
cmake --build ../roots-gui-sdl3 --target roots-gui -j 2
cmake --install ../roots-gui-sdl3 --prefix "$PWD/../roots-gui-sdl3-stage"
```

The preparation script stages the X11 dependencies from Alpine's signed v3.23
repository and builds Xau from checksum-pinned source. CMake and pkg-config
search the toolchain's bundled libraries and the X11 overlay. CMake
configuration is offline. The static build uses the normal install layout.
`ROOTS_GUI_BUILD_TESTS` and `ROOTS_GUI_LINUX_PACKAGE` apply to the default
build. The SDL3 CI job checks the installed binary's ELF headers and verifies
rendered pixels and window-manager close under Xvfb.

## Install and runtime assets

```sh
cmake --install ../roots-gui-build --prefix "$HOME/.local"
"$HOME/.local/bin/roots-gui" --build-contract
```

The normal install includes the executable, `bin/roots-gui-assets/fonts`,
desktop integration on Linux, and dependency notices under
`share/doc/roots-gui/licenses`. It uses system shared libraries. Keep the fonts
beside a copied executable; font lookup is executable-relative, independent of
its working directory. `--version` and `--build-contract` do not initialize a
native window, connect to a node or open wallet metadata.

The separate `ROOTS_GUI_LINUX_PACKAGE=ON` option creates the Debian x86-64
relocatable distribution layout, including private OpenSSL/libsodium libraries.
It requires tests disabled and the distro license files. Use
[the package guide](linux-package.md); a normal CMake install does not require
that option or create a release archive.

## Developer builds

`ROOTS_GUI_BUILD_TESTS` defaults OFF. Enable it explicitly:

```sh
cmake -S . -B ../roots-gui-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DROOTS_GUI_BUILD_TESTS=ON
cmake --build ../roots-gui-tests --target roots-gui roots_gui_model_tests -j 2
python3 -m unittest discover -s tests/project
```

The model target builds its prerequisites and executes its suite. Other test
entry points and their prerequisites are in [the test guide](test-harness.md).
Tests use separate ImGui/Test Engine objects and never add test hooks to the
production executable. Test-enabled builds also export a compilation database
and CMake file-API model for boundary audits.

| Target | Responsibility |
| --- | --- |
| `roots_gui_core` | Domain, RPC/transport, public-data protocols and local storage |
| `roots_gui_ui` | Production drawing, workspace/recovery orchestration and ImKit |
| `roots_gui_platform` | Native host, input, DPI/fonts and accessibility |
| `roots-gui` | Executable and explicit diagnostic dispatch |
| `roots_gui_provider_build` / `roots_gui_protocol` | Isolated bundled public-data library and its interface |

Connecting to a wallet is a runtime choice. Node-backed tests accept explicit
external daemon binaries; they do not turn the daemon into a build dependency.
