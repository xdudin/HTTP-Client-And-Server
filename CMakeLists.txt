cmake_minimum_required(VERSION 3.30)
project(HTTPClient C)

set(CMAKE_C_COMPILER "cc")
set(CMAKE_C_STANDARD 23)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")

set(CMAKE_C_FLAGS "-Oz -ffunction-sections -fdata-sections -Wl,--gc-sections -s" CACHE STRING "Optimize for size" FORCE)

add_executable(HTTPClient client.c)
add_executable(HTTPServer server.c)
