# Use Dyne's x86_64 musl compiler and bundled libraries, plus the X11 overlay.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

if(NOT CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER "$ENV{CC}" CACHE FILEPATH "Dyne musl C compiler")
endif()
execute_process(
  COMMAND "${CMAKE_C_COMPILER}" -print-sysroot
  RESULT_VARIABLE musl_sysroot_result
  OUTPUT_VARIABLE musl_sysroot
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
# Dyne ships its library prefix next to gcc-musl, outside GCC's libc sysroot.
set(musl_libraries "${musl_sysroot}/../../x86_64-linux-musl")
if(NOT musl_sysroot_result STREQUAL "0" OR
   NOT IS_ABSOLUTE "${musl_sysroot}" OR
   NOT EXISTS "${musl_libraries}/lib/pkgconfig/gio-2.0.pc")
  message(FATAL_ERROR "Set CC and CXX to the Dyne x86_64 musl v2.7.0 compilers.")
endif()
file(REAL_PATH "${musl_libraries}" musl_libraries)
if(NOT IS_ABSOLUTE "${ROOTS_GUI_MUSL_X11_ROOT}" OR
   NOT EXISTS "${ROOTS_GUI_MUSL_X11_ROOT}/usr/lib/pkgconfig/x11.pc")
  message(FATAL_ERROR "Set ROOTS_GUI_MUSL_X11_ROOT to the absolute X11 sysroot prepared by prepare-musl-x11.sh.")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ROOTS_GUI_MUSL_X11_ROOT)
set(CMAKE_FIND_ROOT_PATH "${musl_libraries}" "${ROOTS_GUI_MUSL_X11_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Both prefixes provide relocatable .pc files. Apply neither a common sysroot
# offset nor the host's optional pkg-config paths.
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH FALSE)
unset(ENV{PKG_CONFIG_PATH})
unset(ENV{PKG_CONFIG_SYSROOT_DIR})
set(ENV{PKG_CONFIG_LIBDIR} "${musl_libraries}/lib/pkgconfig:${ROOTS_GUI_MUSL_X11_ROOT}/usr/lib/pkgconfig:${ROOTS_GUI_MUSL_X11_ROOT}/usr/share/pkgconfig")
