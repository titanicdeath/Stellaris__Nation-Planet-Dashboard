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
- Emit one JSON file per selected nation under `output/<game-date>/`.
- Resolve planet-owned IDs into self-contained structures where practical.
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

## Notes on game definitions

`parse_game_definitions=true` currently builds a lightweight definition-source index from selected `common/` folders. It does not yet fully localize names/descriptions from `localisation/`; the emitted JSON still primarily uses save tokens such as `building_system_capital` or `district_generator`.

## Important implementation notes

The parser stores a full generic AST in memory. That is convenient and robust for v1, but large late-game saves may use substantial RAM. A future v2 should replace this with selective streaming extraction once the dashboard schema stabilizes.

The manifest can still store FNV-1a 64-bit hashes when metadata-based skip is disabled. These hashes are for change detection, not security.
