cmake_minimum_required(VERSION 3.13)
project(spice-kitten C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "-Wall -Wextra -pedantic")

add_subdirectory(./libspice libspice_build)

find_package(PkgConfig REQUIRED)
find_package(PNG REQUIRED)
find_library(CRYPTO_LIB crypto)

option(WITH_X11 "Build the X11 input backend" ON)

set(SK_SOURCES
    spice_kitten.c
    input_tty.c
    tty_parse.c
)

if(WITH_X11)
    find_package(X11)
    if(X11_FOUND)
        list(APPEND SK_SOURCES input_x11.c)
    else()
        message(STATUS "X11 not found - building without the X11 input backend")
    endif()
endif()

add_executable(spice-kitten ${SK_SOURCES})

target_include_directories(spice-kitten PRIVATE
    ./libspice/include)

target_link_libraries(spice-kitten PRIVATE
    m
    crypto
    png
    spice
)

if(X11_FOUND)
    target_compile_definitions(spice-kitten PRIVATE HAVE_X11)
    target_link_libraries(spice-kitten PRIVATE X11::X11)
endif()

install(TARGETS spice-kitten
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime
)
