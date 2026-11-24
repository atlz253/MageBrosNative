cmake_minimum_required(VERSION 3.20)
# The project version is the single source of the program's version number.
project(MageBrosNative VERSION 0.4.47 LANGUAGES C CXX)

# Android is built by Gradle/NDK: it has no desktop GL and no separate install step.
if(ANDROID)
  set(MBN_GLES ON CACHE BOOL "" FORCE)
  set(MBN_SDL_FROM_SOURCE OFF CACHE BOOL "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------------------------
# mbn_core: reading the user's MageBros.exe (PE resources, bitmaps, sounds, .x models, levels).
# No SDL dependency, so the tools and tests build anywhere.
set(MBN_CORE_SOURCES
  src/core/benchmark_level.cpp src/core/game_files.cpp src/core/image.cpp src/core/import_names.cpp
  src/core/level.cpp src/core/log.cpp src/core/touch_curve.cpp src/core/touch_layout.cpp
  src/core/pe_resources.cpp src/core/sha256.cpp src/core/wave.cpp src/core/xfile.cpp)
add_library(mbn_core STATIC ${MBN_CORE_SOURCES})
target_include_directories(mbn_core PUBLIC src)

if(MSVC)
  target_compile_options(mbn_core PUBLIC /W3 /utf-8 /fp:precise)
  target_compile_definitions(mbn_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  # the original game was built with x87/SSE floats in program order; no FMA contraction
  target_compile_options(mbn_core PUBLIC -Wall -Wno-unused-parameter -Wno-sign-compare -ffp-contract=off)
endif()
if(MINGW)
  # MinGW-w64 / llvm-mingw: link the C++ runtime (libstdc++ or libc++, libunwind, winpthread)
  # into every exe, so they run without extra DLLs next to them.
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

# MageBrosAssets: inventory, self-test and asset export for the user's own copy.
if(NOT ANDROID)
add_executable(MageBrosAssets tools/mbn_assets.cpp)
target_link_libraries(MageBrosAssets PRIVATE mbn_core)

# Tests that need no game files (the format readers on small hand-written inputs).
enable_testing()
add_executable(mbn_tests tests/format_tests.cpp)
target_link_libraries(mbn_tests PRIVATE mbn_core)
add_test(NAME formats COMMAND mbn_tests)

# The Android import's file-name rules, kept in the shared core so the host can test them.
add_executable(mbn_import_tests tests/import_tests.cpp)
target_link_libraries(mbn_import_tests PRIVATE mbn_core)
add_test(NAME import_names COMMAND mbn_import_tests)

# The touch overlay's stick curve, coordinate mapping and hit rules.
add_executable(mbn_touch_tests tests/touch_tests.cpp)
target_link_libraries(mbn_touch_tests PRIVATE mbn_core)
add_test(NAME touch_controls COMMAND mbn_touch_tests)

# The generated benchmark level: valid rows, a dense mix and buildable geometry, with no game
# files needed. It is the first half of the R36S performance acceptance (docs/…).
add_executable(mbn_benchmark_tests tests/benchmark_tests.cpp src/game/XMapGeometry.cpp src/framework/d3dutil.cpp)
target_link_libraries(mbn_benchmark_tests PRIVATE mbn_core)
add_test(NAME benchmark_level COMMAND mbn_benchmark_tests)

# The benchmark report's summary math and JSON escaping (no SDL, no game).
add_executable(mbn_benchmark_stats_tests tests/benchmark_stats_tests.cpp src/framework/benchmark_stats.cpp)
target_include_directories(mbn_benchmark_stats_tests PRIVATE src)
add_test(NAME benchmark_stats COMMAND mbn_benchmark_stats_tests)

# The draw-only culling rule used by the campaign fragment runs (no SDL, no game files).
add_executable(mbn_fragment_tests tests/fragment_tests.cpp)
target_include_directories(mbn_fragment_tests PRIVATE src)
add_test(NAME fragment_culling COMMAND mbn_fragment_tests)

# The bundled TimGM6mb bank, loaded through the game's TinySoundFont build. No game files or
# sound device are needed: it only proves that the packaged bank is readable and renders notes.
add_executable(mbn_soundfont_tests tests/soundfont_tests.cpp)
target_include_directories(mbn_soundfont_tests PRIVATE third_party)
target_compile_definitions(mbn_soundfont_tests PRIVATE
  MBN_SOUNDFONT_PATH="${CMAKE_SOURCE_DIR}/third_party/soundfonts/TimGM6mb.sf2")
if(NOT MSVC)
  target_link_libraries(mbn_soundfont_tests PRIVATE m)
endif()
add_test(NAME soundfont COMMAND mbn_soundfont_tests ${CMAKE_SOURCE_DIR}/third_party/soundfonts/TimGM6mb.sf2)

# macOS: the platform's General MIDI synthesizer, which plays the music when no SoundFont is
# installed. It renders directly, so no audio output device is needed.
if(APPLE)
  add_executable(mbn_synth_tests tests/synth_tests.cpp)
  target_link_libraries(mbn_synth_tests PRIVATE
    "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio")
  add_test(NAME synth COMMAND mbn_synth_tests)
  set_tests_properties(synth PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The level geometry builder, checked against all 40 levels of the user's copy:
#   mbn_map_tests --game PATH   (added to ctest when configured with -DMBN_GAME_EXE=PATH)
add_executable(mbn_map_tests tests/map_tests.cpp src/game/XMapGeometry.cpp src/framework/d3dutil.cpp)
target_link_libraries(mbn_map_tests PRIVATE mbn_core)
set(MBN_GAME_EXE "" CACHE FILEPATH "MageBros.exe for the tests that need the game")
# Player 1's movement on all 40 levels, run without a renderer.
add_executable(mbn_hero_tests tests/hero_tests.cpp tests/jmodel_stub.cpp src/game/XHero.cpp src/game/XMath.cpp src/game/XCollideBox.cpp
  src/game/XMapCollision.cpp src/game/XMapGeometry.cpp src/framework/d3dutil.cpp)
target_link_libraries(mbn_hero_tests PRIVATE mbn_core)
if(MBN_GAME_EXE)
  add_test(NAME map_geometry COMMAND mbn_map_tests --game ${MBN_GAME_EXE})
  add_test(NAME hero_movement COMMAND mbn_hero_tests --game ${MBN_GAME_EXE})
endif()
endif()  # NOT ANDROID (tools and tests)

# ---------------------------------------------------------------------------------------------
# ColDet, the collision library the original game was built with (LGPL-2.0-or-later, its own
# license in third_party/coldet). Built from its published source when that folder is present.
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/coldet/CMakeLists.txt)
  add_subdirectory(third_party/coldet)
  if(NOT ANDROID)
    add_executable(mbn_collision_tests tests/collision_tests.cpp)
    target_link_libraries(mbn_collision_tests PRIVATE coldet)
    add_test(NAME collision COMMAND mbn_collision_tests)
    install(TARGETS mbn_collision_tests RUNTIME DESTINATION .)
  endif()
endif()

# ---------------------------------------------------------------------------------------------
# The game itself (SDL3 + OpenGL 3.3). The engine layers and the reconstructed game code are
# being brought up module by module; see docs/ROADMAP.md. Until the game logic lands it opens
# the asset viewer.
option(MBN_BUILD_GAME "Build the MageBrosNative game executable" ON)
option(MBN_BUILD_R36S_PROBE "Build the R36S platform probe (uses the game's SDL3)" OFF)
# Temporary diagnostics: log texture sizes and GL errors while porting the renderer.
option(MBN_TEXTURE_DIAG "Log texture uploads and GL errors" OFF)
if(MBN_TEXTURE_DIAG)
  add_compile_definitions(MBN_TEXTURE_DIAG=1)
endif()
# The renderer compiled for OpenGL ES 3.0 (the R36S, and any GLES-only Linux device).
option(MBN_GLES "Build the renderer for OpenGL ES 3.0" OFF)
if(MBN_GLES)
  add_compile_definitions(MBN_GLES=1)
endif()
option(MBN_FETCH_SDL "Download and build SDL3 if it is not installed" ON)
# Packaging and the R36S build must not pick up whatever SDL3 the host happens to have:
# this forces the pinned source build even when a system package is installed.
option(MBN_SDL_FROM_SOURCE "Build the pinned SDL3 from source even if a system one is installed" OFF)

if(MBN_BUILD_GAME)
  if(ANDROID)
    # Gradle fetches the pinned SDL3 source (android/fetch-sdl.sh); the same tree supplies the
    # Java shim the activity is built against. A shared SDL3 is required on Android.
    if(NOT MBN_SDL_SOURCE_DIR)
      set(MBN_SDL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android/app/jni/SDL" CACHE PATH "SDL3 source for Android")
    endif()
    if(NOT EXISTS "${MBN_SDL_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR "SDL3 source not found at ${MBN_SDL_SOURCE_DIR}. Run android/fetch-sdl.sh first.")
    endif()
    message(STATUS "SDL3: building from ${MBN_SDL_SOURCE_DIR}")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${MBN_SDL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/sdl")
  else()
    if(NOT MBN_SDL_FROM_SOURCE)
      find_package(SDL3 CONFIG QUIET)
    endif()
    if(SDL3_FOUND)
      # say which SDL the build will use: the platform ports differ in how they get it
      message(STATUS "SDL3: system package at ${SDL3_DIR}")
    endif()
    if(NOT SDL3_FOUND)
      if(MBN_FETCH_SDL)
        message(STATUS "SDL3: building release-3.4.18 from source")
        include(FetchContent)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
          GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
          GIT_TAG release-3.4.18
          GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(SDL3)
      else()
        message(FATAL_ERROR "SDL3 not found. Install it or enable MBN_FETCH_SDL.")
      endif()
    endif()
  endif()

  # the engine (MIT, from Hopmon Native) and, as it lands, the reconstructed game code
  file(GLOB_RECURSE MBN_GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/gfx/*.cpp ${CMAKE_SOURCE_DIR}/src/audio/*.cpp
    ${CMAKE_SOURCE_DIR}/src/input/*.cpp ${CMAKE_SOURCE_DIR}/src/ui/*.cpp
    ${CMAKE_SOURCE_DIR}/src/framework/*.cpp ${CMAKE_SOURCE_DIR}/src/game/*.cpp)
  # macOS writes AppleDouble "._name" files onto FAT cards and network shares; they are not
  # source, and a compiler that picks them up fails with a screenful of nonsense
  list(FILTER MBN_GAME_SOURCES EXCLUDE REGEX "/\\._[^/]*$")
  list(APPEND MBN_GAME_SOURCES
    src/main.cpp src/core/assets.cpp src/core/app_paths.cpp src/core/config.cpp src/core/mods.cpp
    src/core/platform_actions.cpp src/core/saves.cpp src/core/third_party_impl.cpp)
  if(ANDROID)
    # The launcher's verification call lives in libmain.so as well.
    list(APPEND MBN_GAME_SOURCES src/android/verify_jni.cpp)
  endif()
  if(ANDROID)
    # SDL's Android activity loads "main" and calls the SDL_main this shared library exports.
    add_library(main SHARED ${MBN_GAME_SOURCES})
    set(MBN_GAME_TARGET main)
  elseif(WIN32)
    # the icon and version information (res/magebros_native.rc)
    add_executable(MageBrosNative WIN32 ${MBN_GAME_SOURCES} res/magebros_native.rc)
    set(MBN_GAME_TARGET MageBrosNative)
    # the resource compiler doesn't track header changes: rebuild the version info when it changes
    set_source_files_properties(res/magebros_native.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/generated/version.h)
    if(MINGW)
      # windres looks for the icon relative to the build folder, so point it at res/ as well
      set_property(SOURCE res/magebros_native.rc APPEND PROPERTY INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/res)
    endif()
  else()
    if(APPLE)
      # a bundle: double-clickable, and it keeps the executable where SDL's base path expects it
      add_executable(MageBrosNative MACOSX_BUNDLE ${MBN_GAME_SOURCES})
    else()
      add_executable(MageBrosNative ${MBN_GAME_SOURCES})
    endif()
    set(MBN_GAME_TARGET MageBrosNative)
  endif()
  configure_file(res/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)
  if(APPLE)
    set_target_properties(${MBN_GAME_TARGET} PROPERTIES
      MACOSX_BUNDLE_BUNDLE_NAME MageBrosNative
      MACOSX_BUNDLE_GUI_IDENTIFIER com.magebros.native
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/res/Info.plist.in)
    # Music through the platform's General MIDI synthesizer (the Apple DLS synth), used when no
    # SoundFont is installed.
    target_link_libraries(${MBN_GAME_TARGET} PRIVATE
      "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio")
  endif()
  target_include_directories(${MBN_GAME_TARGET} PRIVATE src third_party ${CMAKE_BINARY_DIR}/generated)
  target_link_libraries(${MBN_GAME_TARGET} PRIVATE mbn_core)
  if(TARGET coldet)
    target_link_libraries(${MBN_GAME_TARGET} PRIVATE coldet)
  endif()
  if(TARGET SDL3::SDL3)
    target_link_libraries(${MBN_GAME_TARGET} PRIVATE SDL3::SDL3)
  else()
    target_link_libraries(${MBN_GAME_TARGET} PRIVATE SDL3::SDL3-static)
  endif()
  if(WIN32)
    target_link_libraries(${MBN_GAME_TARGET} PRIVATE winmm)
  elseif(ANDROID)
    target_link_libraries(${MBN_GAME_TARGET} PRIVATE m)
    # 16 KB page-size devices (Android 15 and newer) are part of the supported range.
    target_link_options(${MBN_GAME_TARGET} PRIVATE
      "-Wl,-z,max-page-size=16384"
      "-Wl,--undefined=Java_com_magebros_game_GameFilesActivity_nativeVerifyExecutable"
      "-Wl,--undefined=Java_com_magebros_game_GameFilesActivity_nativeTargetName"
      "-Wl,--undefined=Java_com_magebros_game_MageBrosActivity_nativeBack")
  elseif(NOT APPLE)
    target_link_libraries(${MBN_GAME_TARGET} PRIVATE m pthread)
    # Keep the release tarball independent of the builder's GLIBCXX/libgcc versions.
    target_link_options(${MBN_GAME_TARGET} PRIVATE -static-libstdc++ -static-libgcc)
  endif()
  if(MSVC)
    # main() entry point in a Windows GUI program
    target_link_options(${MBN_GAME_TARGET} PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(NOT ANDROID)
    if(APPLE)
      install(TARGETS MageBrosNative BUNDLE DESTINATION .)
    else()
      install(TARGETS MageBrosNative RUNTIME DESTINATION .)
    endif()
  endif()

  # The R36S platform probe: does SDL3's KMSDRM video, GLES context, ALSA and gamepad work on
  # this device? A development tool, never packaged with the game.
  if(MBN_BUILD_R36S_PROBE AND NOT ANDROID)
    add_executable(mbn_r36s_probe tools/r36s/probe.cpp src/gfx/gl.cpp src/core/log.cpp)
    target_include_directories(mbn_r36s_probe PRIVATE src third_party)
    target_link_libraries(mbn_r36s_probe PRIVATE mbn_core)
    if(TARGET SDL3::SDL3)
      target_link_libraries(mbn_r36s_probe PRIVATE SDL3::SDL3)
    else()
      target_link_libraries(mbn_r36s_probe PRIVATE SDL3::SDL3-static)
    endif()
    if(NOT WIN32 AND NOT APPLE)
      target_link_libraries(mbn_r36s_probe PRIVATE m pthread)
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------------------------
# Release package: `cmake --build build --config Release --target package` (or make_release.bat)
# Android is packaged by Gradle, not by CMake/CPack.
if(NOT ANDROID)
install(TARGETS MageBrosAssets mbn_tests mbn_soundfont_tests RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
# The bundled General MIDI bank: beside the program on Windows/Linux, in the bundle's Resources
# on macOS. The game looks for TimGM6mb.sf2 for its Built-in synth and its automatic fallback.
if(APPLE)
  install(FILES third_party/soundfonts/TimGM6mb.sf2
          DESTINATION "MageBrosNative.app/Contents/Resources" RENAME TimGM6mb.sf2)
else()
  install(FILES third_party/soundfonts/TimGM6mb.sf2 DESTINATION . RENAME TimGM6mb.sf2)
endif()
# the LGPL text must travel with binaries that contain ColDet
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/coldet/COPYING)
  install(FILES third_party/coldet/COPYING DESTINATION licenses RENAME coldet-LGPL.txt)
endif()
file(GLOB MBN_THIRD_PARTY_LICENSES ${CMAKE_SOURCE_DIR}/third_party/*-LICENSE)
install(FILES ${MBN_THIRD_PARTY_LICENSES} DESTINATION licenses)
install(DIRECTORY docs/ DESTINATION docs PATTERN ".DS_Store" EXCLUDE PATTERN "._*" EXCLUDE)
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME MageBrosNative)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(WIN32)
  set(CPACK_SYSTEM_NAME windows-x64)
elseif(APPLE)
  set(CPACK_SYSTEM_NAME macos-arm64)  # Apple Silicon only, as the port targets it
elseif(UNIX)
  # A portable tarball: TGZ keeps the executable bit, which a zip extractor may not.
  set(CPACK_SYSTEM_NAME "linux-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
if(UNIX AND NOT APPLE)
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
endif()
include(CPack)
endif()  # NOT ANDROID
