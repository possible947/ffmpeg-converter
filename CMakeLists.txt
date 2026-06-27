 cmake_minimum_required(VERSION 3.16)

project(ffmpeg_converter VERSION 2.5.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# --- JANSSON VENDORED HEADERS ---
add_subdirectory(third_party/jansson)

# --- МОДУЛЬНАЯ СТРУКТУРА ---
add_subdirectory(src)

# --- Windows CLI build (FPC) ---
if(WIN32)
  add_custom_target(fpc_converter_windows
    COMMAND fpc
      -Fu${CMAKE_CURRENT_SOURCE_DIR}/fpc/converter
      -Fu${CMAKE_CURRENT_SOURCE_DIR}/fpc/common
      -Fu${CMAKE_CURRENT_SOURCE_DIR}/fpc/json
      -Fu${CMAKE_CURRENT_SOURCE_DIR}/fpc/platform
      -Fu${CMAKE_CURRENT_SOURCE_DIR}/fpc/cli
      -Ww
      -Werror
      ${CMAKE_CURRENT_SOURCE_DIR}/fpc/cli/ffmpeg_converter_windows.lpr
      -offmpeg_converter_windows.exe
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building Windows CLI (ffmpeg_converter_windows.exe)"
  )

  # --- Windows GUI build (Lazarus / lazbuild) ---
  find_program(LAZBUILD_EXE lazbuild)
  if(LAZBUILD_EXE)
    add_custom_target(fpc_gui_windows
      COMMAND ${LAZBUILD_EXE}
        --build-mode=default
        ${CMAKE_CURRENT_SOURCE_DIR}/fpc/gui/form.lpi
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/fpc/gui
      COMMENT "Building Windows GUI (ffmpeg_converter_gui.exe) via Lazarus"
    )
  else()
    message(STATUS
      "lazbuild not found -- fpc_gui_windows target skipped. "
      "Install Lazarus IDE to enable the GUI build.")
  endif()
endif()

