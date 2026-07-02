cmake_minimum_required(VERSION 3.22)

project(lattice_multiplexed_protocol_gateway
  VERSION 0.1.0
  DESCRIPTION "Lattice multiplexed binary protocol gateway core"
  LANGUAGES CXX)

include(CTest)
include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lattice_core
  src/format/frame_codec.cpp
  src/connection/negotiator.cpp
  src/connection/event_loop.cpp
  src/channel/channel_state.cpp
  src/replay/replay_window.cpp
  src/replay/replay_store.cpp
  src/replay/trace.cpp
  src/schedule/outbound_scheduler.cpp
  src/plugin/registry.cpp
  src/gateway/gateway.cpp
  src/transport/memory_transport.cpp
  src/transport/unix_transport.cpp
  src/connection/engine.cpp)
target_include_directories(lattice_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
lattice_apply_warnings(lattice_core)
lattice_apply_sanitizers(lattice_core)

add_executable(lattice tools/lattice.cpp)
target_link_libraries(lattice PRIVATE lattice_core)
lattice_apply_warnings(lattice)
lattice_apply_sanitizers(lattice)

add_executable(lattice_bench tools/lattice_bench.cpp)
target_link_libraries(lattice_bench PRIVATE lattice_core)
if(WIN32)
  target_link_libraries(lattice_bench PRIVATE Psapi)
endif()
lattice_apply_warnings(lattice_bench)
lattice_apply_sanitizers(lattice_bench)
add_test(NAME lattice_bench_acceptance COMMAND lattice_bench)

add_executable(lattice_tests
  tests/unit/frame_tests.cpp
  tests/unit/channel_tests.cpp
  tests/unit/flow_tests.cpp
  tests/unit/gateway_tests.cpp
  tests/unit/executor_tests.cpp
  tests/unit/plugin_tests.cpp
  tests/unit/replay_tests.cpp
  tests/unit/scheduler_tests.cpp
  tests/unit/trace_tests.cpp
  tests/unit/transport_tests.cpp
  tests/integration/handshake.cpp
  tests/integration/multiplex.cpp
  tests/test_main.cpp)
target_link_libraries(lattice_tests PRIVATE lattice_core)
target_include_directories(lattice_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
lattice_apply_warnings(lattice_tests)
lattice_apply_sanitizers(lattice_tests)
add_test(NAME lattice_tests COMMAND lattice_tests)

add_test(NAME lattice_cli_probe_memory COMMAND lattice probe --memory)
add_test(NAME lattice_cli_probe_memory_snapshot
  COMMAND lattice probe --memory --snapshot ${CMAKE_CURRENT_BINARY_DIR}/memory_probe.ltxreplay)
add_test(NAME lattice_cli_bridge_memory_policy
  COMMAND lattice bridge --memory --policy ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ltx1/memory_bridge.policy)
if(WIN32)
  add_test(NAME lattice_cli_probe_unix_unsupported COMMAND lattice probe --socket lattice-test.sock)
  add_test(NAME lattice_cli_serve_unix_unsupported
    COMMAND lattice serve --socket lattice-test.sock --plugin echo)
  add_test(NAME lattice_cli_bridge_unix_unsupported
    COMMAND lattice bridge --left left.sock --right right.sock --policy
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ltx1/memory_bridge.policy)
  set_tests_properties(lattice_cli_probe_unix_unsupported
                       lattice_cli_serve_unix_unsupported
                       lattice_cli_bridge_unix_unsupported
                       PROPERTIES WILL_FAIL TRUE)
endif()

foreach(fuzz_name IN ITEMS lattice_frame_fuzz lattice_connection_event_fuzz lattice_gateway_trace_fuzz)
  add_executable(${fuzz_name} fuzz/${fuzz_name}.cpp)
  target_link_libraries(${fuzz_name} PRIVATE lattice_core)
  lattice_apply_warnings(${fuzz_name})
  lattice_apply_sanitizers(${fuzz_name})
endforeach()

set(LATTICE_DEEP_FUZZ_TARGETS
  lattice_frame_stream_fuzzer
  lattice_frame_split_fuzzer
  lattice_frame_extension_fuzzer
  lattice_frame_header_limit_fuzzer
  lattice_frame_crc_mutation_fuzzer
  lattice_frame_concat_eof_fuzzer
  lattice_hello_payload_fuzzer
  lattice_negotiation_matrix_fuzzer
  lattice_capability_policy_fuzzer
  lattice_connection_peer_bytes_fuzzer
  lattice_connection_chunk_fuzzer
  lattice_connection_open_fuzzer
  lattice_connection_data_ext_fuzzer
  lattice_connection_credit_ack_fuzzer
  lattice_connection_resume_fuzzer
  lattice_connection_timer_fuzzer
  lattice_connection_reset_goaway_fuzzer
  lattice_connection_outbound_fuzzer
  lattice_connection_multichannel_fuzzer
  lattice_connection_event_script_fuzzer
  lattice_channel_table_fuzzer
  lattice_reassembler_ranges_fuzzer
  lattice_reassembler_overlap_fuzzer
  lattice_flow_account_fuzzer
  lattice_replay_window_fuzzer
  lattice_replay_snapshot_fuzzer
  lattice_ack_payload_fuzzer
  lattice_timer_wheel_fuzzer
  lattice_trace_parse_fuzzer
  lattice_trace_build_fuzzer
  lattice_scheduler_mixed_fuzzer
  lattice_scheduler_backpressure_fuzzer
  lattice_scheduler_order_fuzzer
  lattice_gateway_policy_fuzzer
  lattice_gateway_route_fuzzer
  lattice_gateway_bridge_fuzzer
  lattice_plugin_registry_fuzzer
  lattice_executor_queue_fuzzer
  lattice_threaded_executor_fuzzer
  lattice_memory_transport_fuzzer)

foreach(fuzz_name IN LISTS LATTICE_DEEP_FUZZ_TARGETS)
  add_executable(${fuzz_name} fuzz/deep/${fuzz_name}.cpp)
  target_include_directories(${fuzz_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
  target_link_libraries(${fuzz_name} PRIVATE lattice_core)
  lattice_apply_warnings(${fuzz_name})
  lattice_apply_sanitizers(${fuzz_name})
  add_test(NAME ${fuzz_name}_smoke COMMAND ${fuzz_name})
endforeach()

install(TARGETS lattice_core lattice
  EXPORT latticeTargets
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
