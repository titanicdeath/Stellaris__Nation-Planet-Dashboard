# Dashboard Country Export Schema v0.1

This document describes the practical JSON contract emitted by the parser for one selected country.

Per-country files are written under `output/<game-date>/` and include the save date in the filename:

```txt
output/2220.12.16/0-(Tetra)_2220-12-16.json
```

The folder keeps the dotted save date. The filename suffix uses `_YYYY-MM-DD`.

## Top-level fields

- `schema_version`: Current schema label (`dashboard-country-v0.1`).
- `parser_version`: Parser build/version string.
- `save`: Save metadata (`file`, `game_date`, version/name when available).
- `country`: Selected country data and metrics.
- `nat_finance_economy`: The only top-level finance and stockpile block.
- `capital_planet`: Navigation stub for `country.capital`, or `null`.
- `colonies`: Expanded array of owned planets (`country.owned_planets`).
- `controlled_planet_ids`: Raw controlled planet IDs.
- `systems`: Selected-country relevant star system context, keyed by system ID.
- `map_summary`: Compact map/export summary for the selected-country system subset.
- `fleets`: Fleet summaries owned by the country.
- `owned_armies`: Top-level non-defense army placeholders/legacy list. Defense armies are not part of the export contract.
- `army_formations`: Grouped non-defense army formation summaries.
- `species`: Referenced species objects, keyed by species ID.
- `demographics`: Empire-level census rollup derived from owned colonies.
- `workforce_summary`: Empire-level job, workforce, and pop category rollups.
- `leaders`: Referenced leader objects, keyed by leader ID.
- `references`: Raw ID/reference support block for dashboard joins.
- `warnings`: Structured warning payloads.
- `debug`: Optional parser diagnostics when debug sections are enabled.

## `save`

Contains metadata captured from the save root:

- `file`
- `game_date`
- `version`
- `save_name`

## `country`

Contains country identity plus selected raw/evaluated fields. Common fields include:

- `country_id`, `name`, `adjective`
- `type`, `personality`, `capital`, `starting_system`
- power/score/economy/naval/pop metrics
- `founder_species_ref`, `built_species_ref`
- optional blocks (`flag`, `ethics`, `government`, etc.)

Full localisation from Stellaris `localisation/*.yml` files is not implemented yet. Display fields may still contain save-side localisation keys or templates. When a display value looks unresolved, the exporter keeps the raw value and adds a sibling marker such as `name_unresolved=true` or `adjective_unresolved=true`.

## `nat_finance_economy`

`nat_finance_economy` is the only emitted country finance/stockpile block. It contains:

- `budget.income`: `country.budget.current_month.income`
- `budget.expenses`: `country.budget.current_month.expenses`
- `budget.balance`: `country.budget.current_month.balance`
- `net_monthly_resource`: Resource totals derived by summing all current-month `budget.balance` source categories by resource. Negative values stay negative; effectively zero totals are omitted.
- `stored_resources`: Current stockpiled resources from the country standard economy module resources path.

`stored_resources` appears only at `nat_finance_economy.stored_resources`. The old top-level `stored_resources`, top-level `economy`, `country.budget`, and `derived_summary.economy.stored_resources` locations are not emitted.

## `capital_planet` and `colonies`

`capital_planet` is a compact navigation stub:

- `planet_id`
- `name`
- optional `name_unresolved`
- `system_id`
- `system_name`
- optional `system_name_unresolved`

The full capital colony data lives in `colonies[]`; consumers should join by `planet_id`.

Each object in `colonies` includes:

- identity (`planet_id`, `name`, ownership)
- economy/planet state fields where available
- building/district/deposit/pop summaries
- `derived_summary` for dashboard-ready colony facts and card display
- optional governor details
- optional local source sections (depending on config)

Colonies do not embed a top-level `system` object. Use `colonies[].derived_summary.system_id`, `colonies[].derived_summary.system_name`, or `colonies[].derived_summary.map`, then join to top-level `systems[system_id]` for full system data.

If a referenced planet cannot be resolved, a placeholder object with `resolved=false` is emitted and an unresolved warning entry is added.

### Colony `derived_summary`

Each resolved colony object includes `derived_summary` without removing or rewriting the detailed/raw colony sections. Fields are emitted when the source save data or resolved indexes make them available:

- identity/location: `planet_id`, `planet_name`, `system_id`, `system_name`
- optional compact `map` object with `system_id` and `system_name`
- classification: `planet_class`, `planet_size`, `designation`, `final_designation`
- ownership/state: `owner`, `controller`, `stability`, `crime`
- capacity: `amenities`, `amenities_usage`, `free_amenities`, `total_housing`, `housing_usage`, `free_housing`
- pops: `num_sapient_pops`, `employable_pops`, `species_counts_by_id`, `dominant_species_id`, `dominant_species_name`, `species_count`
- economy: `production`, `upkeep`, `profit`
- local composition: `district_counts_by_type`, `building_counts_by_type`, `deposit_counts_by_type`
- demographics/workforce: `pop_category_counts`, `job_counts_by_type`, `workforce_by_job_type`, `species_counts_by_name`
- diagnostics: `warning_count`

`planet_name` and `system_name` may include `planet_name_unresolved` or `system_name_unresolved` when the value is a hard localisation placeholder. Readable generated keys are cleaned into fallback names and keep the original value in `planet_name_raw` or `system_name_raw` with `planet_name_generated_from_key=true` or `system_name_generated_from_key=true`. `species_counts_by_name` is still a convenience map for dashboards, but hard unresolved species names are disambiguated with the species ID, for example `"$affix$$base$ [#317]"`, so distinct species are not silently merged under the same unresolved template.

`derived_summary.presentation_card` is a stable compact display object for dashboard cards:

- `title`: planet name when available, otherwise planet ID.
- `subtitle`: designation, planet class, and size joined as display text when available.
- `system`: resolved system name, or an empty string when unresolved.
- `role`: inferred colony role.
- `primary_metric_label`: currently `Pops`.
- `primary_metric_value`: `num_sapient_pops` when available, otherwise `null`.
- `secondary_metrics`: stable object with `stability`, `crime`, `free_housing`, and `free_amenities`, using `null` for unavailable values.

Role inference is deterministic and conservative. Capital planets, including `col_capital`, are `Capital`. Otherwise the parser scores obvious production keys and district/building type tokens for forge/alloy, factory/consumer goods, mining/minerals, generator/energy, research, unity, and trade signals. A single strongest signal maps to `Forge`, `Factory`, `Mining`, `Generator`, `Research`, `Unity`, or `Trade`; ties or missing signals map to `Mixed`.

## `systems` and `map_summary`

`systems` is a top-level object keyed by galactic object/system ID. It is selected-country relevant context for the current country snapshot, not a full galaxy export.

Current system selection is conservative:

- every resolved system containing one or more owned colony planets
- the country capital planet system when resolvable
- systems for `controlled_planet_ids` when their planets resolve to `coordinate.origin`
- systems for owned fleet `coordinate.origin` when present

Each `systems.<system_id>` entry emits:

- `system_id`, `name`, `coordinate`, `star_class`, `type`, `sector` when available
- `planet_ids` from the galactic object
- `colony_planet_ids`, `owned_planet_ids`, and `controlled_planet_ids` relevant to the selected country
- `starbase_ids` from the system object when available
- `hyperlanes` from the system object, or an empty array
- `is_colony_system`, `has_capital`, and `capital_planet_id` when applicable
- optional `name_unresolved` when the system name is still a localisation placeholder

`map_summary` is a compact dashboard convenience object:

- `system_count`
- `colony_system_count`
- `capital_system_id`
- `capital_system_name`
- `hyperlane_edge_count`
- `systems_missing_coordinates`
- `colonies_missing_systems`

Political borders, full-galaxy ownership boundaries, claimed systems without colonies, and complete war/diplomatic map overlays are not inferred in this milestone. Hyperlanes may point to systems outside the selected-country subset; those targets are diagnostic warnings rather than export failures.

## `species` and `leaders`

Both are keyed objects where keys are IDs referenced from the selected country context:

- resolved entries include expanded details
- unresolved entries include `resolved=false`
- unresolved entries are mirrored in `warnings.unresolved_references`
- resolved display names may include `name_unresolved=true`; species adjectives may include `adjective_unresolved=true`

## Unresolved Localisation Diagnostics

This milestone does not resolve full Stellaris localisation. Instead, it separates hard unresolved placeholders from readable generated keys.

Hard unresolved placeholders include:

- values containing `$`, such as `$affix$$base$`
- values containing `%`, such as `%ADJECTIVE%`
- generic all-uppercase localisation keys without a useful suffix, such as `PLANET_NAME_FORMAT` or `HABITAT_PLANET_NAME`

For hard unresolved placeholders, the original value is preserved and a sibling marker is emitted:

- `name` -> `name_unresolved`
- `adjective` -> `adjective_unresolved`
- `planet_name` -> `planet_name_unresolved`
- `system_name` -> `system_name_unresolved`
- `formation_name` -> `formation_name_unresolved`

Readable generated keys such as `LITHOID3_PLANET_Lonntoch`, `SPEC_Magonid_planet`, or `Rixikars_Maw` are cleaned into fallback display names. The original value is emitted as `*_raw`, and `*_generated_from_key=true` marks that cleanup was applied. For example, `Rixikars_Maw` becomes `name="Rixikars Maw"` with `name_raw="Rixikars_Maw"` and `name_generated_from_key=true`.

These markers are diagnostics only. They are not final localisation and should not be treated as translated display text.

## ID and Reference Types

JSON ID/reference values are strings everywhere in the dashboard schema. This includes fields ending in `_id` or `_ids`, object keys under top-level `systems`, `species`, and `leaders`, and known references such as `owner`, `controller`, `original_owner`, `capital`, `starting_system`, `home_planet`, `species`, `country`, `heir`, `council_positions`, `subjects`, `planet`, `spawning_planet`, `pop_faction`, and `sector`.

`coordinate.origin` is the explicit exception. It remains numeric because it belongs to the coordinate triple and can use the unsigned sentinel `4294967295` for galactic-root/no-parent coordinates.

## `demographics`

`demographics` is a top-level empire census rollup built from resolved owned colonies. It is intended for dashboard display and does not replace the raw/resolved colony and species sections.

Current fields:

- `total_sapient_pops`: Sum of species population counts across resolved owned colonies.
- `species_count`: Number of distinct species IDs found in the colony census.
- `dominant_species_id` and `dominant_species_name`: Species with the largest aggregated population.
- `species`: Array of species rollups. Each entry includes `species_id`, resolved `name`, `plural`, `class`, `portrait`, `traits`, `total_pops`, `empire_share`, and `planet_distribution`.
- `species[].planet_distribution`: Per-planet counts with `planet_id`, `planet_name`, and `pops`.
- `species[].enslaved_pops`: Emitted only when the save exposes an easy colony-level `num_enslaved` count in `species_information`.

Species populations use resolved `pop_groups` sizes keyed by species when pop groups are available, excluding `pre_sapient` categories from the sapient census. If pop groups are unavailable for a colony, the parser falls back to `species_information[*].num_pops`. Species identity fields are resolved through `species_db`; unresolved IDs are kept in the rollup with empty identity fields and listed in `validation.species_without_resolution`.

Legal status, citizenship, and slavery rights are not inferred. The parser only emits `enslaved_pops` when a direct `num_enslaved` value is present in the save data already being exported.

## `workforce_summary`

`workforce_summary` is a top-level empire workforce rollup built from resolved owned colonies, pop groups, and jobs.

Current fields:

- `job_counts_by_type`: Count of active/dashboard-relevant job records by exact save job `type`. This is intentionally filtered.
- `active_job_counts_by_type`: Explicit alias for the filtered active job counts.
- `workforce_by_job_type`: Sum of non-negative `workforce` values by exact save job `type`.
- `pop_category_counts`: Sum of pop group `size` by exact `pop_groups[].key.category` strings.
- `pop_category_share`: Category share computed from `pop_category_counts`.
- `jobs_by_planet`: Per-planet active job counts by exact job `type`. This is intentionally filtered.
- `active_jobs_by_planet`: Explicit alias for filtered per-planet active job counts.
- `pop_categories_by_planet`: Per-planet pop group sizes by exact category.

Inactive/sentinel jobs are not part of the export contract. Their records and names are not emitted in detailed colony jobs, active job maps, derived summaries, workforce summaries, debug sections, or raw escape hatches. Suppressed jobs only affect `validation.inactive_job_records_suppressed`, a numeric total. Exportable jobs must have a type plus meaningful workforce or positive assigned pop group amounts. Legitimate `civilian` jobs are allowed when they are real occupied jobs.

## `fleets`, `ship_designs`, and `owned_armies`

`fleets` contains military navy fleets only. Fleet-like records are suppressed when they are marked `station=yes`, `orbital_station=yes`, or `civilian=yes`, when their direct or resolved ship class is `shipclass_starbase`, `shipclass_mining_station`, `shipclass_research_station`, `shipclass_science_ship`, `shipclass_constructor`, `shipclass_colonizer`, or a transport class, or when they have zero military power and no clear military fleet markers. Starbase and system context remains available under `systems` and map summaries. Transport and ground-force data belongs under `army_formations`, not navy fleets.

The navy model has three layers:

- `fleets[]`: Fleet-level dashboard facts such as `fleet_id`, `name`, optional `fleet_template_id`, `ship_class`, `military_power`, `diplomacy_weight`, `hit_points`, `weapon`, `mobile`, `valid_for_combat`, `ground_support_stance`, `ships`, and `summary`.
- `fleets[].ships[]`: Individual ship objects. These entries are never bare IDs. Resolved ships can include `ship_id`, `fleet_id`, `name`, `name_raw`, `name_generated_from_key`, `ship_class`, `ship_size`, `design_id`, `upgrade_design_id`, `graphical_culture`, `experience`, `leader_id`, `construction_date`, `hull`/`max_hull`, `armor`/`max_armor`, `shields`/`max_shields`, and slim `sections[].weapons[]` data.
- `ship_designs`: A top-level object keyed by design ID. It contains only designs referenced by exported ships, with `design_id`, generated-name diagnostics, `graphical_culture`, `ship_size`, design `sections[].components[]`, `required_components`, and `resource_cost`.

Unresolved ship references become stubs:

```json
{ "ship_id": "123", "resolved": false }
```

Those stubs increment `validation.unresolved_ship_references` and add a warning with `kind="unresolved_ship_reference"`. Unresolved design IDs increment `validation.unresolved_design_references`; when referenced, they are represented as unresolved design entries in `ship_designs`.

Fleet `summary` contains dashboard rollups derived from exported ship objects and referenced designs:

- `ship_count`, `resolved_ship_count`, `unresolved_ship_count`
- `ship_sizes`, `ship_classes`, `design_counts`
- `component_counts`, `weapon_component_counts`, `utility_component_counts`
- optional health totals such as `total_hull`, `total_max_hull`, `total_armor`, `total_max_armor`, `total_shields`, and `total_max_shields`
- optional `average_experience`
- `estimated_resource_value.available=false` until component cost catalog support exists

This is slim dashboard military data, not a tactical combat export. Runtime movement/combat fields such as coordinates, rotations, targets, orders, paths, weapon cooldowns, and targeting state are not part of the fleet/ship/design contract.

Defense armies are not part of the country JSON export contract. They are filtered before top-level army output, colony/local army lists, formations, and debug sections. Suppressed defense armies only affect `validation.defense_armies_suppressed`, a numeric total. Dashboard consumers should use `army_formations` for non-defense armies.

`army_formations` groups non-defense armies by `fleet_name` when present, otherwise by planet and army type. Each formation includes:

- `formation_name`
- `owner`
- `planet`
- `army_count`
- `composition_by_type`
- `composition_by_species`
- `total_health` / `average_health`
- `total_morale` / `average_morale`
- optional `average_experience`
- `army_ids`

- unresolved fleet/army IDs emit placeholder entries (`resolved=false`)
- unresolved fleet/army IDs are recorded in `warnings.unresolved_references`

## `stored_resources`

`nat_finance_economy.stored_resources` is the source of truth for current country stockpiles. It is copied from `country.standard_economy_module.resources`, or from the live-save module path `country.modules.standard_economy_module.resources`, and may include resources such as energy, minerals, food, research, influence, unity, trade value, consumer goods, alloys, strategic resources, and minor artifacts when present in the save.

## `references`

Current reference helpers:

- `raw_country_id`
- `raw_capital_planet_id`
- `referenced_species_ids`
- `referenced_leader_ids`

This block is intentionally stable and dashboard-friendly for follow-up joins.

## `warnings`

Current warning payload:

- `unresolved_references`: array of objects with:
  - `kind` (for example `planet`, `leader`, `species`, `fleet`, `army`, `unresolved_ship_reference`, `unresolved_design_reference`, or `unresolved_name`)
  - `id`
  - `context` (source field path in export logic)
  - optional `value` for unresolved display-name diagnostics

Generated-key fallback cleanup does not emit `kind="unresolved_name"` warnings. Hard unresolved placeholders do.

## `validation`

Current top-level validation fields include:

- `owned_planets_match_exported_colonies`
- `capital_in_colonies`
- `unresolved_reference_count`
- `warning_count`
- `unresolved_name_count`: Total unresolved display-name fields detected in this country export.
- `unresolved_name_kinds`: Counts grouped by broad display kind, such as `planet`, `species`, `country`, `country_adjective`, `leader`, `fleet`, `army_formation`, `system`, and `capital_planet`.
- `generated_name_key_count`: Total readable generated-key display fields cleaned into fallback names.
- `generated_name_key_kinds`: Counts grouped by the same broad display kinds for cleaned generated keys.
- `colonies_missing_systems`
- `systems_exported_count`
- `colony_systems_exported_count`
- `systems_missing_coordinates`
- `colonies_with_unexported_systems`
- `hyperlane_targets_missing_from_export`: hyperlane target system IDs referenced by exported systems but not included in this selected-country subset.
- `colonies_with_owner_mismatch`
- `colonies_missing_derived_summary`: colony IDs that did not receive `colonies[].derived_summary`; normally empty for resolved colonies.
- `demographics_species_count`
- `demographics_total_pops`
- `demographics_matches_country_pop_count`: compares the aggregated demographics total against `country.num_sapient_pops` when available, allowing a tolerance of the larger of 1 pop or 0.1% of the country value.
- `colonies_missing_demographic_summary`: colony IDs that did not receive the compact demographic/workforce summary; normally empty for resolved colonies.
- `species_without_resolution`: species IDs present in colony census data but missing from `species_db`.
- `inactive_job_records_suppressed`: Numeric total of inactive/sentinel pop job records filtered before export.
- `non_military_fleet_records_suppressed`: Owned fleet-like records excluded from top-level military `fleets`.
- `fleet_count`: Number of exported military fleets.
- `ship_count`: Sum of exported `fleets[].ships[]` entries.
- `resolved_ship_count`: Number of ship entries resolved through the ship index.
- `unresolved_ship_count`: Number of unresolved ship stubs.
- `unresolved_ship_references`: Number of fleet ship IDs missing from the ship index.
- `ship_design_count`: Number of referenced design IDs emitted under `ship_designs`.
- `resolved_design_count`: Number of referenced design IDs resolved through the design index.
- `unresolved_design_references`: Number of ship design IDs missing from the design index.
- `resource_value_available`: Whether component-cost/resource valuation is available; currently `false` unless future component catalog support is added.
- `defense_armies_suppressed`: Numeric total of defense armies filtered before export.
- `army_formations_count`: Number of grouped non-defense army formations exported.
- `has_stored_resources`: Whether `nat_finance_economy.stored_resources` contains at least one named resource.
- `stored_resource_count`: Number of named entries under `nat_finance_economy.stored_resources`.


## New in Milestone 3

- Added `derived_summary` with identity/economy/military/colonies/validation rollups.
- Added top-level `validation` block with unresolved/warning counts and consistency checks.
- Added optional timeline index export under `output/timeline/<country-id>-(<safe-name>).timeline.json`.
- Timeline snapshots include key metrics plus `output_json_path` pointing to the full per-save self-contained country snapshot.

## New in Milestone 3C

- Made inactive/sentinel pop job records non-exportable; only numeric suppression totals remain.
- Changed top-level `fleets` to military fleets only, with suppressed non-military fleet counts in validation.
- Made defense army records non-exportable and added grouped `army_formations` for non-defense armies.
- Added country stockpile export from the country standard economy module resources path.

## New in Milestone 3B-1

- Added per-colony `derived_summary` objects for resolved `colonies[]` entries.
- Added per-colony `derived_summary.presentation_card` for compact dashboard card rendering.
- Added simple colony role inference for `Capital`, `Forge`, `Factory`, `Mining`, `Generator`, `Research`, `Unity`, `Trade`, and `Mixed`.
- Added `validation.colonies_missing_derived_summary`.

## New in Milestone 3B-2

- Added top-level `demographics` with empire species census, dominant species, species resolution, and planet distribution.
- Added top-level `workforce_summary` with job counts, non-negative workforce totals, pop category totals/shares, and per-planet rollups.
- Added compact colony rollups inside `colonies[].derived_summary`: `pop_category_counts`, `job_counts_by_type`, `workforce_by_job_type`, and `species_counts_by_name`.
- Added validation fields for demographics totals, missing colony demographic summaries, and unresolved census species.
- Documented current limits around legal status, slavery rights, and exact stratum inference.

## New in Milestone 3B-3

- Added top-level `systems` keyed by selected-country relevant system ID.
- Added top-level `map_summary` and compact `derived_summary.map` rollup.
- Linked colony `derived_summary.system_id`/`system_name` to the top-level system subset without duplicating full system objects inside colonies.
- Added validation fields for exported system counts, missing coordinates, missing colony systems, colonies whose resolved systems were not exported, and hyperlane targets outside the selected subset.
- Documented current limits around full galaxy export, political borders, and hyperlane target coverage.

## PR 1 Core Schema Cleanup

- Per-country filenames now include `_YYYY-MM-DD` before `.json`.
- Added `nat_finance_economy` as the only finance/stockpile block, with current-month budget, derived `net_monthly_resource`, and `stored_resources`.
- Removed duplicate stockpile locations: top-level `stored_resources`, top-level `economy`, `country.budget`, and `derived_summary.economy.stored_resources`.
- Changed `capital_planet` from a full colony copy to a navigation stub.
- Removed top-level `colonies[].system`; full system data lives in top-level `systems`.
- Standardized JSON ID/reference values as strings, except numeric `coordinate.origin`.

## PR 2 Name / Unresolved Localization Diagnostics

- Added hard unresolved display-name detection for `$` templates, `%` placeholders, and generic all-uppercase localisation keys.
- Added `*_unresolved=true` sibling markers for hard unresolved display fields such as names, adjectives, planet names, system names, and army formation names.
- Added readable generated-key fallback cleanup with `*_raw` and `*_generated_from_key=true`.
- Disambiguated unresolved `species_counts_by_name` keys with species IDs to avoid merging distinct species that share the same unresolved template.
- Added `validation.unresolved_name_count`, `validation.unresolved_name_kinds`, `validation.generated_name_key_count`, and `validation.generated_name_key_kinds`.
- Added unresolved display-name entries to `warnings.unresolved_references` with `kind="unresolved_name"` and a compact `value`.

## PR 3 Ship and Fleet Expansion

- Added ship and ship-design indexing for military dashboard export.
- Changed `fleets[].ships[]` from bare IDs to slim ship objects, with unresolved ship stubs when lookup fails.
- Added fleet-level ship, health, design, and component rollups under `fleets[].summary`.
- Added top-level `ship_designs` containing only designs referenced by exported ships.
- Added top-level military and validation rollups for fleet, ship, design, and resource-value availability counts.
- Kept non-military fleets, stations, civilian ships, transports, and defense armies suppressed from navy output.
