 cmake_minimum_required(VERSION 3.16)

project(ffmpeg_converter VERSION 3.0 LANGUAGES C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# HQ_converter is an external, prebuilt Linux/macOS dependency.  This step
# only determines the runtime feature flag and prepares the generated preset
# catalog; Phase 3 owns the actual HQ execution path.
set(HQ_CONVERTER_ENABLED false)
set(HQ_CONVERTER_RELEASE_DIR "${CMAKE_SOURCE_DIR}/third_party/hq_converter")
set(HQ_CONVERTER_PRESET_FILE
    "${HQ_CONVERTER_RELEASE_DIR}/modules/encoder_orchestrator_v2/config/presets/encoder_presets_v2.json")

if (UNIX AND NOT WIN32)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  set(HQ_CONVERTER_REQUIRED_PATHS
    "${HQ_CONVERTER_RELEASE_DIR}/bin/ffmpeg"
    "${HQ_CONVERTER_RELEASE_DIR}/bin/ffprobe"
    "${HQ_CONVERTER_RELEASE_DIR}/bin/mkvmerge"
    "${HQ_CONVERTER_RELEASE_DIR}/modules/encoder_orchestrator_v2/runner/encoder_orchestrator_v2_runner"
    "${HQ_CONVERTER_RELEASE_DIR}/modules/encoder_orchestrator_v2/config/toolchain_runtime.ini"
    "${HQ_CONVERTER_PRESET_FILE}"
    "${HQ_CONVERTER_RELEASE_DIR}/native/libav-sdk/lib"
  )
  set(HQ_CONVERTER_RELEASE_VALID TRUE)
  foreach(HQ_REQUIRED_PATH IN LISTS HQ_CONVERTER_REQUIRED_PATHS)
    if (NOT EXISTS "${HQ_REQUIRED_PATH}")
      set(HQ_CONVERTER_RELEASE_VALID FALSE)
      message(STATUS "HQ_converter release is missing: ${HQ_REQUIRED_PATH}")
    endif()
  endforeach()
  if (HQ_CONVERTER_RELEASE_VALID)
    if (Python3_Interpreter_FOUND)
      execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c
          "import json, sys; data=json.load(open(sys.argv[1])); assert isinstance(data.get('encoders'), dict) and data['encoders']"
          "${HQ_CONVERTER_PRESET_FILE}"
        RESULT_VARIABLE HQ_CONVERTER_PRESET_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
      )
      if (HQ_CONVERTER_PRESET_RESULT EQUAL 0)
        set(HQ_CONVERTER_ENABLED true)
      else()
        message(STATUS "HQ_converter preset catalog is invalid")
      endif()
    else()
      message(STATUS "Python 3 not found; HQ_converter preset catalog validation is unavailable")
    endif()
  endif()
endif()

set(GENERATED_PRESETS_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_PRESETS_DIR}")

# Generate the unified catalog with build-time availability gates.
configure_file(
  "${CMAKE_SOURCE_DIR}/presets.json.in"
  "${GENERATED_PRESETS_DIR}/presets.json"
  @ONLY
)

file(READ "${GENERATED_PRESETS_DIR}/presets.json" PRESETS_CONTENT)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  string(REPLACE "\"macos\": {\n        \"hwaccel\": {\n          \"enabled\": true"
     "\"macos\": {\n        \"hwaccel\": {\n          \"enabled\": false"
   PRESETS_CONTENT "${PRESETS_CONTENT}")
  string(REPLACE "\"windows\": {\n        \"hwaccel\": {\n          \"enabled\": true"
     "\"windows\": {\n        \"hwaccel\": {\n          \"enabled\": false"
   PRESETS_CONTENT "${PRESETS_CONTENT}")
elseif (APPLE)
  string(REPLACE "\"linux\": {\n        \"hwaccel\": {\n          \"enabled\": true"
     "\"linux\": {\n        \"hwaccel\": {\n          \"enabled\": false"
   PRESETS_CONTENT "${PRESETS_CONTENT}")
  string(REPLACE "\"windows\": {\n        \"hwaccel\": {\n          \"enabled\": true"
     "\"windows\": {\n        \"hwaccel\": {\n          \"enabled\": false"
   PRESETS_CONTENT "${PRESETS_CONTENT}")
else()
  string(REPLACE "\"linux\": {\n        \"hwaccel\": {\n          \"enabled\": true"
     "\"linux\": {\n        \"hwaccel\": {\n          \"enabled\": false"
   PRESETS_CONTENT "${PRESETS_CONTENT}")
  string(REPLACE "\"macos\": {\n        \"hwaccel\": {\n          \"enabled\": true"
     "\"macos\": {\n        \"hwaccel\": {\n          \"enabled\": false"
   PRESETS_CONTENT "${PRESETS_CONTENT}")
endif()
file(WRITE "${GENERATED_PRESETS_DIR}/presets.json" "${PRESETS_CONTENT}")

if (HQ_CONVERTER_ENABLED)
  message(STATUS "HQ_converter runtime feature: enabled")
else()
  message(STATUS "HQ_converter runtime feature: disabled")
endif()

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

