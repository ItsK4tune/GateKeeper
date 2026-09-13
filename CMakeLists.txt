cmake_minimum_required(VERSION 3.20)

project(RedisMini
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(redis-mini
    src/main.cpp
)

target_include_directories(redis-mini
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
