#pragma once

#include "config.hpp"

struct ManifestEntry {
    std::string save_path;
    std::string save_file_name;
    uintmax_t file_size = 0;
    std::string last_write_time;
    std::string save_hash;
    std::string settings_hash;
    std::string parser_version;
    std::string game_date;
    std::vector<std::string> outputs;
    std::string parsed_at;
};

struct SaveFileMetadata {
    std::string absolute_path;
    std::string file_name;
    uintmax_t file_size = 0;
    std::string last_write_time;
};

SaveFileMetadata get_save_file_metadata(const fs::path& save);
uintmax_t json_extract_uint_field(const std::string& obj, const std::string& field);
std::vector<std::string> json_extract_string_array_field(const std::string& obj, const std::string& field);
std::vector<ManifestEntry> load_manifest(const fs::path& p);
void save_manifest(const fs::path& p, const std::vector<ManifestEntry>& entries, bool pretty);
bool outputs_present_for_manifest_entry(const ManifestEntry& e);
bool should_skip_from_manifest(const Settings& st, const std::vector<ManifestEntry>& manifest, const SaveFileMetadata& meta, std::string* reason);
