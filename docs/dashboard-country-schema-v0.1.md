# Dashboard Country Export Schema v0.1

This document describes the practical JSON contract emitted by the parser for one selected country.

## Top-level fields

- `schema_version`: Current schema label (`dashboard-country-v0.1`).
- `parser_version`: Parser build/version string.
- `save`: Save metadata (`file`, `game_date`, version/name when available).
- `country`: Selected country data and metrics.
- `capital_planet`: Expanded planet object for `country.capital`, or `null`.
- `colonies`: Expanded array of owned planets (`country.owned_planets`).
- `controlled_planet_ids`: Raw controlled planet IDs.
- `systems`: Selected-country relevant star system context, keyed by system ID.
- `map_summary`: Compact map/export summary for the selected-country system subset.
- `fleets`: Fleet summaries owned by the country.
- `owned_armies`: Army summaries owned by the country.
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
- optional blocks (`flag`, `ethics`, `government`, `budget`, etc.)

## `capital_planet` and `colonies`

`capital_planet` and each object in `colonies` include:

- identity (`planet_id`, `name`, ownership)
- economy/planet state fields where available
- building/district/deposit/pop summaries
- `derived_summary` for dashboard-ready colony facts and card display
- optional governor details
- optional local source/raw sections (depending on config)

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

- `job_counts_by_type`: Count of job records by exact save job `type`.
- `workforce_by_job_type`: Sum of non-negative `workforce` values by exact save job `type`.
- `pop_category_counts`: Sum of pop group `size` by exact `pop_groups[].key.category` strings.
- `pop_category_share`: Category share computed from `pop_category_counts`.
- `jobs_by_planet`: Per-planet job counts by exact job `type`.
- `pop_categories_by_planet`: Per-planet pop group sizes by exact category.

Negative job workforce values, such as `-1`, are treated as unavailable/special sentinels and are excluded from `workforce_by_job_type`. The parser does not invent strata; it uses the category strings present in the save, such as `ruler`, `specialist`, `worker`, `slave`, or `civilian`.

## `fleets` and `owned_armies`

Arrays include per-object summaries and selected raw fields.

- unresolved fleet/army IDs emit placeholder entries (`resolved=false`)
- unresolved fleet/army IDs are recorded in `warnings.unresolved_references`

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
  - `kind` (for example `planet`, `leader`, `species`, `fleet`, `army`)
  - `id`
  - `context` (source field path in export logic)

## `validation`

Current top-level validation fields include:

- `owned_planets_match_exported_colonies`
- `capital_in_colonies`
- `unresolved_reference_count`
- `warning_count`
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


## New in Milestone 3

- Added `derived_summary` with identity/economy/military/colonies/validation rollups.
- Added top-level `validation` block with unresolved/warning counts and consistency checks.
- Added optional timeline index export under `output/timeline/<country-id>-(<safe-name>).timeline.json`.
- Timeline snapshots include key metrics plus `output_json_path` pointing to the full per-save self-contained country snapshot.

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
