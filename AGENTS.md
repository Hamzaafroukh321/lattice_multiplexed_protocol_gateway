# Repository Agent Guide

## Build

```powershell
cmake --preset debug
cmake --build --preset debug
cmake --preset release
cmake --build --preset release
```

## Test

```powershell
ctest --preset debug
```

## Sanitizers

```powershell
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

TSan is available as a preset for supported non-MSVC toolchains:

```powershell
cmake --preset tsan
cmake --build --preset tsan
```

## Fuzz Smoke

```powershell
.\build\debug\lattice_frame_fuzz.exe
.\build\debug\lattice_connection_event_fuzz.exe
.\build\debug\lattice_gateway_trace_fuzz.exe
```

## Layout

- `include/lattice`: public API.
- `src/format`: LTX frame codec.
- `src/connection`: negotiation and connection engine.
- `src/channel`: channels, flow, reassembly.
- `src/replay`: replay retention.
- `src/plugin`: registry and built-in echo plugin.
- `src/gateway`: schema-safe forwarding policy.
- `src/transport`: deterministic memory transport.
- `tests`: unit and integration tests.
- `fuzz`: production-linked smoke fuzz targets.
- `docs`: architecture, protocol, testing, recovery, traceability.

## Conventions

Use C++20, RAII, checked arithmetic for serialized lengths and credits, explicit endian conversion, stable errors, and no raw owning pointers. Public APIs return `Result<T>`. Do not add network downloads to the normal build.

## Commit Expectations

Stage deliberately. Keep feature code and evidence tests together. Do not commit build directories, crash artifacts, coverage databases, local IDE files, or credentials.

## Definition Of Done

A change is done when relevant debug tests pass, sanitizer commands pass where applicable, fuzz smoke still runs, docs remain accurate, and traceability/status files are updated for changed requirements.
