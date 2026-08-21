# Preset loader module - sources included directly in converter target
# Unit test target for development/validation

enable_testing()

add_executable(test_preset_loader
    test_preset_loader.c
    preset_loader.c
)

target_include_directories(test_preset_loader PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preset
    ${CMAKE_SOURCE_DIR}/third_party/jansson/src
)

target_link_libraries(test_preset_loader PRIVATE
    jansson
)

add_test(NAME PresetLoaderTests COMMAND test_preset_loader)

# Extended tests: malformed JSON, file search, performance, env vars
add_executable(test_preset_loader_extended
    test_preset_loader_extended.c
    preset_loader.c
)

target_include_directories(test_preset_loader_extended PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preset
    ${CMAKE_SOURCE_DIR}/third_party/jansson/src
)

target_link_libraries(test_preset_loader_extended PRIVATE
    jansson
)

add_test(NAME PresetLoaderExtendedTests COMMAND test_preset_loader_extended)

