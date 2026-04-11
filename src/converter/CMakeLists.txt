# Модуль converter содержит только заголовки (.h)
# Поэтому он должен быть INTERFACE, как и остальные модули

add_library(converter STATIC
    converter.c
)
target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/platform
)
target_link_libraries(converter PUBLIC jansson_headers)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(converter PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/linux/runtime_probe.c
    )
    target_compile_definitions(converter PRIVATE
        FFMPEG_CONVERTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
endif()

