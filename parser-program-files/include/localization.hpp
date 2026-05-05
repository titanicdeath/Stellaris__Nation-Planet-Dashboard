#pragma once

#include "config.hpp"
#include "json_writer.hpp"

struct LocalizationDb {
    bool enabled = false;
    bool available = false;
    std::string language = "english";
    std::string reason = "disabled";
    size_t entry_count = 0;
    size_t warning_count = 0;
    std::vector<std::string> source_files;
    std::unordered_map<std::string, std::string> entries;

    std::optional<std::string> lookup(const std::string& key) const;
};

struct LocalizedText {
    std::string display;
    std::string raw;
    std::string status;
};

LocalizationDb load_localization_db(const Settings& st);
LocalizedText localize_display_name(const std::string& raw_value,
                                    const std::string& context,
                                    const LocalizationDb* localization_db);
void write_localization_status_block(JsonWriter& j,
                                     const LocalizationDb* localization_db,
                                     size_t localized_field_count,
                                     size_t generated_fallback_count,
                                     size_t unresolved_field_count);
