#include "manifest.hpp"
#include "json_writer.hpp"
#include "utils.hpp"

SaveFileMetadata get_save_file_metadata(const fs::path& save) {
    SaveFileMetadata meta;
    meta.absolute_path = fs::absolute(save).string();
    meta.file_name = save.filename().string();
    meta.file_size = fs::file_size(save);
    meta.last_write_time = std::to_string(fs::last_write_time(save).time_since_epoch().count());
    return meta;
}

std::string json_extract_string_field(const std::string& obj, const std::string& field) {
    const std::string needle = "\"" + field + "\"";
    size_t p = obj.find(needle);
    if (p == std::string::npos) return "";
    p = obj.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    p = obj.find('"', p + 1);
    if (p == std::string::npos) return "";
    ++p;
    std::string out;
    bool esc = false;
    for (; p < obj.size(); ++p) {
        char c = obj[p];
        if (esc) { out.push_back(c); esc = false; continue; }
        if (c == '\\') { esc = true; continue; }
        if (c == '"') break;
        out.push_back(c);
    }
    return out;
}

uintmax_t json_extract_uint_field(const std::string& obj, const std::string& field);
std::vector<std::string> json_extract_string_array_field(const std::string& obj, const std::string& field);

std::vector<ManifestEntry> load_manifest(const fs::path& p) {
    std::vector<ManifestEntry> out;
    if (!fs::exists(p)) return out;
    std::string s = read_text_file(p);
    size_t pos = 0;
    while (true) {
        size_t sp = s.find("\"save_path\"", pos);
        if (sp == std::string::npos) break;
        size_t obj_start = s.rfind('{', sp);
        size_t obj_end = s.find('}', sp);
        if (obj_start == std::string::npos || obj_end == std::string::npos) break;
        std::string obj = s.substr(obj_start, obj_end - obj_start + 1);
        ManifestEntry e;
        e.save_path = json_extract_string_field(obj, "save_path");
        e.save_file_name = json_extract_string_field(obj, "save_file_name");
        e.file_size = json_extract_uint_field(obj, "file_size");
        e.last_write_time = json_extract_string_field(obj, "last_write_time");
        e.save_hash = json_extract_string_field(obj, "save_hash");
        e.settings_hash = json_extract_string_field(obj, "settings_hash");
        e.parser_version = json_extract_string_field(obj, "parser_version");
        e.game_date = json_extract_string_field(obj, "game_date");
        e.parsed_at = json_extract_string_field(obj, "parsed_at");
        e.outputs = json_extract_string_array_field(obj, "outputs");
        if (!e.save_path.empty()) out.push_back(std::move(e));
        pos = obj_end + 1;
    }
    return out;
}

void save_manifest(const fs::path& p, const std::vector<ManifestEntry>& entries, bool pretty) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    JsonWriter j(out, pretty);
    j.begin_object();
    j.key("schema_version"); j.value("1.0");
    j.key("hash_algorithm"); j.value("fnv1a64");
    j.key("entries");
    j.begin_array();
    for (const auto& e : entries) {
        j.begin_object();
        j.key("save_path"); j.value(e.save_path);
        j.key("save_file_name"); j.value(e.save_file_name);
        j.key("file_size"); j.raw_number(std::to_string(e.file_size));
        j.key("last_write_time"); j.value(e.last_write_time);
        j.key("save_hash"); j.value(e.save_hash);
        j.key("settings_hash"); j.value(e.settings_hash);
        j.key("parser_version"); j.value(e.parser_version);
        j.key("game_date"); j.value(e.game_date);
        j.key("parsed_at"); j.value(e.parsed_at);
        j.key("outputs");
        j.begin_array();
        for (const auto& o : e.outputs) j.value(o);
        j.end_array();
        j.end_object();
    }
    j.end_array();
    j.end_object();
}

bool outputs_present_for_manifest_entry(const ManifestEntry& e) {
    if (e.outputs.empty()) return false;
    for (const auto& out : e.outputs) {
        if (out.empty() || !fs::exists(fs::path(out))) return false;
    }
    return true;
}

bool should_skip_from_manifest(const Settings& st,
                                      const std::vector<ManifestEntry>& manifest,
                                      const SaveFileMetadata& meta,
                                      std::string* reason) {
    auto set_reason = [&](const std::string& r) {
        if (reason) *reason = r;
    };
    if (!st.manifest_enable_skip) { set_reason("manifest skip disabled"); return false; }
    if (st.force_reparse) { set_reason("force_reparse enabled"); return false; }
    const std::string parser_version = STELLARIS_PARSER_VERSION;
    for (const auto& e : manifest) {
        if (e.save_path == meta.absolute_path) {
            if (st.manifest_skip_uses_file_metadata) {
                if (e.file_size == 0 || e.last_write_time.empty()) {
                    set_reason("manifest entry lacks file metadata");
                    return false;
                }
                if (e.file_size != meta.file_size || e.last_write_time != meta.last_write_time) {
                    set_reason("save file metadata changed");
                    return false;
                }
            } else {
                const std::string save_hash = file_hash_fnv1a64(fs::path(meta.absolute_path));
                if (e.save_hash.empty() || e.save_hash != save_hash) {
                    set_reason("save file hash changed");
                    return false;
                }
            }
            if (st.reparse_when_settings_change && e.settings_hash != st.settings_hash) {
                set_reason("settings hash changed");
                return false;
            }
            if (e.parser_version != parser_version) {
                set_reason("parser version changed");
                return false;
            }
            if (st.manifest_skip_requires_output_files && !outputs_present_for_manifest_entry(e)) {
                set_reason("manifest outputs missing");
                return false;
            }
            set_reason("unchanged manifest entry and outputs present");
            return true;
        }
    }
    set_reason("no matching manifest entry");
    return false;
}

// ================================================================
// Save context and emit helpers
