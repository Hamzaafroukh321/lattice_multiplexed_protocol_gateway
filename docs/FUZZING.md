# Fuzzing

The repository includes three production-linked smoke fuzz targets.

```powershell
.\build\debug\lattice_frame_fuzz.exe
.\build\debug\lattice_connection_event_fuzz.exe
.\build\debug\lattice_gateway_trace_fuzz.exe
```

`lattice_frame_fuzz` feeds bytes to `FrameCodec` and canonical re-encodes decoded frames.

`lattice_connection_event_fuzz` creates two production `ConnectionEngine` instances, negotiates memory HELLO frames, and maps input bytes to bounded connection operations or malformed transport fragments.

`lattice_gateway_trace_fuzz` calls production gateway translation policy and rejects schema-mismatched opaque forwarding.

Seed corpora live under `corpus/frames`, `corpus/events`, and `corpus/traces`. Reproducers should be converted into ordinary tests before being kept as regression evidence.
