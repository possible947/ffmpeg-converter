cmake_minimum_required(VERSION 3.16)
project(ffmpeg_converter_gui LANGUAGES C)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

# Find GTK4
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
include_directories(${GTK4_INCLUDE_DIRS})
link_directories(${GTK4_LIBRARY_DIRS})

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

# Compile GLib resources (window icon)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(RESOURCES_XML  "${CMAKE_CURRENT_SOURCE_DIR}/resources.gresource.xml")
set(RESOURCES_C    "${CMAKE_CURRENT_BINARY_DIR}/resources.c")
add_custom_command(
    OUTPUT  "${RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            "--sourcedir=${CMAKE_CURRENT_SOURCE_DIR}"
            "--c-name=ffmpeg_converter_res"
            "--target=${RESOURCES_C}"
            "--generate-source"
            "${RESOURCES_XML}"
    DEPENDS "${RESOURCES_XML}" "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"
    COMMENT "Compiling GLib resources"
)

# Add the GUI library (just a convenience target)
add_library(gui_lib
    gui_window.c
    gui_callbacks.c
    "${RESOURCES_C}"
)

target_include_directories(gui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gui_lib PRIVATE)
target_link_libraries(gui_lib
    PRIVATE PkgConfig::GTK4
    PUBLIC converter   # already defined in src/converter/CMakeLists
    PUBLIC m4v
    PUBLIC mux
)

# Executable for Linux
add_executable(linux_gui gui_main.c)
set_target_properties(linux_gui PROPERTIES
    OUTPUT_NAME "ffmpeg_converter_gui"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_compile_options(linux_gui PRIVATE)
target_link_libraries(linux_gui
    PRIVATE gui_lib
    PRIVATE converter
    PRIVATE jansson
)

if (EXISTS "${LINUX_BUNDLED_FFMPEG}" AND EXISTS "${LINUX_BUNDLED_FFPROBE}")
    add_custom_command(TARGET linux_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINUX_BUNDLED_FFMPEG}" "$<TARGET_FILE_DIR:linux_gui>/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINUX_BUNDLED_FFPROBE}" "$<TARGET_FILE_DIR:linux_gui>/ffprobe"
        COMMENT "Copying bundled ffmpeg/ffprobe next to linux_gui"
    )
else()
    message(WARNING "src/platform/linux/bin/ffmpeg or ffprobe not found; linux_gui will rely on env vars or PATH.")
endif()

if (EXISTS "${LINUX_SYSTEM_MP4BOX}")
    add_custom_command(TARGET linux_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINUX_SYSTEM_MP4BOX}" "$<TARGET_FILE_DIR:linux_gui>/MP4Box"
        COMMENT "Copying MP4Box next to linux_gui"
    )
else()
    message(WARNING "MP4Box not found in common Linux locations; linux_gui will rely on MP4BOX_BIN or PATH.")
endif()

if (EXISTS "${LINUX_SYSTEM_MKVMERGE}")
    add_custom_command(TARGET linux_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINUX_SYSTEM_MKVMERGE}" "$<TARGET_FILE_DIR:linux_gui>/mkvmerge"
        COMMENT "Copying mkvmerge next to linux_gui"
    )
else()
    message(WARNING "mkvmerge not found in common Linux locations; linux_gui will rely on MKVMERGE_BIN or PATH.")
endif()

# Copy presets.json next to GUI binary
if (EXISTS "${CMAKE_SOURCE_DIR}/presets.json")
    add_custom_command(TARGET linux_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/presets.json" "$<TARGET_FILE_DIR:linux_gui>/presets.json"
        COMMENT "Copying presets.json next to linux_gui"
    )
else()
    message(WARNING "presets.json not found at ${CMAKE_SOURCE_DIR}/presets.json; linux_gui will use config directory or built-in fallback.")
endif()

 install(TARGETS linux_gui RUNTIME DESTINATION bin)

 # --- AppImage packaging (optional) ---
 option(ENABLE_APPIMAGE "Build AppImage package for linux_gui" OFF)

 if (ENABLE_APPIMAGE)
     find_program(APPIMAGETOOL NAMES appimagetool appimagetool.AppImage)
     if (NOT APPIMAGETOOL)
         message(WARNING "appimagetool not found — ENABLE_APPIMAGE=ON but tool missing. Install from https://github.com/AppImage/AppImageKit")
     else()
         add_custom_target(package_appimage
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/package_appimage.sh" "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/bin"
             DEPENDS linux_gui
             WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
             COMMENT "Building AppImage package..."
         )
         message(STATUS "AppImage packaging enabled: run 'cmake --build . --target package_appimage'")
     endif()
 endif()

