cmake_minimum_required(VERSION 3.16)

project(ffmpeg_converter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# --- МОДУЛЬНАЯ СТРУКТУРА ---
add_subdirectory(src)

