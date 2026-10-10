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

if (UNIX)
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
elseif (APPLE)
  string(REPLACE "\"linux\": {\n        \"hwaccel\": {\n          \"enabled\": true"
     "\"linux\": {\n        \"hwaccel\": {\n          \"enabled\": false"
   PRESETS_CONTENT "${PRESETS_CONTENT}")
endif()
file(WRITE "${GENERATED_PRESETS_DIR}/presets.json" "${PRESETS_CONTENT}")

if (HQ_CONVERTER_ENABLED)
  message(STATUS "HQ_converter runtime feature: enabled")

  # Copy the complete prepared HQ release to the build output.
  # The full directory tree must be preserved — the HQ library resolves
  # preset paths by walking up from its own location.
  set(HQ_RUNTIME_DEST "${CMAKE_BINARY_DIR}/hq_converter")
  add_custom_target(hq_runtime_copy ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${HQ_CONVERTER_RELEASE_DIR}"
      "${HQ_RUNTIME_DEST}"
    COMMENT "Copying HQ_converter runtime tree"
  )
else()
  message(STATUS "HQ_converter runtime feature: disabled")
endif()

# --- JANSSON VENDORED HEADERS ---
add_subdirectory(third_party/jansson)

# --- МОДУЛЬНАЯ СТРУКТУРА ---
add_subdirectory(src)

