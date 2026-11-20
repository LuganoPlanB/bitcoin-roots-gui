# Standard standalone install: binary, runtime fonts and dependency notices.
include(GNUInstallDirs)
if(WIN32)
  get_filename_component(roots_gui_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(ROOTS_GUI_WINDOWS_RUNTIME_DIR "${roots_gui_runtime_dir}" CACHE PATH "Windows compiler and dependency DLL directory")
  install(TARGETS roots-gui RUNTIME_DEPENDENCY_SET roots_gui_windows_dlls
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(RUNTIME_DEPENDENCY_SET roots_gui_windows_dlls
    DIRECTORIES "${ROOTS_GUI_WINDOWS_RUNTIME_DIR}"
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
elseif(APPLE)
  install(TARGETS roots-gui BUNDLE DESTINATION .)
else()
  install(TARGETS roots-gui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(NOT APPLE)
  install(FILES ${roots_gui_font_files}
    DESTINATION "${CMAKE_INSTALL_BINDIR}/roots-gui-assets/fonts")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  install(FILES packaging/org.bitcoinroots.RootsGui.desktop DESTINATION share/applications)
  install(FILES assets/brand/bitcoinroots-logo.svg
    DESTINATION share/icons/hicolor/scalable/apps RENAME org.bitcoinroots.RootsGui.svg)
endif()
install(FILES README.md docs/linux-package.md DESTINATION share/doc/roots-gui)
install(DIRECTORY docs/ DESTINATION share/doc/roots-gui/docs)
if(ROOTS_GUI_LINUX_PACKAGE)
  install(PROGRAMS packaging/install.py DESTINATION .)
endif()

set(notice_dir share/doc/roots-gui/licenses)
install(FILES packaging/COPYING.txt packaging/embedded-notices.txt DESTINATION "${notice_dir}")
install(FILES third_party/NOTICES.md DESTINATION "${notice_dir}" RENAME third-party.md)
install(FILES assets/brand/NOTICES.md DESTINATION "${notice_dir}" RENAME artwork.md)
if(ROOTS_GUI_LINUX_PACKAGE)
  install(FILES "${roots_gui_package_openssl_notice}" DESTINATION "${notice_dir}" RENAME openssl.txt)
  install(FILES "${roots_gui_package_sodium_notice}" DESTINATION "${notice_dir}" RENAME libsodium.txt)
endif()
install(FILES third_party/licenses/Apache-2.0.txt third_party/qr/Bytewords-LICENSE DESTINATION "${notice_dir}")
install(FILES third_party/qr/quirc/LICENSE DESTINATION "${notice_dir}" RENAME quirc.txt)
install(FILES third_party/qr/stb/LICENSE DESTINATION "${notice_dir}" RENAME stb-image.txt)
install(FILES "${roots_gui_ui_source}/imkit/LICENSE" DESTINATION "${notice_dir}" RENAME imkit.txt)
install(FILES "${roots_gui_ui_source}/imkit/THIRD_PARTY_NOTICES.md" DESTINATION "${notice_dir}" RENAME imkit-dependencies.md)
install(FILES "${roots_gui_ui_source}/imgui/LICENSE.txt" DESTINATION "${notice_dir}" RENAME imgui.txt)
if(ROOTS_GUI_SDL3)
  install(FILES "${roots_gui_ui_source}/sdl3/LICENSE.txt" DESTINATION "${notice_dir}" RENAME sdl3.txt)
else()
  install(FILES "${roots_gui_ui_source}/glfw/LICENSE.md" DESTINATION "${notice_dir}" RENAME glfw.md)
endif()
install(FILES "${roots_gui_ui_source}/curl/COPYING" DESTINATION "${notice_dir}" RENAME curl.txt)
install(FILES "${roots_gui_provider_source}/LICENSE" DESTINATION "${notice_dir}" RENAME libwally.txt)
install(FILES "${roots_gui_provider_source}/src/secp256k1/COPYING" DESTINATION "${notice_dir}" RENAME secp256k1.txt)
install(FILES "${roots_gui_provider_source}/src/ctaes/COPYING" DESTINATION "${notice_dir}" RENAME ctaes.txt)
# The per-module LICENSE entries link to these common license texts.
install(FILES "${roots_gui_provider_source}/src/ccan/licenses/BSD-MIT"
  DESTINATION "${notice_dir}" RENAME ccan-BSD-MIT.txt)
install(FILES "${roots_gui_provider_source}/src/ccan/licenses/CC0"
  DESTINATION "${notice_dir}" RENAME ccan-CC0.txt)
foreach(component IN ITEMS libXcursor libXfixes libXi libXinerama libXrandr)
  install(FILES "${roots_gui_ui_source}/x11/notices/${component}/COPYING"
    DESTINATION "${notice_dir}" RENAME "${component}.txt")
endforeach()
