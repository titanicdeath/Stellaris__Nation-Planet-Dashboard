param(
    [string]$Config = "settings.config"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $Root "build\Release\stellaris_parser.exe"
if (-not (Test-Path $Exe)) {
    $Exe = Join-Path $Root "build\stellaris_parser.exe"
}
if (-not (Test-Path $Exe)) {
    Write-Host "Parser executable not found. Build first with:" -ForegroundColor Yellow
    Write-Host "  cmake -S . -B build"
    Write-Host "  cmake --build build --config Release"
    exit 1
}

& $Exe --config (Join-Path $Root $Config)
