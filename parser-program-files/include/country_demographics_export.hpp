#pragma once

#include "country_export_helpers.hpp"

void write_resolved_species(JsonWriter& j,
                            const std::string& id,
                            const PdxValue* sp,
                            const Settings& st,
                            const DefinitionIndex* defs,
                            NameDiagnostics* diagnostics = nullptr);
EmpireRollups build_empire_rollups(const std::vector<std::string>& owned_planets, const SaveIndexes& ix);
void write_demographics(JsonWriter& j, const EmpireDemographicRollup& demographics, NameDiagnostics* diagnostics = nullptr);
void write_workforce_summary(JsonWriter& j, const EmpireWorkforceRollup& workforce);
