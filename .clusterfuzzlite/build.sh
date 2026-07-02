#!/bin/bash -eu

SRC="${SRC:-$(pwd)}"
OUT="${OUT:-$SRC/out}"
WORK="${WORK:-/tmp/lattice-cflite-build}"
mkdir -p "$OUT" "$WORK"

CXX="${CXX:-clang++}"
CXXFLAGS="${CXXFLAGS:-}"
LIB_FUZZING_ENGINE="${LIB_FUZZING_ENGINE:--fsanitize=fuzzer}"

COMMON_FLAGS=(
  -std=c++20
  -I"$SRC/include"
  -I"$SRC/fuzz"
)

CORE_SOURCES=(
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
  src/connection/engine.cpp
)

CORE_OBJECTS=()
for source in "${CORE_SOURCES[@]}"; do
  object="$WORK/$(basename "$source").o"
  "$CXX" $CXXFLAGS "${COMMON_FLAGS[@]}" -c "$SRC/$source" -o "$object"
  CORE_OBJECTS+=("$object")
done

FUZZ_TARGETS=(
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
  lattice_memory_transport_fuzzer
)

for target in "${FUZZ_TARGETS[@]}"; do
  "$CXX" $CXXFLAGS "${COMMON_FLAGS[@]}" -DLATTICE_LIBFUZZER \
    "$SRC/fuzz/deep/$target.cpp" "${CORE_OBJECTS[@]}" \
    $LIB_FUZZING_ENGINE -o "$OUT/$target"
done
