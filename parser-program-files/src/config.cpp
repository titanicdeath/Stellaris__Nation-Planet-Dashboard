#include "config.hpp"
#include "utils.hpp"

Settings load_settings(const fs::path& config_path) {
    Settings st;
    st.config_path = fs::absolute(config_path);
    st.project_root = st.config_path.parent_path();
    const std::string content = read_text_file(st.config_path);
    st.settings_hash = string_hash_fnv1a64(content);

    std::map<std::string, std::map<std::string, std::string>> ini;
    std::string section;
    std::istringstream in(content);
    std::string line;
    while (std::getline(in, line)) {
        // Strip comments only if # or ; starts the line after whitespace.
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = lower_copy(trim(t.substr(1, t.size() - 2)));
            continue;
        }
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower_copy(trim(t.substr(0, eq)));
        std::string val = trim(t.substr(eq + 1));
        ini[section][key] = val;
    }

    auto get = [&](const std::string& sec, const std::string& key, const std::string& def = "") -> std::string {
        auto si = ini.find(sec);
        if (si == ini.end()) return def;
        auto ki = si->second.find(key);
        if (ki == si->second.end()) return def;
        return ki->second;
    };
    auto getb = [&](const std::string& sec, const std::string& key, bool def) -> bool {
        return parse_bool(get(sec, key, def ? "true" : "false"), def);
    };

    st.stellaris_game_path = resolve_path(st.project_root, get("paths", "stellaris_game_path"));
    st.save_files_path = resolve_path(st.project_root, get("paths", "save_files_path", "save_files"));
    st.output_path = resolve_path(st.project_root, get("paths", "output_path", "output"));
    st.manifest_path = resolve_path(st.project_root, get("paths", "manifest_path", "output/manifest.json"));

    st.parse_all_save_files = getb("save_selection", "parse_all_save_files", true);
    st.specific_save_files = split_csv(get("save_selection", "specific_save_files"));
    st.ignore_autosaves = getb("save_selection", "ignore_autosaves", true);
    st.latest_save_only = getb("save_selection", "latest_save_only", false);
    st.latest_save_include_autosaves = getb("save_selection", "latest_save_include_autosaves", false);
    st.force_reparse = getb("save_selection", "force_reparse", false);
    st.reparse_when_settings_change = getb("save_selection", "reparse_when_settings_change", true);
    st.retain_extracted_gamestate = getb("save_selection", "retain_extracted_gamestate", false);
    st.retained_gamestate_path = resolve_path(st.project_root, get("save_selection", "retained_gamestate_path", "output/_extracted_gamestate"));

    st.manifest_enable_skip = getb("manifest", "enable_skip", true);
    st.manifest_skip_uses_file_metadata = getb("manifest", "skip_uses_file_metadata", true);
    st.manifest_skip_requires_output_files = getb("manifest", "skip_requires_output_files", true);
    st.manifest_force_reparse = getb("manifest", "force_reparse", false);
    if (st.manifest_force_reparse) st.force_reparse = true;

    st.parse_all_nations = getb("nation_selection", "parse_all_nations", false);
    st.player_only = getb("nation_selection", "player_only", true);
    st.nation_ids = split_csv(get("nation_selection", "nation_ids", "0"));
    st.nation_name_filter = get("nation_selection", "nation_name_filter");

    st.include_all_special_nations = getb("special_nations", "include_all_special_nations", false);
    st.include_fallen_empires = getb("special_nations", "include_fallen_empires", true);
    st.include_subjects = getb("special_nations", "include_subjects", true);
    st.include_primitives = getb("special_nations", "include_primitives", false);
    st.include_enclaves = getb("special_nations", "include_enclaves", false);
    st.include_crisis = getb("special_nations", "include_crisis", false);
    st.include_event_countries = getb("special_nations", "include_event_countries", false);

    st.parse_game_definitions = getb("game_definitions", "parse_game_definitions", false);
    st.include_definition_sources = getb("game_definitions", "include_definition_sources", true);

    st.pretty_json = getb("output", "pretty_json", true);
    st.include_raw_ids = getb("output", "include_raw_ids", true);
    st.include_debug_sections = getb("output", "include_debug_sections", true);
    st.include_source_locations = getb("output", "include_source_locations", true);
    st.include_raw_pdx_objects = getb("output", "include_raw_pdx_objects", false);
    st.export_timeline = getb("output", "export_timeline", true);

    st.print_performance_timings = getb("debug", "print_performance_timings", false);
    st.write_performance_log = getb("debug", "write_performance_log", false);

    return st;
}

// ================================================================
// Simple JSON writer
// ================================================================
