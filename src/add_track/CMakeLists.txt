add_library(add_track STATIC
    add_track.c
)

target_include_directories(add_track PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/converter
    ${CMAKE_SOURCE_DIR}/src/mux
    ${CMAKE_SOURCE_DIR}/src/platform
)

target_link_libraries(add_track PUBLIC
    converter
    mux
)
