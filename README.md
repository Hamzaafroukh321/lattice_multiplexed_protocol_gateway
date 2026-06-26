# Lattice Multiplexed Protocol Gateway

Lattice is a C++20 local gateway core for the LTX/1 multiplexed binary protocol. It negotiates capabilities, opens generation-tagged channels, reassembles fragmented messages, enforces byte-credit windows, dispatches negotiated plugin families, records replay bytes, and rejects schema-mismatched gateway forwarding.

Maturity: initial implementation. The memory transport, frame codec, HELLO negotiation, generated channels, reassembly, echo plugin, replay window, CLI surface, tests, and fuzz smoke targets are present. Full-version items such as Unix sockets, loop sharding, RESUME, keepalive timers, quiescent dynamic plugin unload, TSan stress, and long performance soaks remain in progress.

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

## Safety

All external bytes are decoded by `FrameCodec` before reaching connection state. Frames use explicit network byte order, canonical ULEB128 lengths, sorted TLVs, and CRC32C. Channel reuse is generation-tagged so stale frames cannot mutate a reused slot.
