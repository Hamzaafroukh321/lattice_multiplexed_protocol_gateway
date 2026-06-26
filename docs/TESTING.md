# Testing

The test executable `lattice_tests` contains unit and integration coverage for:

- Frame split-at-every-byte streaming decode.
- CRC mismatch rejection.
- Canonical extension ordering.
- Channel generation increments and stale ID rejection.
- Fragment completion and conflicting overlap.
- Half-close direction independence.
- Credit conservation and overflow.
- Replay ACK retirement and identical retry bytes.
- ACK payload canonicalization, resume-window rejection, and generation-tagged timer cancellation.
- Bounded outbound scheduling, control priority, and per-channel data ordering.
- Deterministic trace serialization/parsing.
- HELLO limit intersection and schema mismatch rejection.
- End-to-end memory negotiation and echo plugin delivery.

Run:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The local environment used for this implementation did not have CMake or a C++ compiler on PATH, so these tests are authored but not executed here.
