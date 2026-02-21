add_library(progress STATIC
    progress.c
)

target_include_directories(progress PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

