# BigBubbleMuff — native Linux VST3 emulation of the Russian "Bubble Font" Big Muff Pi.
# Copyright (C) 2026  BigBubbleMuff contributors.
# Licensed under the GNU GPL v3 or later — see COPYING.
#
# Dependencies are pinned by tag AND commit SHA (supply-chain traceability):
#   JUCE         8.0.13  7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2
#   chowdsp_wdf  v1.0.0  36b5775555af21f0f417d2bc866ba7b4b2788614
#
# Offline builds: set -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to reuse a
# local pinned checkout instead of cloning.

cmake_minimum_required(VERSION 3.25)

project(BigBubbleMuff VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Position-independent code everywhere (the plugin is a shared object).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Reference copy of the Steinberg VST3 SDK provided by the project owner. JUCE 8
# bundles its OWN version-matched VST3 SDK (in juce_audio_processors_headless),
# and mixing a second external SDK causes ODR/redefinition clashes in the VST3
# manifest helper. We therefore build against JUCE's bundled, pinned SDK and keep
# this path only for inspection/reference. Set BBM_VST3_SDK_PATH + -DBBM_USE_EXTERNAL_VST3_SDK=ON
# only if you have verified the external SDK matches JUCE's bundled version.
set(BBM_VST3_SDK_PATH "/home/human/third_party/vst3sdk"
    CACHE PATH "Reference Steinberg VST3 SDK (not used for the build by default)")
option(BBM_USE_EXTERNAL_VST3_SDK "Build against BBM_VST3_SDK_PATH instead of JUCE's bundled SDK" OFF)

include(Hardening)
include(FetchContent)

# ---- JUCE 8.0.13 (pinned) --------------------------------------------------
# SYSTEM: treat dependency headers as system includes so their warnings do not
# trip our -Werror (CMake >= 3.25).
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2  # tag 8.0.13
  GIT_SHALLOW    TRUE
  SYSTEM)

# ---- chowdsp_wdf v1.0.0 (pinned, header-only, BSD-3) -----------------------
FetchContent_Declare(chowdsp_wdf
  GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_wdf.git
  GIT_TAG        36b5775555af21f0f417d2bc866ba7b4b2788614  # tag v1.0.0
  GIT_SHALLOW    TRUE
  SYSTEM)

FetchContent_MakeAvailable(JUCE chowdsp_wdf)

# Only override JUCE's bundled VST3 SDK if explicitly requested AND present.
if(BBM_USE_EXTERNAL_VST3_SDK)
  if(EXISTS "${BBM_VST3_SDK_PATH}/pluginterfaces")
    juce_set_vst3_sdk_path("${BBM_VST3_SDK_PATH}")
    message(STATUS "Using external VST3 SDK at ${BBM_VST3_SDK_PATH}")
  else()
    message(WARNING "External VST3 SDK not found at ${BBM_VST3_SDK_PATH}; "
                    "falling back to JUCE's bundled SDK.")
  endif()
endif()

juce_add_plugin(BigBubbleMuff
  PRODUCT_NAME              "BigBubbleMuff"
  COMPANY_NAME              "rations"
  BUNDLE_ID                 "org.bigbubblemuff.vst3"
  PLUGIN_MANUFACTURER_CODE  Bbm0
  PLUGIN_CODE               Bbmf
  FORMATS                   VST3 Standalone
  NEEDS_STANDALONE          TRUE
  VST3_CATEGORIES           Fx Distortion
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD   TRUE)

juce_generate_juce_header(BigBubbleMuff)

# ---- GUI image assets (embedded as a C array; no runtime file I/O) ----------
# Names map to BinaryData::<file>_png (e.g. gui/dialknob.png -> dialknob_png).
juce_add_binary_data(BBMBinaryData SOURCES
  gui/mufffbase.png
  gui/dialknob.png
  gui/footswitch_up.png
  gui/footswitch_down.png
  gui/onlightmuff.png
  gui/offlightmuff.png)

set(BBM_SOURCES
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  src/PresetManager.cpp
  src/ui/MuffLookAndFeel.cpp
  src/ui/FootswitchButton.cpp
  src/ui/PresetBar.cpp
  src/dsp/BigMuffPi.cpp)

target_sources(BigBubbleMuff PRIVATE ${BBM_SOURCES})

# Strict warnings/-Werror only on our own translation units.
set_source_files_properties(${BBM_SOURCES} TARGET_DIRECTORY BigBubbleMuff
  PROPERTIES COMPILE_OPTIONS "${BBM_WARNING_FLAGS}")

target_include_directories(BigBubbleMuff PRIVATE src)

target_compile_definitions(BigBubbleMuff PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0
  JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(BigBubbleMuff
  PRIVATE
    BBMBinaryData
    juce::juce_audio_utils
    juce::juce_dsp
    chowdsp::chowdsp_wdf
    BigBubbleMuff::hardening
    BigBubbleMuff::link_hardening
    BigBubbleMuff::sanitize
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

# ---- Tests -----------------------------------------------------------------
option(BBM_BUILD_TESTS "Build the BigBubbleMuff test suite" ON)
if(BBM_BUILD_TESTS)
  enable_testing()
  # The Haiku port carries its own framework-free copy of the engine; when that
  # tree is present the test suite also builds the harness that compares the two.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/haiku/source/dsp/BigMuffPi.cpp)
    set(BBM_HAIKU_TREE_PRESENT ON)
  else()
    set(BBM_HAIKU_TREE_PRESENT OFF)
  endif()
  add_subdirectory(tests)
endif()
