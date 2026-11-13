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

if(PCE_BUILD_TOOLS)
  add_executable(psg_demo tools/psg_demo.cpp)
  target_link_libraries(psg_demo PRIVATE pce_psg pce_wav)
endif()

if(PCE_BUILD_TESTS)
  enable_testing()
  add_executable(test_psg tests/test_psg.cpp)
  target_link_libraries(test_psg PRIVATE pce_psg)
  add_test(NAME psg COMMAND test_psg)

  if(PCE_BUILD_TOOLS)
    add_test(NAME psg_demo_render COMMAND psg_demo ${CMAKE_BINARY_DIR}/demo_test.wav --bits 24)
  endif()
endif()
