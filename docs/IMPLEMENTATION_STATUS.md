# Implementation Status

Generated from this environment: 2026-06-26T16:33:55.7674858+01:00.

## Selected Specification

`02_lattice_multiplexed_protocol_gateway.md` was selected because it matches the repository directory name and contains the complete numbered architecture, fuzzing, MVP acceptance, and full-version acceptance sections.

## Current Phase

Phase 6/7: hardening, fuzzing, and documentation. The compact production implementation now builds with local CMake 4.3.3, Ninja, and Clang 22.1.8. Scheduler, timer, trace, ACK, RESUME-window, PING/PONG, retry, drain, replay snapshot storage, Unix transport and socket CLI loop, gateway route/data-plane pump, deterministic/threaded executor primitives, executor-backed plugin dispatch, and plugin lease primitives are present.

## Last Completed Ticket

LAT-018 advanced: a bounded `ThreadedExecutor` now runs per-shard worker queues with serial order inside each shard, stable shutdown, and first-error reporting.

## Next Actionable Ticket

Next source tickets:

- Extend model/performance stress coverage beyond the current smoke and 5000-iteration soak.

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
- Compatibility fixtures: `fixtures/ltx1/memory_hello.trace` publishes deterministic LTX/1 HELLO bytes; `fixtures/ltx1/memory_bridge.policy` exercises route-policy parsing.

## In Progress Modules

- Keepalive/retransmission timers, in-process retained RESUME continuation, replay snapshot serialization, engine pre-start snapshot loading, and CLI memory snapshot storage are integrated.
- POSIX Unix socket bridge pump is covered by Linux Docker runtime tests using `UnixTransport::pair_for_test`, production `ConnectionEngine` instances, and gateway forwarding.

## Known Blockers

- Windows Clang still cannot build TSan: `clang++: error: unsupported option '-fsanitize=thread' for target 'x86_64-pc-windows-msvc'`.
- Docker/Linux TSan requires ASLR disabled for the test process; `setarch x86_64 -R ctest --test-dir build/linux-tsan --output-on-failure` passed.
- LLVM installation through `winget install LLVM.LLVM` was cancelled by user/UAC, but usable LLVM binaries were already present under `C:\Program Files\LLVM`.

## Build And Test Status

- Debug configure/build: Passed with CMake 4.3.3, Ninja, Clang 22.1.8 using the checked-in `local-clang-debug` preset.
- Release configure/build: Passed with CMake 4.3.3, Ninja, Clang 22.1.8 using the checked-in `local-clang-release` preset and the generic `release` tree.
- Unit/integration and CLI CTest smokes: Passed in Debug, Release, ASan/UBSan Release, local Clang Release, local Clang Debug, and Ubuntu 24.04 Docker Debug.
- Fuzz smoke: Passed in Debug and ASan/UBSan Release for `lattice_frame_fuzz`, `lattice_connection_event_fuzz`, and `lattice_gateway_trace_fuzz`.
- ASan/UBSan: Release build and tests passed. Debug ASan hit Windows debug CRT/ASan runtime mismatch during shutdown.
- TSan: Passed under Ubuntu 24.04 Docker with GCC 13.3 using `setarch x86_64 -R`; Windows Clang target remains unsupported for TSan.
- Benchmark: `build/local-clang-release/lattice_bench.exe` measured 1189.73, 1186.52, 1278.26, and 1306.83 MiB/s in current samples; this meets the MVP frame decode target with the pinned local Clang preset. The generic `build/release` tree sampled lower on this host after full rebuild.
- Soak: `scripts/soak.ps1 -BuildDir build/debug -Iterations 5000` passed memory probe repetitions.
- Static analysis: Not executed locally.
- Source checks: `rg -n "TODO|FIXME|unimplemented|abort\(" --glob "!docs/IMPLEMENTATION_STATUS.md" .` returned no matches.

## Deviations

- ADR-0001 records a compact MVP implementation with memory transport and deterministic single-loop state.
- ADR-0002 records static C++ plugin registration instead of dynamic code loading.
- ADR-0003 records memory transport plus POSIX Unix transport/listener API behavior; Windows Unix CLI invocations return stable unsupported failures while Linux Docker covers the POSIX socket bridge pump.

## Last Verified Commit

This commit. The exact hash is not embedded because amending the file changes the hash.
