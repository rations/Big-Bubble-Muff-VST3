# BigBubbleMuff test suite — JUCE UnitTest based console runner.
# No third-party test framework (supply-chain minimalism).

juce_add_console_app(bbm_tests PRODUCT_NAME "bbm_tests")

juce_generate_juce_header(bbm_tests)

set(BBM_TEST_SOURCES
  main.cpp
  dsp_tests.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/BigMuffPi.cpp)

target_sources(bbm_tests PRIVATE ${BBM_TEST_SOURCES})

set_source_files_properties(${BBM_TEST_SOURCES} TARGET_DIRECTORY bbm_tests
  PROPERTIES COMPILE_OPTIONS "${BBM_WARNING_FLAGS}")

target_include_directories(bbm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# NB: do NOT define JUCE_UNIT_TESTS=1 — that registers JUCE's own internal test
# suite into the runner. We only want our tests (registered via static globals).
target_compile_definitions(bbm_tests PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(bbm_tests
  PRIVATE
    juce::juce_dsp
    chowdsp::chowdsp_wdf
    BigBubbleMuff::hardening
    BigBubbleMuff::sanitize
  PUBLIC
    juce::juce_recommended_config_flags)

add_test(NAME bbm_unit_tests COMMAND bbm_tests)
set_tests_properties(bbm_unit_tests PROPERTIES
  FAIL_REGULAR_EXPRESSION "FAILED|!!! Test")

# ---- Haiku engine parity harness -------------------------------------------
# Development-host only: runs the shipping Linux engine against the JUCE-free
# engine the Haiku plug-in uses. Never packaged, never installed.
option(BBM_BUILD_HAIKU_PARITY "Build the Linux/Haiku engine parity harness"
  ${BBM_HAIKU_TREE_PRESENT})

if(BBM_BUILD_HAIKU_PARITY)
  set(BBM_HAIKU_DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../haiku/source)

  # Compiled on its own, with only the Haiku tree on the include path and NOTHING
  # linked: this object library is the standing proof that the Haiku engine needs
  # no framework. The circuit headers it shares with src/dsp are byte-identical
  # copies declaring the same namespace, so rename it here to keep the two sets of
  # definitions apart inside one binary.
  add_library(bbm_haiku_dsp OBJECT ${BBM_HAIKU_DSP_DIR}/dsp/BigMuffPi.cpp)
  target_include_directories(bbm_haiku_dsp PRIVATE ${BBM_HAIKU_DSP_DIR})
  target_compile_definitions(bbm_haiku_dsp PRIVATE bbm=bbm_haiku_circuit)
  target_compile_features(bbm_haiku_dsp PRIVATE cxx_std_20)
  set_source_files_properties(${BBM_HAIKU_DSP_DIR}/dsp/BigMuffPi.cpp
    TARGET_DIRECTORY bbm_haiku_dsp
    PROPERTIES COMPILE_OPTIONS "${BBM_WARNING_FLAGS}")

  juce_add_console_app(bbm_haiku_parity PRODUCT_NAME "bbm_haiku_parity")
  juce_generate_juce_header(bbm_haiku_parity)

  set(BBM_PARITY_SOURCES
    haiku_parity_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/BigMuffPi.cpp)

  target_sources(bbm_haiku_parity PRIVATE
    ${BBM_PARITY_SOURCES}
    $<TARGET_OBJECTS:bbm_haiku_dsp>)

  set_source_files_properties(${BBM_PARITY_SOURCES} TARGET_DIRECTORY bbm_haiku_parity
    PROPERTIES COMPILE_OPTIONS "${BBM_WARNING_FLAGS}")

  # src/ first: both trees carry a dsp/BigMuffPi.h, and this target's Linux
  # translation unit must resolve to the Linux one. The harness itself reaches the
  # Haiku header by explicit relative path, and dsp/AudioMath.h exists only under
  # the Haiku tree.
  target_include_directories(bbm_haiku_parity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${BBM_HAIKU_DSP_DIR})

  target_compile_definitions(bbm_haiku_parity PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

  target_link_libraries(bbm_haiku_parity
    PRIVATE
      juce::juce_dsp
      chowdsp::chowdsp_wdf
      BigBubbleMuff::hardening
      BigBubbleMuff::sanitize
    PUBLIC
      juce::juce_recommended_config_flags)

  add_test(NAME bbm_haiku_parity COMMAND bbm_haiku_parity)
endif()
