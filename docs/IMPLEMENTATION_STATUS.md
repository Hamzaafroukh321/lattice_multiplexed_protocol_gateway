# Implementation Status

Generated from this environment: 2026-06-26T16:09:25.7330517+01:00.

## Selected Specification

`02_lattice_multiplexed_protocol_gateway.md` was selected because it matches the repository directory name and contains the complete numbered architecture, fuzzing, MVP acceptance, and full-version acceptance sections.

## Current Phase

Phase 6/7: hardening, fuzzing, and documentation. The compact production implementation now builds with local CMake 4.3.3, Ninja, and Clang 22.1.8. Scheduler, timer, trace, ACK, RESUME-window, PING/PONG, retry, drain, and plugin lease primitives are present.

## Last Completed Ticket

LAT-026 partially: plugin leases now make unregister wait for active dispatch leases. LAT-022/LAT-023/LAT-024 are further integrated into `ConnectionEngine` with handshake timeout, idle ping, retry, drain timers, ACK, PING/PONG, and RESUME rejection paths.

## Next Actionable Ticket

Next source tickets:

- Implement Unix-domain transport where the target platform supports it.
- Integrate gateway routes into two-sided connection forwarding scenarios.
- Integrate `DeterministicExecutor` with asynchronous plugin execution instead of using synchronous dispatch by default.
- Add long-running sequence wrap, generation wrap, backpressure, allocation-failure, and compatibility fixtures.
- Replace ad hoc explicit tool paths with a documented local toolchain preset.

## Completed Modules

- `FrameCodec`: LTX1 framing, CRC32C, canonical ULEB128, sorted TLVs, streaming decode.
- `FrameCodec` compatibility: unknown optional extensions are accepted, unknown required extensions reject with a stable error.
- `Negotiator`: HELLO encode/decode, version/limit/feature/plugin schema intersection, transcript hash.
- `ChannelTable`: generated channels, generations, tombstones, remote OPEN acceptance, half-close/reset.
- `Reassembler`: bounded non-overlapping fragments and exact complete-message delivery.
- `FlowAccount`: checked reserve/release/grant and overflow/underflow errors.
- `ReplayWindow`: retained encoded frames, ACK-range retirement, retry bytes.
- `TimerWheel`: generation-tagged deterministic timer scheduling, cancellation, and expiration.
- `OutboundScheduler`: bounded control/data queues with per-channel sequence order.
- `TraceLog`: deterministic `LTXTRACE/1` serialization and parsing.
- `PluginRegistry`: static family registration and built-in echo plugin.
- `PluginLease`: quiescent unregister blocks while active dispatch leases exist.
- `Gateway`: route IDs, exact schema-match forwarding policy, typed translators, and destination limit checks.
- `DeterministicExecutor`: bounded task admission, deterministic drain order, cancellation, and shard validation.
- `ConnectionEngine`: deterministic single-loop HELLO/OPEN/DATA/CREDIT/ACK/PING/PONG/RESUME/HALF_CLOSE/RESET/GOAWAY dispatch.
- CLI and fuzz smoke targets.

## In Progress Modules

- `ConnectionEngine` routes emitted frames through `OutboundScheduler`, but partial-write transport integration remains shallow.
- Keepalive/retransmission timers are integrated at a basic engine level; full peer liveness policy and retained resume proof remain incomplete.
- Transport coverage is memory-only in production code; Unix-domain adapter remains.

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
- Benchmark: `lattice_bench` measured 283.889 MiB/s for 1 KiB frame decode on this environment, below the MVP target.
- Static analysis: Not executed locally.
- Source checks: `rg -n "TODO|FIXME|unimplemented|abort\(" .` returned no matches.

## Deviations

- ADR-0001 records a compact MVP implementation with memory transport and deterministic single-loop state.
- ADR-0002 records static C++ plugin registration instead of dynamic code loading.
- ADR-0003 records portable CLI behavior for Unix socket commands as partial-result exit `6` on this Windows environment.

## Last Verified Commit

`527c195d6455438b858f09ef315e7b56d6f26b8f`.
