cmake_minimum_required(VERSION 3.20)

project(GateKeeper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(gatekeeper_library target)
    add_library(${target} STATIC ${ARGN})
    target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/include)
endfunction()

gatekeeper_library(gk_log
    src/core/log/logger.cpp
    src/core/log/sink.cpp
)

gatekeeper_library(gk_config
    src/core/config/arguments.cpp
    src/core/config/server_config.cpp
    src/core/config/cli_config.cpp
)
target_link_libraries(gk_config PUBLIC gk_log)

gatekeeper_library(gk_command_names
    src/command/name.cpp
)

gatekeeper_library(gk_protocol
    src/protocol/gkwp/frame.cpp
    src/protocol/gkwp/decoder.cpp
    src/protocol/gkwp/parser.cpp
    src/protocol/gkwp/response.cpp
    src/protocol/gkwp/json_reader.cpp
    src/protocol/http/http_channel.cpp
)

gatekeeper_library(gk_storage
    src/storage/memory_store.cpp
    src/storage/aof/aof_writer.cpp
    src/storage/aof/aof_loader.cpp
)

gatekeeper_library(gk_domain
    src/domain/kv/string_ops.cpp
    src/domain/kv/string_body.cpp
    src/domain/kv/key_ops.cpp
    src/domain/kv/ttl_ops.cpp
    src/domain/ratelimit/counter_ops.cpp
    src/domain/quota/reservation_ops.cpp
    src/domain/idempotency/idempotency_ops.cpp
    src/domain/idempotency/parking_lot.cpp
)
target_link_libraries(gk_domain PUBLIC gk_protocol gk_command_names gk_storage)

gatekeeper_library(gk_commands
    src/command/dispatcher.cpp
)
target_link_libraries(gk_commands PUBLIC gk_domain gk_protocol gk_command_names gk_storage)

gatekeeper_library(gk_service
    src/service/processor.cpp
    src/service/http_service.cpp
)
target_link_libraries(gk_service PUBLIC gk_config gk_commands gk_log gk_net)

gatekeeper_library(gk_net
    src/net/error.cpp
    src/net/connection.cpp
    src/net/server.cpp
    src/net/client.cpp
    src/net/event_loop.cpp
    src/net/channel.cpp
)
target_link_libraries(gk_net PUBLIC gk_protocol gk_log)

gatekeeper_library(gk_cli
    src/cli/suggest.cpp
    src/cli/registry.cpp
    src/cli/app.cpp
    src/cli/format.cpp
)
target_link_libraries(gk_cli PUBLIC gk_config gk_command_names gk_net)

if(UNIX)
    add_executable(gatekeeper src/server_main.cpp)
    target_link_libraries(gatekeeper PRIVATE gk_service gk_net gk_log)

    add_executable(gate src/cli_main.cpp)
    target_link_libraries(gate PRIVATE gk_cli gk_net gk_log)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(string_commands_tests tests/domain/kv/string_commands_test.cpp)
    target_link_libraries(string_commands_tests PRIVATE gk_commands)
    add_test(NAME string_commands_tests COMMAND string_commands_tests)

    add_executable(hash_table_tests tests/storage/hash_table_test.cpp)
    target_link_libraries(hash_table_tests PRIVATE gk_storage)
    add_test(NAME hash_table_tests COMMAND hash_table_tests)

    add_executable(key_commands_tests tests/domain/kv/key_commands_test.cpp)
    target_link_libraries(key_commands_tests PRIVATE gk_commands)
    add_test(NAME key_commands_tests COMMAND key_commands_tests)

    add_executable(ttl_commands_tests tests/domain/kv/ttl_commands_test.cpp)
    target_link_libraries(ttl_commands_tests PRIVATE gk_commands)
    add_test(NAME ttl_commands_tests COMMAND ttl_commands_tests)

    add_executable(counter_commands_tests tests/domain/ratelimit/counter_commands_test.cpp)
    target_link_libraries(counter_commands_tests PRIVATE gk_commands)
    add_test(NAME counter_commands_tests COMMAND counter_commands_tests)

    add_executable(reservation_commands_tests tests/domain/quota/reservation_commands_test.cpp)
    target_link_libraries(reservation_commands_tests PRIVATE gk_commands)
    add_test(NAME reservation_commands_tests COMMAND reservation_commands_tests)

    add_executable(idempotency_state_tests tests/domain/idempotency/idempotency_state_test.cpp)
    target_link_libraries(idempotency_state_tests PRIVATE gk_storage)
    add_test(NAME idempotency_state_tests COMMAND idempotency_state_tests)

    add_executable(idempotency_commands_tests tests/domain/idempotency/idempotency_commands_test.cpp)
    target_link_libraries(idempotency_commands_tests PRIVATE gk_commands)
    add_test(NAME idempotency_commands_tests COMMAND idempotency_commands_tests)

    add_executable(parking_lot_tests tests/domain/idempotency/parking_lot_test.cpp)
    target_link_libraries(parking_lot_tests PRIVATE gk_domain)
    add_test(NAME parking_lot_tests COMMAND parking_lot_tests)

    add_executable(aof_tests tests/storage/aof_test.cpp)
    target_link_libraries(aof_tests PRIVATE gk_commands)
    add_test(NAME aof_tests COMMAND aof_tests)

    add_executable(concurrent_clients_tests tests/net/concurrent_clients_test.cpp)
    target_link_libraries(concurrent_clients_tests PRIVATE gk_net gk_storage)
    add_test(NAME concurrent_clients_tests COMMAND concurrent_clients_tests)

    add_executable(event_loop_tests tests/net/event_loop_test.cpp)
    target_link_libraries(event_loop_tests PRIVATE gk_net)
    add_test(NAME event_loop_tests COMMAND event_loop_tests)

    add_executable(options_tests tests/options_test.cpp)
    target_link_libraries(options_tests PRIVATE gk_config)
    add_test(NAME options_tests COMMAND options_tests)

    add_executable(protocol_tests
        tests/protocol/gkwp/frame_decoder_test.cpp
        tests/protocol/gkwp/json_request_parser_test.cpp
        tests/protocol/gkwp/response_test.cpp
        tests/command/command_dispatcher_test.cpp
        tests/cli/command_test.cpp
    )
    target_link_libraries(protocol_tests PRIVATE gk_commands gk_cli)
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
        target_link_libraries(network_server_probe PRIVATE gk_net)

        add_executable(client_connection_tests tests/net/client_connection_test.cpp)
        target_link_libraries(client_connection_tests PRIVATE gk_net)
        add_test(NAME client_connection_tests COMMAND client_connection_tests)
        set_tests_properties(client_connection_tests PROPERTIES TIMEOUT 10)

        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME string_cli_integration_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/cli/string_integration_test.py
                    $<TARGET_FILE:gatekeeper> $<TARGET_FILE:gate>)
            set_tests_properties(string_cli_integration_tests PROPERTIES TIMEOUT 30)

            add_test(NAME cli_integration_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/cli/integration_test.py $<TARGET_FILE:gate>)

            add_test(NAME network_error_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/net/error_test.py
                    $<TARGET_FILE:gate> $<TARGET_FILE:network_server_probe>)

            add_test(NAME service_port_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/service/port_test.py
                    $<TARGET_FILE:gatekeeper> $<TARGET_FILE:gate>)

            add_test(NAME http_api_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/protocol/http/http_api_test.py
                    $<TARGET_FILE:gatekeeper>)

            add_test(NAME concurrency_rate_limit_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/stress/concurrency_rate_limit_test.py
                    $<TARGET_FILE:gatekeeper>)

            add_test(NAME auto_rollback_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/stress/auto_rollback_test.py
                    $<TARGET_FILE:gatekeeper>)

                        add_test(NAME idempotency_coalescing_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/stress/idempotency_coalescing_test.py
                    $<TARGET_FILE:gatekeeper>)

            add_test(NAME idempotency_recovery_tests
                COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/stress/idempotency_recovery_test.py
                    $<TARGET_FILE:gatekeeper>)

            set_tests_properties(cli_integration_tests network_error_tests service_port_tests http_api_tests concurrency_rate_limit_tests auto_rollback_tests idempotency_coalescing_tests idempotency_recovery_tests PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()