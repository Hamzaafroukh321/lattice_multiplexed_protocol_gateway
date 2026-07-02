# Lattice Implementation Plan

Selected specification: `02_lattice_multiplexed_protocol_gateway.md`. It matches the repository name and contains the complete numbered architecture, fuzzing, MVP, and full-version acceptance sections.

## Architecture Summary

Lattice is implemented as a C++20 core library with typed protocol errors, a canonical LTX/1 frame codec, immutable negotiated capabilities, generation-aware channels, bounded flow accounts, fragment reassembly, replay retention, a static plugin registry with a built-in echo family, deterministic memory transport, gateway schema policy, CLI tooling, and smoke fuzz targets.

Mutable connection and channel state is owned by `ConnectionEngine` and processed in actor-confined event order. The current implementation has deterministic test execution, a bounded threaded shard executor, and Linux Docker coverage for the POSIX socket bridge pump.

## Phases

1. Foundations: build graph, warnings, sanitizer options, typed errors, checked arithmetic.
2. Format: ULEB128, TLVs, CRC32C, streaming frame decoder, canonical writer.
3. Data model: stable channel IDs, channel generations, flow accounts, reassembly.
4. Negotiation and engine: HELLO, capabilities, OPEN/DATA/CREDIT/HALF_CLOSE/RESET/GOAWAY.
5. Plugins and gateway: built-in echo plugin, static registry, schema-safe forwarding policy.
6. Replay and tools: replay window, CLI dump/probe/replay, fuzz smoke executables.
7. Verification and docs: tests, fuzz smoke commands, protocol/API/testing docs, traceability.
8. Full version: Unix sockets, selective ACK/RESUME, keepalive timers, async plugin unload, loop sharding, long soak, performance budgets.

## Dependency Graph

`errors/types -> frame codec -> negotiation -> channel table/flow/reassembly -> connection engine -> plugins/gateway -> CLI/fuzz/tests -> docs/traceability`.

## Validation Commands

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --preset release
cmake --build --preset release
.\build\debug\lattice_frame_fuzz.exe
.\build\debug\lattice_connection_event_fuzz.exe
.\build\debug\lattice_gateway_trace_fuzz.exe
```

Local note: CMake and a C++ compiler were not available on this machine's PATH during this run. The commands are therefore documented but not completed here.

## Risks And Mitigations

- Toolchain drift: CMake presets keep the expected command surface stable.
- Format ambiguity: LTX framing is centralized in `FrameCodec` and tests round-trip canonical bytes.
- Stale generation mutation: `ChannelTable` validates compound IDs and rejects old generations.
- Credit leaks: `FlowAccount` exposes conservation tests and checked overflow behavior.
- Schema mismatch: gateway forwarding requires exact plugin family and schema hash.
- TSan remains unsupported on Windows Clang but passes under Ubuntu Docker when run with `setarch x86_64 -R`.

## Definition Of Done

MVP done requires green builds/tests/fuzz smoke on a C++20 toolchain, plus implemented memory negotiation, generated channels, fragmentation, flow checks, echo dispatch, malformed input handling, and docs. Full-version done additionally requires supported TSan, long soaks, performance budgets, and compatibility fixtures.

## Checklist

- [x] Build graph and public API.
- [x] Frame codec, CRC, TLV, varint.
- [x] HELLO negotiation.
- [x] Generation-aware channel table.
- [x] Fragment reassembly and overlap rejection.
- [x] Byte-credit accounting.
- [x] Built-in echo plugin.
- [x] Replay retention and ACK retirement.
- [x] Gateway schema policy.
- [x] CLI and fuzz smoke targets.
- [x] Unit/integration test sources.
- [x] Local debug/release build verified.
- [x] ASan/UBSan verified on this host.
- [x] Unix-domain transport and socket CLI negotiation.
- [x] Selective ACK, RESUME, keepalive, retransmission timers.
- [x] Threaded runtime execution primitive.
- [x] POSIX socket bridge pump verification under Linux Docker.
- [x] TSan on a supported non-MSVC target via Ubuntu Docker and `setarch x86_64 -R`.
- [x] 5000-iteration memory probe soak and pinned local Clang frame benchmark.
