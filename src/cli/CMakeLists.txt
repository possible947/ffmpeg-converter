# Подключаем include‑директории модулей
include_directories(
    ${CMAKE_SOURCE_DIR}/src/utils
    ${CMAKE_SOURCE_DIR}/src/progress
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/video
    ${CMAKE_SOURCE_DIR}/src/ffmpeg_cmd
)

# Ищем библиотеку jansson (MacPorts, Homebrew Intel, Homebrew Apple Silicon)
find_library(JANSSON_LIB jansson
    HINTS
        /opt/local/lib
        /usr/local/lib
        /opt/homebrew/lib
)

if (NOT JANSSON_LIB)
    message(FATAL_ERROR "jansson library not found. Please install it (e.g. 'brew install jansson' or 'port install jansson').")
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

# --- LINUX CLI ---
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Файлы CLI для Linux (если есть)
    file(GLOB LINUX_CLI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/linux/*.c")

    # Создаём бинарник, добавляя платформенные исходники
    add_executable(linux_cli
        ${LINUX_CLI_SRC}
        ${PLATFORM_LINUX_SRC}
    )

    set_target_properties(linux_cli PROPERTIES
        OUTPUT_NAME "ffmpeg_converter"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    target_link_libraries(linux_cli
        PRIVATE converter
        PRIVATE mux
        platform
        ${JANSSON_LIB}
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
if (APPLE)
    # Файлы CLI для macOS (если есть)
    file(GLOB MACOS_CLI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/macos/*.c")

    # Создаём бинарник, добавляя платформенные исходники
    add_executable(macos_cli
        ${MACOS_CLI_SRC}
        ${PLATFORM_MACOS_SRC}
    )

    set_target_properties(macos_cli PROPERTIES OUTPUT_NAME "ffmpeg_converter")

    # Пути MacPorts
    target_include_directories(macos_cli PRIVATE
        /opt/local/include
    )
    target_link_directories(macos_cli PRIVATE
        /opt/local/lib
    )

    # Линкуем только то, что реально нужно
    target_link_libraries(macos_cli
        PRIVATE converter
        platform
        jansson_headers
        ${JANSSON_LIB}
    )
endif()

# --- WINDOWS CLI (пока пустой, добавим позже) ---
if (WIN32)
    file(GLOB WINDOWS_CLI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/windows/*.c")

    add_executable(windows_cli
        ${WINDOWS_CLI_SRC}
    )

    set_target_properties(windows_cli PROPERTIES OUTPUT_NAME "ffmpeg_converter")

    target_link_libraries(windows_cli
        PRIVATE converter
        platform
        ${JANSSON_LIB}
    )
endif()

