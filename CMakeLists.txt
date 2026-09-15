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
    src/command/command_dispatcher.cpp
)

add_executable(gate
    src/gate_main.cpp
    src/cli/application.cpp
    src/cli/command_registry.cpp
    src/cli/tcp_client.cpp
    src/protocol/frame.cpp
)

target_include_directories(gate
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
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
        src/command/command_dispatcher.cpp
        tests/command/command_dispatcher_test.cpp
        tests/cli/command_test.cpp
    )

    target_include_directories(protocol_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )

    add_test(NAME protocol_tests COMMAND protocol_tests)
    add_executable(cli_tests
        tests/cli/application_test.cpp
        src/cli/application.cpp
        src/cli/command_registry.cpp
    )
    target_include_directories(cli_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
    add_test(NAME cli_tests COMMAND cli_tests)
    if(UNIX)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME cli_integration_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/cli/integration_test.py $<TARGET_FILE:gate>)
            set_tests_properties(cli_integration_tests PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()
