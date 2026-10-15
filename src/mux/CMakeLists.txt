add_library(mux STATIC
    mux.c
    policy/mux_policy.c
)

target_include_directories(mux PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/policy
    ${CMAKE_SOURCE_DIR}/src/converter
    ${CMAKE_SOURCE_DIR}/src/platform
)

target_compile_definitions(mux PRIVATE
    FFMPEG_CONVERTER_FFMPEG_PROFILE="${FFMPEG_CONVERTER_FFMPEG_PROFILE}"
)

target_sources(mux PRIVATE platform/mux_platform_posix.c)

target_link_libraries(mux PUBLIC
    converter
    m4v
)

add_executable(test_mux_policy
    policy/test_mux_policy.c
)
target_link_libraries(test_mux_policy PRIVATE mux)
add_test(NAME MuxPolicyTests COMMAND test_mux_policy)
