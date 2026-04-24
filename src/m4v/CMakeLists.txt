cmake_minimum_required(VERSION 3.16)

add_library(m4v STATIC
    m4v.c
)

target_include_directories(m4v PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/converter
    ${CMAKE_SOURCE_DIR}/src/platform
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(m4v PRIVATE platform/m4v_platform_windows.c)
else()
    target_sources(m4v PRIVATE platform/m4v_platform_posix.c)
endif()

target_link_libraries(m4v
    PUBLIC converter
    PRIVATE jansson
)