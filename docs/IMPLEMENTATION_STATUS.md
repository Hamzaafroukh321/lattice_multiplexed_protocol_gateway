# Implementation Status

Generated from this environment: 2026-06-26T16:33:55.7674858+01:00.

## Selected Specification

`02_lattice_multiplexed_protocol_gateway.md` was selected because it matches the repository directory name and contains the complete numbered architecture, fuzzing, MVP acceptance, and full-version acceptance sections.

## Current Phase

Phase 6/7: hardening, fuzzing, and documentation. The compact production implementation now builds with local CMake 4.3.3, Ninja, and Clang 22.1.8. Scheduler, timer, trace, ACK, RESUME-window, PING/PONG, retry, drain, Unix transport, gateway route, executor-backed plugin dispatch, and plugin lease primitives are present.

## Last Completed Ticket

LAT-027/LAT-028/LAT-025/LAT-036 advanced: gateway routes now bridge registered source channels and typed translators can cross schema hashes while opaque mismatches are rejected; `ConnectionEngine` can schedule plugin work on `DeterministicExecutor`; frame decode uses cached SSE4.2 CRC32C when available with a slicing-by-8 fallback and now exceeds the MVP decode budget in Release on this environment.

## Next Actionable Ticket

Next source tickets:

- Add long-running sequence wrap, generation wrap, backpressure, and allocation-failure fixtures.
- Implement Unix endpoint serve/bridge CLI mode and a route-policy file parser.
- Replace ad hoc explicit tool paths with a documented local toolchain preset.

## Completed Modules

- `FrameCodec`: LTX1 framing, CRC32C with cached SSE4.2 acceleration and portable slicing-by-8 fallback, canonical ULEB128, sorted TLVs, streaming decode.
- `FrameCodec` compatibility: unknown optional extensions are accepted, unknown required extensions reject with a stable error.
- `Negotiator`: HELLO encode/decode, version/limit/feature/plugin schema intersection, transcript hash.
- `ChannelTable`: generated channels, generations, tombstones, remote OPEN acceptance, half-close/reset.
- `Reassembler`: bounded non-overlapping fragments and exact complete-message delivery.
- `FlowAccount`: checked reserve/release/grant and overflow/underflow errors.
- `ReplayWindow`: retained encoded frames, ACK-range retirement, retry bytes, and exact retained suffix lookup for RESUME.
- `TimerWheel`: generation-tagged deterministic timer scheduling, cancellation, and expiration.
- `OutboundScheduler`: bounded control/data queues with per-channel sequence order.
- `TraceLog`: deterministic `LTXTRACE/1` serialization, parsing, and canonical replay verification summaries.
- `PluginRegistry`: static family registration and built-in echo plugin.
- `PluginLease`: quiescent unregister blocks while active or queued dispatch leases exist.
- `Gateway`: route IDs, registered source-route bridging, exact schema-match opaque forwarding, typed asymmetric translators, and destination limit checks.
- `DeterministicExecutor`: bounded task admission, deterministic drain order, cancellation, shard validation, and stable connection-to-shard routing.
- `ConnectionEngine`: deterministic single-loop HELLO/OPEN/DATA/CREDIT/ACK/PING/PONG/RESUME/HALF_CLOSE/RESET/GOAWAY dispatch, retained-frame RESUME continuation, optional executor-backed plugin dispatch, and PONG deadline liveness timeout.
- `UnixTransport`: POSIX connect/socketpair/read/write adapter with stable transport errors on Windows.
- CLI `probe`, `dump`, canonical `replay` verification, and fixture generation commands plus fuzz smoke targets.
- Compatibility fixtures: `fixtures/ltx1/memory_hello.trace` publishes deterministic LTX/1 HELLO bytes.

## In Progress Modules

- `ConnectionEngine` routes emitted frames through `OutboundScheduler`, but partial-write transport integration remains shallow.
- Keepalive/retransmission timers and in-process retained RESUME continuation are integrated; durable retained state across process restart remains incomplete.
- CLI Unix endpoint serving and policy-file bridge mode remain incomplete.

## Known Blockers

- TSan is unsupported by the available Windows Clang target: `clang++: error: unsupported option '-fsanitize=thread' for target 'x86_64-pc-windows-msvc'`.
- LLVM installation through `winget install LLVM.LLVM` was cancelled by user/UAC, but usable LLVM binaries were already present under `C:\Program Files\LLVM`.

## Build And Test Status

- Debug configure/build: Passed with CMake 4.3.3, Ninja, Clang 22.1.8 using explicit tool paths.
- Release configure/build: Passed with CMake 4.3.3, Ninja, Clang 22.1.8 using explicit tool paths.
- Unit/integration tests: Passed in Debug and Release.
- Fuzz smoke: Passed in Debug and ASan/UBSan Release for `lattice_frame_fuzz`, `lattice_connection_event_fuzz`, and `lattice_gateway_trace_fuzz`.
- ASan/UBSan: Release build and tests passed. Debug ASan hit Windows debug CRT/ASan runtime mismatch during shutdown.
- TSan: Blocked on Windows Clang target support.
- Benchmark: `lattice_bench` measured 1097.48 MiB/s in the final verification sample and 1.35-1.87 GiB/s in repeated samples after cached SSE4.2 CRC32C; this meets the MVP frame decode target here.
- Static analysis: Not executed locally.
- Source checks: `rg -n "TODO|FIXME|unimplemented|abort\(" --glob "!docs/IMPLEMENTATION_STATUS.md" .` returned no matches.

## Deviations

- ADR-0001 records a compact MVP implementation with memory transport and deterministic single-loop state.
- ADR-0002 records static C++ plugin registration instead of dynamic code loading.
- ADR-0003 records memory transport plus POSIX Unix transport API behavior; CLI endpoint serving remains partial-result exit `6` on this Windows environment.

## Last Verified Commit

This commit. The exact hash is not embedded because amending the file changes the hash.
