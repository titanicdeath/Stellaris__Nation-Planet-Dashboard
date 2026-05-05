#pragma once

#include "country_export_helpers.hpp"

MapExportContext build_map_export_context(const std::vector<std::string>& owned_planets,
                                          const std::vector<std::string>& controlled_planets,
                                          const std::vector<std::string>& owned_fleets,
                                          const std::string& capital_id,
                                          const SaveIndexes& ix);
void write_systems_block(JsonWriter& j, const MapExportContext& map_ctx, const SaveIndexes& ix, const Settings& st, NameDiagnostics* diagnostics = nullptr);
size_t colony_system_count(const MapExportContext& map_ctx);
void write_map_summary(JsonWriter& j, const MapExportContext& map_ctx);
