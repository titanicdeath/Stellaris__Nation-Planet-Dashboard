#include "localization.hpp"
#include "json_writer.hpp"
#include "utils.hpp"

std::optional<std::string> LocalizationDb::lookup(const std::string& key) const {
    if (!available) return std::nullopt;
    auto it = entries.find(key);
    if (it == entries.end()) return std::nullopt;
    return it->second;
}

bool path_exists_directory(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_directory(p, ec);
}

std::string lower_filename(const fs::path& p) {
    return lower_copy(p.filename().string());
}

bool localization_filename_matches(const fs::path& p, const std::string& language) {
    const std::string name = lower_filename(p);
    const std::string suffix = "_l_" + lower_copy(language) + ".yml";
    return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void add_unique_path(std::vector<fs::path>& paths, const fs::path& p) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(p, ec);
    if (ec) normalized = fs::absolute(p, ec);
    if (ec) normalized = p;
    for (const fs::path& existing : paths) {
        if (existing == normalized) return;
    }
    paths.push_back(normalized);
}

std::vector<fs::path> candidate_localization_roots(const fs::path& root, const std::string& language) {
    std::vector<fs::path> out;
    if (!path_exists_directory(root)) return out;

    const std::string root_name = lower_copy(root.filename().string());
    if (root_name == lower_copy(language)) add_unique_path(out, root);

    add_unique_path(out, root / language);
    add_unique_path(out, root / "localisation" / language);
    add_unique_path(out, root / "localization" / language);

    // Also support older/flattened layouts where *_l_english.yml files live directly
    // inside localisation/ rather than localisation/english/.
    add_unique_path(out, root);
    add_unique_path(out, root / "localisation");
    add_unique_path(out, root / "localization");
    return out;
}

std::vector<fs::path> discover_localization_files(const fs::path& root, const std::string& language, size_t* warning_count) {
    std::vector<fs::path> files;
    std::set<std::string> seen;
    const auto roots = candidate_localization_roots(root, language);
    for (const fs::path& candidate : roots) {
        if (!path_exists_directory(candidate)) continue;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(candidate, fs::directory_options::skip_permission_denied, ec), end;
             it != end;
             it.increment(ec)) {
            if (ec) {
                if (warning_count) (*warning_count)++;
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            if (!localization_filename_matches(it->path(), language)) continue;
            const std::string key = fs::absolute(it->path()).string();
            if (seen.insert(key).second) files.push_back(it->path());
        }
        if (ec && warning_count) (*warning_count)++;
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string strip_utf8_bom(std::string text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

std::optional<std::pair<std::string, std::string>> parse_localization_line(const std::string& line) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') return std::nullopt;
    if (starts_with_ci(t, "l_") && t.back() == ':' && t.find(' ') == std::string::npos) return std::nullopt;

    const size_t colon = t.find(':');
    if (colon == std::string::npos) return std::nullopt;
    std::string key = trim(t.substr(0, colon));
    if (key.empty() || key[0] == '#') return std::nullopt;

    std::string rest = trim(t.substr(colon + 1));
    while (!rest.empty() && std::isdigit(static_cast<unsigned char>(rest.front()))) rest.erase(rest.begin());
    rest = trim(rest);
    if (rest.empty() || rest.front() != '"') return std::nullopt;

    std::string value;
    bool escape = false;
    for (size_t i = 1; i < rest.size(); ++i) {
        const char c = rest[i];
        if (escape) {
            if (c == '"' || c == '\\') value.push_back(c);
            else {
                value.push_back('\\');
                value.push_back(c);
            }
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') return std::make_pair(key, value);
        value.push_back(c);
    }

    return std::nullopt;
}

void parse_localization_file(const fs::path& file, LocalizationDb& db) {
    std::string content;
    try {
        content = strip_utf8_bom(read_text_file(file));
    } catch (...) {
        db.warning_count++;
        return;
    }

    std::istringstream in(content);
    std::string line;
    while (std::getline(in, line)) {
        auto parsed = parse_localization_line(line);
        if (!parsed) continue;
        db.entries[parsed->first] = parsed->second;
    }
}

LocalizationDb load_localization_db(const Settings& st) {
    LocalizationDb db;
    db.enabled = st.localization_enabled;
    db.language = st.localization_language.empty() ? "english" : st.localization_language;

    if (!db.enabled) {
        db.available = false;
        db.reason = "disabled";
        return db;
    }

    if (st.localisation_root.empty() || !path_exists_directory(st.localisation_root)) {
        db.available = false;
        db.reason = "localisation_root not found";
        db.warning_count++;
        return db;
    }

    const std::vector<fs::path> files = discover_localization_files(st.localisation_root, db.language, &db.warning_count);
    if (files.empty()) {
        db.available = false;
        db.reason = "no localization files found";
        return db;
    }

    for (const fs::path& file : files) {
        db.source_files.push_back(file.string());
        parse_localization_file(file, db);
    }
    db.entry_count = db.entries.size();
    db.available = db.entry_count > 0;
    db.reason = db.available ? "" : "no localization entries parsed";
    return db;
}

LocalizedText localize_display_name(const std::string& raw_value,
                                    const std::string& context,
                                    const LocalizationDb* localization_db) {
    (void)context;
    LocalizedText out;
    out.raw = trim(raw_value);
    out.display = out.raw;

    if (out.raw.empty()) {
        out.status = "raw";
        return out;
    }

    if (localization_db && localization_db->available) {
        if (auto localized = localization_db->lookup(out.raw)) {
            out.display = *localized;
            out.status = is_hard_unresolved_name(out.display) ? "unresolved" : "localized";
            return out;
        }
    }

    if (is_hard_unresolved_name(out.raw)) {
        out.status = "unresolved";
        return out;
    }

    if (is_generated_name_key(out.raw)) {
        out.display = make_display_name_from_key(out.raw);
        out.status = "generated_from_key";
        return out;
    }

    out.status = (out.raw.find('_') == std::string::npos) ? "literal" : "raw";
    return out;
}

void write_localization_status_block(JsonWriter& j,
                                     const LocalizationDb* localization_db,
                                     size_t localized_field_count,
                                     size_t generated_fallback_count,
                                     size_t unresolved_field_count) {
    const LocalizationDb empty;
    const LocalizationDb& db = localization_db ? *localization_db : empty;
    j.begin_object();
    j.key("enabled"); j.value(db.enabled);
    j.key("available"); j.value(db.available);
    if (!db.reason.empty()) { j.key("reason"); j.value(db.reason); }
    j.key("language"); j.value(db.language);
    j.key("entry_count"); j.raw_number(std::to_string(db.entry_count));
    j.key("source_file_count"); j.raw_number(std::to_string(db.source_files.size()));
    if (db.available) {
        j.key("source_files_sample");
        j.begin_array();
        const size_t sample_count = std::min<size_t>(db.source_files.size(), 5);
        for (size_t i = 0; i < sample_count; ++i) {
            j.value(fs::path(db.source_files[i]).filename().string());
        }
        j.end_array();
    }
    j.key("localized_field_count"); j.raw_number(std::to_string(localized_field_count));
    j.key("generated_fallback_count"); j.raw_number(std::to_string(generated_fallback_count));
    j.key("unresolved_field_count"); j.raw_number(std::to_string(unresolved_field_count));
    j.end_object();
}
