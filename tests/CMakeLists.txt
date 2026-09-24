# BigBubbleMuff test suite — a dependency-free runner under ctest.
# SPDX-License-Identifier: MIT

set(BBM_TEST_SOURCES
  test_main.cpp
  dsp_tests.cpp
  plugin_tests.cpp
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
  bbm_dsp
  BigBubbleMuff::hardening
  BigBubbleMuff::sanitize)

add_test(NAME bbm_unit_tests COMMAND bbm_tests)
set_tests_properties(bbm_unit_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAILED")
