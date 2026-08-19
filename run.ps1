# One-command build + run for the WebSocket student server.
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not $env:VCPKG_ROOT) {
    if (Test-Path "D:\vcpkg") { $env:VCPKG_ROOT = "D:\vcpkg" }
}

if (-not $env:VCPKG_ROOT -or -not (Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake")) {
    Write-Error "vcpkg not found. Set VCPKG_ROOT (e.g. `$env:VCPKG_ROOT = 'D:\vcpkg') and try again."
}

$vcpkgBin = Join-Path $env:VCPKG_ROOT "installed\x64-windows\bin"
if (Test-Path $vcpkgBin) {
    $env:PATH = "$vcpkgBin;$env:PATH"
}

$cache = Join-Path $PSScriptRoot "build\CMakeCache.txt"
if (-not (Test-Path $cache)) {
    Write-Host "Configuring CMake with vcpkg..."
    cmake -S . -B build "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Building student_server..."
cmake --build build --config Release --target student_server
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$html = Join-Path $PSScriptRoot "index.html"
if (Test-Path $html) {
    Start-Process $html
}

Write-Host "Starting server at ws://localhost:8080  (Ctrl+C to stop)"
& ".\build\Release\student_server.exe" students.csv 8080
