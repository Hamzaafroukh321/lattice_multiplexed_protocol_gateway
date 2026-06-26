param(
  [string]$BuildDir = "build/debug"
)

$ErrorActionPreference = "Stop"
& ".\$BuildDir\lattice_frame_fuzz.exe"
& ".\$BuildDir\lattice_connection_event_fuzz.exe"
& ".\$BuildDir\lattice_gateway_trace_fuzz.exe"
