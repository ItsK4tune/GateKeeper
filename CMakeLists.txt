cmake_minimum_required(VERSION 3.20)

project(RedisMini
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(redis-mini
    src/main.cpp
    src/server.cpp
)

target_include_directories(redis-mini
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
