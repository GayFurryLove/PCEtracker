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
option(PCE_BUILD_TOOLS "Build command-line tools (psg_demo)" ON)

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
  src/tracker/player.cpp)
target_include_directories(pce_tracker PUBLIC src)
target_link_libraries(pce_tracker PUBLIC pce_psg) # register names only

# --- module 10: project files ---------------------------------------------
add_library(pce_project STATIC src/project/text_format.cpp)
target_link_libraries(pce_project PUBLIC pce_tracker)

# --- module 5: audio renderer (player -> PSG -> PCM/WAV) -------------------
add_library(pce_render STATIC src/audio/song_render.cpp)
target_link_libraries(pce_render PUBLIC pce_tracker pce_psg pce_wav)

if(PCE_BUILD_TOOLS)
  add_executable(psg_demo tools/psg_demo.cpp)
  target_link_libraries(psg_demo PRIVATE pce_psg pce_wav)
  add_executable(pcet_render tools/pcet_render.cpp)
  target_link_libraries(pcet_render PRIVATE pce_render pce_project)
endif()

if(PCE_BUILD_TESTS)
  enable_testing()
  add_executable(test_psg tests/test_psg.cpp)
  target_link_libraries(test_psg PRIVATE pce_psg)
  add_test(NAME psg COMMAND test_psg)

  add_executable(test_tracker tests/test_tracker.cpp)
  target_link_libraries(test_tracker PRIVATE pce_render pce_project)
  add_test(NAME tracker COMMAND test_tracker ${CMAKE_SOURCE_DIR}/songs/demo.pcet)

  if(PCE_BUILD_TOOLS)
    add_test(NAME psg_demo_render COMMAND psg_demo ${CMAKE_BINARY_DIR}/demo_test.wav --bits 24)
    add_test(NAME song_render COMMAND pcet_render ${CMAKE_SOURCE_DIR}/songs/demo.pcet
             ${CMAKE_BINARY_DIR}/song_test.wav --bits 24 --resave ${CMAKE_BINARY_DIR}/resaved.pcet)
  endif()
endif()
