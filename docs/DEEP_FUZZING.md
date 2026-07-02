# Deep Fuzzing

The repository includes 40 independent libFuzzer-compatible harnesses under
`fuzz/deep/`. Each harness has its own executable and `LLVMFuzzerTestOneInput`
entry point, while sharing bounded input helpers in `fuzz/deep_fuzz_support.hpp`.

The design follows four rules from the fuzzing benchmark guidance:

- Harnesses must call production APIs, not duplicate protocol logic in the harness.
- Inputs must drive stateful paths where possible: negotiated connections, fragmented messages, replay retention, timers, schedulers, gateway routes, and executor-backed plugin dispatch.
- Harness code must stay deterministic, bounded, offline, and non-interactive.
- Crashes are useful only when they expose a real protocol invariant; do not add synthetic harness-only bugs or magic-byte gates.

## ClusterFuzzLite Layout

The repository root now contains:

- `.clusterfuzzlite/build.sh`: builds every deep harness into `$OUT` without network access.
- `.clusterfuzzlite/project.yaml`: lists all 40 fuzz targets.
- `fuzz/dictionary.txt`: protocol tokens, frame type bytes, trace/replay markers, and API words.
- `corpus/`: existing seed corpus for frame, trace, and event inputs.

The build script compiles the project sources once into objects and links each
fuzzer against `$LIB_FUZZING_ENGINE`. It expects the standard ClusterFuzzLite
environment variables `$SRC`, `$OUT`, `$WORK`, `$CXX`, `$CXXFLAGS`, and
`$LIB_FUZZING_ENGINE`; local defaults are provided for smoke use.

## Target Inventory

| Target | Primary surface |
|---|---|
| `lattice_frame_stream_fuzzer` | Arbitrary chunked streaming decode with bounded frame/header limits |
| `lattice_frame_split_fuzzer` | Byte-by-byte decode of canonical encoded frames |
| `lattice_frame_extension_fuzzer` | Required/optional extension ordering, sizes, and canonical reparse |
| `lattice_frame_header_limit_fuzzer` | Header and frame allocation-limit rejection |
| `lattice_frame_crc_mutation_fuzzer` | CRC mismatch, mutated encoded bytes, and decoder recovery |
| `lattice_frame_concat_eof_fuzzer` | Concatenated frames, truncation, and EOF handling |
| `lattice_hello_payload_fuzzer` | HELLO payload decode/encode and policy limits |
| `lattice_negotiation_matrix_fuzzer` | Version, feature, limit, and plugin schema negotiation |
| `lattice_capability_policy_fuzzer` | Gateway policy forwarding and translator decision paths |
| `lattice_connection_peer_bytes_fuzzer` | Negotiated engines receiving malformed peer byte chunks |
| `lattice_connection_chunk_fuzzer` | Split HELLO delivery through production connection decode |
| `lattice_connection_open_fuzzer` | Repeated OPEN operations and remote acceptance |
| `lattice_connection_data_ext_fuzzer` | DATA frames with message sequence, offset, total, and family extensions |
| `lattice_connection_credit_ack_fuzzer` | CREDIT and ACK range payload interactions |
| `lattice_connection_resume_fuzzer` | RESUME transcript/epoch/retained-sequence behavior |
| `lattice_connection_timer_fuzzer` | Handshake, idle, PONG, retry, and drain timer advancement |
| `lattice_connection_reset_goaway_fuzzer` | HALF_CLOSE, RESET, GOAWAY, and close-state transitions |
| `lattice_connection_outbound_fuzzer` | Outbound scheduler flushing through the connection API |
| `lattice_connection_multichannel_fuzzer` | Multiple generated channels and cross-channel DATA delivery |
| `lattice_connection_event_script_fuzzer` | Stateful API script with executor-backed plugin dispatch |
| `lattice_channel_table_fuzzer` | Channel allocation, remote acceptance, activation, reset, tombstones |
| `lattice_reassembler_ranges_fuzzer` | Fragment offsets, totals, sparse retention, and reset |
| `lattice_reassembler_overlap_fuzzer` | Conflicting and adjacent fragment overlap behavior |
| `lattice_flow_account_fuzzer` | Reserve/release/grant conservation and overflow/underflow errors |
| `lattice_replay_window_fuzzer` | Replay record, ACK retirement, retry selection, retained suffixes |
| `lattice_replay_snapshot_fuzzer` | `LTXREPLAY/1` restore/serialize round trips and malformed text |
| `lattice_ack_payload_fuzzer` | ACK payload decode/encode and range validation |
| `lattice_timer_wheel_fuzzer` | Timer schedule/cancel/expire ordering and generation tags |
| `lattice_trace_parse_fuzzer` | `LTXTRACE/1` parse, canonical serialize, and replay verification |
| `lattice_trace_build_fuzzer` | Generated trace events across all trace kinds |
| `lattice_scheduler_mixed_fuzzer` | Control/data enqueueing, byte limits, and drains |
| `lattice_scheduler_backpressure_fuzzer` | Sustained small writes and partial tail retention |
| `lattice_scheduler_order_fuzzer` | Per-channel sequence order and round-robin data scheduling |
| `lattice_gateway_policy_fuzzer` | Opaque forwarding, schema checks, translators, and limits |
| `lattice_gateway_route_fuzzer` | Route creation, source lookup, and bridge-message policy |
| `lattice_gateway_bridge_fuzzer` | End-to-end message forwarding into a destination connection |
| `lattice_plugin_registry_fuzzer` | Register/create/lease/unregister and quiescent unload errors |
| `lattice_executor_queue_fuzzer` | Bounded deterministic executor submission, cancel, and drain |
| `lattice_threaded_executor_fuzzer` | Bounded threaded executor shard submission, idle wait, shutdown |
| `lattice_memory_transport_fuzzer` | Memory transport queues, reads, close behavior, and byte accounting |

## Local Smoke

The normal CMake build creates standalone versions of all deep fuzz targets.
Each executable can run with no argument using a small built-in seed or with a
single input file:

```powershell
cmake --preset debug
cmake --build --preset debug
.\build\debug\lattice_frame_stream_fuzzer.exe
.\build\debug\lattice_gateway_bridge_fuzzer.exe corpus\frames\hello.ltxframe
```

CTest also registers one smoke test per deep fuzz executable, so:

```powershell
ctest --preset debug --output-on-failure
```

verifies that all harnesses build, start, and reach production code.

## Campaign Guidance

Run broad campaigns first, then spend longer time on the stateful targets:

- Frame/HELLO harnesses are fast structural coverage and good for corpus growth.
- Connection, fragment, replay, scheduler, and gateway harnesses are slower but reach the highest-value state transitions.
- Preserve minimized crash inputs under the relevant corpus directory only after turning the bug into a deterministic regression test.
- A valid fix must preserve neighboring valid behavior: split frames, fragmented messages, RESUME, gateway forwarding, and scheduler ordering must still pass after the crash input stops reproducing.

Avoid benchmark anti-patterns:

- Do not introduce intentional one-function bugs in `fuzz/` or helper glue.
- Do not hide behavior behind a single magic byte or one obvious length field.
- Do not accept patches that merely reject a whole frame type, disable reassembly, or skip gateway forwarding.
- Do not rely on network access, filesystem prompts, private credentials, or machine-specific absolute paths.
