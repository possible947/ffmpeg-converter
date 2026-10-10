# HQ adapter module — STATIC library
# Wraps the HQ Converter Library public ABI

add_library(hq_adapter STATIC
    hq_adapter.c
    hq_catalog.c
)

target_include_directories(hq_adapter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/converter
)

# jansson for JSON parsing (eov2 session, preset catalog)
target_link_libraries(hq_adapter PUBLIC jansson)

# dlopen / dlfcn
target_link_libraries(hq_adapter PRIVATE dl)

# Link to converter for ConverterCallbacks type
target_link_libraries(hq_adapter PUBLIC converter)
