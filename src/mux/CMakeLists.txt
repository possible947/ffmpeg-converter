add_library(mux_policy STATIC
    policy/mux_policy.c
)
target_include_directories(mux_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/policy)
target_compile_definitions(mux_policy PRIVATE
    FFMPEG_CONVERTER_FFMPEG_PROFILE="${FFMPEG_CONVERTER_FFMPEG_PROFILE}"
)

add_library(mux STATIC mux.c)

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
    mux_policy
)

add_executable(test_mux_policy
    policy/test_mux_policy.c
)
target_link_libraries(test_mux_policy PRIVATE mux_policy)
add_test(NAME MuxPolicyTests COMMAND test_mux_policy)

add_executable(test_audio_mode_availability policy/test_audio_mode_availability.c)
target_link_libraries(test_audio_mode_availability PRIVATE mux_policy)
add_test(NAME AudioModeAvailabilityTests COMMAND test_audio_mode_availability)

if (UNIX)
    add_executable(test_mux_replacement test_mux_replacement.c)
    target_link_libraries(test_mux_replacement PRIVATE mux add_track)
    add_test(NAME MuxReplacementIsolationTests COMMAND test_mux_replacement)
    set_tests_properties(MuxReplacementIsolationTests PROPERTIES SKIP_RETURN_CODE 77)
endif()
