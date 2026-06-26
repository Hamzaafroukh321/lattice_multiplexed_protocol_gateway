# Performance

No numeric benchmark results were produced in this environment because CMake and a C++ compiler were unavailable on PATH.

Budgets from the specification remain the target:

- MVP frame decode: at least 1 GiB/s contiguous small frames.
- MVP in-memory p99 message latency: under 1 ms for 1 KiB excluding plugin work.
- Default connection memory: 64 MiB.
- MVP active channels: 256.
- MVP frame/message size: 64 KiB / 1 MiB.
- MVP fuzz speed: more than 50k frame exec/s and more than 5k event exec/s.

Benchmark commands should be added after the debug/release builds are verified.
