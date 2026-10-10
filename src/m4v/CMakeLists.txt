cmake_minimum_required(VERSION 3.16)

add_library(m4v STATIC
    m4v.c
)

target_include_directories(m4v PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/converter
    ${CMAKE_SOURCE_DIR}/src/platform
)

target_sources(m4v PRIVATE platform/m4v_platform_posix.c)

target_link_libraries(m4v
    PUBLIC converter
    PRIVATE jansson
)