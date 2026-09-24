# BigBubbleMuff for Haiku — a raw VST3 plug-in (no framework).
#
# Same circuit model as the Linux build, same GUI art, no JUCE: the DSP is the
# repository's own circuit sources compiled against nothing but the standard
# library, and the editor is written directly against the Interface Kit.
#
# This directory is MIT-licensed (see LICENSE); the Linux tree above it is
# GPL-3.0-or-later because it links JUCE. Nothing here may include a JUCE header.
#
# Builds two ways:
#   - standalone: point VST3_SDK_DIR at a Haiku-patched VST 3 SDK checkout
#     (default: ../../VST3-haiku/vst3sdk) and configure this directory directly;
#   - as a subdirectory of a project that already builds that SDK (the parent
#     must provide the SDK targets and public_sdk_SOURCE_DIR).

cmake_minimum_required(VERSION 3.25.0)

project(smtg-bigbubblemuff
    VERSION 1.0.0
    DESCRIPTION "BigBubbleMuff Big Muff Pi fuzz VST3 for Haiku"
)

if(NOT TARGET sdk)
    set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../VST3-haiku/vst3sdk"
        CACHE PATH "Haiku-patched VST 3 SDK source tree")
    if(NOT EXISTS ${VST3_SDK_DIR}/CMakeLists.txt)
        message(FATAL_ERROR
            "VST 3 SDK not found at '${VST3_SDK_DIR}' - set -DVST3_SDK_DIR=<path>")
    endif()

    # Headless SDK: core libraries plus the validator (part of the hosting
    # examples), no VSTGUI (no Haiku backend), no plug-in examples.
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "VSTGUI has no Haiku backend" FORCE)
    set(SMTG_ENABLE_WAYLAND_SUPPORT OFF CACHE BOOL "no Wayland on Haiku" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "validator" FORCE)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "plug-in examples not needed" FORCE)
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "run manually instead" FORCE)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "install manually instead" FORCE)

    add_subdirectory(${VST3_SDK_DIR} vst3sdk)

    # The SDK's plugin helper functions read these directory-scoped variables
    # (smtg_target_add_library_main silently adds no module entry point without
    # public_sdk_SOURCE_DIR); the SDK sets them only in its own scope.
    set(public_sdk_SOURCE_DIR "${VST3_SDK_DIR}/public.sdk")
    set(pluginterfaces_SOURCE_DIR "${VST3_SDK_DIR}/pluginterfaces")
endif()

# ---------------------------------------------------------------------------
# The plug-in
# ---------------------------------------------------------------------------
set(BBM_SOURCES
    source/bbmids.h
    source/bbmgeometry.h
    source/version.h
    source/bbmprocessor.h
    source/bbmprocessor.cpp
    source/bbmcontroller.h
    source/bbmcontroller.cpp
    source/bbmview.h
    source/bbmview.cpp
    source/bbmfactory.cpp
    source/dsp/AudioMath.h
    source/dsp/BigMuffPi.h
    source/dsp/BigMuffPi.cpp
    source/dsp/DiodeClipper.h
    source/dsp/Filters.h
    source/dsp/Netlist.h
    source/dsp/NoiseGate.h
    source/dsp/Oversampler.h
    source/dsp/ToneStack.h
    source/dsp/TransistorStage.h
)

smtg_add_vst3plugin(BigBubbleMuff ${BBM_SOURCES})

smtg_target_configure_version_file(BigBubbleMuff)

target_compile_features(BigBubbleMuff PUBLIC cxx_std_20)

target_include_directories(BigBubbleMuff PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source)

# Strict warnings on our own translation units only (the SDK carries its own).
# -Wshadow and -Wnon-virtual-dtor are deliberately absent: every VST3 interface
# derives from FUnknown, which has no virtual destructor by design (lifetime is
# refcounted through release()), so those two fire hundreds of times inside the
# SDK headers our sources include and would bury any real diagnostic.
set_source_files_properties(${BBM_SOURCES} TARGET_DIRECTORY BigBubbleMuff
    PROPERTIES COMPILE_OPTIONS
        "-Wall;-Wextra;-Wpedantic;-Wconversion;-Wcast-align")

# Editor art: the SAME PNGs the Linux build uses, copied straight out of the
# repository's gui/ directory. smtg_target_add_plugin_resources keys the output
# on the basename, so these land in Contents/Resources/gui/ unchanged.
smtg_target_add_plugin_resources(BigBubbleMuff
    RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../gui/mufffbase.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/../gui/dialknob.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/../gui/onlightmuff.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/../gui/offlightmuff.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/../gui/footswitch_up.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/../gui/footswitch_down.png"
    OUTPUT_SUBDIRECTORY gui
)

# Native Haiku editor: the HaikuPlugView helper header lives next to the
# Haiku-patched SDK tree (VST3-haiku/haiku). A missing checkout produces a
# confusing failure much later, so fail here with a clear message.
if(DEFINED VST3_SDK_DIR)
    set(BBM_HAIKU_SUPPORT_DIR "${VST3_SDK_DIR}/../haiku")
else()
    set(BBM_HAIKU_SUPPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../VST3-haiku/haiku")
endif()
if(NOT EXISTS "${BBM_HAIKU_SUPPORT_DIR}/haikuplugview.h")
    message(FATAL_ERROR
        "HaikuPlugView helper not found at '${BBM_HAIKU_SUPPORT_DIR}' - "
        "check out VST3-haiku next to this repository")
endif()
target_include_directories(BigBubbleMuff PRIVATE "${BBM_HAIKU_SUPPORT_DIR}")

target_link_libraries(BigBubbleMuff
    PRIVATE
        sdk
        be          # Interface Kit
        translation # BTranslationUtils
)
