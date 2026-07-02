# Changelog

## 0.1.0

- Added initial C++20 Lattice core.
- Added LTX/1 frame codec with CRC32C, ULEB128, TLVs, and streaming decode.
- Added HELLO negotiation, generated channels, flow accounting, reassembly, replay retention, echo plugin, gateway policy, CLI, tests, fuzz smoke targets, and documentation.
- Added ACK payload helpers, resume-window checks, generation-tagged timer wheel, bounded outbound scheduler, and deterministic trace parser.
- Integrated engine ACK/PING/PONG/RESUME timeout paths, scheduler-backed emission, plugin completion tokens, and quiescent plugin leases.
- Added gateway routes, typed translators, destination limit revalidation, and deterministic bounded executor primitives.
- Added a frame decode benchmark and removed O(n^2) contiguous decode buffer erasure.
- Enforced required-extension compatibility rejection while preserving optional unknown extensions.
- Added a POSIX Unix transport adapter with portable Windows unsupported errors and transport tests.
- Added gateway registered-route bridging and typed asymmetric schema translation.
- Added executor-backed plugin dispatch in `ConnectionEngine`.
- Added cached SSE4.2 CRC32C acceleration with portable slicing-by-8 fallback; release frame decode now exceeds the MVP throughput target in this environment.
- Added canonical trace replay verification summaries to the library and `lattice replay`.
- Added PONG deadline liveness timeout after idle keepalive PINGs.
- Added in-process RESUME continuation that returns exact retained encoded frames from the requested sequence.
- Added `lattice fixture --memory-hello` and a checked-in LTX/1 memory HELLO compatibility fixture.
- Added stable connection-to-shard routing over the deterministic executor.
- Added outbound scheduler partial-write tail retention for control and data queues.
- Added sparse-fragment retained budget enforcement and retained-byte cleanup on reassembly reset.
- Added explicit channel generation wrap rejection coverage.
- Added send sequence wrap rejection before `uint32_t` rollover.
- Added explicit frame codec allocation-boundary rejection tests.
- Added `lattice bridge --memory` as a portable gateway route smoke command.
- Added sustained backpressure byte-stream preservation coverage.
- Added checked-in local Clang/Ninja CMake presets.
- Added TOML-like route-policy parsing for `lattice bridge --memory --policy`.
