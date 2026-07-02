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
- Replay retained-state snapshot round trip, corrupt-state rejection, durable store load/save, and pre-start engine snapshot loading.
- ACK payload canonicalization, resume-window rejection, retained resume suffix replay, and generation-tagged timer cancellation.
- Bounded outbound scheduling, control priority, and per-channel data ordering.
- Partial write retention for data and control queues.
- Sustained one-byte backpressure drains preserve exact data stream.
- Deterministic trace serialization/parsing and canonical replay summary verification.
- Published LTX/1 compatibility fixture replay verification.
- CLI memory bridge route smoke and route-policy fixture parsing.
- CLI memory replay snapshot load/save smoke.
- CLI Unix socket probe/serve/bridge command surface with stable unsupported behavior on Windows and POSIX socket bridge pump coverage under Linux Docker.
- Plugin unregister quiescence and stale completion discard.
- Handshake timeout, PING/PONG, missed-PONG liveness timeout, and RESUME rejection.
- Gateway route creation, pure translation, destination limit revalidation, and schema mismatch rejection.
- Gateway registered-route bridging, connection data-plane pumping, and typed asymmetric schema translation.
- Unix transport/listener primitives on POSIX or stable unsupported transport errors on Windows.
- `scripts/soak.ps1 -BuildDir build/debug -Iterations 5000` memory probe soak.
- Executor-backed plugin dispatch and deferred completion.
- Bounded executor admission, deterministic drain order, and cancellation.
- Bounded threaded executor shard ordering, task error reporting, and shutdown rejection.
- Stable connection-to-shard routing.
- HELLO limit intersection and schema mismatch rejection.
- End-to-end memory negotiation and echo plugin delivery.

Linux TSan verification in Docker needs ASLR disabled for the test process:

```powershell
docker run --rm --security-opt seccomp=unconfined --cap-add SYS_PTRACE -v "${PWD}:/work" -w /work ubuntu:24.04 bash -lc "apt-get update >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y cmake ninja-build g++ util-linux >/dev/null && setarch x86_64 -R ctest --test-dir build/linux-tsan --output-on-failure"
```

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
