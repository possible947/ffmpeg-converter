add_library(mux STATIC
    mux.c
)

target_include_directories(mux PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/converter
    ${CMAKE_SOURCE_DIR}/src/platform
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(mux PRIVATE platform/mux_platform_windows.c)
else()
    target_sources(mux PRIVATE platform/mux_platform_posix.c)
endif()

target_link_libraries(mux PUBLIC
    converter
)
