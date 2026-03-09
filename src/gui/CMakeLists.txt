cmake_minimum_required(VERSION 3.16)
project(ffmpeg_converter_gui LANGUAGES C)

# Find GTK4
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
include_directories(${GTK4_INCLUDE_DIRS})
link_directories(${GTK4_LIBRARY_DIRS})

# Find jansson (used by converter)
find_library(JANSSON_LIB jansson)

# Add the GUI library (just a convenience target)
add_library(gui_lib
    gui_window.c
    gui_callbacks.c
)

target_include_directories(gui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gui_lib
    PRIVATE PkgConfig::GTK4
    PUBLIC converter   # already defined in src/converter/CMakeLists
)

# Executable for Linux
add_executable(linux_gui gui_main.c)
set_target_properties(linux_gui PROPERTIES OUTPUT_NAME "ffmpeg_converter_gui")
target_link_libraries(linux_gui
    PRIVATE gui_lib
    PRIVATE converter
    PRIVATE ${JANSSON_LIB}
)

# Executable for macOS
add_executable(macos_gui gui_main.c)
set_target_properties(macos_gui PROPERTIES OUTPUT_NAME "ffmpeg_converter_gui_macos")
target_link_libraries(macos_gui
    PRIVATE gui_lib
    PRIVATE converter
    PRIVATE ${JANSSON_LIB}
)

install(TARGETS linux_gui macos_gui RUNTIME DESTINATION bin)
