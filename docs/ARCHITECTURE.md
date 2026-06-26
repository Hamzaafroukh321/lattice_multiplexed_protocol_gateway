# Architecture

Lattice is organized as a small set of deterministic modules.

`FrameCodec` owns incremental input bytes and yields validated `Frame` values only after magic, reserved flags, payload length, extensions, size limits, and CRC32C pass.

`Negotiator` decodes HELLO payloads and freezes one immutable `CapabilitySet`. Limits are the minimum of both peers. Required features must be common. Plugin families must match by family ID and schema hash.

`ConnectionEngine` is the single mutation point for protocol state. It handles frames in event order, owns `ChannelTable`, records outbound bytes in `ReplayWindow`, and emits diagnostic events for scoped errors.

`ChannelTable` owns slots keyed by `channel_number:generation`. A slot moves through `free -> opening/open -> half closed/closing -> tombstone -> free`. Reuse increments generation and old compound IDs are rejected.

`Reassembler` stores byte ranges by message sequence and validates total length, offset arithmetic, exact duplicate bytes, and overlap conflicts before producing an immutable logical message.

`PluginRegistry` maps negotiated family IDs to static factories. The built-in echo plugin receives only completed messages.

`PluginLease` pins plugin dispatch lifetime. Unregister marks a family draining and returns `WouldBlock` until active leases have released.

`Gateway` allows opaque forwarding only when source and destination advertise the same family ID and schema hash.

`OutboundScheduler` provides bounded control/data queues. Control frames drain first; data queues preserve per-channel sequence order and rotate across channels.

`TimerWheel` stores timer events with stable IDs, channel generations, and deterministic due-time ordering.

`TraceLog` provides a canonical text form for replay fixtures and CLI inspection.

## Lock Hierarchy

The current implementation is deterministic single-loop and does not lock connection or channel state. Future full-version concurrency should use registry mutex, loop-shard registry, then trace sink ordering.

## State Machines

Connection states: Created, Negotiating, Active, Draining, Closed.

Channel states: Free, Opening, Open, LocalClosed, RemoteClosed, Closing, Tombstone.

Illegal transitions return stable `ErrorCode` values and a close recommendation.
