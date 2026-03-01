# CMakeLists.txt for vendored jansson headers
# Detects platform features and generates jansson_config.h from jansson_config.h.in,
# then exposes an INTERFACE library so other targets can use these headers.

include(CheckCSourceCompiles)

# Detect inline keyword support
check_c_source_compiles("static inline int f(void) { return 0; } int main(void) { return f(); }" HAVE_C_INLINE)
if(HAVE_C_INLINE)
    set(json_inline "inline")
else()
    set(json_inline "")
endif()

# Detect long long support
check_c_source_compiles("int main(void) { long long x = 0; (void)x; return 0; }" json_have_long_long)
if(json_have_long_long)
    set(json_have_long_long 1)
else()
    set(json_have_long_long 0)
endif()

# Detect localeconv support
check_c_source_compiles("#include <locale.h>\nint main(void) { localeconv(); return 0; }" json_have_localeconv)
if(json_have_localeconv)
    set(json_have_localeconv 1)
else()
    set(json_have_localeconv 0)
endif()

# Detect __atomic builtins
check_c_source_compiles(
    "int main(void) { volatile int v = 0; __atomic_add_fetch(&v, 1, __ATOMIC_ACQUIRE); __atomic_sub_fetch(&v, 1, __ATOMIC_RELEASE); return 0; }"
    json_have_atomic_builtins
)
if(json_have_atomic_builtins)
    set(json_have_atomic_builtins 1)
else()
    set(json_have_atomic_builtins 0)
endif()

# Detect __sync builtins (only if __atomic not available)
if(json_have_atomic_builtins)
    set(json_have_sync_builtins 0)
else()
    check_c_source_compiles(
        "int main(void) { volatile int v = 0; __sync_add_and_fetch(&v, 1); __sync_sub_and_fetch(&v, 1); return 0; }"
        json_have_sync_builtins_detected
    )
    if(json_have_sync_builtins_detected)
        set(json_have_sync_builtins 1)
    else()
        set(json_have_sync_builtins 0)
    endif()
endif()

# Generate jansson_config.h into the build directory
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/jansson_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/jansson_config.h"
    @ONLY
)

# Create an INTERFACE library that provides the vendored jansson headers.
# Consumers link against this to get the include paths.
add_library(jansson_headers INTERFACE)
target_include_directories(jansson_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)
