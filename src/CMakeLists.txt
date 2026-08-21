# Модульные списки исходников больше не нужны — в модулях только .h
# Вместо STATIC‑библиотек создаём INTERFACE‑библиотеки

add_library(core       INTERFACE)
add_library(utils      INTERFACE)
add_library(progress   INTERFACE)
add_library(audio      INTERFACE)
add_library(video      INTERFACE)
add_library(ffmpeg_cmd INTERFACE)

# Подключаем include‑директории модулей
target_include_directories(core       INTERFACE ${CMAKE_SOURCE_DIR}/src/core)
target_include_directories(utils      INTERFACE ${CMAKE_SOURCE_DIR}/src/utils)
target_include_directories(progress   INTERFACE ${CMAKE_SOURCE_DIR}/src/progress)
target_include_directories(audio      INTERFACE ${CMAKE_SOURCE_DIR}/src/audio)
target_include_directories(video      INTERFACE ${CMAKE_SOURCE_DIR}/src/video)
target_include_directories(ffmpeg_cmd INTERFACE ${CMAKE_SOURCE_DIR}/src/ffmpeg_cmd)

# Подкаталоги (platform теперь только отдаёт списки .c)
add_subdirectory(converter)
add_subdirectory(m4v)
add_subdirectory(mux)
add_subdirectory(platform)
add_subdirectory(preset)

# CLI (после объявления библиотек)
add_subdirectory(cli)

# GUI split by platform:
# - Linux keeps GTK4 GUI
# - macOS uses native Cocoa GUI
option(ENABLE_LINUX_GUI "Build Linux GTK GUI" ON)
option(ENABLE_MACOS_NATIVE_GUI "Build macOS native GUI" ON)

if (ENABLE_LINUX_GUI AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_subdirectory(gui)
endif()

if (ENABLE_MACOS_NATIVE_GUI AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	add_subdirectory(gui_macos_native)
endif()

