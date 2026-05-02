# Stellaris Nation / Planet Dashboard Parser


C++20 parser scaffold for extracting nation- and colony-level data from Stellaris saves into dashboard-ready JSON files.


## Current milestone


This version is aimed at the first practical milestone:

- Read `.sav` files directly.
- Extract the embedded `gamestate` in memory.
- Optionally retain the extracted `gamestate` to disk for debugging.
- Parse Paradox `key=value` / nested-brace syntax into a generic AST.
- Build lookup indexes for countries, planets, species, leaders, buildings, districts, zones, deposits, pop groups, pop jobs, fleets, armies, systems, sectors, and construction queues.
- Select nations according to `settings.config`.
- Emit one JSON file per selected nation under `output/<game-date>/`, with filenames ending in `_YYYY-MM-DD.json`.
- Resolve planet-owned IDs into self-contained structures where practical.
- Export dashboard hygiene fields: active job summaries, numeric suppression totals, military-only fleets, grouped non-defense army formations, and `nat_finance_economy` finance/stockpile data.
- Maintain a metadata-first manifest so unchanged saves/settings are skipped before `.sav` extraction and PDX parsing.

## Build requirements

- C++20 compiler
- CMake 3.20+
- zlib development package

On Windows 11, the easiest route is usually Visual Studio Build Tools plus vcpkg:

```powershell
vcpkg install zlib:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
.\run.ps1
```

If zlib is installed in another way, normal CMake package discovery may work:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\run.ps1
```

## Settings

Edit `settings.config`. The defaults are conservative:

- Parse all non-autosave files in `save_files/`.
- Parse only the player country.
- Do not parse game definition files by default.
- Do not retain extracted `gamestate` by default.
- Reparse automatically when `settings.config` changes.

### Manifest skip and targeted runs

`[manifest] enable_skip=true` lets the parser skip saves that already have a matching manifest entry. The skip check uses cheap file metadata by default: absolute path, filename, file size, last modified timestamp, settings hash, parser version, and the presence of previously exported country JSON files. This happens before `gamestate` extraction and before `parse_document`.

Set `[manifest] force_reparse=true` or `[save_selection] force_reparse=true` to disable skipping for a run. Set `skip_requires_output_files=true` to stay conservative when generated JSON is missing.

`[save_selection] specific_save_files=2410.12.02.sav,autosave_2410.07.01.sav` with `parse_all_save_files=false` parses only those files. `latest_save_only=true` parses the newest matching save by modified time; `latest_save_include_autosaves=true` allows autosaves in that latest-save choice.

CLI overrides are available for development:

```powershell
.\build\stellaris_parser.exe --save 2410.12.02.sav
.\build\stellaris_parser.exe --latest-save
.\build\stellaris_parser.exe --include-autosaves
.\build\stellaris_parser.exe --force-reparse
```

Timeline export preserves existing timeline files during a single-save targeted run if timeline files already exist. This avoids replacing a full multi-save timeline with one snapshot; perfect timeline merging is left for a later milestone.

Per-country timeline snapshots store `output_json_path` values pointing at the dated per-country filenames, for example `output/2220.12.16/0-(Tetra)_2220-12-16.json`.

### Debug performance timings

`[debug] print_performance_timings=true` prints per-save and total parser-internal timings. If the setting is omitted, it defaults to `false`.

The timing blocks cover manifest hash/check-skip work, `.sav` load and `gamestate` extraction, PDX document parsing, index building, country selection, per-country export, total country export, manifest writes, timeline writes, and total run time. They also include inexpensive counts such as indexed countries, planets, species, fleets, armies, selected countries, exported colonies, and exported systems.

`[debug] write_performance_log=true` additionally writes the same instrumentation to `output/performance-log.json`. It defaults to `false`.

For skip benchmarking:

```powershell
.\build.ps1 -Clean -t
.\build.ps1 -t -Fast
```

The first command cleans build/output and performs a full fresh validation. The second command keeps output and should show unchanged saves skipped before `parse_document`, then validates the existing/generated JSON. This prepares the parser for a future save monitor by making targeted parsing cheap, but the live save monitor itself is not implemented yet.

### Dashboard payload hygiene

Job count summaries report active dashboard jobs only. Inactive/sentinel jobs are not part of the export contract: their records and names are not emitted in colony jobs, active job maps, derived summaries, workforce summaries, debug sections, or raw output paths. Suppressed jobs only increment the numeric `validation.inactive_job_records_suppressed` counter. `workforce_by_job_type` remains available for exportable jobs.

Top-level `fleets` now contains military fleets only. Starbases, mining/research stations, science ships, constructors, civilian fleets, orbital stations, and zero-power records without military markers are suppressed from that array but remain available through system/map context when relevant.

Defense armies are not part of the export contract and are filtered before top-level, colony/local, formation, debug, or raw output paths. Suppressed defense armies only increment `validation.defense_armies_suppressed`. Non-defense armies are grouped under `army_formations` by `fleet_name` when present, otherwise by planet and army type.

Country finance data is exposed only under top-level `nat_finance_economy`. Its `budget` uses the current-month income, expenses, and balance breakdown; `net_monthly_resource` is derived by summing current-month balance categories by resource; and `stored_resources` is the only stockpile location. The old top-level `economy`, top-level `stored_resources`, `country.budget`, and `derived_summary.economy.stored_resources` locations are not emitted.

`capital_planet` is a navigation stub with `planet_id`, `name`, `system_id`, and `system_name`; the full capital colony record lives in `colonies[]`. Colonies no longer embed a top-level `system` object; use `derived_summary.system_id` or `derived_summary.map.system_id` to join to top-level `systems`.

JSON ID/reference values are strings throughout the dashboard schema. Object keys under top-level `systems`, `species`, and `leaders` are ID keys. `coordinate.origin` is the explicit exception and remains numeric because it belongs to coordinate data and may use the `4294967295` sentinel.

## Notes on game definitions

`parse_game_definitions=true` currently builds a lightweight definition-source index from selected `common/` folders. It does not yet fully localize names/descriptions from `localisation/`; the emitted JSON still primarily uses save tokens such as `building_system_capital` or `district_generator`.

## Important implementation notes

The parser stores a full generic AST in memory. That is convenient and robust for v1, but large late-game saves may use substantial RAM. A future v2 should replace this with selective streaming extraction once the dashboard schema stabilizes.

The manifest can still store FNV-1a 64-bit hashes when metadata-based skip is disabled. These hashes are for change detection, not security.
