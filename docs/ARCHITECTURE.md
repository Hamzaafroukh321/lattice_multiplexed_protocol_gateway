# Architecture

Lattice is organized as a small set of deterministic modules.

`FrameCodec` owns incremental input bytes and yields validated `Frame` values only after magic, reserved flags, payload length, extensions, size limits, and CRC32C pass. CRC32C uses a cached SSE4.2 fast path when available and falls back to portable slicing-by-8 tables.

`Negotiator` decodes HELLO payloads and freezes one immutable `CapabilitySet`. Limits are the minimum of both peers. Required features must be common. Plugin families must match by family ID and schema hash.

`ConnectionEngine` is the single mutation point for protocol state. It handles frames in event order, owns `ChannelTable`, records outbound bytes in `ReplayWindow`, and emits diagnostic events for scoped errors.

`ReplaySnapshotStore` provides the process file boundary for canonical `LTXREPLAY/1` snapshots. It loads text snapshots before connection start and saves through a temporary file followed by rename publication.

`ChannelTable` owns slots keyed by `channel_number:generation`. A slot moves through `free -> opening/open -> half closed/closing -> tombstone -> free`. Reuse increments generation and old compound IDs are rejected.

`Reassembler` stores byte ranges by message sequence and validates total length, offset arithmetic, exact duplicate bytes, and overlap conflicts before producing an immutable logical message.

`PluginRegistry` maps negotiated family IDs to static factories. The built-in echo plugin receives only completed messages.

`PluginLease` pins plugin dispatch lifetime. Unregister marks a family draining and returns `WouldBlock` until active or queued dispatch leases have released.

`Gateway` allows opaque forwarding only when source and destination advertise the same family ID and schema hash. Routes carry source channel, destination channel, and plugin family IDs. Registered source routes can produce explicit forwarded-message objects or emit DATA frames through an already-open destination `ConnectionEngine`. Optional pure translators may transform payloads across asymmetric schema hashes before destination limits are revalidated.

`DeterministicExecutor` provides bounded task admission and deterministic task draining for tests, plugin dispatch, and loop sharding. `ThreadedExecutor` provides bounded per-shard worker queues for runtime execution while preserving serial order within each shard. `ConnectionShardRouter` assigns connection IDs to stable executor shards so multi-connection work preserves actor confinement.

`UnixTransport` provides POSIX Unix-domain connect/socketpair/read/write primitives. `UnixListener` provides POSIX bind/listen/accept primitives for endpoint integration. Windows builds return stable transport-scoped unsupported errors for those operations.

`OutboundScheduler` provides bounded control/data queues. Control frames drain first; data queues preserve per-channel sequence order and rotate across channels. Short writes return exact prefix chunks and keep the unsent tail at the head of its queue.

`TimerWheel` stores timer events with stable IDs, channel generations, and deterministic due-time ordering.

`TraceLog` provides a canonical text form for replay fixtures and CLI inspection.

## Lock Hierarchy

The current implementation is deterministic single-loop and does not lock connection or channel state. Future full-version concurrency should use registry mutex, loop-shard registry, then trace sink ordering.

## State Machines

Connection states: Created, Negotiating, Active, Draining, Closed.

Channel states: Free, Opening, Open, LocalClosed, RemoteClosed, Closing, Tombstone.

Illegal transitions return stable `ErrorCode` values and a close recommendation.
