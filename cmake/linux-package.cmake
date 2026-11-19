if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_CROSSCOMPILING OR ROOTS_GUI_BUILD_TESTS)
  message(FATAL_ERROR "The Linux distribution requires a standalone x86-64 Linux build with tests disabled")
endif()

# Bundle only the application-level TLS and password-protection shared objects.
# X11, GL and its driver stack, libc, and desktop libraries come from the OS.
find_package(OpenSSL 3 REQUIRED)
find_library(roots_gui_package_sodium NAMES libsodium.so.23 REQUIRED)
foreach(library IN ITEMS "${OPENSSL_SSL_LIBRARY}" "${OPENSSL_CRYPTO_LIBRARY}" "${roots_gui_package_sodium}")
  file(REAL_PATH "${library}" resolved_library)
  get_filename_component(library_name "${library}" NAME)
  if(library STREQUAL OPENSSL_SSL_LIBRARY)
    set(library_name libssl.so.3)
  elseif(library STREQUAL OPENSSL_CRYPTO_LIBRARY)
    set(library_name libcrypto.so.3)
  endif()
  install(FILES "${resolved_library}" DESTINATION lib/roots-gui RENAME "${library_name}")
endforeach()
find_file(roots_gui_package_openssl_notice copyright
  PATHS /usr/share/doc/libssl3t64 NO_DEFAULT_PATH REQUIRED)
find_file(roots_gui_package_sodium_notice copyright
  PATHS /usr/share/doc/libsodium23 NO_DEFAULT_PATH REQUIRED)

set_target_properties(roots-gui PROPERTIES INSTALL_RPATH "$ORIGIN/../lib/roots-gui")
