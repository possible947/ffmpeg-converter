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

# pthread — the stop-forwarding watcher thread inside the isolated HQ
# worker process (see hq_adapter.c for the process-isolation model)
find_package(Threads REQUIRED)
target_link_libraries(hq_adapter PRIVATE Threads::Threads)

# Link to converter for ConverterCallbacks type
target_link_libraries(hq_adapter PUBLIC converter)

# hq_worker — dedicated helper binary exec()'d by hq_adapter.c
# immediately after fork() to run a single HQ job in a fresh process
# image (see hq_adapter.c's file header comment: a plain fork() alone
# is not safe when the caller is multi-threaded, e.g. the GUI). Built
# into the same bin/ directory as linux_cli/linux_gui/macos_cli so that
# resolve_exe_dir() + "/hq_worker" finds it next to the main binary.
add_executable(hq_worker hq_worker_main.c)
target_link_libraries(hq_worker PRIVATE hq_adapter)
set_target_properties(hq_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
