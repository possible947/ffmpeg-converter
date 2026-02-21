# Подключаем include‑директории модулей
include_directories(
    ${CMAKE_SOURCE_DIR}/src/utils
    ${CMAKE_SOURCE_DIR}/src/progress
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/video
    ${CMAKE_SOURCE_DIR}/src/ffmpeg_cmd
)

# Ищем библиотеку jansson
find_library(JANSSON_LIB jansson)

# --- LINUX CLI ---
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Файлы CLI для Linux (если есть)
    file(GLOB LINUX_CLI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/linux/*.c")

    # Создаём бинарник, добавляя платформенные исходники
    add_executable(linux_cli
        ${LINUX_CLI_SRC}
        ${PLATFORM_LINUX_SRC}
    )

    set_target_properties(linux_cli PROPERTIES OUTPUT_NAME "ffmpeg_converter")

    target_link_libraries(linux_cli
        PRIVATE converter
        platform
        ${JANSSON_LIB}
    )
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
    target_include_directories(macos_cli PRIVATE /opt/local/include)
    target_link_directories(macos_cli PRIVATE /opt/local/lib)

    # Линкуем только то, что реально нужно
    target_link_libraries(macos_cli
        PRIVATE converter
        platform
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

