# Implementation Status

Generated from this environment: 2026-07-02.

## Selected Specification

`02_lattice_multiplexed_protocol_gateway.md` was selected because it matches the repository directory name and contains the complete numbered architecture, fuzzing, MVP acceptance, and full-version acceptance sections.

## Current Phase

Full implementation verification. The compact production implementation now builds with local CMake 4.3.3, Ninja, and Clang 22.1.8. Scheduler, timer, trace, ACK, RESUME-window, PING/PONG, retry, drain, replay snapshot storage, Unix transport and socket CLI loop, gateway route/data-plane pump, deterministic/threaded executor primitives, executor-backed plugin dispatch, plugin lease primitives, acceptance benchmark probes, three historical fuzz smoke targets, 40 deep fuzz targets, ClusterFuzzLite metadata, and traceability documentation are present.

## Last Completed Ticket

Deep fuzzing advanced: the repository now includes 40 independent libFuzzer-compatible entry points plus `.clusterfuzzlite/build.sh`, `.clusterfuzzlite/project.yaml`, and `fuzz/dictionary.txt`.

## Next Actionable Ticket

No source ticket is currently open from the selected specification. Remaining work is normal maintenance: rerun the recorded verification matrix after future code changes.

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
- `DeterministicExecutor`/`ThreadedExecutor`: bounded task admission, deterministic drain order for tests, per-shard threaded runtime execution, cancellation/shutdown, shard validation, and stable connection-to-shard routing.
- `ConnectionEngine`: deterministic single-loop HELLO/OPEN/DATA/CREDIT/ACK/PING/PONG/RESUME/HALF_CLOSE/RESET/GOAWAY dispatch, retained-frame RESUME continuation, pre-start replay snapshot load/export with next-sequence restoration, optional executor-backed plugin dispatch, and PONG deadline liveness timeout.
- `UnixTransport`/`UnixListener`: POSIX connect/socketpair/read/write and bind/listen/accept adapters with stable transport errors on Windows.
- CLI `probe`, memory `bridge`, memory snapshot load/save, Unix socket `probe`/one-connection `serve`/continuous bridge loop, route-policy parsing, `dump`, canonical `replay` verification, and fixture generation commands plus fuzz smoke targets.
- Deep fuzz suite: 40 independent libFuzzer-compatible harnesses covering frame, HELLO, negotiation, connection, channel, reassembly, flow, replay, timer, trace, scheduler, gateway, plugin, executor, threaded executor, and memory transport paths.
- Compatibility fixtures: `fixtures/ltx1/memory_hello.trace` publishes deterministic LTX/1 HELLO bytes; `fixtures/ltx1/memory_bridge.policy` exercises route-policy parsing.

## Integrated Full-Version Modules

- Keepalive/retransmission timers, in-process retained RESUME continuation, replay snapshot serialization, engine pre-start snapshot loading, and CLI memory snapshot storage are integrated.
- POSIX Unix socket bridge pump is covered by Linux Docker runtime tests using `UnixTransport::pair_for_test`, production `ConnectionEngine` instances, and gateway forwarding.

## Known Blockers

- Windows Clang still cannot build TSan: `clang++: error: unsupported option '-fsanitize=thread' for target 'x86_64-pc-windows-msvc'`.
- Docker/Linux TSan requires ASLR disabled for the test process; `setarch x86_64 -R ctest --test-dir build/linux-tsan --output-on-failure` passed.
- LLVM installation through `winget install LLVM.LLVM` was cancelled by user/UAC, but usable LLVM binaries were already present under `C:\Program Files\LLVM`.

## Build And Test Status

- Debug configure/build: Passed with CMake 4.3.3, Ninja, Clang 22.1.8 using the checked-in `local-clang-debug` preset.
- Release configure/build: Passed with CMake 4.3.3, Ninja, Clang 22.1.8 using the checked-in `local-clang-release` preset and the generic `release` tree.
- Unit/integration, CLI, benchmark, and deep fuzz CTest smokes: Passed in Debug, Release, ASan/UBSan Release, local Clang Release, local Clang Debug, and Ubuntu 24.04 Docker Debug.
- Fuzz smoke: Passed in Debug and ASan/UBSan Release for `lattice_frame_fuzz`, `lattice_connection_event_fuzz`, `lattice_gateway_trace_fuzz`, and the 40-target deep fuzz suite.
- ClusterFuzzLite: Ubuntu 24.04 Docker with Clang built all 40 `.clusterfuzzlite` fuzzers into `$OUT`.
- ASan/UBSan: Release build and tests passed with LLVM sanitizer runtime on `PATH`. Debug ASan hit Windows debug CRT/ASan runtime mismatch during shutdown.
- TSan: Passed under Ubuntu 24.04 Docker with GCC 13.3 using `setarch x86_64 -R`; Windows Clang target remains unsupported for TSan.
- Benchmark: `build/local-clang-release/lattice_bench.exe` measured 1241.36 MiB/s decode throughput, 47.4 us p99 in-memory 1 KiB message latency with plugin work deferred, 256 active channels, 5.23047 MiB process RSS, about 6.75M frame fuzz exec/s, and 11.63k connection-event exec/s.
- Soak: `scripts/soak.ps1 -BuildDir build/debug -Iterations 5000` passed memory probe repetitions.
- Static analysis: Not executed locally.
- Source checks: `rg -n "TODO|FIXME|unimplemented|abort\(" --glob "!docs/IMPLEMENTATION_STATUS.md" .` returned no matches.

## Deviations

- ADR-0001 records a compact MVP implementation with memory transport and deterministic single-loop state.
- ADR-0002 records static C++ plugin registration instead of dynamic code loading.
- ADR-0003 records memory transport plus POSIX Unix transport/listener API behavior; Windows Unix CLI invocations return stable unsupported failures while Linux Docker covers the POSIX socket bridge pump.

## Last Verified Commit

This commit. The exact hash is not embedded because amending the file changes the hash.
