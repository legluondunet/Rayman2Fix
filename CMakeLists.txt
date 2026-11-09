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
  # The Windows artifact is now a single EXE. No INI, log, runtime folder,
  # game assets, GameFrame or SDL3 are added to the archive.
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
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(disc_tests tests/disc_tests.cpp)
  target_link_libraries(disc_tests PRIVATE rayman2_disc)
  add_test(NAME disc-image-parser COMMAND disc_tests)

  add_executable(auto_flow_tests
    tests/auto_flow_tests.cpp
    src/launcher/auto_flow.cpp
    src/launcher/patch_install.cpp
    src/launcher/startup.cpp)
  target_link_libraries(auto_flow_tests PRIVATE rayman2_disc rayman2_patch)
  if(WIN32)
    # Startup tests need the same embedded DLL resource as the real launcher.
    target_sources(auto_flow_tests PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_payload.rc")
    add_dependencies(auto_flow_tests Rayman2FixRuntime)
  endif()
  add_test(NAME automatic-cd-install COMMAND auto_flow_tests)

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
