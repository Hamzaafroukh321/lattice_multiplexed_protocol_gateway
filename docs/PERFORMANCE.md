# Performance

Benchmark command:

```powershell
.\build\release\lattice_bench.exe
```

The benchmark runs production-linked acceptance probes for frame decode throughput, in-memory message latency, default sizing, active channel capacity, process RSS, and lightweight fuzz-harness speed proxies.

Latest measurement on this Windows environment with the checked-in `local-clang-release`
preset, Clang 22.1.8, and Ninja:

| Command | Frames | Decoded MiB | Seconds | MiB/s | Status |
| --- | ---: | ---: | ---: | ---: | --- |
| `.\build\local-clang-release\lattice_bench.exe` | 20000 | 19.5312 | 0.0164165 | 1189.73 | Meets MVP target |
| `.\build\local-clang-release\lattice_bench.exe` | 20000 | 19.5312 | 0.0164610 | 1186.52 | Meets MVP target |
| `.\build\local-clang-release\lattice_bench.exe` | 20000 | 19.5312 | 0.0152795 | 1278.26 | Meets MVP target |
| `.\build\local-clang-release\lattice_bench.exe` | 20000 | 19.5312 | 0.0149455 | 1306.83 | Meets MVP target |
| `.\build\local-clang-release\lattice_bench.exe` | 20000 | 19.5312 | 0.0105322 | 1854.43 | Meets MVP target |
| `.\build\local-clang-release\lattice_bench.exe` | 20000 | 19.5312 | 0.0157337 | 1241.36 | Meets MVP target |

Latest full acceptance probe from the same command:

```text
frames=20000
decoded_mib=19.5312
seconds=0.0157337
mib_per_second=1241.36
latency_messages=512
latency_p50_us=5.8
latency_p99_us=47.4
latency_max_us=225
default_max_frame_bytes=65536
default_max_message_bytes=1048576
default_connection_window_bytes=1048576
active_channels=256
process_rss_bytes=5484544
process_rss_mib=5.23047
frame_fuzz_exec_per_second=6.74741e+06
connection_event_exec_per_second=11633.1
```

The previous O(n^2) contiguous decode behavior was removed by changing `FrameCodec::feed` to consume by cursor and erase once. CRC32C now uses a cached SSE4.2 path when the CPU reports support and a portable slicing-by-8 fallback otherwise. Five earlier release samples after this change ranged from 1350.89 to 1869.86 MiB/s; the final full-verification sample remained above the 1 GiB/s target.

The generic `build/release` tree on this Windows host produced lower samples
after a full rebuild (roughly 784-916 MiB/s). The pinned local Clang preset is
the documented reference command for this repository because it fixes the
compiler and Ninja paths.

Budgets from the specification:

- MVP frame decode: at least 1 GiB/s contiguous small frames. Met at 1241.36 MiB/s.
- MVP in-memory p99 message latency: under 1 ms for 1 KiB excluding plugin work. Met at 47.4 us with executor-deferred plugin work.
- Default connection memory: 64 MiB. Met at 5.23047 MiB process RSS after the active-channel probe on this host.
- MVP active channels: 256. Met with 256 negotiated/opened channels.
- MVP frame/message size: 64 KiB / 1 MiB. Defaults are 65536 and 1048576 bytes.
- MVP fuzz speed: more than 50k frame exec/s and more than 5k event exec/s. Met at about 6.75M frame exec/s and 11.63k connection-event exec/s.

A 5000-iteration memory probe soak also passed on the Windows debug build.
