# Stellaris Nation / Planet Dashboard Parser


C++20 parser scaffold for extracting nation- and colony-level data from Stellaris saves into dashboard-ready JSON files.


## Current milestone


This version is aimed at the first practical milestone:

- Read `.sav` files directly.
- Extract the embedded `gamestate` in memory.
- Optionally retain the extracted `gamestate` to disk for debugging.
- Parse Paradox `key=value` / nested-brace syntax into a generic AST.
- Build lookup indexes for countries, planets, species, leaders, buildings, districts, zones, deposits, pop groups, pop jobs, fleets, ships, ship designs, armies, systems, sectors, and construction queues.
- Select nations according to `settings.config`.
- Emit one JSON file per selected nation under `output/<game-date>/`, with filenames ending in `_YYYY-MM-DD.json`.
- Resolve planet-owned IDs into self-contained structures where practical.
- Export dashboard hygiene fields: active job summaries, numeric suppression totals, military-only fleet/ship/design data, grouped non-defense army formations, and `nat_finance_economy` finance/stockpile data.
- Export the save `market` block as structured market state when present, including resource-index mapping, selected-country market activity, and compact galaxy-wide activity totals.
- Optionally load English Stellaris localisation `.yml` files and mark display fields as localized, generated fallback, unresolved, literal, or raw.
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
- Do not load Stellaris localisation files by default.
- Do not retain extracted `gamestate` by default.
- Reparse automatically when `settings.config` changes.

### Localization

Localization is optional and disabled by default. When disabled, the exporter keeps the previous generated-name fallback behavior: readable generated keys are cleaned into display guesses with `*_raw` plus `*_generated_from_key=true`, and hard placeholders keep `*_unresolved=true`.

Enable English localisation by pointing `localisation_root` at either the parent Stellaris `localisation` folder or the specific language folder:

```ini
[localization]
enabled=true
language="english"
localisation_root="C:\Program Files (x86)\Steam\steamapps\common\Stellaris\localisation"
include_mods=false
```

The current discovery path supports the modern game layout, including `localisation\english\*_l_english.yml`, `localisation\english\name_lists\*_l_english.yml`, and `localisation\english\random_names\*_l_english.yml`. It also works when `localisation_root` points directly at `localisation\english`. Both `localisation` and `localization` spellings are accepted for config and folder discovery.

Milestone 6A supports direct key lookup only. It does not yet perform full `$VARIABLE$` or `%TEMPLATE%` substitution, and it intentionally defers broad trait/civic/ethic/job/building/component localization to later localization milestones.

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

Top-level `fleets` contains military navy fleets only. Starbases, mining/research stations, science ships, constructors, civilian fleets, orbital stations, transport/army formations, and zero-power records without military markers are suppressed from that array. Transport and ground-force data belongs under `army_formations`, not navy fleets.

Military data is exported in three layers: `fleets[]` for fleet-level facts, `fleets[].ships[]` for individual vessel records, and top-level `ship_designs` for the design templates referenced by exported ships. `fleets[].ships[]` entries are objects with string IDs, not bare ship IDs; unresolved ship references are emitted as `{ "ship_id": "...", "resolved": false }` stubs. Fleet summaries include ship counts, resolved/unresolved counts, hull/armor/shield totals when present, ship size/class/design composition, and component rollups derived from referenced design templates.

`ship_designs` contains only designs referenced by exported ships. Design sections and component template tokens are included for dashboard inspection, but component/resource valuation is deliberately future-aware rather than hard-coded: `resource_cost.available=false` and fleet `estimated_resource_value.available=false` mean a component cost catalog has not been loaded yet.

Defense armies are not part of the export contract and are filtered before top-level, colony/local, formation, debug, or raw output paths. Suppressed defense armies only increment `validation.defense_armies_suppressed`. Non-defense armies are grouped under `army_formations` by `fleet_name` when present, otherwise by planet and army type.

Country finance data is exposed only under top-level `nat_finance_economy`. Its `budget` uses the current-month income, expenses, and balance breakdown; `net_monthly_resource` is derived by summing current-month balance categories by resource; and `stored_resources` is the only stockpile location. The old top-level `economy`, top-level `stored_resources`, `country.budget`, and `derived_summary.economy.stored_resources` locations are not emitted.

When the save contains a `market` block, the parser emits a top-level `market` object. This is a market-state export, not final Stellaris UI buy/sell price derivation. Market arrays are mapped by resource index where known: `energy`, `minerals`, `food`, `consumer_goods`, `alloys`, `volatile_motes`, `exotic_gases`, `rare_crystals`, `sr_living_metal`, `sr_zro`, and `sr_dark_matter`; unknown indices are preserved as `unknown_N`. `market.player_market_activity` is specific to the exported country JSON, while `market.all_country_market_activity_summary` compactly aggregates bought/sold/net totals across countries. `observed_buy_price` and `observed_sell_price` remain `null`, and `price_derivation_status.available=false`, until a reliable price formula or component/catalog input is implemented. Manually observed UI prices are deliberately not hard-coded.

`capital_planet` is a navigation stub with `planet_id`, `name`, `system_id`, and `system_name`; the full capital colony record lives in `colonies[]`. Colonies no longer embed a top-level `system` object; use `derived_summary.system_id` or `derived_summary.map.system_id` to join to top-level `systems`.

JSON ID/reference values are strings throughout the dashboard schema. Object keys under top-level `systems`, `species`, and `leaders` are ID keys. `coordinate.origin` is the explicit exception and remains numeric because it belongs to coordinate data and may use the `4294967295` sentinel.

Display-name localization is metadata-preserving. If a direct key is found in configured localisation files, the display field is replaced and gets `*_raw`, `*_localized=true`, and `*_localization_status="localized"`. If no key is found but the value is a readable generated key, the previous cleanup fallback is used with `*_raw`, `*_generated_from_key=true`, and `*_localization_status="generated_from_key"`. Hard unresolved placeholders such as `$...$`, `%...%`, and generic all-uppercase localisation keys keep `*_unresolved=true` with `*_localization_status="unresolved"`. Literal save text gets `*_localization_status="literal"`; other unchanged tokens are `raw`.

Each country JSON includes a top-level `localization` block with enabled/available status, language, entry and file counts when available, a small source-file sample, and field counters. Matching counters also appear under `validation`: `localized_field_count`, `generated_fallback_count`, `unresolved_localization_count`, `localization_entry_count`, `localization_file_count`, and `localization_warnings`.

Leader names also use the structured save-side `name.full_names` block when available. Literal leader names use the `full_names.key` directly. Generated templates such as `%LEADER_2%` are expanded from variables `1` and `2`, with species/name-list prefixes such as `LITHOID3_CHR_` stripped, underscores converted to spaces, and hyphenated casing normalized; the raw template is preserved as `name_raw` with `name_generated_from_key=true`. Leader profile output now preserves gender, job, ethic, tier, experience, creator, planet, location, and council location when present. `birth_date` comes from the save `date` field, `date_added` and `recruitment_date` are preserved, and `service_start_date` prefers `date_added` before `recruitment_date`. `age_years` and `service_length_years` are calculated from the save game date; save-side `age` is kept as `raw_age` but is not treated as authoritative.

`species_counts_by_name` is a convenience rollup, not a canonical species identity map. When a species name is unresolved, the species ID is appended to the key, for example `"$affix$$base$ [#317]"`, to prevent distinct unresolved species from merging into one count.

## Notes on game definitions

`parse_game_definitions=true` currently builds a lightweight definition-source index from selected `common/` folders. Milestone 6A localisation is separate and only applies to high-value display fields; definition tokens such as traits, jobs, buildings, and components are still primarily emitted as save tokens until later localization milestones.

## Important implementation notes

The parser stores a full generic AST in memory. That is convenient and robust for v1, but large late-game saves may use substantial RAM. A future v2 should replace this with selective streaming extraction once the dashboard schema stabilizes.

The manifest can still store FNV-1a 64-bit hashes when metadata-based skip is disabled. These hashes are for change detection, not security.
