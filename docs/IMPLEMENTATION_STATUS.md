# Implementation Status

Generated from this environment: 2026-06-26T16:33:55.7674858+01:00.

## Selected Specification

`02_lattice_multiplexed_protocol_gateway.md` was selected because it matches the repository directory name and contains the complete numbered architecture, fuzzing, MVP acceptance, and full-version acceptance sections.

## Current Phase

Phase 6/7: hardening, fuzzing, and documentation. The compact production implementation now builds with local CMake 4.3.3, Ninja, and Clang 22.1.8. Scheduler, timer, trace, ACK, RESUME-window, PING/PONG, retry, drain, Unix transport and socket CLI negotiation, gateway route, executor-backed plugin dispatch, and plugin lease primitives are present.

## Last Completed Ticket

LAT-017/LAT-018 advanced: the CLI now registers memory command smokes in CTest and exposes Unix socket probe, one-connection serve, and bridge route-validation commands over the POSIX transport/listener adapters while Windows returns stable unsupported transport failures.

## Next Actionable Ticket

Next source tickets:

- Implement continuous Unix bridge data-plane channel pumping.
- Wire durable replay snapshots into CLI/daemon storage lifecycle.

## Completed Modules

- `FrameCodec`: LTX1 framing, CRC32C with cached SSE4.2 acceleration and portable slicing-by-8 fallback, canonical ULEB128, sorted TLVs, streaming decode.
- `FrameCodec` compatibility: unknown optional extensions are accepted, unknown required extensions reject with a stable error.
- `Negotiator`: HELLO encode/decode, version/limit/feature/plugin schema intersection, transcript hash.
- `ChannelTable`: generated channels, generations, tombstones, send-sequence wrap rejection, remote OPEN acceptance, half-close/reset.
- `Reassembler`: bounded non-overlapping fragments, retained sparse-fragment budget enforcement, retained-byte cleanup on reset, and exact complete-message delivery.
- `FlowAccount`: checked reserve/release/grant and overflow/underflow errors.
- `ReplayWindow`: retained encoded frames, ACK-range retirement, retry bytes, exact retained suffix lookup for RESUME, and canonical `LTXREPLAY/1` snapshot serialization/restore.
- `TimerWheel`: generation-tagged deterministic timer scheduling, cancellation, and expiration.
- `OutboundScheduler`: bounded control/data queues with per-channel sequence order and partial-write tail retention.
- `TraceLog`: deterministic `LTXTRACE/1` serialization, parsing, and canonical replay verification summaries.
- `PluginRegistry`: static family registration and built-in echo plugin.
- `PluginLease`: quiescent unregister blocks while active or queued dispatch leases exist.
- `Gateway`: route IDs, registered source-route bridging, exact schema-match opaque forwarding, typed asymmetric translators, and destination limit checks.
- `DeterministicExecutor`: bounded task admission, deterministic drain order, cancellation, shard validation, and stable connection-to-shard routing.
- `ConnectionEngine`: deterministic single-loop HELLO/OPEN/DATA/CREDIT/ACK/PING/PONG/RESUME/HALF_CLOSE/RESET/GOAWAY dispatch, retained-frame RESUME continuation, pre-start replay snapshot load/export, optional executor-backed plugin dispatch, and PONG deadline liveness timeout.
- `UnixTransport`/`UnixListener`: POSIX connect/socketpair/read/write and bind/listen/accept adapters with stable transport errors on Windows.
- CLI `probe`, memory `bridge`, Unix socket `probe`/one-connection `serve`/bridge route validation, route-policy parsing, `dump`, canonical `replay` verification, and fixture generation commands plus fuzz smoke targets.
- Compatibility fixtures: `fixtures/ltx1/memory_hello.trace` publishes deterministic LTX/1 HELLO bytes; `fixtures/ltx1/memory_bridge.policy` exercises route-policy parsing.

## In Progress Modules

- Keepalive/retransmission timers, in-process retained RESUME continuation, replay snapshot serialization, and engine pre-start snapshot loading are integrated; CLI/daemon durable storage lifecycle wiring remains incomplete.
- Continuous CLI Unix bridge data-plane pumping remains incomplete; POSIX listener/transport primitives, Unix negotiation commands, portable memory bridge, and policy-file parsing are implemented.

## Known Blockers

- TSan is unsupported by the available Windows Clang target: `clang++: error: unsupported option '-fsanitize=thread' for target 'x86_64-pc-windows-msvc'`.
- LLVM installation through `winget install LLVM.LLVM` was cancelled by user/UAC, but usable LLVM binaries were already present under `C:\Program Files\LLVM`.

## Build And Test Status

- Debug configure/build: Passed with CMake 4.3.3, Ninja, Clang 22.1.8 using the checked-in `local-clang-debug` preset.
- Release configure/build: Passed with CMake 4.3.3, Ninja, Clang 22.1.8 using the checked-in local Clang/Ninja path.
- Unit/integration and CLI CTest smokes: Passed in Debug and Release.
- Fuzz smoke: Passed in Debug and ASan/UBSan Release for `lattice_frame_fuzz`, `lattice_connection_event_fuzz`, and `lattice_gateway_trace_fuzz`.
- ASan/UBSan: Release build and tests passed. Debug ASan hit Windows debug CRT/ASan runtime mismatch during shutdown.
- TSan: Blocked on Windows Clang target support.
- Benchmark: `lattice_bench` measured 1097.48 MiB/s in the final verification sample and 1.35-1.87 GiB/s in repeated samples after cached SSE4.2 CRC32C; this meets the MVP frame decode target here.
- Soak: `scripts/soak.ps1 -BuildDir build/debug -Iterations 100` passed memory probe repetitions.
- Static analysis: Not executed locally.
- Source checks: `rg -n "TODO|FIXME|unimplemented|abort\(" --glob "!docs/IMPLEMENTATION_STATUS.md" .` returned no matches.

## Deviations

- ADR-0001 records a compact MVP implementation with memory transport and deterministic single-loop state.
- ADR-0002 records static C++ plugin registration instead of dynamic code loading.
- ADR-0003 records memory transport plus POSIX Unix transport/listener API behavior; Windows Unix CLI invocations return stable unsupported failures while continuous bridge forwarding remains a documented full-version gap.

## Last Verified Commit

This commit. The exact hash is not embedded because amending the file changes the hash.
