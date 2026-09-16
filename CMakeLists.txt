cmake_minimum_required(VERSION 3.20)

project(GateKeeper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(gatekeeper_library target)
    add_library(${target} STATIC ${ARGN})
    target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/include)
endfunction()

gatekeeper_library(gk_config src/config/arguments.cpp)
gatekeeper_library(gk_command_names src/command/name.cpp)
gatekeeper_library(gk_protocol
    src/protocol/frame.cpp
    src/protocol/frame_decoder.cpp
    src/protocol/json_request_parser.cpp
    src/protocol/response.cpp
)
gatekeeper_library(gk_commands src/command/command_dispatcher.cpp)
target_link_libraries(gk_commands PUBLIC gk_protocol gk_command_names)

gatekeeper_library(gk_service src/service/options.cpp src/service/request_processor.cpp)
target_link_libraries(gk_service PUBLIC gk_config gk_commands)

gatekeeper_library(gk_cli
    src/cli/options.cpp
    src/cli/application.cpp
    src/cli/command_registry.cpp
    src/cli/command_suggestions.cpp
)
target_link_libraries(gk_cli PUBLIC gk_config gk_command_names)

if(UNIX)
    gatekeeper_library(gk_network_errors src/net/network_error.cpp)
    gatekeeper_library(gk_server_transport src/net/tcp_server.cpp src/net/client_connection.cpp)
    target_link_libraries(gk_server_transport PUBLIC gk_protocol gk_network_errors)
    gatekeeper_library(gk_client_transport src/cli/tcp_client.cpp)
    target_link_libraries(gk_client_transport PUBLIC gk_protocol gk_network_errors)

    add_executable(gatekeeper src/main.cpp)
    target_link_libraries(gatekeeper PRIVATE gk_service gk_server_transport)
    add_executable(gate src/gate_main.cpp)
    target_link_libraries(gate PRIVATE gk_cli gk_client_transport)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(options_tests tests/options_test.cpp)
    target_link_libraries(options_tests PRIVATE gk_cli gk_service)
    add_test(NAME options_tests COMMAND options_tests)

    add_executable(protocol_tests
        tests/protocol/frame_decoder_test.cpp
        tests/protocol/json_request_parser_test.cpp
        tests/protocol/response_test.cpp
        tests/command/command_dispatcher_test.cpp
        tests/cli/command_test.cpp
    )
    target_link_libraries(protocol_tests PRIVATE gk_commands)
    if(MSVC)
        target_compile_options(protocol_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(protocol_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME protocol_tests COMMAND protocol_tests)

    add_executable(cli_tests tests/cli/application_test.cpp)
    target_link_libraries(cli_tests PRIVATE gk_cli)
    add_test(NAME cli_tests COMMAND cli_tests)

    add_executable(request_processor_tests tests/service/request_processor_test.cpp)
    target_link_libraries(request_processor_tests PRIVATE gk_service)
    add_test(NAME request_processor_tests COMMAND request_processor_tests)

    if(UNIX)
        add_executable(network_server_probe tests/net/server_probe.cpp)
        target_link_libraries(network_server_probe PRIVATE gk_server_transport)
        add_executable(client_connection_tests tests/net/client_connection_test.cpp)
        target_link_libraries(client_connection_tests PRIVATE gk_server_transport)
        add_test(NAME client_connection_tests COMMAND client_connection_tests)
        set_tests_properties(client_connection_tests PROPERTIES TIMEOUT 10)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME cli_integration_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/cli/integration_test.py $<TARGET_FILE:gate>)
            add_test(NAME network_error_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/net/error_test.py
                    $<TARGET_FILE:gate> $<TARGET_FILE:network_server_probe>)
            add_test(NAME service_port_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/service/port_test.py
                    $<TARGET_FILE:gatekeeper> $<TARGET_FILE:gate>)
            set_tests_properties(cli_integration_tests network_error_tests service_port_tests PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()
