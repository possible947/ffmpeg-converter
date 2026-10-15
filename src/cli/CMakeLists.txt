# Common include directories for all CLI targets
include_directories(
    ${CMAKE_SOURCE_DIR}/src/utils
    ${CMAKE_SOURCE_DIR}/src/progress
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/video
    ${CMAKE_SOURCE_DIR}/src/ffmpeg_cmd
)

# Shared CLI sources (platform-agnostic)
set(CLI_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cli_common.c
)

if (UNIX)
    add_executable(test_cli_m4v_policy test_cli_m4v_policy.c cli_common.c)
    target_include_directories(test_cli_m4v_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_cli_m4v_policy PRIVATE
        CLI_TEST_PRESETS_PATH="${GENERATED_PRESETS_DIR}/presets.json"
    )
    target_link_libraries(test_cli_m4v_policy PRIVATE
        converter hq_adapter mux_policy jansson
    )
    add_test(NAME CliM4VPolicyTests COMMAND test_cli_m4v_policy)
    set_tests_properties(CliM4VPolicyTests PROPERTIES
        ENVIRONMENT "PRESETS_V2_PATH=${GENERATED_PRESETS_DIR}/presets.json;PRESETS_PATH=${GENERATED_PRESETS_DIR}"
    )
    if (APPLE)
        target_compile_definitions(test_cli_m4v_policy PRIVATE
            FFMPEG_CONVERTER_CLI_PORTABLE_POLICY_ONLY=1
        )
    endif()
endif()

set(LINUX_BUNDLED_FFMPEG "${CMAKE_SOURCE_DIR}/src/platform/linux/bin/ffmpeg")
set(LINUX_BUNDLED_FFPROBE "${CMAKE_SOURCE_DIR}/src/platform/linux/bin/ffprobe")
set(LINUX_SYSTEM_MKVMERGE_CANDIDATES
    /usr/bin/mkvmerge
    /usr/local/bin/mkvmerge
    /opt/local/bin/mkvmerge
)
set(LINUX_SYSTEM_MP4BOX_CANDIDATES
    /usr/bin/MP4Box
    /usr/local/bin/MP4Box
    /opt/local/bin/MP4Box
)

set(LINUX_SYSTEM_MP4BOX "")
set(LINUX_SYSTEM_MKVMERGE "")
foreach(MKVMERGE_CANDIDATE ${LINUX_SYSTEM_MKVMERGE_CANDIDATES})
    if (EXISTS "${MKVMERGE_CANDIDATE}")
        set(LINUX_SYSTEM_MKVMERGE "${MKVMERGE_CANDIDATE}")
        break()
    endif()
endforeach()

foreach(MP4BOX_CANDIDATE ${LINUX_SYSTEM_MP4BOX_CANDIDATES})
    if (EXISTS "${MP4BOX_CANDIDATE}")
        set(LINUX_SYSTEM_MP4BOX "${MP4BOX_CANDIDATE}")
        break()
    endif()
endforeach()

# =============================================================
# --- UNIFIED CLI (new platform-abstracted architecture) ---
# =============================================================

# --- LINUX CLI ---
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(linux_cli
        ${CLI_COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/cli_linux.c
    )

    set_target_properties(linux_cli PROPERTIES
        OUTPUT_NAME "ffmpeg_converter"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    target_include_directories(linux_cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/platform
        ${CMAKE_SOURCE_DIR}/src/platform/linux
        ${CMAKE_SOURCE_DIR}/src/mux
        ${CMAKE_SOURCE_DIR}/src/m4v
        ${CMAKE_SOURCE_DIR}/src/hq
    )

    target_link_libraries(linux_cli
        PRIVATE converter
        PRIVATE mux
        PRIVATE m4v
        PRIVATE add_track
        PRIVATE hq_adapter
        PRIVATE platform
        PRIVATE jansson
    )

    # hq_worker already builds into the same bin/ directory as linux_cli
    # (both set RUNTIME_OUTPUT_DIRECTORY to CMAKE_BINARY_DIR/bin), so no
    # copy step is needed — just make sure it gets built when linux_cli
    # is built as a standalone target.
    if (TARGET hq_worker)
        add_dependencies(linux_cli hq_worker)
    endif()

    if (EXISTS "${LINUX_BUNDLED_FFMPEG}" AND EXISTS "${LINUX_BUNDLED_FFPROBE}")
        add_custom_command(TARGET linux_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINUX_BUNDLED_FFMPEG}" "$<TARGET_FILE_DIR:linux_cli>/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINUX_BUNDLED_FFPROBE}" "$<TARGET_FILE_DIR:linux_cli>/ffprobe"
            COMMENT "Copying bundled ffmpeg/ffprobe next to linux_cli"
        )
    else()
        message(WARNING "src/platform/linux/bin/ffmpeg or ffprobe not found; linux_cli will rely on env vars or PATH.")
    endif()

    if (EXISTS "${LINUX_SYSTEM_MP4BOX}")
        add_custom_command(TARGET linux_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINUX_SYSTEM_MP4BOX}" "$<TARGET_FILE_DIR:linux_cli>/MP4Box"
            COMMENT "Copying MP4Box next to linux_cli"
        )
    else()
        message(WARNING "MP4Box not found in common Linux locations; linux_cli will rely on MP4BOX_BIN or PATH.")
    endif()

    if (EXISTS "${LINUX_SYSTEM_MKVMERGE}")
        add_custom_command(TARGET linux_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINUX_SYSTEM_MKVMERGE}" "$<TARGET_FILE_DIR:linux_cli>/mkvmerge"
            COMMENT "Copying mkvmerge next to linux_cli"
        )
    else()
        message(WARNING "mkvmerge not found in common Linux locations; linux_cli will rely on MKVMERGE_BIN or PATH.")
    endif()

    # Copy the build-generated preset catalog next to the CLI binary.
    add_custom_command(TARGET linux_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GENERATED_PRESETS_DIR}/presets.json" "$<TARGET_FILE_DIR:linux_cli>/presets.json"
        COMMENT "Copying presets.json next to linux_cli"
    )

    # Copy the HQ Converter runtime next to the CLI binary.
    if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/hq_converter")
        add_custom_command(TARGET linux_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/hq_converter" "$<TARGET_FILE_DIR:linux_cli>/hq_converter"
            COMMENT "Copying HQ Converter runtime next to linux_cli"
        )
    endif()
endif()


# --- MACOS CLI ---
set(MACOS_BUNDLED_FFMPEG "${CMAKE_SOURCE_DIR}/src/platform/macos/bin/ffmpeg")
set(MACOS_BUNDLED_FFPROBE "${CMAKE_SOURCE_DIR}/src/platform/macos/bin/ffprobe")

if (APPLE)
    if (NOT EXISTS "${MACOS_BUNDLED_FFMPEG}" OR NOT EXISTS "${MACOS_BUNDLED_FFPROBE}")
        message(FATAL_ERROR "src/platform/macos/bin/ffmpeg or ffprobe not found. Place static binaries there before building macOS CLI.")
    endif()

    add_executable(macos_cli
        ${CLI_COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/cli_macos.c
    )

    set_target_properties(macos_cli PROPERTIES
        OUTPUT_NAME "ffmpeg_converter"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    target_include_directories(macos_cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/platform
        ${CMAKE_SOURCE_DIR}/src/platform/macos
        ${CMAKE_SOURCE_DIR}/src/mux
        ${CMAKE_SOURCE_DIR}/src/m4v
        ${CMAKE_SOURCE_DIR}/src/hq
    )

    target_link_libraries(macos_cli
        PRIVATE converter
        PRIVATE mux
        PRIVATE m4v
        PRIVATE add_track
        PRIVATE hq_adapter
        PRIVATE platform
        PRIVATE jansson
    )

    # macos_cli does not share hq_worker's RUNTIME_OUTPUT_DIRECTORY, so
    # explicitly copy the helper binary next to it (resolve_exe_dir()
    # looks for hq_worker in the same directory as the running binary).
    if (TARGET hq_worker)
        add_dependencies(macos_cli hq_worker)
        add_custom_command(TARGET macos_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:hq_worker>" "$<TARGET_FILE_DIR:macos_cli>/hq_worker"
            COMMENT "Copying hq_worker helper next to macos_cli"
        )
    endif()

    # Copy the whole bin/ dir (not just ffmpeg/ffprobe): ffmpeg's @rpath deps
    # (libxcb, libvulkan, etc.) resolve via @loader_path, so they must sit
    # next to the binary or dyld fails with "Library not loaded" (SIGABRT).
    add_custom_command(TARGET macos_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/platform/macos/bin" "$<TARGET_FILE_DIR:macos_cli>"
        COMMENT "Copying bundled ffmpeg/ffprobe and dylib dependencies next to macos_cli"
    )

    # Copy the build-generated preset catalog next to the CLI binary.
    add_custom_command(TARGET macos_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GENERATED_PRESETS_DIR}/presets.json" "$<TARGET_FILE_DIR:macos_cli>/presets.json"
        COMMENT "Copying presets.json next to macos_cli"
    )

    # Copy the HQ Converter runtime next to the CLI binary.
    if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/hq_converter")
        add_custom_command(TARGET macos_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/hq_converter" "$<TARGET_FILE_DIR:macos_cli>/hq_converter"
            COMMENT "Copying HQ Converter runtime next to macos_cli"
        )
    endif()
endif()
