# Apple Silicon build

Build the native ARM64 app on macOS 15 or later with Xcode 26 and Homebrew.
The app uses GLFW for the window and input events and Metal for rendering.

```sh
brew install cmake ninja pkgconf openssl@3 libsodium
openssl="$(brew --prefix openssl@3)"
sodium="$(brew --prefix libsodium)"
cmake -S . -B ../roots-gui-macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_PREFIX_PATH="$sodium;$openssl" -DOPENSSL_ROOT_DIR="$openssl"
cmake --build ../roots-gui-macos --target roots-gui -j 2
cmake --install ../roots-gui-macos --prefix ../roots-gui-macos-stage
open "../roots-gui-macos-stage/Bitcoin Roots.app"
```

The app bundle includes its fonts and statically links OpenSSL and libsodium.
HTTPS uses macOS certificate verification. An explicit trusted CA file in a
connection profile is still supported.

The **Apple Silicon GUI** workflow produces a ZIP containing the app and
dependency notices. The CI bundle is ad hoc signed for development use.
Developer ID signing and notarization are release steps outside this build.

Configure with `-DROOTS_GUI_BUILD_TESTS=ON` and build
`roots_gui_macos_window` to exercise three Metal frames and teardown with CTest.
This test needs a graphical session and a Metal device. On runners without a
Metal device, CTest reports it as skipped. CI also verifies the app layout,
ARM64 executable, runtime library paths, and CLI contract after extracting
the ZIP.

The macOS build uses the shared RPC and workspace code. Native clipboard, file
dialogs, and accessibility adapters are unavailable on macOS.
