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
    )

    target_link_libraries(linux_cli
        PRIVATE converter
        PRIVATE mux
        PRIVATE platform
        PRIVATE jansson
    )

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

    set_target_properties(macos_cli PROPERTIES OUTPUT_NAME "ffmpeg_converter")

    target_include_directories(macos_cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/platform
        ${CMAKE_SOURCE_DIR}/src/platform/macos
    )

    target_link_libraries(macos_cli
        PRIVATE converter
        PRIVATE platform
        PRIVATE jansson
    )

    add_custom_command(TARGET macos_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MACOS_BUNDLED_FFMPEG}" "$<TARGET_FILE_DIR:macos_cli>/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MACOS_BUNDLED_FFPROBE}" "$<TARGET_FILE_DIR:macos_cli>/ffprobe"
        COMMENT "Copying bundled ffmpeg/ffprobe next to macos_cli"
    )
endif()

# --- WINDOWS CLI ---
if (WIN32)
    set(WINDOWS_BUNDLED_BIN_DIR "${CMAKE_SOURCE_DIR}/src/platform/windows/bin")

    if (NOT EXISTS "${WINDOWS_BUNDLED_BIN_DIR}")
        message(FATAL_ERROR "src/platform/windows/bin not found. Place bundled ffmpeg/ffprobe and dependencies there before building windows_cli.")
    endif()

    add_executable(windows_cli
        ${CLI_COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/cli_windows.c
    )

    set_target_properties(windows_cli PROPERTIES OUTPUT_NAME "ffmpeg_converter")

    target_include_directories(windows_cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/platform
        ${CMAKE_SOURCE_DIR}/src/platform/windows
    )

    target_link_libraries(windows_cli
        PRIVATE converter
        PRIVATE platform
        PRIVATE jansson
    )

    add_custom_command(TARGET windows_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${WINDOWS_BUNDLED_BIN_DIR}" "$<TARGET_FILE_DIR:windows_cli>"
        COMMENT "Copying bundled ffmpeg/ffprobe and dependencies next to windows_cli"
    )
endif()

