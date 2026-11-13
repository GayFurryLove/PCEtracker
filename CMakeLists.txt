# PCEtracker - dedicated PC Engine / TurboGrafx-16 music tracker.
# Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

# 3.13 = Debian 10 (buster) era; keeps old antiX releases able to build.
cmake_minimum_required(VERSION 3.13...3.31)
project(PCEtracker VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

option(PCE_BUILD_TESTS "Build headless tests" ON)
option(PCE_BUILD_TOOLS "Build command-line tools (psg_demo, pcet_render)" ON)
option(PCE_BUILD_APP "Build the SDL2 tracker application" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W3 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# --- third party ----------------------------------------------------------
add_library(blip_buf STATIC third_party/blip_buf/blip_buf.c)
target_include_directories(blip_buf PUBLIC third_party/blip_buf)

# --- module 1: HuC6280 PSG sound engine (no UI, no SDL) -------------------
add_library(pce_psg STATIC
  src/psg/huc6280_psg_core.cpp
  src/psg/psg.cpp)
target_include_directories(pce_psg PUBLIC src)
target_link_libraries(pce_psg PUBLIC blip_buf)

# --- module 6: WAV exporter -----------------------------------------------
add_library(pce_wav STATIC src/audio/wav_writer.cpp)
target_include_directories(pce_wav PUBLIC src)

# --- modules 2-4: tracker engine, song model, instruments/macros -----------
add_library(pce_tracker STATIC
  src/tracker/song.cpp
  src/tracker/player.cpp
  src/tracker/editor.cpp)
target_include_directories(pce_tracker PUBLIC src)
target_link_libraries(pce_tracker PUBLIC pce_psg) # register names only

# --- module 10: project files ---------------------------------------------
add_library(pce_project STATIC src/project/text_format.cpp)
target_link_libraries(pce_project PUBLIC pce_tracker)

# --- module 5: audio renderer (player -> PSG -> PCM/WAV) -------------------
add_library(pce_render STATIC src/audio/song_render.cpp src/audio/wav_reader.cpp)
target_link_libraries(pce_render PUBLIC pce_tracker pce_psg pce_wav)

if(PCE_BUILD_TOOLS)
  add_executable(psg_demo tools/psg_demo.cpp)
  target_link_libraries(psg_demo PRIVATE pce_psg pce_wav)
  add_executable(pcet_render tools/pcet_render.cpp)
  target_link_libraries(pcet_render PRIVATE pce_render pce_project)
endif()

# --- module 9: user interface (SDL2) ----------------------------------------
if(PCE_BUILD_APP)
  # Windows: point SDL2_DIR at the unpacked SDL2-devel-*-VC package (deps/ is searched).
  file(GLOB _pce_sdl_dirs "${CMAKE_SOURCE_DIR}/deps/SDL2-*/cmake")
  find_package(SDL2 CONFIG QUIET HINTS ${_pce_sdl_dirs})
  if(TARGET SDL2::SDL2)
    set(PCE_SDL2_LIBS SDL2::SDL2)
  elseif(SDL2_FOUND AND SDL2_LIBRARIES)
    # Old sdl2-config.cmake (SDL < 2.0.12, e.g. Debian 10): variables only.
    set(PCE_SDL2_LIBS ${SDL2_LIBRARIES})
    set(PCE_SDL2_INCLUDES ${SDL2_INCLUDE_DIRS})
  else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(PCE_SDL2 QUIET sdl2)
      if(PCE_SDL2_FOUND)
        set(PCE_SDL2_LIBS ${PCE_SDL2_LIBRARIES})
        set(PCE_SDL2_INCLUDES ${PCE_SDL2_INCLUDE_DIRS})
      endif()
    endif()
  endif()

  if(PCE_SDL2_LIBS)
    add_executable(pcetracker
      src/main.cpp
      src/ui/app.cpp
      src/ui/app_instrument.cpp
      src/ui/config.cpp
      src/ui/screen.cpp)
    target_include_directories(pcetracker PRIVATE src third_party/font8x8 ${PCE_SDL2_INCLUDES})
    target_link_libraries(pcetracker PRIVATE pce_render pce_project ${PCE_SDL2_LIBS})
    if(WIN32 AND TARGET SDL2::SDL2)
      add_custom_command(TARGET pcetracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:pcetracker>)
    endif()
  else()
    message(WARNING "SDL2 not found: the pcetracker application is skipped (tools and tests still build). "
                    "Linux: apt install libsdl2-dev. Windows: unpack SDL2-devel-*-VC.zip into deps/.")
  endif()
endif()

if(PCE_BUILD_TESTS)
  enable_testing()
  add_executable(test_psg tests/test_psg.cpp)
  target_link_libraries(test_psg PRIVATE pce_psg)
  add_test(NAME psg COMMAND test_psg)

  add_executable(test_tracker tests/test_tracker.cpp)
  target_link_libraries(test_tracker PRIVATE pce_render pce_project)
  add_test(NAME tracker COMMAND test_tracker ${CMAKE_SOURCE_DIR}/songs/demo.pcet)

  add_executable(test_editor tests/test_editor.cpp)
  target_link_libraries(test_editor PRIVATE pce_tracker)
  add_test(NAME editor COMMAND test_editor)

  if(PCE_BUILD_TOOLS)
    add_test(NAME psg_demo_render COMMAND psg_demo ${CMAKE_BINARY_DIR}/demo_test.wav --bits 24)
    add_test(NAME song_render COMMAND pcet_render ${CMAKE_SOURCE_DIR}/songs/demo.pcet
             ${CMAKE_BINARY_DIR}/song_test.wav --bits 24 --resave ${CMAKE_BINARY_DIR}/resaved.pcet)
  endif()
endif()
