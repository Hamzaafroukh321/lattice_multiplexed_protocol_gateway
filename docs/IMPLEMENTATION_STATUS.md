# Implementation Status

## Current Phase

Full implementation verification. The compact production implementation builds
with CMake, Ninja, and Clang. Scheduler, timer, trace, ACK, RESUME-window,
PING/PONG, retry, drain, replay snapshot storage, Unix transport and socket CLI
loop, gateway route/data-plane pump, route policy validation, frame inspection,
health scoring, deterministic/threaded executor primitives,
executor-backed plugin dispatch, plugin lease primitives, acceptance benchmark
probes, three production fuzz smoke targets, 40 deep fuzz targets,
ClusterFuzzLite metadata, and traceability documentation are present.

## Completed Modules

- `FrameCodec`: LTX1 framing, CRC32C with cached SSE4.2 acceleration and portable slicing-by-8 fallback, canonical ULEB128, sorted TLVs, streaming decode.
- `FrameCodec` compatibility: unknown optional extensions are accepted, unknown required extensions reject with a stable error.
- `Negotiator`: HELLO encode/decode, version/limit/feature/plugin schema intersection, transcript hash.
- `ChannelTable`: generated channels, generations, tombstones, send-sequence wrap rejection, remote OPEN acceptance, half-close/reset.
- `Reassembler`: bounded non-overlapping fragments, retained sparse-fragment budget enforcement, retained-byte cleanup on reset, and exact complete-message delivery.
- `FlowAccount`: checked reserve/release/grant and overflow/underflow errors.
- `ReplayWindow`: retained encoded frames, ACK-range retirement, retry bytes, exact retained suffix lookup for RESUME, and canonical `LTXREPLAY/1` snapshot serialization/restore.
- `ReplaySnapshotStore`: durable text snapshot load/save with temp-file publication.
- `TimerWheel`: generation-tagged deterministic timer scheduling, cancellation, and expiration.
- `OutboundScheduler`: bounded control/data queues with per-channel sequence order and partial-write tail retention.
- `TraceLog`: deterministic `LTXTRACE/1` serialization, parsing, and canonical replay verification summaries.
- `PluginRegistry`: static family registration and built-in echo plugin.
- `PluginLease`: quiescent unregister blocks while active or queued dispatch leases exist.
- `Gateway`: route IDs, registered source-route bridging, connection data-plane pumping into active destination channels, exact schema-match opaque forwarding, typed asymmetric translators, and destination limit checks.
- `RoutePolicy`: canonical text parsing, duplicate source/name validation, stable serialization, source lookup, and direct gateway route registration.
- `FrameInspection`: production decoder-backed stream inspection, DATA extension validation, sequence/goaway/control payload diagnostics, and connection event summaries.
- `HealthReport`: scored healthy/degraded/failed reports over streams, connection summaries, and route policies with filterable and bucketed signals.
- `DeterministicExecutor`/`ThreadedExecutor`: bounded task admission, deterministic drain order for tests, per-shard threaded runtime execution, cancellation/shutdown, shard validation, and stable connection-to-shard routing.
- `ConnectionEngine`: deterministic single-loop HELLO/OPEN/DATA/CREDIT/ACK/PING/PONG/RESUME/HALF_CLOSE/RESET/GOAWAY dispatch, retained-frame RESUME continuation, pre-start replay snapshot load/export with next-sequence restoration, optional executor-backed plugin dispatch, and PONG deadline liveness timeout.
- `UnixTransport`/`UnixListener`: POSIX connect/socketpair/read/write and bind/listen/accept adapters with stable transport errors on Windows.
- CLI `probe`, memory `bridge`, memory snapshot load/save, Unix socket `probe`/one-connection `serve`/continuous bridge loop, route-policy parsing, `dump`, canonical `replay` verification, and fixture generation commands plus fuzz smoke targets.
- Deep fuzz suite: 40 independent libFuzzer-compatible harnesses covering frame, HELLO, negotiation, connection, channel, reassembly, flow, replay, timer, trace, scheduler, gateway, plugin, executor, threaded executor, and memory transport paths.
- Compatibility fixtures: `fixtures/ltx1/memory_hello.trace` publishes deterministic LTX/1 HELLO bytes; `fixtures/ltx1/memory_bridge.policy` exercises route-policy parsing.

## Integrated Full-Version Modules

- Keepalive/retransmission timers, in-process retained RESUME continuation, replay snapshot serialization, engine pre-start snapshot loading, and CLI memory snapshot storage are integrated.
- POSIX Unix socket bridge pump is covered by Linux Docker runtime tests using `UnixTransport::pair_for_test`, production `ConnectionEngine` instances, and gateway forwarding.

## Platform Notes

- Windows Clang does not support this repository's TSan preset: `clang++: error: unsupported option '-fsanitize=thread' for target 'x86_64-pc-windows-msvc'`.
- Docker/Linux TSan requires ASLR disabled for the test process; `setarch x86_64 -R ctest --test-dir build/linux-tsan --output-on-failure` passed.
- Local ASan/UBSan test runs on Windows require the LLVM sanitizer runtime directory on `PATH`.

## Build And Test Status

- Debug configure/build: Passed with CMake 4.3.3, Ninja, and Clang 22.1.8 using the checked-in `local-clang-debug` preset.
- Release configure/build: Passed with CMake 4.3.3, Ninja, and Clang 22.1.8 using the checked-in `local-clang-release` preset and the generic `release` tree.
- Unit/integration, CLI, benchmark, and deep fuzz CTest smokes: Passed in Debug, Release, ASan/UBSan Release, local Clang Release, local Clang Debug, and Ubuntu 24.04 Docker Debug. Latest local Debug CTest passed 48/48 after the diagnostics and health modules were added.
- Fuzz smoke: Passed in Debug and ASan/UBSan Release for `lattice_frame_fuzz`, `lattice_connection_event_fuzz`, `lattice_gateway_trace_fuzz`, and the 40-target deep fuzz suite. Latest local Debug run passed the three historical fuzz smoke executables and all 40 deep fuzz CTest smokes.
- ClusterFuzzLite: Ubuntu 24.04 Docker with Clang built all 40 `.clusterfuzzlite` fuzzers into `$OUT`.
- ASan/UBSan: Release build and tests passed with LLVM sanitizer runtime on `PATH`.
- TSan: Passed under Ubuntu 24.04 Docker with GCC 13.3 using `setarch x86_64 -R`.
- Benchmark: `build/local-clang-release/lattice_bench.exe` measured 1241.36 MiB/s decode throughput, 47.4 us p99 in-memory 1 KiB message latency with plugin work deferred, 256 active channels, 5.23047 MiB process RSS, about 6.75M frame fuzz exec/s, and 11.63k connection-event exec/s.
- Soak: `scripts/soak.ps1 -BuildDir build/debug -Iterations 5000` passed memory probe repetitions.
- Static analysis: Not executed locally.

## Design Decisions

- ADR-0001 records a compact MVP implementation with memory transport and deterministic single-loop state.
- ADR-0002 records static C++ plugin registration instead of dynamic code loading.
- ADR-0003 records memory transport plus POSIX Unix transport/listener API behavior; Windows Unix CLI invocations return stable unsupported failures while Linux Docker covers the POSIX socket bridge pump.
