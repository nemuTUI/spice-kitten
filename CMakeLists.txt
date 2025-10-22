cmake_minimum_required(VERSION 3.13)
project(spice-kitten C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "-Wall -Wextra -pedantic")

add_subdirectory(./libspice libspice_build)

find_package(PkgConfig REQUIRED)
find_package(X11 REQUIRED)
find_package(PNG REQUIRED)
find_library(CRYPTO_LIB crypto)

add_executable(spice-kitten spice_kitten.c)

target_include_directories(spice-kitten PRIVATE
    ./libspice/include)

target_link_libraries(spice-kitten PRIVATE
    m
    X11::X11
    crypto
    png
    spice
)
