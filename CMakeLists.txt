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
  src/launcher/display.cpp
  src/launcher/extraction_console.cpp
  src/launcher/game_config.cpp
  src/launcher/gameframe.cpp
  src/launcher/game_launch.cpp
  src/launcher/presentation.cpp
  src/launcher/runtime_inject.cpp
  src/launcher/patch_install.cpp
  src/launcher/startup.cpp
  src/launcher/system_inventory.cpp
  src/launcher/wine_host_reexec.cpp)
target_link_libraries(Rayman2Fix PRIVATE rayman2_disc rayman2_patch)
if(NOT WIN32)
  # Linux host-test binary uses a distinct name from the Windows launcher.
  set_target_properties(Rayman2Fix PROPERTIES OUTPUT_NAME rayman2fix_cli)
endif()
if(WIN32)
  target_link_libraries(Rayman2Fix PRIVATE dxguid shell32)
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

# Preserve the read-only Learn_10 reverse-engineering probes for explicitly
# requested developer builds, but exclude their per-frame memory scans and
# extra native script detours from normal Windows/Wine releases.
# No additional user-facing INI option is needed.
option(RAYMAN2FIX_RESEARCH_PROBES
  "Enable invasive read-only research instrumentation in the x86 runtime" OFF)
option(RAYMAN2FIX_AUDIO_TRACE
  "Build bounded audio pause/resume research trace; never suppress native calls" OFF)
option(RAYMAN2FIX_GRAPHICS_INFO_PROTO
  "Build experimental native in-menu graphics information and exact video-action guards" OFF)
option(RAYMAN2FIX_GRAPHICS_MENU_TRACE
  "Build separate passive native Resolution menu diagnostics (no mode hooks)" OFF)
option(RAYMAN2FIX_ESC_MUSIC_PROTO
  "Build explicit opt-in, experimental native ESC music compensation for Wine testing" OFF)
if(RAYMAN2FIX_GRAPHICS_INFO_PROTO AND NOT RAYMAN2FIX_GRAPHICS_MENU_TRACE)
  message(FATAL_ERROR "Native graphics info prototype requires graphics menu trace")
endif()
if(RAYMAN2FIX_ESC_MUSIC_PROTO AND NOT RAYMAN2FIX_AUDIO_TRACE)
  message(FATAL_ERROR "ESC audio prototype requires RAYMAN2FIX_AUDIO_TRACE=ON")
endif()

# Rayman 2 itself is 32-bit; runtime builds are Windows x86 only.
if(WIN32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Rayman2Fix runtime must be compiled for 32-bit Windows")
  endif()
  add_library(Rayman2FixRuntime SHARED
    src/runtime/runtime.cpp
    src/runtime/game_window_icon.cpp)
  set_target_properties(Rayman2FixRuntime PROPERTIES OUTPUT_NAME fix)
  if(RAYMAN2FIX_GRAPHICS_MENU_TRACE)
    target_sources(Rayman2FixRuntime PRIVATE
      src/runtime/graphics_menu_trace.cpp
      src/runtime/graphics_menu_controller_trace.cpp)
    target_compile_definitions(Rayman2FixRuntime PRIVATE
      RAYMAN2FIX_GRAPHICS_MENU_TRACE=1)
  endif()
  if(RAYMAN2FIX_GRAPHICS_INFO_PROTO)
    target_sources(Rayman2FixRuntime PRIVATE src/runtime/native_graphics_info_proto.cpp)
    target_compile_definitions(Rayman2FixRuntime PRIVATE
      RAYMAN2FIX_GRAPHICS_INFO_PROTO=1)
  endif()
  target_compile_definitions(Rayman2FixRuntime PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX
    RAYMAN2FIX_ESC_MUSIC_FEATURE=1
    RAYMAN2FIX_BUILD_COMMIT="${RAYMAN2FIX_BUILD_COMMIT}")
  if(RAYMAN2FIX_RESEARCH_PROBES)
    target_compile_definitions(Rayman2FixRuntime PRIVATE RAYMAN2FIX_RESEARCH_PROBES=1)
  endif()
  # Compile the guarded F248 path in normal builds. Its runtime activation
  # is controlled solely by [Features] SkipCinematics, not developer flags.
  target_compile_definitions(Rayman2FixRuntime PRIVATE RAYMAN2FIX_LEARN10_F248_FEATURE=1)
  if(RAYMAN2FIX_AUDIO_TRACE)
    target_compile_definitions(Rayman2FixRuntime PRIVATE RAYMAN2FIX_AUDIO_TRACE=1)
  endif()
  if(RAYMAN2FIX_ESC_MUSIC_PROTO)
    target_compile_definitions(Rayman2FixRuntime PRIVATE RAYMAN2FIX_ESC_MUSIC_PROTO=1)
  endif()
  target_include_directories(Rayman2FixRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

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

  # A concise installation guide is embedded for docs/README.md.
  # GitHub README.md remains at the repo root; do not copy it into the game directory.
  # Project licensing has not yet been selected (README.md); never invent one.
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_readme.rc"
    CONTENT "1105 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/docs/README.md\"\n")
  set_source_files_properties(
    "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_readme.rc"
    PROPERTIES GENERATED TRUE)
  target_sources(Rayman2Fix PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_readme.rc")

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
    src/launcher/display.cpp
    src/launcher/extraction_console.cpp
    src/launcher/game_config.cpp
    src/launcher/gameframe.cpp
    src/launcher/game_launch.cpp
    src/launcher/presentation.cpp
    src/launcher/runtime_inject.cpp
    src/launcher/patch_install.cpp
    src/launcher/startup.cpp)
  target_link_libraries(auto_flow_tests PRIVATE rayman2_disc rayman2_patch)
  if(WIN32)
    target_link_libraries(auto_flow_tests PRIVATE shell32)
  endif()
  if(WIN32)
    # Startup tests need the same embedded DLL resource as the real launcher.
    target_sources(auto_flow_tests PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_payload.rc"
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/rayman2fix_readme.rc")
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
    src/launcher/display.cpp
    src/launcher/presentation.cpp
    src/launcher/runtime_inject.cpp
    src/launcher/game_config.cpp
    src/launcher/gameframe.cpp
    src/launcher/patch_install.cpp
    src/launcher/startup.cpp)
  target_link_libraries(game_launch_tests PRIVATE rayman2_patch)
  if(WIN32)
    target_link_libraries(game_launch_tests PRIVATE shell32)
  endif()
  add_test(NAME game-launch-preflight COMMAND game_launch_tests)

  # Test all policy decisions on both Windows and Linux.
  add_executable(wine_host_audio_tests tests/wine_host_audio_tests.cpp)
  target_include_directories(wine_host_audio_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME wine-host-audio-reexec-policy COMMAND wine_host_audio_tests)

  # Non-invasive data model for the future read-only native Resolution menu.
  # No SDL3, menu hook, video mode change, or external on-screen overlay.
  add_executable(graphics_menu_info_tests tests/graphics_menu_info_tests.cpp)
  target_include_directories(graphics_menu_info_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME graphics-menu-readonly-info-model COMMAND graphics_menu_info_tests)
  # Guard against confusing original CD (0x117000) with the authenticated
  # expanded/deprotected game image (0x1D7000), without shipping game bytes.
  add_executable(graphics_menu_target_tests tests/graphics_menu_target_tests.cpp)
  target_include_directories(graphics_menu_target_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME graphics-menu-deprotected-pe-fingerprint
    COMMAND graphics_menu_target_tests)
  add_executable(menu_text_probe_tests tests/menu_text_probe_tests.cpp)
  target_include_directories(menu_text_probe_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME menu-text-handle-scoping COMMAND menu_text_probe_tests)
  add_executable(graphics_menu_controller_tests
    tests/graphics_menu_controller_model_tests.cpp)
  target_include_directories(graphics_menu_controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME graphics-menu-controller-relocation-and-state
    COMMAND graphics_menu_controller_tests)
  add_executable(native_graphics_info_policy_tests
    tests/native_graphics_info_policy_tests.cpp)
  target_include_directories(native_graphics_info_policy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME native-graphics-info-prototype-scope
    COMMAND native_graphics_info_policy_tests)
  add_executable(graphics_menu_start_policy_tests
    tests/graphics_menu_start_policy_tests.cpp)
  target_include_directories(graphics_menu_start_policy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME graphics-menu-log0-feature-startup
    COMMAND graphics_menu_start_policy_tests)
  add_executable(native_graphics_text_storage_tests
    tests/native_graphics_text_storage_tests.cpp)
  target_include_directories(native_graphics_text_storage_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME graphics-menu-video-pointer-restore
    COMMAND native_graphics_text_storage_tests)
  add_executable(native_graphics_static_text_tests
    tests/native_graphics_static_text_tests.cpp)
  target_include_directories(native_graphics_static_text_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME native-menu-verified-static-text-script-copy
    COMMAND native_graphics_static_text_tests)
  add_executable(native_video_disable_choices_tests
    tests/native_video_disable_choices_tests.cpp)
  target_include_directories(native_video_disable_choices_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME native-menu-disable-resolution-choice-copy
    COMMAND native_video_disable_choices_tests)
  add_executable(native_video_navigation_tests
    tests/native_video_navigation_tests.cpp)
  target_include_directories(native_video_navigation_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME native-menu-fixed-brightness-navigation-copy
    COMMAND native_video_navigation_tests)
  add_executable(native_video_brightness_suppression_tests
    tests/native_video_brightness_suppression_tests.cpp)
  target_include_directories(native_video_brightness_suppression_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME native-menu-readonly-no-brightness-transition
    COMMAND native_video_brightness_suppression_tests)
  add_executable(graphics_menu_cursor_actor_anchor_tests
    tests/graphics_menu_cursor_actor_anchor_tests.cpp)
  target_include_directories(graphics_menu_cursor_actor_anchor_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME graphics-menu-cursor-actor-name-fingerprint
    COMMAND graphics_menu_cursor_actor_anchor_tests)
  add_executable(graphics_menu_cursor_reference_tests
    tests/graphics_menu_cursor_reference_tests.cpp)
  target_include_directories(graphics_menu_cursor_reference_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME graphics-menu-cursor-ptr-value-candidates
    COMMAND graphics_menu_cursor_reference_tests)
  # Pure native-JFFTXT glyph placement contract: stable 4:3 canvas and
  # intentional exclusion of the original brightness control.
  add_executable(graphics_menu_glyph_layout_tests
    tests/graphics_menu_glyph_layout_tests.cpp)
  target_include_directories(graphics_menu_glyph_layout_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME graphics-menu-native-glyph-layout
    COMMAND graphics_menu_glyph_layout_tests)

  # Pure C++17 research-only ESC music shadow core, no Win32 or game
  # driver calls. Run on both Linux and Windows (even in Release/NDEBUG).
  add_executable(esc_music_shadow_cpp_tests tests/esc_music_shadow_cpp_tests.cpp)
  target_include_directories(esc_music_shadow_cpp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME esc-music-shadow-state-machine COMMAND esc_music_shadow_cpp_tests)
  # Backend adapter cannot run without an independently validated real
  # APM driver contract; unit tests use fake callbacks, never real music.
  add_executable(esc_music_mixer_bridge_tests
    tests/esc_music_mixer_bridge_tests.cpp)
  target_include_directories(esc_music_mixer_bridge_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME esc-music-mixer-adapter-contract
    COMMAND esc_music_mixer_bridge_tests)
  add_executable(esc_music_wine_prototype_tests
    tests/esc_music_wine_prototype_tests.cpp)
  target_include_directories(esc_music_wine_prototype_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME esc-music-wine-prototype-policy
    COMMAND esc_music_wine_prototype_tests)
  # Policy-only research; this does not intercept native audio.
  add_executable(esc_pre_driver_pause_tests
    tests/esc_pre_driver_pause_tests.cpp)
  target_include_directories(esc_pre_driver_pause_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME esc-pre-driver-pause-research-model
    COMMAND esc_pre_driver_pause_tests)
  # Pure synthetic flag-bit research. No game/DLL linking or runtime hooks.
  add_executable(esc_pause_only_model_tests
    tests/esc_pause_only_model_tests.cpp)
  target_include_directories(esc_pause_only_model_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME esc-pause-only-stable-voice-research
    COMMAND esc_pause_only_model_tests)
  # Correlate read-only source-table aliases using synthetic records.
  add_executable(esc_source_alias_tests
    tests/esc_source_alias_tests.cpp)
  target_include_directories(esc_source_alias_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME esc-source-sound-table-alias-research
    COMMAND esc_source_alias_tests)
  # Independent fail-closed early pending-Theme authorization model,
  # enabled only by MusicInPauseMenu (no native game/driver APIs).
  add_executable(esc_early_theme_tests tests/esc_early_theme_tests.cpp)
  target_include_directories(esc_early_theme_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME esc-early-theme-experimental-gate
    COMMAND esc_early_theme_tests)

  add_executable(fov_math_tests tests/fov_math_tests.cpp)
  target_include_directories(fov_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME widescreen-hor-plus-math COMMAND fov_math_tests)

  add_executable(hud_math_tests tests/hud_math_tests.cpp)
  target_include_directories(hud_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME centered-hud-4-by-3-math COMMAND hud_math_tests)

  # Opt-in native GLI source-frame filter; test only synthetic percentage
  # rectangles on Linux/Windows, without launching Rayman or changing INI.
  add_executable(black_frame_filter_tests tests/black_frame_filter_tests.cpp)
  target_include_directories(black_frame_filter_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME optional-black-frame-geometry COMMAND black_frame_filter_tests)
  add_executable(cinematic_black_polygon_tests
    tests/cinematic_black_polygon_tests.cpp)
  target_include_directories(cinematic_black_polygon_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME exact-native-black-polygon-callsite-guards
    COMMAND cinematic_black_polygon_tests)
  add_executable(cinematic_viewport_fill_tests
    tests/cinematic_viewport_fill_tests.cpp)
  target_include_directories(cinematic_viewport_fill_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME guarded-native-cinematic-viewport-fill
    COMMAND cinematic_viewport_fill_tests)

  add_executable(screen_camera_tests tests/screen_camera_tests.cpp)
  target_include_directories(screen_camera_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME native-4-by-3-screen-world-camera COMMAND screen_camera_tests)

  add_executable(superimposed_math_tests tests/superimposed_math_tests.cpp)
  target_include_directories(superimposed_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME screen-world-superimposed-actor-math COMMAND superimposed_math_tests)

  add_executable(intro_skip_tests tests/intro_skip_tests.cpp)
  target_include_directories(intro_skip_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME second-intro-skip-guard COMMAND intro_skip_tests)

  # Verify multi-source callbacks stay on the trusted game thread and
  # never duplicate native menu or intro observations within one frame.
  add_executable(render_poll_gate_tests tests/render_poll_gate_tests.cpp)
  target_include_directories(render_poll_gate_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME native-render-callback-fallback-gate
    COMMAND render_poll_gate_tests)

  add_executable(learn10_f248_result_scope_tests tests/learn10_f248_result_scope_tests.cpp)
  target_include_directories(learn10_f248_result_scope_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME learn10-f248-result-scope COMMAND learn10_f248_result_scope_tests)

  add_executable(scene_script_probe_tests tests/scene_script_probe_tests.cpp)
  target_include_directories(scene_script_probe_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME learn10-scene-script-readonly-probe COMMAND scene_script_probe_tests)

  add_executable(intro_gate_timeline_tests tests/intro_gate_timeline_tests.cpp)
  target_include_directories(intro_gate_timeline_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME learn10-bounded-gate-event-timeline COMMAND intro_gate_timeline_tests)

  add_executable(intro_integer_reader_tests tests/intro_integer_reader_tests.cpp)
  target_include_directories(intro_integer_reader_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME learn10-typed-integer-reader COMMAND intro_integer_reader_tests)

  if(WIN32)
    # Harmless child executable for a Win32 CreateProcessW smoke test. This
    # checks spaces in paths and explicit game working directory, without
    # launching any commercial executable.
    add_executable(test_child_process
      tests/test_child_process.cpp
      src/runtime/game_window_icon.cpp)
    target_include_directories(test_child_process PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test_child_process PRIVATE user32)
    add_dependencies(game_launch_tests test_child_process Rayman2FixRuntime)
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
