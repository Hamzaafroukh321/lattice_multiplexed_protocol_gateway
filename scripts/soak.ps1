param(
  [string]$BuildDir = "build/debug",
  [int]$Iterations = 100
)

$ErrorActionPreference = "Stop"
for ($i = 0; $i -lt $Iterations; $i++) {
  & ".\$BuildDir\lattice.exe" probe --memory | Out-Null
}
