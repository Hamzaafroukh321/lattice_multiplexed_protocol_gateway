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
- Plugin unregister quiescence and stale completion discard.
- Handshake timeout, PING/PONG, and RESUME rejection.
- Gateway route creation, pure translation, destination limit revalidation, and schema mismatch rejection.
- Bounded executor admission, deterministic drain order, and cancellation.
- HELLO limit intersection and schema mismatch rejection.
- End-to-end memory negotiation and echo plugin delivery.

Run:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The local environment can build with explicit tool paths:

```powershell
$env:PATH='C:\Program Files\LLVM\bin;C:\Users\Hamz\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;C:\Program Files\CMake\bin;' + $env:PATH
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER='C:\Program Files\LLVM\bin\clang++.exe' -DCMAKE_MAKE_PROGRAM='C:\Users\Hamz\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe'
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```
