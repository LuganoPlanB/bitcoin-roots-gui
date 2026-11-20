add_executable(roots_gui_macos_window window.mm)
set_target_properties(roots_gui_macos_window PROPERTIES OBJCXX_STANDARD 20)
target_compile_options(roots_gui_macos_window PRIVATE -fobjc-arc)
target_link_libraries(roots_gui_macos_window PRIVATE
  roots_gui_platform roots_gui_ui roots_gui_core roots_gui_backends)
add_dependencies(roots_gui_macos_window roots-gui)
add_test(NAME roots_gui.macos.window COMMAND roots_gui_macos_window
  "$<TARGET_BUNDLE_CONTENT_DIR:roots-gui>/Resources/roots-gui-assets/fonts")
set_tests_properties(roots_gui.macos.window PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
