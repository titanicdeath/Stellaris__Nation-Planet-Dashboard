param(
    [switch]$UseVcpkg,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [switch]$Clean
)
#fuck off
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

if ($Clean -and (Test-Path ".\build")) {
    Remove-Item -Recurse -Force ".\build"
}

if ($UseVcpkg) {
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        throw "VcpkgRoot was not provided and VCPKG_ROOT is not set. Example: .\build.ps1 -UseVcpkg -VcpkgRoot C:\dev\vcpkg -Clean"
    }
    $Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if (-not (Test-Path $Toolchain)) {
        throw "Could not find vcpkg toolchain file at: $Toolchain"
    }
    cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE="$Toolchain"
} else {
    cmake -S . -B build -A x64
}

cmake --build build --config Release
