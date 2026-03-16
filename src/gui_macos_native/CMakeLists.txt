cmake_minimum_required(VERSION 3.16)
project(ffmpeg_converter_gui_macos_native LANGUAGES C)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    return()
endif()

include(CheckLanguage)
check_language(OBJC)
if (NOT CMAKE_OBJC_COMPILER)
    message(FATAL_ERROR "Objective-C compiler is required for macOS native GUI target")
endif()
enable_language(OBJC)

# Keep jansson linkage aligned with the existing GUI targets.
find_library(JANSSON_LIB jansson)
set(BUNDLED_FFMPEG "${CMAKE_SOURCE_DIR}/src/platform/macos/bin/ffmpeg")
set(BUNDLED_FFPROBE "${CMAKE_SOURCE_DIR}/src/platform/macos/bin/ffprobe")

add_executable(macos_gui_native MACOSX_BUNDLE
    main.m
    converter_bridge.m
)

set_target_properties(macos_gui_native PROPERTIES
    OUTPUT_NAME "ffmpeg_converter_gui_macos"
)

target_compile_options(macos_gui_native PRIVATE -fobjc-arc)

target_include_directories(macos_gui_native
    PRIVATE ${CMAKE_SOURCE_DIR}/src/converter
)

target_link_libraries(macos_gui_native
    PRIVATE converter
    PRIVATE ${JANSSON_LIB}
    PRIVATE "-framework Cocoa"
)

if (EXISTS "${BUNDLED_FFMPEG}" AND EXISTS "${BUNDLED_FFPROBE}")
    add_custom_command(TARGET macos_gui_native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUNDLED_FFMPEG}" "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/bin/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUNDLED_FFPROBE}" "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/bin/ffprobe"
        COMMENT "Bundling ffmpeg/ffprobe into app Resources/bin"
    )
else()
    message(WARNING "src/platform/macos/bin/ffmpeg or ffprobe not found. App will rely on system PATH or FFMPEG/FFPROBE env vars.")
endif()

install(TARGETS macos_gui_native
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
