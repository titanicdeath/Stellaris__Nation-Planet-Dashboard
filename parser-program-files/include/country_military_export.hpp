#pragma once

#include "country_export_helpers.hpp"

bool is_dashboard_military_fleet(const PdxValue* fleet, const SaveIndexes& ix);
void write_fleet(JsonWriter& j,
                 const std::string& fleet_id,
                 const PdxValue* fleet,
                 const SaveIndexes& ix,
                 const Settings& st,
                 const DefinitionIndex* defs,
                 const std::string& game_date,
                 std::set<std::string>& referenced_leaders,
                 NameDiagnostics* diagnostics,
                 std::vector<UnresolvedReference>* unresolved_refs,
                 MilitaryRollup& total_rollup);
void write_ship_designs(JsonWriter& j,
                        const MilitaryRollup& military_rollup,
                        const SaveIndexes& ix,
                        const Settings& st,
                        NameDiagnostics* diagnostics);
ArmyExportContext build_army_export_context(const PdxValue* country, const SaveIndexes& ix);
void write_army_formations(JsonWriter& j, const ArmyExportContext& army_context, NameDiagnostics* diagnostics = nullptr);
