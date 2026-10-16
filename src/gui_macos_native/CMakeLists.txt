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
set(PRESETS_FILE "${GENERATED_PRESETS_DIR}/presets.json")

set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/icon.icns")
set_source_files_properties("${ICON_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
set_source_files_properties("${PRESETS_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

add_executable(macos_gui_native MACOSX_BUNDLE
    main.m
    converter_bridge.m
    apple_m4v_creator.m
    "${ICON_FILE}"
    "${PRESETS_FILE}"
)

set_target_properties(macos_gui_native PROPERTIES
    OUTPUT_NAME "ffmpeg_converter_gui_macos"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

target_compile_options(macos_gui_native PRIVATE -fobjc-arc)

target_include_directories(macos_gui_native
    PRIVATE ${CMAKE_SOURCE_DIR}/src/converter
    PRIVATE ${CMAKE_SOURCE_DIR}/src/platform
)

target_link_libraries(macos_gui_native
    PRIVATE converter
    PRIVATE mux
    PRIVATE add_track
    PRIVATE hq_adapter
    PRIVATE jansson
    PRIVATE "-framework Cocoa"
)

add_executable(test_macos_audio_availability
    test_macos_audio_availability.m converter_bridge.m apple_m4v_creator.m)
target_compile_options(test_macos_audio_availability PRIVATE -fobjc-arc)
target_link_libraries(test_macos_audio_availability PRIVATE
    converter mux add_track hq_adapter jansson "-framework Cocoa")
add_test(NAME MacosAudioAvailabilityTests COMMAND test_macos_audio_availability)
set_tests_properties(MacosAudioAvailabilityTests PROPERTIES TIMEOUT 30)

if (EXISTS "${BUNDLED_FFMPEG}" AND EXISTS "${BUNDLED_FFPROBE}")
    # Copy the whole bin/ dir (not just ffmpeg/ffprobe): ffmpeg's @rpath deps
    # (libxcb, libvulkan, etc.) resolve via @loader_path, so they must sit
    # next to the binary in Resources/bin or dyld fails with "Library not
    # loaded" (SIGABRT) — bundle_ffmpeg_deps.sh only rewrites absolute-path
    # deps and does not cover this @rpath case.
    add_custom_command(TARGET macos_gui_native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/platform/macos/bin" "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/bin"
        COMMENT "Bundling ffmpeg/ffprobe and dylib dependencies into app Resources/bin"
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

if (HQ_CONVERTER_ENABLED)
    add_custom_command(TARGET macos_gui_native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/hq_converter"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${HQ_CONVERTER_RELEASE_DIR}" "$<TARGET_FILE_DIR:macos_gui_native>/../Resources/hq_converter"
        COMMENT "Bundling HQ_converter runtime tree into app Resources"
    )
endif()

# hq_worker is the dedicated helper binary hq_adapter.c exec()'s right
# after fork() to run a single HQ job in a fresh process image (see
# hq_adapter.c's file header comment). resolve_exe_dir() looks for it
# next to the running binary, i.e. Contents/MacOS/ for an app bundle.
if (TARGET hq_worker)
    add_dependencies(macos_gui_native hq_worker)
    add_custom_command(TARGET macos_gui_native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:hq_worker>" "$<TARGET_FILE_DIR:macos_gui_native>/hq_worker"
        COMMENT "Copying hq_worker helper into app bundle MacOS dir"
    )
endif()

install(TARGETS macos_gui_native
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
