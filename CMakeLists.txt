# BigBubbleMuff — native Linux VST3 emulation of the Russian "Bubble Font" Big Muff Pi.
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# A raw Steinberg VST 3 plug-in: no JUCE, no VSTGUI, no framework.
#
# Dependencies are pinned by tag AND commit SHA (supply-chain traceability):
#   VST 3 SDK  v3.8.0_build_66  9fad9770f2ae8542ab1a548a68c1ad1ac690abe0  (MIT)
#
# Offline builds: -DFETCHCONTENT_SOURCE_DIR_VST3SDK=/home/human/third_party/vst3sdk
# reuses the pristine local checkout of that same commit instead of cloning.
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build

cmake_minimum_required(VERSION 3.25)

project(BigBubbleMuff VERSION 1.0.0 LANGUAGES C CXX)
set(BBM_BUILD_NUMBER 1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# The plug-in is a shared object.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Hidden by default: a plug-in exports its three entry points and nothing else.
# See cmake/plugin-exports.map for the part visibility alone cannot reach.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(Hardening)
include(FetchContent)

# ---- VST 3 SDK (pinned) -----------------------------------------------------
# Only the four submodules a plug-in needs: no VSTGUI (the editor draws with
# Cairo), no docs, no tutorials. SYSTEM: SDK headers are system includes, so the
# strict warning set below applies to this project's code only.
FetchContent_Declare(vst3sdk
  GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
  GIT_TAG        9fad9770f2ae8542ab1a548a68c1ad1ac690abe0  # tag v3.8.0_build_66
  GIT_SUBMODULES base cmake pluginterfaces public.sdk
  SYSTEM)

set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
# ON for the validator and moduleinfotool, which is how the bundle is checked.
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_CXX_STANDARD 20 CACHE STRING "" FORCE)
if(BBM_SANITIZE)
  # moduleinfotool is not instrumented and cannot dlopen an ASan-built module
  # ("ASan runtime does not come first"). Sanitizer builds are for the tests and
  # for the validator run under LD_PRELOAD, not for shipping.
  set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(vst3sdk)
set(VST3_SDK_DIR "${vst3sdk_SOURCE_DIR}")
if(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/vst/vsttypes.h")
  message(FATAL_ERROR "No VST 3 SDK at '${VST3_SDK_DIR}'")
endif()

# The SDK's helper functions read these DIRECTORY-scoped variables and the SDK
# sets them only in its own scope. Without public_sdk_SOURCE_DIR,
# smtg_target_add_library_main silently adds no entry point and the bundle loads
# as an empty module.
set(public_sdk_SOURCE_DIR "${VST3_SDK_DIR}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${VST3_SDK_DIR}/pluginterfaces")

# ---- The circuit engine (std only; shared by the plug-in and the tests) ------
set(BBM_DSP_SOURCES src/dsp/BigMuffPi.cpp)
add_library(bbm_dsp STATIC ${BBM_DSP_SOURCES})
target_include_directories(bbm_dsp PUBLIC src)
target_link_libraries(bbm_dsp PRIVATE BigBubbleMuff::hardening BigBubbleMuff::sanitize)
set_source_files_properties(${BBM_DSP_SOURCES} TARGET_DIRECTORY bbm_dsp
  PROPERTIES COMPILE_OPTIONS "${BBM_WARNING_FLAGS}")

# ---- The editor (Cairo + FreeType over an X11 embed window) ------------------
# Linked dynamically from the system: none of these is vendored or pinned, and
# none is copyleft (cairo LGPL-2.1/MPL-1.1, FreeType FTL, libX11 MIT).
find_package(PkgConfig REQUIRED)
pkg_check_modules(BBM_GUI REQUIRED IMPORTED_TARGET cairo cairo-ft cairo-xlib freetype2 x11)

# The art and fonts are compiled in (src/gfx/resources.h): the editor reads nothing
# from disk. The generator is a build-host tool and is never shipped.
add_executable(bbm_embed_resources tools/embed_resources.cpp)
target_compile_options(bbm_embed_resources PRIVATE ${BBM_WARNING_FLAGS})
set(BBM_RESOURCES
  img/mufffbase.png=${CMAKE_CURRENT_SOURCE_DIR}/gui/mufffbase.png
  img/dialknob.png=${CMAKE_CURRENT_SOURCE_DIR}/gui/dialknob.png
  img/footswitch_up.png=${CMAKE_CURRENT_SOURCE_DIR}/gui/footswitch_up.png
  img/footswitch_down.png=${CMAKE_CURRENT_SOURCE_DIR}/gui/footswitch_down.png
  img/onlightmuff.png=${CMAKE_CURRENT_SOURCE_DIR}/gui/onlightmuff.png
  img/offlightmuff.png=${CMAKE_CURRENT_SOURCE_DIR}/gui/offlightmuff.png
  fonts/LiberationSans-BoldItalic.ttf=${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/LiberationSans-BoldItalic.ttf
  fonts/LiberationSans-Regular.ttf=${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/LiberationSans-Regular.ttf)
set(BBM_RESOURCE_FILES "")
foreach(_res IN LISTS BBM_RESOURCES)
  string(REGEX REPLACE "^[^=]*=" "" _file "${_res}")
  list(APPEND BBM_RESOURCE_FILES "${_file}")
endforeach()
set(BBM_RESOURCES_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/resources.cpp")
add_custom_command(
  OUTPUT "${BBM_RESOURCES_CPP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND bbm_embed_resources "${BBM_RESOURCES_CPP}" ${BBM_RESOURCES}
  DEPENDS bbm_embed_resources ${BBM_RESOURCE_FILES}
  COMMENT "Embedding editor art and fonts"
  VERBATIM)

set(BBM_UI_SOURCES
  src/ui/bbmview.cpp
  src/ui/presetbar.cpp
  src/presets/presetstore.cpp
  src/platform/x11plugview.cpp
  src/gfx/canvas.cpp
  src/gfx/fontstack.cpp
  src/gfx/image.cpp)
add_library(bbm_ui STATIC ${BBM_UI_SOURCES} "${BBM_RESOURCES_CPP}")
target_include_directories(bbm_ui PUBLIC src)
target_link_libraries(bbm_ui
  PUBLIC sdk PkgConfig::BBM_GUI
  PRIVATE BigBubbleMuff::hardening BigBubbleMuff::sanitize)
set_source_files_properties(${BBM_UI_SOURCES} TARGET_DIRECTORY bbm_ui
  PROPERTIES COMPILE_OPTIONS "${BBM_WARNING_FLAGS}")

# ---- The plug-in -------------------------------------------------------------
configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

set(BBM_PLUGIN_SOURCES
  src/plugin/factory.cpp
  src/plugin/processor.cpp
  src/plugin/controller.cpp
  src/plugin/state.cpp)

smtg_add_vst3plugin(BigBubbleMuff ${BBM_PLUGIN_SOURCES})
smtg_target_configure_version_file(BigBubbleMuff)

# The Linux architecture folder must name the machine the binary is for:
# module_linux.cpp's loader looks only in <uname -m>-linux.
get_target_property(_bbm_outdir BigBubbleMuff LIBRARY_OUTPUT_DIRECTORY)
get_filename_component(_bbm_arch "${_bbm_outdir}" NAME)
if(NOT _bbm_arch STREQUAL "${CMAKE_SYSTEM_PROCESSOR}-linux")
  message(FATAL_ERROR "SDK chose bundle folder '${_bbm_arch}', expected "
                      "'${CMAKE_SYSTEM_PROCESSOR}-linux' (check CROSS_COMPILE)")
endif()

set_source_files_properties(${BBM_PLUGIN_SOURCES} TARGET_DIRECTORY BigBubbleMuff
  PROPERTIES COMPILE_OPTIONS "${BBM_WARNING_FLAGS}")
target_include_directories(BigBubbleMuff PRIVATE
  src
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(BigBubbleMuff PRIVATE
  sdk
  bbm_ui
  bbm_dsp
  BigBubbleMuff::hardening
  BigBubbleMuff::link_hardening
  BigBubbleMuff::sanitize)
# Static archives stay out of the dynamic symbol table, and the version script
# closes what visibility cannot reach (libstdc++'s weak template instantiations).
target_link_options(BigBubbleMuff PRIVATE
  -Wl,--exclude-libs,ALL
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin-exports.map")
set_property(TARGET BigBubbleMuff APPEND PROPERTY
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin-exports.map")

# ---- Tests -------------------------------------------------------------------
option(BBM_BUILD_TESTS "Build the BigBubbleMuff test suite" ON)
if(BBM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
