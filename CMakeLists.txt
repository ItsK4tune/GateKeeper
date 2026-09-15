cmake_minimum_required(VERSION 3.20)

project(GateKeeper
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(gatekeeper
    src/main.cpp
    src/net/tcp_server.cpp
    src/net/client_connection.cpp
    src/protocol/frame.cpp
    src/protocol/frame_decoder.cpp
    src/protocol/json_request_parser.cpp
    src/protocol/response.cpp
)

target_include_directories(gatekeeper
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

include(CTest)

if(BUILD_TESTING)
    add_executable(protocol_tests
        tests/protocol/frame_decoder_test.cpp
        tests/protocol/json_request_parser_test.cpp
        tests/protocol/response_test.cpp
        src/protocol/frame.cpp
        src/protocol/frame_decoder.cpp
        src/protocol/json_request_parser.cpp
        src/protocol/response.cpp
    )

    target_include_directories(protocol_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )

    add_test(NAME protocol_tests COMMAND protocol_tests)
endif()
