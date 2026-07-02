# Testing

The test executable `lattice_tests` contains unit and integration coverage for:

- Frame split-at-every-byte streaming decode.
- CRC mismatch rejection.
- Canonical extension ordering.
- Oversized frame and extension-header rejection.
- Channel generation increments, wrap rejection, and stale ID rejection.
- Send sequence wrap rejection before zero.
- Fragment completion and conflicting overlap.
- Sparse incomplete-fragment retained budget and cleanup after overlap reset.
- Half-close direction independence.
- Credit conservation and overflow.
- Replay ACK retirement and identical retry bytes.
- Replay retained-state snapshot round trip, corrupt-state rejection, and pre-start engine snapshot loading.
- ACK payload canonicalization, resume-window rejection, retained resume suffix replay, and generation-tagged timer cancellation.
- Bounded outbound scheduling, control priority, and per-channel data ordering.
- Partial write retention for data and control queues.
- Sustained one-byte backpressure drains preserve exact data stream.
- Deterministic trace serialization/parsing and canonical replay summary verification.
- Published LTX/1 compatibility fixture replay verification.
- CLI memory bridge route smoke and route-policy fixture parsing.
- CLI Unix socket probe/serve/bridge command surface with stable unsupported behavior on Windows.
- Plugin unregister quiescence and stale completion discard.
- Handshake timeout, PING/PONG, missed-PONG liveness timeout, and RESUME rejection.
- Gateway route creation, pure translation, destination limit revalidation, and schema mismatch rejection.
- Gateway registered-route bridging, connection data-plane pumping, and typed asymmetric schema translation.
- Unix transport/listener primitives on POSIX or stable unsupported transport errors on Windows.
- Executor-backed plugin dispatch and deferred completion.
- Bounded executor admission, deterministic drain order, and cancellation.
- Stable connection-to-shard routing.
- HELLO limit intersection and schema mismatch rejection.
- End-to-end memory negotiation and echo plugin delivery.

Run:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

This local environment also has checked-in Clang/Ninja presets:

```powershell
cmake --preset local-clang-debug
cmake --build --preset local-clang-debug
ctest --preset local-clang-debug
```
