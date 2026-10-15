add_library(platform STATIC
    linux/progress.c
    dummy.c
)

target_include_directories(platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}          # src/platform/
    ${CMAKE_SOURCE_DIR}/src/progress     # src/progress/
)

