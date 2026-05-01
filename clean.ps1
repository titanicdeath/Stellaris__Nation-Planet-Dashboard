$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
if (Test-Path $Build) {
    Remove-Item -Recurse -Force $Build
    Write-Host "Removed build directory: $Build" -ForegroundColor Green
} else {
    Write-Host "No build directory found." -ForegroundColor Yellow
}
