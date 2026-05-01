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
- `fleets`: Fleet summaries owned by the country.
- `owned_armies`: Army summaries owned by the country.
- `species`: Referenced species objects, keyed by species ID.
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
- classification: `planet_class`, `planet_size`, `designation`, `final_designation`
- ownership/state: `owner`, `controller`, `stability`, `crime`
- capacity: `amenities`, `amenities_usage`, `free_amenities`, `total_housing`, `housing_usage`, `free_housing`
- pops: `num_sapient_pops`, `employable_pops`, `species_counts_by_id`, `dominant_species_id`, `dominant_species_name`, `species_count`
- economy: `production`, `upkeep`, `profit`
- local composition: `district_counts_by_type`, `building_counts_by_type`, `deposit_counts_by_type`
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

## `species` and `leaders`

Both are keyed objects where keys are IDs referenced from the selected country context:

- resolved entries include expanded details
- unresolved entries include `resolved=false`
- unresolved entries are mirrored in `warnings.unresolved_references`

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
- `colonies_with_owner_mismatch`
- `colonies_missing_derived_summary`: colony IDs that did not receive `colonies[].derived_summary`; normally empty for resolved colonies.


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
