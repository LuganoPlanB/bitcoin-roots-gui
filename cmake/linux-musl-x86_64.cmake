# Use dyne/musl-action's CC/CXX and a separate musl dependency root.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
# The Dyne toolchain ships static runtimes, including for CMake link probes.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
if(NOT IS_ABSOLUTE "$ENV{ROOTS_GUI_MUSL_DEPS}" OR
   NOT IS_DIRECTORY "$ENV{ROOTS_GUI_MUSL_DEPS}/usr/lib/pkgconfig")
  message(FATAL_ERROR "Set ROOTS_GUI_MUSL_DEPS to the absolute musl dependency root.")
endif()
set(CMAKE_FIND_ROOT_PATH "$ENV{ROOTS_GUI_MUSL_DEPS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "$ENV{ROOTS_GUI_MUSL_DEPS}/usr/lib/pkgconfig:$ENV{ROOTS_GUI_MUSL_DEPS}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "$ENV{ROOTS_GUI_MUSL_DEPS}")
