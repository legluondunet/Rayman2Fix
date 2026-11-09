cmake_minimum_required(VERSION 3.21)
project(Rayman2Fix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rayman2_disc STATIC src/disc/disc.cpp)
target_include_directories(rayman2_disc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(rayman2_crypto STATIC src/security/sha256.cpp)
target_include_directories(rayman2_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(rayman2_patch STATIC src/security/exe_patch.cpp)
target_include_directories(rayman2_patch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(rayman2_patch PUBLIC rayman2_crypto)
if(MSVC)
  target_compile_options(rayman2_disc PRIVATE /W4)
else()
  target_compile_options(rayman2_disc PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(Rayman2Fix
  src/launcher/main.cpp
  src/launcher/auto_flow.cpp
  src/launcher/extraction_console.cpp
  src/launcher/game_config.cpp
  src/launcher/gameframe.cpp
  src/launcher/game_launch.cpp
  src/launcher/patch_install.cpp
  src/launcher/startup.cpp
  src/launcher/system_inventory.cpp)
target_link_libraries(Rayman2Fix PRIVATE rayman2_disc rayman2_patch)
if(NOT WIN32)
  # Linux host-test binary must not collide with the Rayman2Fix/ install directory.
  set_target_properties(Rayman2Fix PROPERTIES OUTPUT_NAME rayman2fix_cli)
endif()
if(WIN32)
  target_link_libraries(Rayman2Fix PRIVATE dxguid)
endif()
find_package(Git QUIET)
set(RAYMAN2FIX_BUILD_COMMIT "unknown")
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE RAYMAN2FIX_GIT_RESULT
    OUTPUT_VARIABLE RAYMAN2FIX_GIT_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(RAYMAN2FIX_GIT_RESULT EQUAL 0)
    set(RAYMAN2FIX_BUILD_COMMIT "${RAYMAN2FIX_GIT_OUTPUT}")
  endif()
endif()
target_compile_definitions(Rayman2Fix PRIVATE
  RAYMAN2FIX_VERSION="${PROJECT_VERSION}"
  RAYMAN2FIX_BUILD_COMMIT="${RAYMAN2FIX_BUILD_COMMIT}"
)

# Rayman 2 itself is 32-bit. This DLL is intentionally only a benign,
# loadable placeholder until per-build injection/hooks are verified.
if(WIN32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Rayman2Fix runtime must be compiled for 32-bit Windows")
  endif()
  add_library(Rayman2FixRuntime SHARED src/runtime/runtime.cpp)
  set_target_properties(Rayman2FixRuntime PROPERTIES OUTPUT_NAME Rayman2Fix)
  target_compile_definitions(Rayman2FixRuntime PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

  # Package the project's own x86 DLL inside Rayman2Fix.exe as RCDATA.
  # Windows artifact: one EXE. GameFrame is compiled separately by CI and
  # embedded as a second RCDATA resource with its Apache license.
  # No INI, log, runtime directory, SDL3 or commercial game assets are shipped.
  # The resource file is config-specific so both Debug and Release resolve
  # their matching DLL. This DLL remains an inert bootstrap placeholder.
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_payload.rc"
    CONTENT "1101 RCDATA \"$<TARGET_FILE:Rayman2FixRuntime>\"\n")
  set_source_files_properties(
    "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_payload.rc"
    PROPERTIES GENERATED TRUE)
  target_sources(Rayman2Fix PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_payload.rc")
  add_dependencies(Rayman2Fix Rayman2FixRuntime)

  set(RAYMAN2FIX_GLIDE_DLL "" CACHE FILEPATH "Pinned Windows x86 GameFrame glide2x.dll")
  set(RAYMAN2FIX_GLIDE_LICENSE "" CACHE FILEPATH "Pinned Apache 2.0 license from GameFrame")
  set(RAYMAN2FIX_GLIDE_SHA256 "" CACHE STRING "Exact SHA256 of bundled GameFrame x86 DLL")
  if(RAYMAN2FIX_GLIDE_DLL OR RAYMAN2FIX_GLIDE_LICENSE OR RAYMAN2FIX_GLIDE_SHA256)
    if(NOT EXISTS "${RAYMAN2FIX_GLIDE_DLL}")
      message(FATAL_ERROR "GameFrame x86 DLL does not exist: ${RAYMAN2FIX_GLIDE_DLL}")
    endif()
    if(NOT EXISTS "${RAYMAN2FIX_GLIDE_LICENSE}")
      message(FATAL_ERROR "GameFrame Apache 2.0 license does not exist: ${RAYMAN2FIX_GLIDE_LICENSE}")
    endif()
    string(LENGTH "${RAYMAN2FIX_GLIDE_SHA256}" RAYMAN2FIX_GLIDE_SHA_LEN)
    if(NOT RAYMAN2FIX_GLIDE_SHA_LEN EQUAL 64 OR
       NOT RAYMAN2FIX_GLIDE_SHA256 MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "GameFrame SHA256 must be exactly 64 lowercase hexadecimal characters")
    endif()
    file(SHA256 "${RAYMAN2FIX_GLIDE_DLL}" RAYMAN2FIX_GLIDE_ACTUAL_SHA)
    if(NOT RAYMAN2FIX_GLIDE_ACTUAL_SHA STREQUAL RAYMAN2FIX_GLIDE_SHA256)
      message(FATAL_ERROR "GameFrame glide2x.dll SHA256 does not match the supplied checksum")
    endif()
    target_compile_definitions(Rayman2Fix PRIVATE
      RAYMAN2FIX_GLIDE_SHA256="${RAYMAN2FIX_GLIDE_SHA256}")
    file(GENERATE
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_gameframe.rc"
      CONTENT "1103 RCDATA \"${RAYMAN2FIX_GLIDE_DLL}\"\n1104 RCDATA \"${RAYMAN2FIX_GLIDE_LICENSE}\"\n")
    set_source_files_properties(
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_gameframe.rc"
      PROPERTIES GENERATED TRUE)
    target_sources(Rayman2Fix PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_gameframe.rc")
  endif()

  # An owner-verified delta is optional and must never be checked into the
  # public repository or bundled in bootstrap CI artifacts. When explicitly
  # supplied for a local Windows build, it becomes RCDATA #1102 in the
  # existing *single* Rayman2Fix.exe distribution.
  set(RAYMAN2FIX_EXE_PATCH "" CACHE FILEPATH "Private SHA256-locked Rayman2 executable .r2patch")
  if(RAYMAN2FIX_EXE_PATCH)
    if(NOT EXISTS "${RAYMAN2FIX_EXE_PATCH}")
      message(FATAL_ERROR "Owner-supplied Rayman2 executable delta not found")
    endif()
    file(SIZE "${RAYMAN2FIX_EXE_PATCH}" RAYMAN2FIX_PATCH_SIZE)
    if(RAYMAN2FIX_PATCH_SIZE GREATER 524288)
      message(FATAL_ERROR "Owner-supplied executable delta exceeds 512 KiB")
    endif()
    file(GENERATE
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_exe_delta.rc"
      CONTENT "1102 RCDATA \"${RAYMAN2FIX_EXE_PATCH}\"\n")
    set_source_files_properties(
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_exe_delta.rc"
      PROPERTIES GENERATED TRUE)
    target_sources(Rayman2Fix PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_exe_delta.rc")
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(disc_tests tests/disc_tests.cpp)
  target_link_libraries(disc_tests PRIVATE rayman2_disc)
  add_test(NAME disc-image-parser COMMAND disc_tests)

  add_executable(extraction_console_tests
    tests/extraction_console_tests.cpp
    src/launcher/extraction_console.cpp)
  target_link_libraries(extraction_console_tests PRIVATE rayman2_disc)
  add_test(NAME extraction-progress-console COMMAND extraction_console_tests)

  add_executable(auto_flow_tests
    tests/auto_flow_tests.cpp
    src/launcher/auto_flow.cpp
    src/launcher/extraction_console.cpp
    src/launcher/game_config.cpp
    src/launcher/gameframe.cpp
    src/launcher/game_launch.cpp
    src/launcher/patch_install.cpp
    src/launcher/startup.cpp)
  target_link_libraries(auto_flow_tests PRIVATE rayman2_disc rayman2_patch)
  if(WIN32)
    # Startup tests need the same embedded DLL resource as the real launcher.
    target_sources(auto_flow_tests PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_payload.rc")
    add_dependencies(auto_flow_tests Rayman2FixRuntime)
    if(RAYMAN2FIX_GLIDE_DLL)
      target_compile_definitions(auto_flow_tests PRIVATE
        RAYMAN2FIX_GLIDE_SHA256="${RAYMAN2FIX_GLIDE_SHA256}")
      target_sources(auto_flow_tests PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_gameframe.rc")
    endif()
  endif()
  add_test(NAME automatic-cd-install COMMAND auto_flow_tests)

  # Pure preflight test verifies that unsupported/unpatched installations are
  # never executed, even when the rest of the CD receipt looks valid.
  add_executable(game_launch_tests
    tests/game_launch_tests.cpp
    src/launcher/game_launch.cpp
    src/launcher/gameframe.cpp
    src/launcher/patch_install.cpp
    src/launcher/startup.cpp)
  target_link_libraries(game_launch_tests PRIVATE rayman2_patch)
  add_test(NAME game-launch-preflight COMMAND game_launch_tests)

  if(WIN32)
    # Harmless child executable for a Win32 CreateProcessW smoke test. This
    # checks spaces in paths and explicit game working directory, without
    # launching any commercial executable.
    add_executable(test_child_process tests/test_child_process.cpp)
    add_dependencies(game_launch_tests test_child_process)
  endif()

  add_executable(sha256_tests tests/sha256_tests.cpp)
  target_link_libraries(sha256_tests PRIVATE rayman2_crypto)
  add_test(NAME sha256-known-vectors COMMAND sha256_tests)

  add_executable(exe_patch_tests tests/exe_patch_tests.cpp)
  target_link_libraries(exe_patch_tests PRIVATE rayman2_patch)
  add_test(NAME executable-delta-safety COMMAND exe_patch_tests)

  add_executable(patch_install_tests
    tests/patch_install_tests.cpp
    src/launcher/patch_install.cpp)
  target_link_libraries(patch_install_tests PRIVATE rayman2_patch)
  add_test(NAME original-exe-backup-safety COMMAND patch_install_tests)
endif()
