#pragma once

#include "common.hpp"

struct Settings {
    fs::path config_path;
    fs::path project_root;

    fs::path stellaris_game_path;
    fs::path save_files_path;
    fs::path output_path;
    fs::path manifest_path;

    bool parse_all_save_files = true;
    std::vector<std::string> specific_save_files;
    bool ignore_autosaves = true;
    bool latest_save_only = false;
    bool latest_save_include_autosaves = false;
    bool force_reparse = false;
    bool reparse_when_settings_change = true;
    bool retain_extracted_gamestate = false;
    fs::path retained_gamestate_path;

    bool manifest_enable_skip = true;
    bool manifest_skip_uses_file_metadata = true;
    bool manifest_skip_requires_output_files = true;
    bool manifest_force_reparse = false;

    bool parse_all_nations = false;
    bool player_only = true;
    std::vector<std::string> nation_ids;
    std::string nation_name_filter;

    bool include_all_special_nations = false;
    bool include_fallen_empires = true;
    bool include_subjects = true;
    bool include_primitives = false;
    bool include_enclaves = false;
    bool include_crisis = false;
    bool include_event_countries = false;

    bool parse_game_definitions = false;
    bool include_definition_sources = true;

    bool pretty_json = true;
    bool include_raw_ids = true;
    bool include_debug_sections = true;
    bool include_source_locations = true;
    bool include_raw_pdx_objects = false;
    bool export_timeline = true;

    bool print_performance_timings = false;
    bool write_performance_log = false;

    std::string settings_hash;
};

Settings load_settings(const fs::path& config_path);
