cmake_minimum_required(VERSION 3.24)
project(cpw_rebuilt C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(cpw_rebuilt WIN32
    src/main.c
)

target_compile_definitions(cpw_rebuilt PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(cpw_rebuilt PRIVATE
    -municode
    -Os
    -flto
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Wno-missing-field-initializers
)

target_link_options(cpw_rebuilt PRIVATE
    -municode
    -mwindows
    -s
    -flto
    -Wl,--gc-sections
)

target_link_libraries(cpw_rebuilt PRIVATE
    ole32
    windowscodecs
    msimg32
    gdi32
    user32
    shell32
)
