# BigBubbleMuff test suite — a dependency-free runner under ctest.
# SPDX-License-Identifier: MIT

set(BBM_TEST_SOURCES
  test_main.cpp
  dsp_tests.cpp
  plugin_tests.cpp
  ui_tests.cpp
  preset_tests.cpp
  circuit_tests.cpp
  ${CMAKE_SOURCE_DIR}/src/plugin/processor.cpp
  ${CMAKE_SOURCE_DIR}/src/plugin/controller.cpp
  ${CMAKE_SOURCE_DIR}/src/plugin/state.cpp)

add_executable(bbm_tests
  ${BBM_TEST_SOURCES}
  # SDK host-side helpers the tests drive the processor with (not in any SDK lib).
  ${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp
  ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp)

set_source_files_properties(${BBM_TEST_SOURCES} TARGET_DIRECTORY bbm_tests
  PROPERTIES COMPILE_OPTIONS "${BBM_WARNING_FLAGS}")

target_include_directories(bbm_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(bbm_tests PRIVATE
  sdk
  bbm_ui
  bbm_dsp
  BigBubbleMuff::hardening
  BigBubbleMuff::sanitize)

# Preset tests write only below here, never to the user's real preset folder.
target_compile_definitions(bbm_tests PRIVATE
  BBM_TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/scratch"
  BBM_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

add_test(NAME bbm_unit_tests COMMAND bbm_tests)
set_tests_properties(bbm_unit_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAILED")

if(TARGET bbm_lv2check)
  add_test(NAME bbm_lv2_gate
    COMMAND bbm_lv2check "${BBM_LV2_SPEC_DIR}" "${BBM_LV2_BUNDLE}")
  set_tests_properties(bbm_lv2_gate PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
endif()
