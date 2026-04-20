if (WIN32)
    add_library(platform STATIC
        windows/platform.c
        windows/progress.c
        dummy.c
    )
    # Link Windows system libraries required by platform.c:
    #   Kernel32 — CreateDirectoryA, GetFileAttributesA, GetModuleFileNameA,
    #               SearchPathA, CreateFileA, CloseHandle, GetTickCount
    #   Shell32  — reserved for future shell-integration helpers (SHGetFolderPath)
    target_link_libraries(platform PUBLIC Kernel32 Shell32)
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

