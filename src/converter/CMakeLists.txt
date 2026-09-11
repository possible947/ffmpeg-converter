# Converter library: platform-agnostic core + platform-specific implementations

add_library(converter STATIC
    converter.c
    converter_common.c
    input_video_info.c
    ${CMAKE_SOURCE_DIR}/src/preset/preset_loader.c
)

target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/platform
    ${CMAKE_SOURCE_DIR}/src/preset
)

# Platform-specific source files and settings
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(converter PRIVATE
        platform/converter_windows.c
        ${CMAKE_SOURCE_DIR}/src/platform/runtime_probe_common.c
        ${CMAKE_SOURCE_DIR}/src/platform/runtime_catalog.c
        ${CMAKE_SOURCE_DIR}/src/platform/windows/runtime_probe.c
    )
    target_include_directories(converter PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/windows
    )
    target_link_libraries(converter PRIVATE
        shlwapi      # PathFindOnPathA, PathFileExistsA
    )

elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(converter PRIVATE
        platform/converter_linux.c
        platform/converter_posix.c
        ${CMAKE_SOURCE_DIR}/src/platform/runtime_probe_common.c
        ${CMAKE_SOURCE_DIR}/src/platform/runtime_catalog.c
        ${CMAKE_SOURCE_DIR}/src/platform/linux/runtime_probe.c
    )
    target_compile_definitions(converter PRIVATE
        FFMPEG_CONVERTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(converter PRIVATE
        platform/converter_macos.c
        platform/converter_posix.c
        ${CMAKE_SOURCE_DIR}/src/platform/runtime_probe_common.c
        ${CMAKE_SOURCE_DIR}/src/platform/runtime_catalog.c
        ${CMAKE_SOURCE_DIR}/src/platform/macos/runtime_probe.c
    )
    target_include_directories(converter PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/macos
    )
    target_link_libraries(converter PRIVATE
        "-framework CoreFoundation"
    )

endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_runtime_capabilities
        ${CMAKE_SOURCE_DIR}/src/platform/linux/test_runtime_capabilities.c
    )
    target_include_directories(test_runtime_capabilities PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/linux
    )
    target_link_libraries(test_runtime_capabilities PRIVATE converter)

    add_executable(test_input_video_info
        ${CMAKE_SOURCE_DIR}/src/converter/test_input_video_info.c
    )
    target_link_libraries(test_input_video_info PRIVATE converter)
    add_test(NAME InputVideoInfoTests COMMAND test_input_video_info)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    enable_testing()
    add_executable(test_runtime_probe_catalog
        ${CMAKE_SOURCE_DIR}/src/platform/linux/test_runtime_probe_catalog.c
    )
    target_include_directories(test_runtime_probe_catalog PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/linux
    )
    target_link_libraries(test_runtime_probe_catalog PRIVATE converter)
    add_test(NAME RuntimeProbeCatalogTests COMMAND test_runtime_probe_catalog)
endif()

# Platform abstraction header directory
target_include_directories(converter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(converter PUBLIC jansson)
