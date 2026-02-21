if (WIN32)
    add_library(platform STATIC
        windows/progress.c
        dummy.c
    )
elseif(APPLE)
    add_library(platform STATIC
        macos/progress.c
        dummy.c
    )
else()
    add_library(platform STATIC
        linux/progress.c
        dummy.c
    )
endif()

target_include_directories(platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}          # src/platform/
    ${CMAKE_SOURCE_DIR}/src/progress     # src/progress/
)

