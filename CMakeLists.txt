cmake_minimum_required(VERSION 3.21)
project(Rayman2Fix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rayman2_disc STATIC src/disc/disc.cpp)
target_include_directories(rayman2_disc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MSVC)
  target_compile_options(rayman2_disc PRIVATE /W4)
else()
  target_compile_options(rayman2_disc PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(Rayman2Fix src/launcher/main.cpp)
target_link_libraries(Rayman2Fix PRIVATE rayman2_disc)
target_compile_definitions(Rayman2Fix PRIVATE RAYMAN2FIX_VERSION="${PROJECT_VERSION}")

# Rayman 2 itself is 32-bit. This DLL is intentionally only a benign,
# loadable placeholder until per-build injection/hooks are verified.
if(WIN32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Rayman2Fix runtime must be compiled for 32-bit Windows")
  endif()
  add_library(Rayman2FixRuntime SHARED src/runtime/runtime.cpp)
  set_target_properties(Rayman2FixRuntime PROPERTIES OUTPUT_NAME Rayman2Fix)
  target_compile_definitions(Rayman2FixRuntime PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(disc_tests tests/disc_tests.cpp)
  target_link_libraries(disc_tests PRIVATE rayman2_disc)
  add_test(NAME disc-image-parser COMMAND disc_tests)
endif()
