# Модуль converter содержит только заголовки (.h)
# Поэтому он должен быть INTERFACE, как и остальные модули

add_library(converter STATIC
    converter.c
)
target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(converter PUBLIC jansson_headers)

