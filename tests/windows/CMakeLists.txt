add_executable(roots_gui_windows_input input.cpp)
target_compile_features(roots_gui_windows_input PRIVATE cxx_std_20)
target_compile_definitions(roots_gui_windows_input PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(roots_gui_windows_input PRIVATE roots_gui_platform roots_gui_backends)
set_target_properties(roots_gui_windows_input PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(roots_gui_windows_input roots-gui)

add_executable(roots_gui_windows_rpc ../rpc/transport_client.cpp)
target_link_libraries(roots_gui_windows_rpc PRIVATE roots_gui_core)
if(MINGW)
  target_link_options(roots_gui_windows_rpc PRIVATE -municode)
endif()

add_test(NAME roots_gui.windows.build_contract
  COMMAND "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/tests/build/command_contract.py"
    "$<TARGET_FILE:roots-gui>" "${ROOTS_GUI_VERSION}")
add_test(NAME roots_gui.windows.rpc
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/rpc.py" "$<TARGET_FILE:roots_gui_windows_rpc>")
set_tests_properties(roots_gui.windows.build_contract roots_gui.windows.rpc PROPERTIES TIMEOUT 30)
