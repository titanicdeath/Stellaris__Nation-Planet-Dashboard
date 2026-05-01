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
- optional governor details
- optional local source/raw sections (depending on config)

If a referenced planet cannot be resolved, a placeholder object with `resolved=false` is emitted and an unresolved warning entry is added.

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
