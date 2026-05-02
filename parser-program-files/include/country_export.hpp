#pragma once

#include "ast.hpp"
#include "config.hpp"
#include "game_indexes.hpp"
#include "timeline_export.hpp"

struct UnresolvedReference {
    std::string kind;
    std::string id;
    std::string context;
};

struct CountryExportSummary {
    std::string country_id;
    std::string country_name;
    std::string capital_id;
    std::string capital_name;
    size_t owned_planets = 0;
    size_t exported_colonies = 0;
    size_t systems_exported = 0;
    size_t unresolved_references = 0;
    size_t warnings = 0;
    fs::path output_file;
};

std::string get_country_name(const PdxValue* country);
std::vector<std::string> select_country_ids(const Settings& st, const SaveIndexes& ix);
std::pair<CountryExportSummary, TimelinePoint> write_country_output(const fs::path& out_path,
                                 const std::string& save_file_name,
                                 const std::string& game_date,
                                 const std::string& country_id,
                                 const PdxValue* country,
                                 const SaveIndexes& ix,
                                 const Settings& st,
                                 const DefinitionIndex* defs);
