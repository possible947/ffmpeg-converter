add_library(mux STATIC
    mux.c
)

target_include_directories(mux PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/converter
    ${CMAKE_SOURCE_DIR}/src/platform
)

target_link_libraries(mux PUBLIC
    converter
)
