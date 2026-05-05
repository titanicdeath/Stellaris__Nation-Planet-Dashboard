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

    # Fast validation keeps build/output in place and lets manifest skip logic
    # prove unchanged saves are skipped before gamestate extraction and parsing.
    [switch]$Fast,

    # Optional. Usually leave blank and let CMake choose.
    # Example:
    # .\build.ps1 -Generator "Visual Studio 18 2026"
    [string]$Generator = "",

    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Format-Duration {
    param([TimeSpan]$Duration)

    if ($Duration.TotalHours -ge 1) {
        return "{0:hh\:mm\:ss\.fff}" -f $Duration
    }

    return "{0:mm\:ss\.fff}" -f $Duration
}

function Invoke-NativeTimed {
    param(
        [string]$Label,
        [string]$File,
        [string[]]$Arguments,
        [ref]$ElapsedOut
    )

    Write-Host "`n== $Label ==" -ForegroundColor Cyan

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    & $File @Arguments
    $exitCode = $LASTEXITCODE
    $timer.Stop()

    if ($null -ne $ElapsedOut) {
        $ElapsedOut.Value = $timer.Elapsed
    }

    Write-Host "Completed $Label in $(Format-Duration $timer.Elapsed)" -ForegroundColor DarkGray

    if ($exitCode -ne 0) {
        throw "$Label failed with exit code $exitCode after $(Format-Duration $timer.Elapsed)"
    }
}

function Get-FirstCommandPath {
    param([string]$CommandName)

    $cmd = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        return $cmd.Source
    }

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $result = cmd /d /c "where $CommandName 2>NUL"
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

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

function Test-IsHardUnresolvedDisplayName {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }

    $s = $Value.Trim()
    if ($s.Contains('$') -or $s.Contains('%')) {
        return $true
    }

    if (-not $s.Contains('_')) {
        return $false
    }

    return ($s -match '[A-Za-z]' -and $s -cnotmatch '[a-z]')
}

function Test-UnresolvedDisplayNameField {
    param(
        [object]$Object,
        [string]$Field,
        [string]$Marker,
        [string]$Path
    )

    if (-not (Has-JsonProperty $Object $Field)) {
        return
    }

    $value = $Object.PSObject.Properties[$Field].Value
    if ($null -eq $value -or $value -isnot [string]) {
        return
    }

    if (-not (Test-IsHardUnresolvedDisplayName $value)) {
        return
    }

    if (-not (Has-JsonProperty $Object $Marker)) {
        throw "${Path}.${Field} contains unresolved display name '$value' but is missing $Marker"
    }

    $markerValue = $Object.PSObject.Properties[$Marker].Value
    if ($markerValue -ne $true) {
        throw "${Path}.${Field} contains unresolved display name '$value' but $Marker is not true"
    }
}

function Test-GeneratedDisplayNameField {
    param(
        [object]$Object,
        [string]$Field,
        [string]$RawField,
        [string]$GeneratedMarker,
        [string]$Path
    )

    if (-not (Has-JsonProperty $Object $GeneratedMarker)) {
        return
    }

    if ($Object.PSObject.Properties[$GeneratedMarker].Value -ne $true) {
        throw "${Path}.${GeneratedMarker} must be true when present"
    }

    if (-not (Has-JsonProperty $Object $RawField)) {
        throw "${Path}.${GeneratedMarker} is present but ${RawField} is missing"
    }

    if (-not (Has-JsonProperty $Object $Field)) {
        throw "${Path}.${GeneratedMarker} is present but ${Field} is missing"
    }

    $rawValue = $Object.PSObject.Properties[$RawField].Value
    if ($null -eq $rawValue -or $rawValue -isnot [string] -or [string]::IsNullOrWhiteSpace($rawValue)) {
        throw "${Path}.${RawField} must be a non-empty string"
    }

    if (Test-IsHardUnresolvedDisplayName $rawValue) {
        throw "${Path}.${RawField} contains hard unresolved placeholder '$rawValue' but is marked as generated fallback"
    }
}

function Test-JsonScalarIdString {
    param(
        [object]$Value,
        [string]$Path
    )

    if ($null -eq $Value) {
        return
    }

    if ($Value -is [string]) {
        return
    }

    throw "$Path must be a JSON string ID/reference, but was $($Value.GetType().Name)"
}

function Test-JsonIdStringArray {
    param(
        [object]$Value,
        [string]$Path
    )

    foreach ($item in @($Value)) {
        Test-JsonScalarIdString $item $Path
    }
}

function Test-JsonIdKeyedMap {
    param(
        [object]$Value,
        [string]$Path
    )

    if ($null -eq $Value) {
        return
    }

    if ($Value -isnot [System.Management.Automation.PSCustomObject]) {
        throw "$Path must be a JSON object/map keyed by ID"
    }
}

function Test-IsJsonCountOrStatPath {
    param(
        [string]$Name,
        [string]$Path
    )

    if ($Path -like '$.debug.index_counts.*' -or $Path -like '*.index_counts.*') {
        return $true
    }

    if ($Name -match '_count$' -or $Name -match '_counts$' -or $Name -match '_kinds$') {
        return $true
    }

    if ($Path -match '_kinds\.') {
        return $true
    }

    return $false
}

function Test-IsSpeciesReferencePath {
    param([string]$Path)

    # `species_id` is handled by the generic *_id rule. Plain `species` is
    # only an ID reference in known entity-reference locations; elsewhere it
    # may be a count/category label such as $.debug.index_counts.species.
    return (
        $Path -match '\.pop_groups\[\d+\]\.key\.species$' -or
        $Path -match '\.armies\[\d+\]\.species$' -or
        $Path -match '\.leaders\.[^.]+\.species$' -or
        $Path -match '\.(leader|commander|governor)\.species$'
    )
}

function Test-JsonIdReferenceShape {
    param(
        [string]$Name,
        [object]$Value,
        [string]$Path
    )

    if ($Path -eq '$.coordinate.origin' -or $Path -like '*.coordinate.origin') {
        return
    }

    if (Test-IsJsonCountOrStatPath $Name $Path) {
        return
    }

    # *_by_id fields are maps keyed by ID. JSON object keys are already strings;
    # the map values are facts such as counts or nested objects, not ID scalars.
    if ($Name -match '_by_id$') {
        Test-JsonIdKeyedMap $Value $Path
        return
    }

    # composition_by_species is keyed by species ID, with numeric counts as values.
    if ($Name -eq "composition_by_species") {
        Test-JsonIdKeyedMap $Value $Path
        return
    }

    # *_ids fields are arrays of ID strings.
    if ($Name -match '_ids$') {
        if ($null -ne $Value -and $Value -isnot [System.Array]) {
            throw "$Path must be an array of JSON string IDs"
        }
        Test-JsonIdStringArray $Value $Path
        return
    }

    # *_id fields are scalar ID strings.
    if ($Name -match '_id$') {
        Test-JsonScalarIdString $Value $Path
        return
    }

    if ($Name -eq "species") {
        if (Test-IsSpeciesReferencePath $Path) {
            Test-JsonScalarIdString $Value $Path
        }
        return
    }

    $knownReferenceFields = @(
        "owner",
        "controller",
        "original_owner",
        "capital",
        "starting_system",
        "home_planet",
        "country",
        "heir",
        "council_positions",
        "subjects",
        "planet",
        "spawning_planet",
        "pop_faction",
        "sector"
    )

    if ($knownReferenceFields -notcontains $Name) {
        return
    }

    if ($null -eq $Value) {
        return
    }

    if ($Value -is [System.Array]) {
        foreach ($item in @($Value)) {
            if ($item -is [System.Management.Automation.PSCustomObject]) {
                return
            }
        }
        Test-JsonIdStringArray $Value $Path
        return
    }

    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        return
    }

    Test-JsonScalarIdString $Value $Path
}

function Get-AssignedPopGroupAmount {
    param([object]$Job)

    if (-not (Has-JsonProperty $Job "pop_groups")) {
        return 0.0
    }

    $total = 0.0
    foreach ($group in @($Job.pop_groups)) {
        if ((Has-JsonProperty $group "pop_group") -and [string]$group.pop_group -ne "4294967295" -and (Has-JsonProperty $group "amount")) {
            $total += [double]$group.amount
        }
    }
    return $total
}

function Test-ExportableJobObject {
    param(
        [object]$Job,
        [string]$Path
    )

    $sentinelTypes = @(
        "crisis_purge",
        "bio_trophy_processing",
        "bio_trophy_unemployment",
        "bio_trophy_unprocessing",
        "neural_chip",
        "neural_chip_processing",
        "neural_chip_unprocessing",
        "purge_unprocessing",
        "slave_processing",
        "slave_unprocessing",
        "presapient_unprocessing",
        "event_purge"
    )

    if (-not (Has-JsonProperty $Job "type") -or [string]::IsNullOrWhiteSpace([string]$Job.type)) {
        throw "$Path contains a job without an exportable type"
    }

    $type = [string]$Job.type
    $assignedAmount = Get-AssignedPopGroupAmount $Job
    $workforce = if (Has-JsonProperty $Job "workforce") { [double]$Job.workforce } else { 0.0 }

    $allSentinelWorkforce =
        (Has-JsonProperty $Job "workforce") -and ([double]$Job.workforce -eq -1.0) -and
        (Has-JsonProperty $Job "max_workforce") -and ([double]$Job.max_workforce -eq -1.0) -and
        (Has-JsonProperty $Job "automated_workforce") -and ([double]$Job.automated_workforce -eq -1.0) -and
        (Has-JsonProperty $Job "workforce_limit") -and ([double]$Job.workforce_limit -eq -1.0)

    if ($allSentinelWorkforce) {
        throw "$Path contains a sentinel job record of type $type"
    }

    if ((Has-JsonProperty $Job "pop_group") -and [string]$Job.pop_group -eq "4294967295" -and $assignedAmount -le 0.0) {
        throw "$Path contains unresolved pop_group sentinel for job type $type"
    }

    if ($workforce -le 0.0 -and $assignedAmount -le 0.0) {
        throw "$Path contains non-exportable job type $type with no workforce or assigned pops"
    }

    if ($sentinelTypes -contains $type -and $assignedAmount -le 0.0) {
        throw "$Path contains suppressed sentinel job type $type"
    }
}

function Test-CountryJsonTree {
    param(
        [object]$Value,
        [string]$Path
    )

    if ($null -eq $Value) {
        return
    }

    if ($Value -is [System.Array]) {
        for ($i = 0; $i -lt $Value.Count; $i++) {
            Test-CountryJsonTree $Value[$i] "$Path[$i]"
        }
        return
    }

    if ($Value -isnot [System.Management.Automation.PSCustomObject]) {
        return
    }

    if ((Has-JsonProperty $Value "type") -and [string]$Value.type -eq "defense_army") {
        throw "$Path contains banned defense_army object"
    }

    Test-UnresolvedDisplayNameField $Value "name" "name_unresolved" $Path
    Test-UnresolvedDisplayNameField $Value "formation_name" "formation_name_unresolved" $Path
    Test-UnresolvedDisplayNameField $Value "adjective" "adjective_unresolved" $Path
    Test-UnresolvedDisplayNameField $Value "planet_name" "planet_name_unresolved" $Path
    Test-UnresolvedDisplayNameField $Value "system_name" "system_name_unresolved" $Path
    Test-GeneratedDisplayNameField $Value "name" "name_raw" "name_generated_from_key" $Path
    Test-GeneratedDisplayNameField $Value "formation_name" "formation_name_raw" "formation_name_generated_from_key" $Path
    Test-GeneratedDisplayNameField $Value "adjective" "adjective_raw" "adjective_generated_from_key" $Path
    Test-GeneratedDisplayNameField $Value "planet_name" "planet_name_raw" "planet_name_generated_from_key" $Path
    Test-GeneratedDisplayNameField $Value "system_name" "system_name_raw" "system_name_generated_from_key" $Path

    foreach ($property in $Value.PSObject.Properties) {
        $childPath = "$Path.$($property.Name)"

        if ($property.Name -like "inactive_job_*" -and $childPath -ne '$.validation.inactive_job_records_suppressed') {
            throw "$childPath uses a banned inactive job field name"
        }

        if ($property.Name -eq "raw_owned_armies") {
            throw "$childPath uses banned raw_owned_armies"
        }

        Test-JsonIdReferenceShape $property.Name $property.Value $childPath

        if ($property.Name -eq "jobs") {
            foreach ($job in @($property.Value)) {
                Test-ExportableJobObject $job $childPath
            }
        }

        Test-CountryJsonTree $property.Value $childPath
    }
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
        $rawCountryJson = Get-Content $file.FullName -Raw
        $json = $rawCountryJson | ConvertFrom-Json

        if ($json.schema_version -ne "dashboard-country-v0.1") {
            Write-Host "SKIP $($file.FullName) - unknown schema_version: $($json.schema_version)"
            continue
        }

        foreach ($bannedTerm in @(
            "crisis_purge",
            "bio_trophy_processing",
            "bio_trophy_unemployment",
            "bio_trophy_unprocessing",
            "neural_chip",
            "neural_chip_processing",
            "neural_chip_unprocessing",
            "purge_unprocessing",
            "slave_processing",
            "slave_unprocessing",
            "presapient_unprocessing",
            "event_purge",
            "defense_army",
            "raw_owned_armies",
            "inactive_jobs_by_planet",
            "inactive_job_types_suppressed",
            "inactive_job_record_counts_by_type",
            "inactive_job_record_count"
        )) {
            if ($rawCountryJson.Contains($bannedTerm)) {
                throw "BANNED TERM FOUND: $bannedTerm in $($file.FullName)"
            }
        }

        Test-CountryJsonTree $json '$'

        if ($file.Name -notmatch '_\d{4}-\d{2}-\d{2}\.json$') {
            throw "$($file.FullName) must include a _YYYY-MM-DD suffix before .json"
        }

        $storedResourceLiteralCount = ([regex]::Matches($rawCountryJson, '"stored_resources"\s*:')).Count
        if ($storedResourceLiteralCount -ne 1) {
            throw "$($file.FullName) must contain literal `"stored_resources`": exactly once; found $storedResourceLiteralCount"
        }

        foreach ($requiredTopLevel in @(
            "country",
            "nat_finance_economy",
            "colonies",
            "derived_summary",
            "validation",
            "demographics",
            "workforce_summary",
            "systems",
            "army_formations"
        )) {
            if (-not (Has-JsonProperty $json $requiredTopLevel)) {
                throw "$($file.FullName) is missing top-level field: $requiredTopLevel"
            }
        }

        if (Has-JsonProperty $json "stored_resources") {
            throw "$($file.FullName) still exposes top-level stored_resources"
        }

        if (Has-JsonProperty $json "economy") {
            throw "$($file.FullName) still exposes top-level economy"
        }

        if (Has-JsonProperty $json.country "budget") {
            throw "$($file.FullName) still exposes country.budget"
        }

        if ((Has-JsonProperty $json.derived_summary "economy") -and
            (Has-JsonProperty $json.derived_summary.economy "stored_resources")) {
            throw "$($file.FullName) still exposes derived_summary.economy.stored_resources"
        }

        foreach ($requiredFinanceField in @(
            "budget",
            "net_monthly_resource",
            "stored_resources"
        )) {
            if (-not (Has-JsonProperty $json.nat_finance_economy $requiredFinanceField)) {
                throw "$($file.FullName) is missing nat_finance_economy.$requiredFinanceField"
            }
        }

        if ((Has-JsonProperty $json "capital_planet") -and $null -ne $json.capital_planet) {
            $allowedCapitalKeys = @("planet_id", "name", "name_unresolved", "name_raw", "name_generated_from_key", "system_id", "system_name", "system_name_unresolved", "system_name_raw", "system_name_generated_from_key")
            foreach ($capitalKey in $json.capital_planet.PSObject.Properties.Name) {
                if ($allowedCapitalKeys -notcontains $capitalKey) {
                    throw "$($file.FullName) capital_planet contains disallowed key: $capitalKey"
                }
            }
        }

        if (-not (Has-JsonProperty $json "map_summary") -and -not (Has-JsonProperty $json.derived_summary "map")) {
            throw "$($file.FullName) is missing map_summary or derived_summary.map"
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

            if (Has-JsonProperty $colony "system") {
                throw "$($file.FullName) colony $planetId still contains top-level system"
            }

            if (-not (Has-JsonProperty $colony "derived_summary")) {
                throw "$($file.FullName) colony $planetId is missing derived_summary"
            }

            if (-not (Has-JsonProperty $colony.derived_summary "presentation_card")) {
                throw "$($file.FullName) colony $planetId is missing derived_summary.presentation_card"
            }

            if ((Has-JsonProperty $colony.derived_summary "system_id") -and
                (-not [string]::IsNullOrWhiteSpace([string]$colony.derived_summary.system_id)) -and
                (-not ($json.systems.PSObject.Properties.Name -contains ([string]$colony.derived_summary.system_id)))) {
                throw "$($file.FullName) colony $planetId has derived_summary.system_id $($colony.derived_summary.system_id), but that system is absent from top-level systems"
            }

            foreach ($requiredColonySummary in @(
                "species_counts_by_id",
                "pop_category_counts",
                "job_counts_by_type",
                "active_job_counts_by_type",
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
            "colonies_missing_demographic_summary",
            "systems_exported_count",
            "colonies_with_unexported_systems",
            "has_stored_resources",
            "stored_resource_count",
            "inactive_job_records_suppressed",
            "non_military_fleet_records_suppressed",
            "defense_armies_suppressed",
            "army_formations_count",
            "unresolved_name_count",
            "unresolved_name_kinds",
            "generated_name_key_count",
            "generated_name_key_kinds"
        )) {
            if (-not (Has-JsonProperty $json.validation $requiredValidationField)) {
                throw "$($file.FullName) is missing validation.$requiredValidationField"
            }
        }

        foreach ($colony in $colonies) {
            if ((Has-JsonProperty $colony "resolved") -and $colony.resolved -eq $false) {
                continue
            }

            foreach ($speciesNameKey in $colony.derived_summary.species_counts_by_name.PSObject.Properties.Name) {
                if ((Test-IsHardUnresolvedDisplayName $speciesNameKey) -and $speciesNameKey -notmatch ' \[#.+\]$') {
                    throw "$($file.FullName) colony $($colony.planet_id) has unresolved species_counts_by_name key without species-id disambiguation: $speciesNameKey"
                }
            }
        }

        if (-not (Has-JsonProperty $json.workforce_summary "active_job_counts_by_type")) {
            throw "$($file.FullName) is missing workforce_summary.active_job_counts_by_type"
        }

        if (-not (Has-JsonProperty $json.workforce_summary "active_jobs_by_planet")) {
            throw "$($file.FullName) is missing workforce_summary.active_jobs_by_planet"
        }

        if ($file.FullName -match '\\2220-12-16\\') {
            if ((Has-JsonProperty $json.workforce_summary "job_counts_by_type") -and
                (Has-JsonProperty $json.workforce_summary.job_counts_by_type "crisis_purge")) {
                throw "$($file.FullName) still exposes crisis_purge in workforce_summary.job_counts_by_type"
            }

            if ((Has-JsonProperty $json.workforce_summary "active_job_counts_by_type") -and
                (Has-JsonProperty $json.workforce_summary.active_job_counts_by_type "crisis_purge")) {
                throw "$($file.FullName) still exposes crisis_purge in workforce_summary.active_job_counts_by_type"
            }

            foreach ($colony in $colonies) {
                if ((Has-JsonProperty $colony "resolved") -and $colony.resolved -eq $false) {
                    continue
                }

                if ((Has-JsonProperty $colony.derived_summary "job_counts_by_type") -and
                    (Has-JsonProperty $colony.derived_summary.job_counts_by_type "crisis_purge")) {
                    throw "$($file.FullName) colony $($colony.planet_id) still exposes crisis_purge in derived_summary.job_counts_by_type"
                }

                if ((Has-JsonProperty $colony.derived_summary "active_job_counts_by_type") -and
                    (Has-JsonProperty $colony.derived_summary.active_job_counts_by_type "crisis_purge")) {
                    throw "$($file.FullName) colony $($colony.planet_id) still exposes crisis_purge in derived_summary.active_job_counts_by_type"
                }
            }

            foreach ($army in @($json.owned_armies)) {
                if ((Has-JsonProperty $army "type") -and $army.type -eq "defense_army") {
                    throw "$($file.FullName) still exposes defense_army in top-level owned_armies"
                }
            }

            $countryMilitaryZero =
                ([double]$json.country.military_power -eq 0.0) -and
                ([double]$json.country.fleet_size -eq 0.0) -and
                ([double]$json.country.used_naval_capacity -eq 0.0)

            if ($countryMilitaryZero -and @($json.fleets).Count -ne 0) {
                throw "$($file.FullName) has zero country military metrics but non-empty top-level fleets"
            }
        }

        if ((Has-JsonProperty $json.validation "hyperlane_targets_missing_from_export")) {
            foreach ($target in @($json.validation.hyperlane_targets_missing_from_export)) {
                if ($null -ne $target -and $target -is [System.Management.Automation.PSCustomObject]) {
                    throw "$($file.FullName) validation.hyperlane_targets_missing_from_export contains a malformed target entry"
                }
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

$scriptTimer = [System.Diagnostics.Stopwatch]::StartNew()

# Auto-enable vcpkg when the repository has a vcpkg manifest.
$UseVcpkgEffective = $UseVcpkg.IsPresent
if (-not $UseVcpkgEffective -and (Test-Path (Join-Path $Root "vcpkg.json"))) {
    $UseVcpkgEffective = $true
}

Assert-BuildEnvironment

if ($Clean -and (Test-Path ".\build")) {
    Write-Host "Removing build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force ".\build"
}

if ($Test -and -not $Fast -and -not $KeepOutput -and (Test-Path ".\output")) {
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

$configureTime = [TimeSpan]::Zero
$buildTime = [TimeSpan]::Zero
$selfTestTime = [TimeSpan]::Zero
$parseTime = [TimeSpan]::Zero
$jsonValidationTime = [TimeSpan]::Zero

Invoke-NativeTimed -Label "Configure CMake" -File "cmake" -Arguments $configureArgs -ElapsedOut ([ref]$configureTime)
Invoke-NativeTimed -Label "Build parser" -File "cmake" -Arguments @("--build", "build", "--config", "Release") -ElapsedOut ([ref]$buildTime)

if ($Test) {
    $ParserExe = Get-ParserExe

    Invoke-NativeTimed -Label "Parser self-test" -File $ParserExe -Arguments @("--self-test") -ElapsedOut ([ref]$selfTestTime)

    # Full test mode forces a fresh parser pass. Fast mode intentionally does
    # not clean output or force reparse, so unchanged saves can be skipped.
    $parserArgs = @()
    if (-not $Fast) {
        $parserArgs += "--force-reparse"
    }

    Invoke-NativeTimed -Label "Parse real saves" -File $ParserExe -Arguments $parserArgs -ElapsedOut ([ref]$parseTime)

    Write-Host "`n== JSON/schema validation ==" -ForegroundColor Cyan
    $validationTimer = [System.Diagnostics.Stopwatch]::StartNew()
    Test-GeneratedJson
    $validationTimer.Stop()
    $jsonValidationTime = $validationTimer.Elapsed

    Write-Host "Completed JSON/schema validation in $(Format-Duration $jsonValidationTime)" -ForegroundColor DarkGray
}

$scriptTimer.Stop()

Write-Host "`n== Timing Summary ==" -ForegroundColor Cyan
Write-Host ("Configure CMake:        {0}" -f (Format-Duration $configureTime))
Write-Host ("Build parser:           {0}" -f (Format-Duration $buildTime))

if ($Test) {
    Write-Host ("Parser self-test:       {0}" -f (Format-Duration $selfTestTime))
    Write-Host ("Parse real saves:       {0}" -f (Format-Duration $parseTime)) -ForegroundColor Green
    Write-Host ("JSON/schema validation: {0}" -f (Format-Duration $jsonValidationTime))
}

Write-Host ("Total script time:      {0}" -f (Format-Duration $scriptTimer.Elapsed))

Write-Host "`nBuild script completed successfully." -ForegroundColor Green
