# Converter library: platform-agnostic core + platform-specific implementations

add_library(converter STATIC
    converter.c
    converter_common.c
    input_video_info.c
    subtitle_preservation.c
    ${CMAKE_SOURCE_DIR}/src/audio/audio_orchestrator.c
    ${CMAKE_SOURCE_DIR}/src/preset/preset_loader.c
    ${CMAKE_SOURCE_DIR}/src/platform/selection_catalog.c
)

target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/platform
    ${CMAKE_SOURCE_DIR}/src/preset
)

# Platform-specific source files and settings
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
elseif (APPLE)
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

add_executable(test_input_video_info
    ${CMAKE_SOURCE_DIR}/src/converter/test_input_video_info.c
)
target_link_libraries(test_input_video_info PRIVATE converter)
add_test(NAME InputVideoInfoTests COMMAND test_input_video_info)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_runtime_capabilities
        ${CMAKE_SOURCE_DIR}/src/platform/linux/test_runtime_capabilities.c
    )
    target_include_directories(test_runtime_capabilities PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/linux
    )
    target_link_libraries(test_runtime_capabilities PRIVATE converter)

    add_executable(test_linux_preset_args
        ${CMAKE_SOURCE_DIR}/src/converter/test_linux_preset_args.c
    )
    target_compile_definitions(test_linux_preset_args PRIVATE
        FFMPEG_CONVERTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(test_linux_preset_args PRIVATE converter)
    add_test(NAME LinuxPresetArgsTests COMMAND test_linux_preset_args)

endif()

if (UNIX)
    add_executable(test_subtitle_preservation test_subtitle_preservation.c)
    target_link_libraries(test_subtitle_preservation PRIVATE converter)
    add_test(NAME SubtitlePreservationTests COMMAND test_subtitle_preservation)
    set_tests_properties(SubtitlePreservationTests PROPERTIES SKIP_RETURN_CODE 77)
    if (APPLE)
        set_tests_properties(SubtitlePreservationTests PROPERTIES
            ENVIRONMENT
                "FFMPEG=${CMAKE_SOURCE_DIR}/src/platform/macos/bin/ffmpeg;FFPROBE=${CMAKE_SOURCE_DIR}/src/platform/macos/bin/ffprobe"
        )
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
            EXISTS "${CMAKE_SOURCE_DIR}/src/platform/linux/bin/ffmpeg" AND
            EXISTS "${CMAKE_SOURCE_DIR}/src/platform/linux/bin/ffprobe")
        set_tests_properties(SubtitlePreservationTests PROPERTIES
            ENVIRONMENT
                "FFMPEG=${CMAKE_SOURCE_DIR}/src/platform/linux/bin/ffmpeg;FFPROBE=${CMAKE_SOURCE_DIR}/src/platform/linux/bin/ffprobe"
        )
    endif()
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

add_executable(test_selection_catalog
    ${CMAKE_SOURCE_DIR}/src/platform/test_selection_catalog.c
)
target_compile_definitions(test_selection_catalog PRIVATE
    FFMPEG_CONVERTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_selection_catalog PRIVATE converter)
add_test(NAME SelectionCatalogTests COMMAND test_selection_catalog)

# Platform abstraction header directory
target_include_directories(converter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(converter PUBLIC jansson mux_policy)
