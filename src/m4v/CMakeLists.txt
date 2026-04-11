cmake_minimum_required(VERSION 3.16)

find_library(JANSSON_LIB jansson)

add_library(m4v STATIC
    m4v.c
)

target_include_directories(m4v
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(m4v
    PUBLIC converter
    PRIVATE ${JANSSON_LIB}
)