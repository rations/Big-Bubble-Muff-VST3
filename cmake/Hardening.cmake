# Hardening.cmake — security/hardening flags exposed as an INTERFACE target.
# SPDX-License-Identifier: MIT
#
# Link the `BigBubbleMuff::hardening` target into our own code (NOT into the VST 3
# SDK's own targets, which are not -Werror clean under this warning set).

add_library(bbm_hardening INTERFACE)
add_library(BigBubbleMuff::hardening ALIAS bbm_hardening)

# Strict warnings are applied PER-FILE to our own sources only (not to the SDK's
# translation units, which are not clean under these flags). Consumers
# attach them with: set_source_files_properties(<our.cpp> PROPERTIES
#   COMPILE_OPTIONS "${BBM_WARNING_FLAGS}").
option(BBM_WERROR "Treat warnings as errors in BigBubbleMuff sources" ON)
# Note: SDK headers are consumed as SYSTEM includes (see CMakeLists), so these
# flags catch issues in OUR code without drowning in SDK noise.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(BBM_WARNING_FLAGS
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor -Wcast-align
    -Wunused -Wnull-dereference -Wformat=2 -Wuninitialized -Wfloat-equal)
  if(BBM_WERROR)
    list(APPEND BBM_WARNING_FLAGS -Werror)
  endif()
else()
  set(BBM_WARNING_FLAGS "")
endif()

# Exploit-mitigation hardening (Release-oriented; safe in all configs).
target_compile_options(bbm_hardening INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-fstack-protector-strong -fstack-clash-protection
    -fcf-protection=full>)

# _FORTIFY_SOURCE requires optimisation; only enable when optimising.
target_compile_definitions(bbm_hardening INTERFACE
  $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<NOT:$<CONFIG:Debug>>>:_FORTIFY_SOURCE=2>
  # libstdc++ bounds/precondition assertions; cheap, catches UB in containers.
  $<$<CXX_COMPILER_ID:GNU,Clang>:_GLIBCXX_ASSERTIONS>)

# Linker hardening for the final shared object.
add_library(bbm_link_hardening INTERFACE)
add_library(BigBubbleMuff::link_hardening ALIAS bbm_link_hardening)
target_link_options(bbm_link_hardening INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack>)

# Optional sanitizers for Debug builds and the test suite.
option(BBM_SANITIZE "Build with ASan+UBSan (Debug / tests)" OFF)
if(BBM_SANITIZE)
  add_library(bbm_sanitize INTERFACE)
  add_library(BigBubbleMuff::sanitize ALIAS bbm_sanitize)
  target_compile_options(bbm_sanitize INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(bbm_sanitize INTERFACE -fsanitize=address,undefined)
else()
  add_library(bbm_sanitize INTERFACE)
  add_library(BigBubbleMuff::sanitize ALIAS bbm_sanitize)
endif()
