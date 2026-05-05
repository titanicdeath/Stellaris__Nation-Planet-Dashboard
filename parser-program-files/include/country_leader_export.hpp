#pragma once

#include "country_export_helpers.hpp"

void write_resolved_leader(JsonWriter& j,
                           const std::string& id,
                           const PdxValue* leader,
                           const Settings& st,
                           const SaveIndexes& ix,
                           const DefinitionIndex* defs,
                           const std::string& game_date,
                           NameDiagnostics* diagnostics = nullptr,
                           LeaderExportStats* leader_stats = nullptr,
                           std::vector<UnresolvedReference>* warnings = nullptr);
