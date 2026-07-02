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
lattice_apply_warnings(lattice_bench)
lattice_apply_sanitizers(lattice_bench)

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

install(TARGETS lattice_core lattice
  EXPORT latticeTargets
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
