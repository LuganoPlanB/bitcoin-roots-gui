# Native Windows build

Use a 64-bit Windows environment with an OpenGL 3.3 driver. The tested build
configuration uses MinGW GCC from the MSYS2 UCRT64 shell. From that shell,
install the build dependencies and use an external CMake build directory:

```sh
pacman -S --needed git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,python,pkgconf,openssl,libsodium}
cmake -S . -B ../roots-gui-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../roots-gui-win --target roots-gui -j 2
cmake --install ../roots-gui-win --prefix "$(cygpath -m "$PWD/../roots-gui-win-stage")"
```

The build applies a checksum-pinned libwally portability patch during offline
source preparation. It uses the MinGW system definition of `ssize_t` and
excludes the unused CCAN test helper from the Windows production library.

Run `bin/roots-gui.exe` from the installed directory. Keep the DLLs and
`roots-gui-assets/fonts` beside it. Font lookup uses the executable location,
so Windows Explorer and other working directories work. Appearance
preferences go under `%LOCALAPPDATA%\Bitcoin Roots`. The application uses
the Windows certificate store for default HTTPS trust. An explicit trusted
CA file can still be set on the RPC profile.

The Windows desktop build has a native clipboard, file dialogs and a default
browser handoff for approved Help links. The RPC cookie picker returns a
Unicode file path. Transactions and wallet services still use the existing
RPC boundaries. The protected metadata and recovery store, transaction file
read/write adapters, Windows UI Automation, camera, power and notifications
remain outside this Windows profile.

The **Native Windows GUI** workflow runs on Windows Server x64. It checks
an authenticated loopback RPC, Unicode clipboard, native dialog cancellation,
nonuniform rendered pixels, and launch and close of the installed executable.
The runner uses Mesa software OpenGL for its graphical checks. The workflow
artifact contains the native executable, runtime DLLs, fonts and notices.
Use a desktop with an OpenGL driver to run that artifact. The workflow leaves
the software test driver outside the package.
