# Performance

Benchmark command:

```powershell
.\build\release\lattice_bench.exe
```

The benchmark decodes a canonical 1 KiB DATA frame repeatedly through production `FrameCodec` and prints frame count, decoded MiB, elapsed seconds, and MiB/s.

Latest measurement on this Windows environment with Clang 22.1.8 Release:

| Command | Frames | Decoded MiB | Seconds | MiB/s | Status |
| --- | ---: | ---: | ---: | ---: | --- |
| `.\build\release\lattice_bench.exe` | 20000 | 19.5312 | 0.068799 | 283.889 | Below MVP target |

The previous O(n^2) contiguous decode behavior was removed by changing `FrameCodec::feed` to consume by cursor and erase once. Further performance work is still required to meet the 1 GiB/s MVP budget.

Budgets from the specification remain the target:

- MVP frame decode: at least 1 GiB/s contiguous small frames.
- MVP in-memory p99 message latency: under 1 ms for 1 KiB excluding plugin work.
- Default connection memory: 64 MiB.
- MVP active channels: 256.
- MVP frame/message size: 64 KiB / 1 MiB.
- MVP fuzz speed: more than 50k frame exec/s and more than 5k event exec/s.

Benchmark commands should be added after the debug/release builds are verified.
