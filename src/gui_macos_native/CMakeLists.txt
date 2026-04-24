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

set(BUNDLED_FFMPEG "${CMAKE_SOURCE_DIR}/src/platform/macos/bin/ffmpeg")
set(BUNDLED_FFPROBE "${CMAKE_SOURCE_DIR}/src/platform/macos/bin/ffprobe")
set(BUNDLE_MP4BOX_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/bundle_mp4box_deps.sh")
set(BUNDLE_MKVMERGE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/bundle_mkvmerge_deps.sh")
set(BUNDLE_FFMPEG_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/bundle_ffmpeg_deps.sh")

set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/icon.icns")
set_source_files_properties("${ICON_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

add_executable(macos_gui_native MACOSX_BUNDLE
    main.m
    converter_bridge.m
    apple_m4v_creator.m
    "${ICON_FILE}"
)

set_target_properties(macos_gui_native PROPERTIES
    OUTPUT_NAME "ffmpeg_converter_gui_macos"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

target_compile_options(macos_gui_native PRIVATE -fobjc-arc)

target_include_directories(macos_gui_native
    PRIVATE ${CMAKE_SOURCE_DIR}/src/converter
)

target_link_libraries(macos_gui_native
    PRIVATE converter
    PRIVATE mux
    PRIVATE jansson
    PRIVATE "-framework Cocoa"
)

if (EXISTS "${BUNDLED_FFMPEG}" AND EXISTS "${BUNDLED_FFPROBE}")
    add_custom_command(TARGET macos_gui_native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUNDLED_FFMPEG}" "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/bin/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUNDLED_FFPROBE}" "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/bin/ffprobe"
        COMMENT "Bundling ffmpeg/ffprobe into app Resources/bin"
    )

    if (EXISTS "${BUNDLE_FFMPEG_SCRIPT}")
        add_custom_command(TARGET macos_gui_native POST_BUILD
            COMMAND /bin/bash "${BUNDLE_FFMPEG_SCRIPT}" "$<TARGET_FILE_DIR:macos_gui_native>"
            COMMENT "Bundling ffmpeg/ffprobe dependent dylibs into app Resources"
        )
    else()
        message(WARNING "bundle_ffmpeg_deps.sh not found; ffmpeg/ffprobe dylibs will not be bundled.")
    endif()
else()
    message(WARNING "src/platform/macos/bin/ffmpeg or ffprobe not found. App will rely on system PATH or FFMPEG/FFPROBE env vars.")
endif()

if (EXISTS "${BUNDLE_MP4BOX_SCRIPT}")
    add_custom_command(TARGET macos_gui_native POST_BUILD
        COMMAND /bin/bash "${BUNDLE_MP4BOX_SCRIPT}" "$<TARGET_FILE_DIR:macos_gui_native>"
        COMMENT "Bundling MP4Box and dependent dylibs into app Resources"
    )
else()
    message(WARNING "bundle_mp4box_deps.sh not found; MP4Box will not be bundled into app.")
endif()

if (EXISTS "${BUNDLE_MKVMERGE_SCRIPT}")
    add_custom_command(TARGET macos_gui_native POST_BUILD
        COMMAND /bin/bash "${BUNDLE_MKVMERGE_SCRIPT}" "$<TARGET_FILE_DIR:macos_gui_native>"
        COMMENT "Bundling mkvmerge and dependent dylibs into app Resources"
    )
else()
    message(WARNING "bundle_mkvmerge_deps.sh not found; mkvmerge will not be bundled into app.")
endif()

install(TARGETS macos_gui_native
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
