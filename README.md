# Lattice Multiplexed Protocol Gateway

Lattice is a C++20 local gateway core for the LTX/1 multiplexed binary protocol. It negotiates capabilities, opens generation-tagged channels, reassembles fragmented messages, enforces byte-credit windows, dispatches negotiated plugin families, records replay bytes, and forwards gateway traffic only through schema-safe opaque routes or typed translators.

Maturity: hardening implementation. The memory transport, Unix transport adapter, frame codec, HELLO negotiation, generated channels, reassembly, echo plugin, replay window, retained RESUME continuation, gateway route table, memory and Unix CLI paths, executor-backed plugin dispatch path, stable shard routing, tests, fuzz smoke targets, compatibility fixture, and release decode benchmark are present. Full-version items such as threaded loop runtime, POSIX socket runtime verification, TSan stress, and long soaks remain in progress.

## Build

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Example

```powershell
.\build\debug\lattice.exe probe --memory
```

Expected output shape:

```text
LTX/1 max_frame=65536 max_message=1048576 channels=256 plugins=1
```

Replay retention snapshots can be persisted by the memory probe:

```powershell
.\build\debug\lattice.exe probe --memory --snapshot build\debug\memory_probe.ltxreplay
```

On POSIX hosts, the same HELLO negotiation path is available over Unix-domain
sockets:

```powershell
.\build\debug\lattice.exe serve --socket /tmp/lattice.sock --plugin echo
.\build\debug\lattice.exe probe --socket /tmp/lattice.sock
```

## Safety

All external bytes are decoded by `FrameCodec` before reaching connection state. Frames use explicit network byte order, canonical ULEB128 lengths, sorted TLVs, and CRC32C. Channel reuse is generation-tagged so stale frames cannot mutate a reused slot.
