#pragma once

#include "country_export_helpers.hpp"

void write_planet(JsonWriter& j,
                  const std::string& planet_id,
                  const PdxValue* planet,
                  const SaveIndexes& ix,
                  const Settings& st,
                  const DefinitionIndex* defs,
                  const std::string& capital_id,
                  const ColonyDemographicRollup* colony_rollup,
                  std::set<std::string>& referenced_species,
                  std::set<std::string>& referenced_leaders,
                  const std::string& game_date,
                  NameDiagnostics* diagnostics = nullptr);
void write_capital_planet_stub(JsonWriter& j,
                               const std::string& capital_id,
                               const PdxValue* capital,
                               const MapExportContext& map_context,
                               const SaveIndexes& ix,
                               NameDiagnostics* diagnostics = nullptr);
