# Converter library: platform-agnostic core + platform-specific implementations

add_library(converter STATIC
    converter.c
    converter_common.c
)

target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/platform
)

# Platform-specific source files and settings
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(converter PRIVATE
        platform/converter_windows.c
    )
    target_link_libraries(converter PRIVATE
        shlwapi      # PathFindOnPathA, PathFileExistsA
    )

elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(converter PRIVATE
        platform/converter_linux.c
        ${CMAKE_SOURCE_DIR}/src/platform/linux/runtime_probe.c
    )
    target_compile_definitions(converter PRIVATE
        FFMPEG_CONVERTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(converter PRIVATE
        platform/converter_macos.c
    )
    target_link_libraries(converter PRIVATE
        "-framework CoreFoundation"
    )

endif()

# Platform abstraction header directory
target_include_directories(converter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(converter PUBLIC jansson_headers)
