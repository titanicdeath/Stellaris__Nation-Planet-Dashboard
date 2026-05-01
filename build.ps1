param(
    [switch]$UseVcpkg,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VcpkgTriplet = "x64-windows",

    [switch]$Clean,

    # Allows: .\build.ps1 -t
    [Alias("t")]
    [switch]$Test,

    # In test mode, output is cleaned by default so validation uses fresh exports.
    # Use -KeepOutput to preserve existing output/.
    [switch]$KeepOutput,

    # Optional. Usually leave blank and let CMake choose.
    # Example:
    # .\build.ps1 -Generator "Visual Studio 18 2026"
    [string]$Generator = "",

    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Invoke-Native {
    param(
        [string]$Label,
        [string]$File,
        [string[]]$Arguments
    )

    Write-Host "`n== $Label ==" -ForegroundColor Cyan
    & $File @Arguments

    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Get-FirstCommandPath {
    param([string]$CommandName)

    $result = cmd /c "where $CommandName" 2>$null
    if ($LASTEXITCODE -ne 0 -or $null -eq $result) {
        return ""
    }

    return @($result)[0]
}

function Resolve-VcpkgRoot {
    param([string]$ProvidedRoot)

    if (-not [string]::IsNullOrWhiteSpace($ProvidedRoot)) {
        return $ProvidedRoot
    }

    $vcpkgCmd = Get-Command "vcpkg" -ErrorAction SilentlyContinue
    if ($null -ne $vcpkgCmd) {
        $candidate = Split-Path -Parent $vcpkgCmd.Source
        if (Test-Path (Join-Path $candidate "scripts\buildsystems\vcpkg.cmake")) {
            return $candidate
        }
    }

    $commonCandidates = @(
        "C:\vcpkg",
        "C:\dev\vcpkg",
        "$env:USERPROFILE\vcpkg",
        "$env:USERPROFILE\source\vcpkg",
        "$env:USERPROFILE\dev\vcpkg"
    )

    foreach ($candidate in $commonCandidates) {
        if (Test-Path (Join-Path $candidate "scripts\buildsystems\vcpkg.cmake")) {
            return $candidate
        }
    }

    return ""
}

function Assert-BuildEnvironment {
    $clPath = Get-FirstCommandPath "cl"

    if ([string]::IsNullOrWhiteSpace($clPath)) {
        throw @"
MSVC cl.exe was not found on PATH.

Open a Visual Studio Developer PowerShell / Native Tools prompt, then rerun:
  .\build.ps1 -Clean -t
"@
    }

    Write-Host "MSVC compiler: $clPath" -ForegroundColor DarkGray

    $usingVisualStudioGenerator = (-not [string]::IsNullOrWhiteSpace($Generator)) -and ($Generator -match "Visual Studio")

    if ($Platform -eq "x64" -and -not $usingVisualStudioGenerator) {
        $isX64TargetShell =
            ($clPath -match "\\Hostx64\\x64\\") -or
            ($clPath -match "\\Hostx86\\x64\\")

        if (-not $isX64TargetShell) {
            throw @"
This shell is not configured for x64 MSVC.

Current cl.exe:
  $clPath

Current Platform setting:
  $Platform

Open an x64 Developer PowerShell / x64 Native Tools prompt, then rerun:
  .\build.ps1 -Clean -t

Your current shell appears to target x86, which does not match the intended x64 build.
"@
        }
    }
}

function Get-ParserExe {
    $candidates = @(
        (Join-Path $Root "build\Release\stellaris_parser.exe")
        (Join-Path $Root "build\Debug\stellaris_parser.exe")
        (Join-Path $Root "build\stellaris_parser.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "Parser executable not found. Expected one of: $($candidates -join ', ')"
}

function Has-JsonProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return $false
    }

    return $Object.PSObject.Properties.Name -contains $Name
}

function Get-JsonItemCount {
    param([object]$Value)

    if ($null -eq $Value) {
        return 0
    }

    if ($Value -is [System.Array]) {
        return $Value.Count
    }

    return 1
}

function Test-GeneratedJson {
    $OutputDir = Join-Path $Root "output"

    if (-not (Test-Path $OutputDir)) {
        throw "Output directory not found: $OutputDir"
    }

    $jsonFiles = Get-ChildItem $OutputDir -Recurse -Filter *.json

    if ($jsonFiles.Count -eq 0) {
        throw "No JSON files found under output directory."
    }

    Write-Host "`n== JSON syntax validation ==" -ForegroundColor Cyan

    foreach ($file in $jsonFiles) {
        try {
            Get-Content $file.FullName -Raw | ConvertFrom-Json | Out-Null
            Write-Host "OK  $($file.FullName)"
        } catch {
            Write-Host "BAD $($file.FullName)" -ForegroundColor Red
            Write-Host $_.Exception.Message
            throw "JSON validation failed."
        }
    }

    Write-Host "`n== Dashboard schema sanity checks ==" -ForegroundColor Cyan

    $countryFiles = $jsonFiles | Where-Object {
        $_.FullName -notmatch '\\timeline\\' -and
        $_.Name -ne "manifest.json"
    }

    if ($countryFiles.Count -eq 0) {
        throw "No per-country JSON files found."
    }

    foreach ($file in $countryFiles) {
        $json = Get-Content $file.FullName -Raw | ConvertFrom-Json

        if ($json.schema_version -ne "dashboard-country-v0.1") {
            Write-Host "SKIP $($file.FullName) - unknown schema_version: $($json.schema_version)"
            continue
        }

        foreach ($requiredTopLevel in @(
            "country",
            "colonies",
            "derived_summary",
            "validation",
            "demographics",
            "workforce_summary"
        )) {
            if (-not (Has-JsonProperty $json $requiredTopLevel)) {
                throw "$($file.FullName) is missing top-level field: $requiredTopLevel"
            }
        }

        $colonies = @($json.colonies)

        if ($colonies.Count -eq 0) {
            throw "$($file.FullName) has no colonies array entries"
        }

        foreach ($colony in $colonies) {
            if ((Has-JsonProperty $colony "resolved") -and $colony.resolved -eq $false) {
                continue
            }

            $planetId = if (Has-JsonProperty $colony "planet_id") { $colony.planet_id } else { "<unknown>" }

            if (-not (Has-JsonProperty $colony "derived_summary")) {
                throw "$($file.FullName) colony $planetId is missing derived_summary"
            }

            if (-not (Has-JsonProperty $colony.derived_summary "presentation_card")) {
                throw "$($file.FullName) colony $planetId is missing derived_summary.presentation_card"
            }

            foreach ($requiredColonySummary in @(
                "species_counts_by_id",
                "pop_category_counts",
                "job_counts_by_type",
                "workforce_by_job_type",
                "species_counts_by_name"
            )) {
                if (-not (Has-JsonProperty $colony.derived_summary $requiredColonySummary)) {
                    throw "$($file.FullName) colony $planetId is missing derived_summary.$requiredColonySummary"
                }
            }
        }

        foreach ($requiredValidationField in @(
            "colonies_missing_derived_summary",
            "species_without_resolution",
            "demographics_species_count",
            "demographics_total_pops",
            "demographics_matches_country_pop_count",
            "colonies_missing_demographic_summary"
        )) {
            if (-not (Has-JsonProperty $json.validation $requiredValidationField)) {
                throw "$($file.FullName) is missing validation.$requiredValidationField"
            }
        }

        if ((Get-JsonItemCount $json.demographics.species) -eq 0) {
            throw "$($file.FullName) has empty demographics.species"
        }

        Write-Host "OK  schema sanity: $($file.FullName)"
    }

    $settingsPath = Join-Path $Root "settings.config"
    $timelineDisabled = $false

    if (Test-Path $settingsPath) {
        $timelineDisabled = Select-String -Path $settingsPath -Pattern '^\s*export_timeline\s*=\s*false\s*$' -Quiet
    }

    if (-not $timelineDisabled) {
        $timelineDir = Join-Path $OutputDir "timeline"

        if (-not (Test-Path $timelineDir)) {
            throw "Timeline export appears enabled, but output\timeline was not created."
        }

        $timelineFiles = Get-ChildItem $timelineDir -Recurse -Filter *.json

        if ($timelineFiles.Count -eq 0) {
            throw "Timeline export appears enabled, but no timeline JSON files were written."
        }

        foreach ($file in $timelineFiles) {
            $timeline = Get-Content $file.FullName -Raw | ConvertFrom-Json

            if ($timeline.schema_version -ne "dashboard-country-timeline-v0.1") {
                throw "$($file.FullName) has unexpected timeline schema_version: $($timeline.schema_version)"
            }

            if (-not (Has-JsonProperty $timeline "snapshots")) {
                throw "$($file.FullName) is missing snapshots"
            }

            if ((Get-JsonItemCount $timeline.snapshots) -eq 0) {
                throw "$($file.FullName) has no timeline snapshots"
            }

            Write-Host "OK  timeline sanity: $($file.FullName)"
        }
    }

    Write-Host "`nAll JSON validation checks passed." -ForegroundColor Green
}

# Auto-enable vcpkg when the repository has a vcpkg manifest.
$UseVcpkgEffective = $UseVcpkg.IsPresent
if (-not $UseVcpkgEffective -and (Test-Path (Join-Path $Root "vcpkg.json"))) {
    $UseVcpkgEffective = $true
}

Assert-BuildEnvironment

if (($Clean -or $Test) -and (Test-Path ".\build")) {
    Write-Host "Removing build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force ".\build"
}

if ($Test -and -not $KeepOutput -and (Test-Path ".\output")) {
    Write-Host "Removing output directory for fresh validation..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force ".\output"
}

$configureArgs = @(
    "-S", ".",
    "-B", "build"
)

# Do not pass -A x64 by default. NMake does not support platform args.
# If a Visual Studio generator is explicitly requested, then -A is valid.
if (-not [string]::IsNullOrWhiteSpace($Generator)) {
    $configureArgs += @("-G", $Generator)

    if ($Generator -match "Visual Studio") {
        $configureArgs += @("-A", $Platform)
    }
}

if ($UseVcpkgEffective) {
    $ResolvedVcpkgRoot = Resolve-VcpkgRoot $VcpkgRoot

    if ([string]::IsNullOrWhiteSpace($ResolvedVcpkgRoot)) {
        throw @"
vcpkg is required because this repository contains vcpkg.json, but VcpkgRoot could not be resolved.

Set VCPKG_ROOT or pass it explicitly:
  .\build.ps1 -Clean -t -VcpkgRoot C:\dev\vcpkg
"@
    }

    $Toolchain = Join-Path $ResolvedVcpkgRoot "scripts\buildsystems\vcpkg.cmake"

    if (-not (Test-Path $Toolchain)) {
        throw "Could not find vcpkg toolchain file at: $Toolchain"
    }

    Write-Host "Using vcpkg root: $ResolvedVcpkgRoot" -ForegroundColor DarkGray
    Write-Host "Using vcpkg triplet: $VcpkgTriplet" -ForegroundColor DarkGray

    $configureArgs += @(
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
        "-DVCPKG_TARGET_TRIPLET=$VcpkgTriplet"
    )
}

Invoke-Native "Configure CMake" "cmake" $configureArgs
Invoke-Native "Build parser" "cmake" @("--build", "build", "--config", "Release")

if ($Test) {
    $ParserExe = Get-ParserExe

    Invoke-Native "Parser self-test" $ParserExe @("--self-test")
    Invoke-Native "Parse real saves" $ParserExe @()

    Test-GeneratedJson
}

Write-Host "`nBuild script completed successfully." -ForegroundColor Green