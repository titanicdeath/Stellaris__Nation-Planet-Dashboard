#include "country_export.hpp"
#include "json_writer.hpp"
#include "utils.hpp"

void json_optional_scalar(JsonWriter& j, const PdxValue* obj, const std::string& key) {
    j.key(key);
    if (const PdxValue* v = child(obj, key)) write_pdx_as_schema_json(j, v, key);
    else j.value(nullptr);
}

void write_id_name_object(JsonWriter& j, const std::string& id, const PdxValue* obj, const Settings& st, const std::string& block_name) {
    j.begin_object();
    j.key("id"); j.value(id);
    j.key("block"); j.value(block_name);
    const std::string name = localized_name(child(obj, "name"));
    if (is_generated_name_key(name)) {
        j.key("name_raw"); j.value(name);
        j.key("name"); j.value(make_display_name_from_key(name));
        j.key("name_generated_from_key"); j.value(true);
    } else {
        j.key("name"); j.value(name);
        if (is_hard_unresolved_name(name)) { j.key("name_unresolved"); j.value(true); }
    }
    if (st.include_source_locations) { j.key("source"); write_source(j, obj); }
    j.end_object();
}

std::string get_country_name(const PdxValue* country) {
    std::string n = localized_name(child(country, "name"));
    if (!n.empty()) return n;
    return scalar_or(child(country, "name"), "Unnamed Country");
}

std::vector<std::string> country_owned_fleet_ids(const PdxValue* country) {
    std::vector<std::string> ids;
    const PdxValue* owned = nested_child(country, {"fleets_manager", "owned_fleets"});
    if (!owned || owned->kind != PdxValue::Kind::Container) return ids;
    for (const auto& e : owned->entries) {
        if (e.value && e.value->kind == PdxValue::Kind::Container) {
            if (const PdxValue* f = child(e.value, "fleet")) {
                auto s = scalar(f);
                if (s) ids.push_back(*s);
            }
        }
    }
    return ids;
}

std::vector<std::string> scalar_id_list_from_child(const PdxValue* obj, const std::string& key) {
    return primitive_list(child(obj, key));
}

bool pdx_truthy(const PdxValue* v) {
    const std::string s = lower_copy(scalar_or(v));
    return s == "yes" || s == "true" || s == "1";
}

bool scalar_is_exact_number(const PdxValue* v, double expected) {
    auto d = scalar_double(v);
    return d && *d == expected;
}

double assigned_pop_group_amount_sum(const PdxValue* job) {
    const PdxValue* groups = child(job, "pop_groups");
    if (!groups || groups->kind != PdxValue::Kind::Container) return 0.0;
    double total = 0.0;
    if (groups->kind == PdxValue::Kind::Container) {
        for (const auto& e : groups->entries) {
            if (e.value && e.value->kind == PdxValue::Kind::Container) {
                const std::string nested = scalar_or(child(e.value, "pop_group"));
                if (!nested.empty() && nested != "4294967295") {
                    total += scalar_double(child(e.value, "amount")).value_or(0.0);
                }
            }
        }
    }
    return total;
}

bool is_known_sentinel_job_type(const std::string& type) {
    static const std::set<std::string> sentinel_types = {
        "crisis_purge",
        "bio_trophy_processing",
        "bio_trophy_unemployment",
        "bio_trophy_unprocessing",
        "neural_chip",
        "neural_chip_processing",
        "neural_chip_unprocessing",
        "purge_unprocessing",
        "slave_processing",
        "slave_unprocessing",
        "presapient_unprocessing",
        "event_purge",
    };
    return sentinel_types.find(type) != sentinel_types.end();
}

bool contains_banned_job_export_token(const std::string& value) {
    static const std::vector<std::string> banned_tokens = {
        "crisis_purge",
        "bio_trophy_processing",
        "bio_trophy_unemployment",
        "bio_trophy_unprocessing",
        "neural_chip",
        "neural_chip_processing",
        "neural_chip_unprocessing",
        "purge_unprocessing",
        "slave_processing",
        "slave_unprocessing",
        "presapient_unprocessing",
        "event_purge",
    };
    for (const std::string& token : banned_tokens) {
        if (value.find(token) != std::string::npos) return true;
    }
    return false;
}

bool all_workforce_fields_are_sentinel(const PdxValue* job) {
    for (const std::string& k : {"workforce", "max_workforce", "automated_workforce", "workforce_limit"}) {
        if (!scalar_is_exact_number(child(job, k), -1.0)) return false;
    }
    return true;
}

bool is_exportable_pop_job(const PdxValue* job) {
    if (!job) return false;
    const std::string type = scalar_or(child(job, "type"));
    if (type.empty()) return false;

    const double assigned_amount = assigned_pop_group_amount_sum(job);
    const double workforce = scalar_double(child(job, "workforce")).value_or(0.0);
    if (all_workforce_fields_are_sentinel(job)) return false;
    if (scalar_or(child(job, "pop_group")) == "4294967295" && assigned_amount <= 0.0) return false;
    if (workforce <= 0.0 && assigned_amount <= 0.0) return false;
    if (is_known_sentinel_job_type(type) && assigned_amount <= 0.0) return false;
    return true;
}

bool is_defense_army(const PdxValue* army);

struct NameDiagnostics {
    std::map<std::string, size_t> unresolved_kinds;
    std::map<std::string, size_t> generated_kinds;
    std::vector<UnresolvedReference>* warnings = nullptr;

    size_t unresolved_count() const {
        size_t total = 0;
        for (const auto& [_, n] : unresolved_kinds) total += n;
        return total;
    }

    size_t generated_count() const {
        size_t total = 0;
        for (const auto& [_, n] : generated_kinds) total += n;
        return total;
    }

    void add_unresolved(const std::string& kind, const std::string& id, const std::string& context, const std::string& value) {
        if (value.empty() || !is_hard_unresolved_name(value)) return;
        unresolved_kinds[kind]++;
        if (warnings) warnings->push_back(UnresolvedReference{"unresolved_name", id, context, value});
    }

    void add_generated(const std::string& kind, const std::string& value) {
        if (value.empty() || !is_generated_name_key(value)) return;
        generated_kinds[kind]++;
    }
};

struct LeaderExportStats {
    size_t leader_count = 0;
    size_t leaders_with_generated_names = 0;
    size_t leaders_missing_names = 0;
    size_t leaders_with_calculated_age = 0;
    size_t leaders_with_service_length = 0;
    size_t leader_date_parse_warnings = 0;
};

void write_display_text(JsonWriter& j,
                        const std::string& field,
                        const std::string& value,
                        const std::string& unresolved_field,
                        NameDiagnostics* diagnostics,
                        const std::string& kind,
                        const std::string& id,
                        const std::string& context) {
    if (is_hard_unresolved_name(value)) {
        j.key(field);
        j.value(value);
        j.key(unresolved_field);
        j.value(true);
        if (diagnostics) diagnostics->add_unresolved(kind, id, context, value);
        return;
    }

    if (is_generated_name_key(value)) {
        j.key(field + "_raw");
        j.value(value);
        j.key(field);
        j.value(make_display_name_from_key(value));
        j.key(field + "_generated_from_key");
        j.value(true);
        if (diagnostics) diagnostics->add_generated(kind, value);
        return;
    }

    j.key(field);
    j.value(value);
}

void write_name_text(JsonWriter& j,
                     const std::string& value,
                     NameDiagnostics* diagnostics,
                     const std::string& kind,
                     const std::string& id,
                     const std::string& context) {
    write_display_text(j, "name", value, "name_unresolved", diagnostics, kind, id, context);
}

void write_unresolved_name_kinds(JsonWriter& j, const NameDiagnostics& diagnostics) {
    j.begin_object();
    for (const auto& [kind, count] : diagnostics.unresolved_kinds) {
        j.key(kind);
        j.raw_number(std::to_string(count));
    }
    j.end_object();
}

void write_generated_name_key_kinds(JsonWriter& j, const NameDiagnostics& diagnostics) {
    j.begin_object();
    for (const auto& [kind, count] : diagnostics.generated_kinds) {
        j.key(kind);
        j.raw_number(std::to_string(count));
    }
    j.end_object();
}

bool is_leader_template_key(const std::string& value) {
    const std::string s = trim(value);
    return s.size() >= 10 && s.front() == '%' && s.back() == '%' && starts_with_ci(s.substr(1), "LEADER_");
}

std::map<std::string, std::string> leader_name_variables(const PdxValue* full_names) {
    std::map<std::string, std::string> out;
    const PdxValue* vars = child(full_names, "variables");
    if (!vars || vars->kind != PdxValue::Kind::Container) return out;

    for (const auto& e : vars->entries) {
        const PdxValue* var = e.value;
        if (!var || var->kind != PdxValue::Kind::Container) continue;

        std::string var_key = scalar_or(child(var, "key"));
        if (var_key.empty() && !e.key.empty()) var_key = e.key;
        if (var_key.empty()) continue;

        const PdxValue* value = child(var, "value");
        std::string raw_value = localized_name(value);
        if (raw_value.empty()) raw_value = scalar_or(value);
        if (!raw_value.empty()) out[var_key] = raw_value;
    }
    return out;
}

struct LeaderNameResult {
    std::string display;
    std::string raw;
    bool generated = false;
    bool unresolved = false;
};

LeaderNameResult resolve_leader_name(const PdxValue* leader) {
    LeaderNameResult result;
    const PdxValue* name = child(leader, "name");
    const PdxValue* full_names = child(name, "full_names");
    const std::string full_key = scalar_or(child(full_names, "key"));

    if (!full_key.empty() && pdx_truthy(child(full_names, "literal"))) {
        result.display = full_key;
        return result;
    }

    const std::map<std::string, std::string> variables = leader_name_variables(full_names);
    if (!full_key.empty() && is_leader_template_key(full_key) && !variables.empty()) {
        std::vector<std::string> parts;
        auto add_part = [&](const std::string& key) {
            auto it = variables.find(key);
            if (it == variables.end()) return;
            const std::string part = make_leader_name_part_from_key(it->second);
            if (!part.empty()) parts.push_back(part);
        };

        if (full_key == "%LEADER_2%") {
            add_part("1");
            add_part("2");
        } else if (full_key == "%LEADER_1%") {
            add_part("1");
        } else {
            for (const auto& [_, value] : variables) {
                const std::string part = make_leader_name_part_from_key(value);
                if (!part.empty()) parts.push_back(part);
            }
        }

        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) result.display += " ";
            result.display += parts[i];
        }
        if (!result.display.empty()) {
            result.raw = full_key;
            result.generated = true;
            return result;
        }
    }

    result.display = localized_name(name);
    if (result.display.empty()) result.display = full_key;
    if (result.display.empty()) result.display = scalar_or(name);
    if (is_hard_unresolved_name(result.display)) result.unresolved = true;
    return result;
}

void add_warning(std::vector<UnresolvedReference>* warnings,
                 const std::string& kind,
                 const std::string& id,
                 const std::string& context,
                 const std::string& value) {
    if (warnings) warnings->push_back(UnresolvedReference{kind, id, context, value});
}

void write_leader_name(JsonWriter& j,
                       const std::string& id,
                       const PdxValue* leader,
                       NameDiagnostics* diagnostics,
                       LeaderExportStats* leader_stats,
                       std::vector<UnresolvedReference>* warnings) {
    LeaderNameResult name = resolve_leader_name(leader);
    if (name.generated) {
        j.key("name"); j.value(name.display);
        j.key("name_raw"); j.value(name.raw);
        j.key("name_generated_from_key"); j.value(true);
        if (diagnostics) diagnostics->generated_kinds["leader"]++;
        if (leader_stats) leader_stats->leaders_with_generated_names++;
        return;
    }

    if (name.display.empty()) {
        j.key("name"); j.value(id);
        add_warning(warnings, "missing_leader_name", id, "leaders[].name", "");
        if (leader_stats) leader_stats->leaders_missing_names++;
        return;
    }

    if (is_generated_name_key(name.display)) {
        const std::string cleaned = make_leader_name_part_from_key(name.display);
        j.key("name"); j.value(cleaned.empty() ? name.display : cleaned);
        j.key("name_raw"); j.value(name.display);
        j.key("name_generated_from_key"); j.value(true);
        if (diagnostics) diagnostics->generated_kinds["leader"]++;
        if (leader_stats) leader_stats->leaders_with_generated_names++;
        return;
    }

    j.key("name"); j.value(name.display);
    if (name.unresolved || is_hard_unresolved_name(name.display)) {
        j.key("name_unresolved"); j.value(true);
        if (diagnostics) diagnostics->add_unresolved("leader", id, "leaders[].name", name.display);
    }
}

void write_optional_leader_child(JsonWriter& j, const PdxValue* leader, const std::string& source_key, const std::string& output_key) {
    if (const PdxValue* v = child(leader, source_key)) {
        j.key(output_key);
        write_pdx_as_schema_json(j, v, output_key);
    }
}

void write_leader_date_fields(JsonWriter& j,
                              const std::string& id,
                              const PdxValue* leader,
                              const std::string& game_date,
                              LeaderExportStats* leader_stats,
                              std::vector<UnresolvedReference>* warnings) {
    const std::string birth_date = scalar_or(child(leader, "date"));
    if (!birth_date.empty()) {
        j.key("birth_date"); j.value(birth_date);
        auto age_years = years_between_stellaris_dates(birth_date, game_date);
        if (age_years) {
            j.key("age_years"); j.raw_number(std::to_string(*age_years));
            if (leader_stats) leader_stats->leaders_with_calculated_age++;
        } else {
            if (leader_stats) leader_stats->leader_date_parse_warnings++;
            add_warning(warnings, "leader_date_parse_warning", id, "leaders[].date", birth_date);
        }
    }

    const std::string date_added = scalar_or(child(leader, "date_added"));
    const std::string recruitment_date = scalar_or(child(leader, "recruitment_date"));
    if (!date_added.empty()) { j.key("date_added"); j.value(date_added); }
    if (!recruitment_date.empty()) { j.key("recruitment_date"); j.value(recruitment_date); }

    const std::string service_start_date = !date_added.empty() ? date_added : recruitment_date;
    if (!service_start_date.empty()) {
        j.key("service_start_date"); j.value(service_start_date);
        auto service_years = years_between_stellaris_dates(service_start_date, game_date);
        auto service_days = days_between_stellaris_dates(service_start_date, game_date);
        if (service_years) {
            j.key("service_length_years"); j.raw_number(std::to_string(*service_years));
            if (service_days) {
                j.key("service_length_days"); j.raw_number(std::to_string(*service_days));
            }
            if (leader_stats) leader_stats->leaders_with_service_length++;
        } else {
            if (leader_stats) leader_stats->leader_date_parse_warnings++;
            add_warning(warnings, "leader_date_parse_warning", id, "leaders[].service_start_date", service_start_date);
        }
    }

    write_optional_leader_child(j, leader, "age", "raw_age");
}

std::string species_count_display_key(const std::string& species_id, const std::string& name) {
    if (is_hard_unresolved_name(name)) return name + " [#" + species_id + "]";
    if (is_generated_name_key(name)) return make_display_name_from_key(name);
    return name;
}

void write_resolved_species(JsonWriter& j, const std::string& id, const PdxValue* sp, const Settings& st, const DefinitionIndex* defs, NameDiagnostics* diagnostics = nullptr) {
    j.begin_object();
    j.key("species_id"); write_id(j, id);
    const std::string name = localized_name(child(sp, "name"));
    write_name_text(j, name, diagnostics, "species", id, "species.name");
    j.key("plural"); j.value(localized_name(child(sp, "plural")));
    const std::string adjective = localized_name(child(sp, "adjective"));
    write_display_text(j, "adjective", adjective, "adjective_unresolved", diagnostics, "species", id, "species.adjective");
    json_optional_scalar(j, sp, "class");
    json_optional_scalar(j, sp, "portrait");
    json_optional_scalar(j, sp, "name_list");
    json_optional_scalar(j, sp, "home_planet");
    j.key("traits");
    j.begin_array();
    for (const PdxValue* tv : children(child(sp, "traits"), "trait")) {
        std::string tok = scalar_or(tv);
        j.begin_object();
        j.key("trait"); j.value(tok);
        if (defs) { j.key("definition_source"); write_definition_source(j, defs, tok); }
        j.end_object();
    }
    j.end_array();
    if (st.include_source_locations) { j.key("source"); write_source(j, sp); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_schema_json(j, sp, "raw"); }
    j.end_object();
}

void write_resolved_leader(JsonWriter& j,
                           const std::string& id,
                           const PdxValue* leader,
                           const Settings& st,
                           const SaveIndexes& ix,
                           const DefinitionIndex* defs,
                           const std::string& game_date,
                           NameDiagnostics* diagnostics = nullptr,
                           LeaderExportStats* leader_stats = nullptr,
                           std::vector<UnresolvedReference>* warnings = nullptr) {
    (void)ix;
    if (leader_stats) leader_stats->leader_count++;
    j.begin_object();
    j.key("leader_id"); write_id(j, id);
    write_leader_name(j, id, leader, diagnostics, leader_stats, warnings);
    write_optional_leader_child(j, leader, "gender", "gender");
    json_optional_scalar(j, leader, "class");
    json_optional_scalar(j, leader, "level");
    json_optional_scalar(j, leader, "species");
    json_optional_scalar(j, leader, "country");
    json_optional_scalar(j, leader, "portrait");
    write_optional_leader_child(j, leader, "tier", "tier");
    write_optional_leader_child(j, leader, "experience", "experience");
    write_leader_date_fields(j, id, leader, game_date, leader_stats, warnings);
    write_optional_leader_child(j, leader, "job", "job");
    write_optional_leader_child(j, leader, "ethic", "ethic");
    write_optional_leader_child(j, leader, "planet", "planet");
    write_optional_leader_child(j, leader, "creator", "creator");
    j.key("traits");
    j.begin_array();
    for (const PdxValue* tv : children(leader, "traits")) {
        std::string tok = scalar_or(tv);
        j.begin_object();
        j.key("trait"); j.value(tok);
        if (defs) { j.key("definition_source"); write_definition_source(j, defs, tok); }
        j.end_object();
    }
    j.end_array();
    if (const PdxValue* loc = child(leader, "location")) { j.key("location"); write_pdx_as_schema_json(j, loc, "location"); }
    if (const PdxValue* loc = child(leader, "council_location")) { j.key("council_location"); write_pdx_as_schema_json(j, loc, "council_location"); }
    if (st.include_source_locations) { j.key("source"); write_source(j, leader); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_schema_json(j, leader, "raw"); }
    j.end_object();
}

void write_instance_with_type(JsonWriter& j, const std::string& id, const PdxValue* obj, const std::string& id_name, const Settings& st, const DefinitionIndex* defs) {
    j.begin_object();
    j.key(id_name); write_id(j, id);
    std::string type = scalar_or(child(obj, "type"));
    const bool type_is_safe = !type.empty() && !contains_banned_job_export_token(type);
    if (type_is_safe) { j.key("type"); j.value(type); }
    if (defs && type_is_safe) { j.key("definition_source"); write_definition_source(j, defs, type); }
    if (const PdxValue* pos = child(obj, "position")) { j.key("position"); write_pdx_as_schema_json(j, pos, "position"); }
    if (const PdxValue* lvl = child(obj, "level")) { j.key("level"); write_pdx_as_schema_json(j, lvl, "level"); }
    if (st.include_source_locations) { j.key("source"); write_source(j, obj); }
    if (st.include_raw_pdx_objects && !contains_banned_job_export_token(type)) { j.key("raw"); write_pdx_as_schema_json(j, obj, "raw"); }
    j.end_object();
}

void write_system_summary(JsonWriter& j, const std::string& id, const PdxValue* sys, const Settings& st, NameDiagnostics* diagnostics = nullptr) {
    j.begin_object();
    j.key("system_id"); write_id(j, id);
    write_name_text(j, localized_name(child(sys, "name")), diagnostics, "system", id, "systems[].name");
    if (const PdxValue* coord = child(sys, "coordinate")) { j.key("coordinate"); write_pdx_as_schema_json(j, coord, "coordinate"); }
    json_optional_scalar(j, sys, "type");
    json_optional_scalar(j, sys, "star_class");
    json_optional_scalar(j, sys, "sector");
    j.key("planet_ids");
    j.begin_array();
    for (const PdxValue* pv : children(sys, "planet")) write_id(j, pv);
    j.end_array();
    if (const PdxValue* h = child(sys, "hyperlane")) { j.key("hyperlanes"); write_pdx_as_schema_json(j, h, "hyperlanes"); }
    if (const PdxValue* sb = child(sys, "starbases")) { j.key("starbases"); write_pdx_as_schema_json(j, sb, "starbases"); }
    if (st.include_source_locations) { j.key("source"); write_source(j, sys); }
    j.end_object();
}

std::string planet_system_id(const PdxValue* planet) {
    return scalar_or(child(child(planet, "coordinate"), "origin"));
}

void write_string_array(JsonWriter& j, const std::set<std::string>& values) {
    j.begin_array();
    for (const std::string& value : values) j.value(value);
    j.end_array();
}

std::vector<std::string> system_planet_ids(const PdxValue* sys) {
    std::vector<std::string> out;
    for (const PdxValue* pv : children(sys, "planet")) {
        std::string id = scalar_or(pv);
        if (!id.empty()) out.push_back(id);
    }
    return out;
}

uintmax_t json_extract_uint_field(const std::string& obj, const std::string& field) {
    const std::string needle = "\"" + field + "\"";
    size_t p = obj.find(needle);
    if (p == std::string::npos) return 0;
    p = obj.find(':', p + needle.size());
    if (p == std::string::npos) return 0;
    ++p;
    while (p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) ++p;
    size_t end = p;
    while (end < obj.size() && std::isdigit(static_cast<unsigned char>(obj[end]))) ++end;
    if (end == p) return 0;
    try {
        return static_cast<uintmax_t>(std::stoull(obj.substr(p, end - p)));
    } catch (...) {
        return 0;
    }
}

std::vector<std::string> json_extract_string_array_field(const std::string& obj, const std::string& field) {
    std::vector<std::string> out;
    const std::string needle = "\"" + field + "\"";
    size_t p = obj.find(needle);
    if (p == std::string::npos) return out;
    p = obj.find('[', p + needle.size());
    if (p == std::string::npos) return out;
    size_t end = obj.find(']', p + 1);
    if (end == std::string::npos) return out;
    std::string arr = obj.substr(p + 1, end - p - 1);
    size_t q = 0;
    while (q < arr.size()) {
        q = arr.find('"', q);
        if (q == std::string::npos) break;
        ++q;
        std::string item;
        bool esc = false;
        for (; q < arr.size(); ++q) {
            char c = arr[q];
            if (esc) { item.push_back(c); esc = false; continue; }
            if (c == '\\') { esc = true; continue; }
            if (c == '"') break;
            item.push_back(c);
        }
        out.push_back(std::move(item));
        if (q < arr.size()) ++q;
    }
    return out;
}

std::set<std::string> system_starbase_ids(const PdxValue* sys) {
    std::set<std::string> out;
    for (const std::string& id : primitive_list(child(sys, "starbases"))) {
        if (!id.empty()) out.insert(id);
    }
    for (const PdxValue* sb : children(sys, "starbase")) {
        std::string id = scalar_or(sb);
        if (!id.empty()) out.insert(id);
    }
    return out;
}

std::vector<std::string> system_hyperlane_targets(const PdxValue* sys) {
    std::vector<std::string> out;
    const PdxValue* hyperlanes = child(sys, "hyperlane");
    if (!hyperlanes || hyperlanes->kind != PdxValue::Kind::Container) return out;
    for (const auto& e : hyperlanes->entries) {
        if (e.value && e.value->kind == PdxValue::Kind::Container) {
            std::string to = scalar_or(child(e.value, "to"));
            if (!to.empty()) out.push_back(to);
        }
    }
    return out;
}

bool system_has_dashboard_coordinate(const PdxValue* sys) {
    const PdxValue* coord = child(sys, "coordinate");
    return coord && child(coord, "x") && child(coord, "y");
}

struct MapSystemContext {
    std::set<std::string> colony_planet_ids;
    std::set<std::string> owned_planet_ids;
    std::set<std::string> controlled_planet_ids;
    std::set<std::string> starbase_ids;
    bool is_colony_system = false;
    bool has_capital = false;
    std::string capital_planet_id;
};

struct MapExportContext {
    std::map<std::string, MapSystemContext> systems;
    std::string capital_system_id;
    std::string capital_system_name;
    size_t hyperlane_edge_count = 0;
    std::set<std::string> systems_missing_coordinates;
    std::set<std::string> colonies_missing_systems;
    std::set<std::string> colonies_with_unexported_systems;
    std::set<std::string> hyperlane_targets_missing_from_export;
};

MapExportContext build_map_export_context(const std::vector<std::string>& owned_planets,
                                                 const std::vector<std::string>& controlled_planets,
                                                 const std::vector<std::string>& owned_fleets,
                                                 const std::string& capital_id,
                                                 const SaveIndexes& ix) {
    MapExportContext ctx;

    auto include_system = [&](const std::string& system_id) -> MapSystemContext* {
        if (system_id.empty()) return nullptr;
        auto sys_it = ix.galactic_objects.find(system_id);
        if (sys_it == ix.galactic_objects.end()) return nullptr;
        auto [it, inserted] = ctx.systems.emplace(system_id, MapSystemContext{});
        (void)inserted;
        for (const std::string& sbid : system_starbase_ids(sys_it->second)) it->second.starbase_ids.insert(sbid);
        return &it->second;
    };

    for (const std::string& pid : owned_planets) {
        auto pit = ix.planets.find(pid);
        if (pit == ix.planets.end()) {
            ctx.colonies_missing_systems.insert(pid);
            continue;
        }
        const std::string system_id = planet_system_id(pit->second);
        MapSystemContext* sys = include_system(system_id);
        if (!sys) {
            ctx.colonies_missing_systems.insert(pid);
            continue;
        }
        sys->is_colony_system = true;
        sys->colony_planet_ids.insert(pid);
        sys->owned_planet_ids.insert(pid);
    }

    if (!capital_id.empty()) {
        auto pit = ix.planets.find(capital_id);
        if (pit != ix.planets.end()) {
            const std::string system_id = planet_system_id(pit->second);
            if (MapSystemContext* sys = include_system(system_id)) {
                sys->has_capital = true;
                sys->capital_planet_id = capital_id;
                ctx.capital_system_id = system_id;
                auto sys_it = ix.galactic_objects.find(system_id);
                if (sys_it != ix.galactic_objects.end()) ctx.capital_system_name = localized_name(child(sys_it->second, "name"));
            }
        }
    }

    for (const std::string& pid : controlled_planets) {
        auto pit = ix.planets.find(pid);
        if (pit == ix.planets.end()) continue;
        if (MapSystemContext* sys = include_system(planet_system_id(pit->second))) {
            sys->controlled_planet_ids.insert(pid);
        }
    }

    for (const std::string& fid : owned_fleets) {
        auto fit = ix.fleets.find(fid);
        if (fit == ix.fleets.end()) continue;
        include_system(scalar_or(child(child(fit->second, "coordinate"), "origin")));
    }

    for (const std::string& pid : owned_planets) {
        auto pit = ix.planets.find(pid);
        if (pit == ix.planets.end()) continue;
        const std::string system_id = planet_system_id(pit->second);
        if (!system_id.empty() && ix.galactic_objects.find(system_id) != ix.galactic_objects.end() && ctx.systems.find(system_id) == ctx.systems.end()) {
            ctx.colonies_with_unexported_systems.insert(pid);
        }
    }

    for (const auto& [system_id, _] : ctx.systems) {
        auto sys_it = ix.galactic_objects.find(system_id);
        if (sys_it == ix.galactic_objects.end()) continue;
        if (!system_has_dashboard_coordinate(sys_it->second)) ctx.systems_missing_coordinates.insert(system_id);
        for (const std::string& target : system_hyperlane_targets(sys_it->second)) {
            ctx.hyperlane_edge_count++;
            if (ctx.systems.find(target) == ctx.systems.end()) ctx.hyperlane_targets_missing_from_export.insert(target);
        }
    }

    return ctx;
}

void write_system_context(JsonWriter& j,
                                 const std::string& system_id,
                                 const PdxValue* sys,
                                 const MapSystemContext& ctx,
                                 const Settings& st,
                                 NameDiagnostics* diagnostics = nullptr) {
    j.begin_object();
    j.key("system_id"); write_id(j, system_id);
    write_name_text(j, localized_name(child(sys, "name")), diagnostics, "system", system_id, "systems[].name");
    if (const PdxValue* coord = child(sys, "coordinate")) { j.key("coordinate"); write_pdx_as_schema_json(j, coord, "coordinate"); }
    json_optional_scalar(j, sys, "star_class");
    json_optional_scalar(j, sys, "type");
    json_optional_scalar(j, sys, "sector");
    j.key("planet_ids"); write_id_array(j, system_planet_ids(sys));
    j.key("colony_planet_ids"); write_id_array(j, ctx.colony_planet_ids);
    j.key("owned_planet_ids"); write_id_array(j, ctx.owned_planet_ids);
    j.key("controlled_planet_ids"); write_id_array(j, ctx.controlled_planet_ids);
    j.key("starbase_ids"); write_id_array(j, ctx.starbase_ids);
    j.key("hyperlanes");
    if (const PdxValue* h = child(sys, "hyperlane")) write_pdx_as_schema_json(j, h, "hyperlanes");
    else { j.begin_array(); j.end_array(); }
    j.key("is_colony_system"); j.value(ctx.is_colony_system);
    j.key("has_capital"); j.value(ctx.has_capital);
    if (ctx.has_capital) { j.key("capital_planet_id"); write_id(j, ctx.capital_planet_id); }
    if (st.include_source_locations) { j.key("source"); write_source(j, sys); }
    j.end_object();
}

void write_systems_block(JsonWriter& j, const MapExportContext& map_ctx, const SaveIndexes& ix, const Settings& st, NameDiagnostics* diagnostics = nullptr) {
    j.key("systems");
    j.begin_object();
    for (const auto& [system_id, ctx] : map_ctx.systems) {
        auto sys_it = ix.galactic_objects.find(system_id);
        if (sys_it == ix.galactic_objects.end()) continue;
        j.key(system_id);
        write_system_context(j, system_id, sys_it->second, ctx, st, diagnostics);
    }
    j.end_object();
}

size_t colony_system_count(const MapExportContext& map_ctx) {
    size_t count = 0;
    for (const auto& [_, ctx] : map_ctx.systems) {
        if (ctx.is_colony_system) count++;
    }
    return count;
}

void write_map_summary(JsonWriter& j, const MapExportContext& map_ctx) {
    j.key("map_summary");
    j.begin_object();
    j.key("system_count"); j.raw_number(std::to_string(map_ctx.systems.size()));
    j.key("colony_system_count"); j.raw_number(std::to_string(colony_system_count(map_ctx)));
    j.key("capital_system_id"); write_id(j, map_ctx.capital_system_id);
    j.key("capital_system_name"); j.value(map_ctx.capital_system_name);
    j.key("hyperlane_edge_count"); j.raw_number(std::to_string(map_ctx.hyperlane_edge_count));
    j.key("systems_missing_coordinates"); write_string_array(j, map_ctx.systems_missing_coordinates);
    j.key("colonies_missing_systems"); write_string_array(j, map_ctx.colonies_missing_systems);
    j.end_object();
}

std::optional<double> scalar_double(const PdxValue* v) {
    auto s = scalar(v);
    if (!s) return std::nullopt;
    try {
        size_t pos = 0;
        double d = std::stod(*s, &pos);
        if (pos != s->size()) return std::nullopt;
        return d;
    } catch (...) {
        return std::nullopt;
    }
}

std::string json_number(double v) {
    std::ostringstream ss;
    ss << std::setprecision(15) << v;
    return ss.str();
}

void write_optional_child(JsonWriter& j, const PdxValue* obj, const std::string& source_key, const std::string& output_key) {
    if (const PdxValue* v = child(obj, source_key)) {
        j.key(output_key);
        write_pdx_as_schema_json(j, v, output_key);
    }
}

void write_metric_or_null(JsonWriter& j, const PdxValue* obj, const std::string& key) {
    j.key(key);
    if (const PdxValue* v = child(obj, key)) write_pdx_as_schema_json(j, v, key);
    else j.value(nullptr);
}

void accumulate_balance_resources(const PdxValue* v, std::map<std::string, double>& totals) {
    if (!v || v->kind != PdxValue::Kind::Container) return;
    for (const auto& e : v->entries) {
        if (e.key.empty()) {
            accumulate_balance_resources(e.value, totals);
            continue;
        }
        if (auto amount = scalar_double(e.value)) {
            totals[e.key] += *amount;
        } else {
            accumulate_balance_resources(e.value, totals);
        }
    }
}

void write_budget_child_or_empty(JsonWriter& j, const PdxValue* current_month, const std::string& key) {
    j.key(key);
    if (const PdxValue* v = child(current_month, key)) write_pdx_as_schema_json(j, v, key);
    else { j.begin_object(); j.end_object(); }
}

void write_nat_finance_economy(JsonWriter& j, const PdxValue* country, const PdxValue* stored_resources) {
    const PdxValue* current_month = nested_child(country, {"budget", "current_month"});
    const PdxValue* balance = child(current_month, "balance");
    std::map<std::string, double> net_monthly;
    accumulate_balance_resources(balance, net_monthly);

    j.key("nat_finance_economy");
    j.begin_object();
    j.key("budget");
    j.begin_object();
    write_budget_child_or_empty(j, current_month, "income");
    write_budget_child_or_empty(j, current_month, "expenses");
    write_budget_child_or_empty(j, current_month, "balance");
    j.end_object();

    j.key("net_monthly_resource");
    j.begin_object();
    constexpr double epsilon = 0.000000001;
    for (const auto& [resource, amount] : net_monthly) {
        if (std::abs(amount) <= epsilon) continue;
        j.key(resource);
        j.raw_number(json_number(amount));
    }
    j.end_object();

    j.key("stored_resources");
    if (stored_resources) write_pdx_as_schema_json(j, stored_resources, "stored_resources");
    else { j.begin_object(); j.end_object(); }
    j.end_object();
}

std::string display_token(std::string token) {
    if (starts_with_ci(token, "pc_")) token = token.substr(3);
    if (starts_with_ci(token, "col_")) token = token.substr(4);
    std::replace(token.begin(), token.end(), '_', ' ');
    bool cap_next = true;
    for (char& c : token) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            cap_next = true;
        } else if (cap_next) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            cap_next = false;
        } else {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return token;
}

void add_type_counts(std::map<std::string, size_t>& counts,
                            const std::vector<std::string>& ids,
                            const std::unordered_map<std::string, const PdxValue*>& index) {
    for (const std::string& id : ids) {
        auto it = index.find(id);
        if (it == index.end()) continue;
        std::string type = scalar_or(child(it->second, "type"));
        if (contains_banned_job_export_token(type)) continue;
        if (!type.empty()) counts[type]++;
    }
}

double sum_resource_like(const PdxValue* v, const std::vector<std::string>& needles) {
    if (!v) return 0.0;
    if (v->kind != PdxValue::Kind::Container) return 0.0;
    double total = 0.0;
    for (const auto& e : v->entries) {
        std::string key = lower_copy(e.key);
        bool key_matches = false;
        for (const std::string& needle : needles) {
            if (key.find(needle) != std::string::npos) {
                key_matches = true;
                break;
            }
        }
        if (key_matches) {
            if (auto d = scalar_double(e.value)) total += *d;
        }
        total += sum_resource_like(e.value, needles);
    }
    return total;
}

double count_type_matches(const std::map<std::string, size_t>& counts, const std::vector<std::string>& needles) {
    double total = 0.0;
    for (const auto& [type, count] : counts) {
        std::string t = lower_copy(type);
        for (const std::string& needle : needles) {
            if (t.find(needle) != std::string::npos) {
                total += static_cast<double>(count);
                break;
            }
        }
    }
    return total;
}

size_t direct_named_child_count(const PdxValue* v) {
    if (!v || v->kind != PdxValue::Kind::Container) return 0;
    size_t count = 0;
    for (const auto& e : v->entries) {
        if (!e.key.empty()) count++;
    }
    return count;
}

std::string infer_colony_role(const std::string& planet_id,
                                     const PdxValue* planet,
                                     const std::string& capital_id,
                                     const std::map<std::string, size_t>& district_counts,
                                     const std::map<std::string, size_t>& building_counts) {
    const std::string designation = lower_copy(scalar_or(child(planet, "designation")));
    const std::string final_designation = lower_copy(scalar_or(child(planet, "final_designation")));
    if ((!capital_id.empty() && planet_id == capital_id) || designation == "col_capital" || final_designation == "col_capital") {
        return "Capital";
    }

    std::map<std::string, double> score;
    auto add = [&](const std::string& role, double value) {
        if (value > 0.0) score[role] += value;
    };

    const PdxValue* production = child(planet, "produces");
    add("Forge", sum_resource_like(production, {"alloys"}));
    add("Factory", sum_resource_like(production, {"consumer_goods"}));
    add("Mining", sum_resource_like(production, {"minerals"}));
    add("Generator", sum_resource_like(production, {"energy"}));
    add("Research", sum_resource_like(production, {"physics_research", "society_research", "engineering_research", "research"}));
    add("Unity", sum_resource_like(production, {"unity"}));
    add("Trade", sum_resource_like(production, {"trade"}));

    add("Forge", count_type_matches(district_counts, {"foundry", "alloy"}) + count_type_matches(building_counts, {"foundry", "alloy"}));
    add("Factory", count_type_matches(district_counts, {"factory", "consumer"}) + count_type_matches(building_counts, {"factory", "consumer"}));
    add("Mining", count_type_matches(district_counts, {"mining", "mine"}) + count_type_matches(building_counts, {"mining", "mine", "mineral"}));
    add("Generator", count_type_matches(district_counts, {"generator", "energy"}) + count_type_matches(building_counts, {"generator", "energy"}));
    add("Research", count_type_matches(district_counts, {"research"}) + count_type_matches(building_counts, {"research", "laborator"}));
    add("Unity", count_type_matches(district_counts, {"unity"}) + count_type_matches(building_counts, {"unity", "temple", "monument"}));
    add("Trade", count_type_matches(district_counts, {"trade", "commercial"}) + count_type_matches(building_counts, {"trade", "commercial"}));

    std::string best = "Mixed";
    double best_score = 0.0;
    bool tied = false;
    for (const auto& [role, value] : score) {
        if (value > best_score) {
            best = role;
            best_score = value;
            tied = false;
        } else if (value == best_score && value > 0.0) {
            tied = true;
        }
    }
    return (best_score > 0.0 && !tied) ? best : "Mixed";
}

void write_count_object(JsonWriter& j, const std::map<std::string, size_t>& counts) {
    j.begin_object();
    for (const auto& [type, count] : counts) {
        j.key(type);
        j.raw_number(std::to_string(count));
    }
    j.end_object();
}

void write_number_object(JsonWriter& j, const std::map<std::string, double>& counts) {
    j.begin_object();
    for (const auto& [type, count] : counts) {
        j.key(type);
        j.raw_number(json_number(count));
    }
    j.end_object();
}

void write_species_counts(JsonWriter& j, const std::map<std::string, double>& counts) {
    j.begin_object();
    for (const auto& [id, count] : counts) {
        j.key(id);
        j.raw_number(json_number(count));
    }
    j.end_object();
}

std::optional<double> numeric_child_or_scalar(const PdxValue* v, const std::string& child_key) {
    if (auto direct = scalar_double(v)) return direct;
    return scalar_double(child(v, child_key));
}

struct ColonyDemographicRollup {
    std::map<std::string, double> pop_category_counts;
    std::map<std::string, size_t> job_counts_by_type;
    std::map<std::string, double> workforce_by_job_type;
    std::map<std::string, double> species_counts_by_id;
    std::map<std::string, double> species_counts_by_name;
    std::map<std::string, double> enslaved_by_species_id;
    size_t suppressed_job_record_count = 0;
};

struct PlanetSpeciesDistribution {
    std::string planet_id;
    std::string planet_name;
    double pops = 0.0;
};

struct SpeciesDemographicRollup {
    std::string species_id;
    std::string name;
    std::string plural;
    std::string species_class;
    std::string portrait;
    std::vector<std::string> traits;
    double total_pops = 0.0;
    double enslaved_pops = 0.0;
    std::vector<PlanetSpeciesDistribution> planet_distribution;
};

struct EmpireDemographicRollup {
    double total_sapient_pops = 0.0;
    std::map<std::string, SpeciesDemographicRollup> species;
    std::set<std::string> species_without_resolution;
};

struct EmpireWorkforceRollup {
    std::map<std::string, size_t> job_counts_by_type;
    std::map<std::string, double> workforce_by_job_type;
    std::map<std::string, double> pop_category_counts;
    std::map<std::string, double> pop_category_share;
    std::map<std::string, std::map<std::string, size_t>> jobs_by_planet;
    std::map<std::string, std::map<std::string, double>> pop_categories_by_planet;
    size_t inactive_job_records_suppressed = 0;
};

struct EmpireRollups {
    EmpireDemographicRollup demographics;
    EmpireWorkforceRollup workforce;
    std::map<std::string, ColonyDemographicRollup> colonies;
};

std::vector<std::string> species_trait_tokens(const PdxValue* sp) {
    std::vector<std::string> out;
    for (const PdxValue* tv : children(child(sp, "traits"), "trait")) {
        std::string tok = scalar_or(tv);
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

ColonyDemographicRollup build_colony_demographic_rollup(const std::string& planet_id,
                                                               const PdxValue* planet,
                                                               const SaveIndexes& ix) {
    (void)planet_id;
    ColonyDemographicRollup out;
    std::map<std::string, double> species_counts_from_info;
    std::map<std::string, double> species_counts_from_pop_groups;

    if (const PdxValue* species_info = child(planet, "species_information")) {
        for (const auto& e : species_info->entries) {
            if (e.key.empty()) continue;
            if (auto d = numeric_child_or_scalar(e.value, "num_pops")) species_counts_from_info[e.key] += *d;
            if (auto d = scalar_double(child(e.value, "num_enslaved"))) out.enslaved_by_species_id[e.key] += *d;
        }
    }

    for (const std::string& pgid : scalar_id_list_from_child(planet, "pop_groups")) {
        auto it = ix.pop_groups.find(pgid);
        if (it == ix.pop_groups.end()) continue;
        const PdxValue* key = child(it->second, "key");
        const double size = scalar_double(child(it->second, "size")).value_or(0.0);
        const std::string category = scalar_or(child(key, "category"));
        if (!category.empty() && size > 0.0) out.pop_category_counts[category] += size;
        const std::string sid = scalar_or(child(key, "species"));
        const std::string category_lower = lower_copy(category);
        if (!sid.empty() && size > 0.0 && category_lower.find("pre_sapient") == std::string::npos) {
            species_counts_from_pop_groups[sid] += size;
        }
    }

    out.species_counts_by_id = !species_counts_from_pop_groups.empty() ? species_counts_from_pop_groups : species_counts_from_info;

    for (const std::string& jid : scalar_id_list_from_child(planet, "pop_jobs")) {
        auto it = ix.pop_jobs.find(jid);
        if (it == ix.pop_jobs.end()) continue;
        const std::string type = scalar_or(child(it->second, "type"));
        if (!is_exportable_pop_job(it->second)) {
            out.suppressed_job_record_count++;
            continue;
        }
        out.job_counts_by_type[type]++;
        if (auto workforce = scalar_double(child(it->second, "workforce"))) {
            if (*workforce > 0.0) out.workforce_by_job_type[type] += *workforce;
        }
    }

    for (const auto& [sid, count] : out.species_counts_by_id) {
        auto sp_it = ix.species.find(sid);
        std::string name = sid;
        if (sp_it != ix.species.end()) {
            std::string resolved = localized_name(child(sp_it->second, "name"));
            if (!resolved.empty()) name = resolved;
        }
        out.species_counts_by_name[species_count_display_key(sid, name)] += count;
    }

    return out;
}

EmpireRollups build_empire_rollups(const std::vector<std::string>& owned_planets,
                                          const SaveIndexes& ix) {
    EmpireRollups out;
    for (const std::string& pid : owned_planets) {
        auto pit = ix.planets.find(pid);
        if (pit == ix.planets.end()) continue;
        const PdxValue* planet = pit->second;
        ColonyDemographicRollup colony = build_colony_demographic_rollup(pid, planet, ix);
        const std::string planet_name = localized_name(child(planet, "name"));

        for (const auto& [sid, count] : colony.species_counts_by_id) {
            auto& sp = out.demographics.species[sid];
            if (sp.species_id.empty()) sp.species_id = sid;
            sp.total_pops += count;
            sp.planet_distribution.push_back(PlanetSpeciesDistribution{pid, planet_name, count});
            auto ens = colony.enslaved_by_species_id.find(sid);
            if (ens != colony.enslaved_by_species_id.end()) sp.enslaved_pops += ens->second;
        }
        for (const auto& [type, count] : colony.job_counts_by_type) {
            out.workforce.job_counts_by_type[type] += count;
            out.workforce.jobs_by_planet[pid][type] += count;
        }
        out.workforce.inactive_job_records_suppressed += colony.suppressed_job_record_count;
        for (const auto& [type, workforce] : colony.workforce_by_job_type) {
            out.workforce.workforce_by_job_type[type] += workforce;
        }
        for (const auto& [category, count] : colony.pop_category_counts) {
            out.workforce.pop_category_counts[category] += count;
            out.workforce.pop_categories_by_planet[pid][category] += count;
        }

        out.colonies.emplace(pid, std::move(colony));
    }

    double category_total = 0.0;
    for (const auto& [_, count] : out.workforce.pop_category_counts) category_total += count;
    if (category_total > 0.0) {
        for (const auto& [category, count] : out.workforce.pop_category_counts) {
            out.workforce.pop_category_share[category] = count / category_total;
        }
    }

    for (auto& [sid, sp] : out.demographics.species) {
        out.demographics.total_sapient_pops += sp.total_pops;
        auto sp_it = ix.species.find(sid);
        if (sp_it == ix.species.end()) {
            out.demographics.species_without_resolution.insert(sid);
            continue;
        }
        const PdxValue* obj = sp_it->second;
        sp.name = localized_name(child(obj, "name"));
        sp.plural = localized_name(child(obj, "plural"));
        sp.species_class = scalar_or(child(obj, "class"));
        sp.portrait = scalar_or(child(obj, "portrait"));
        sp.traits = species_trait_tokens(obj);
    }

    return out;
}

void write_nested_count_object(JsonWriter& j, const std::map<std::string, std::map<std::string, size_t>>& counts) {
    j.begin_object();
    for (const auto& [outer, inner] : counts) {
        j.key(outer);
        write_count_object(j, inner);
    }
    j.end_object();
}

void write_nested_number_object(JsonWriter& j, const std::map<std::string, std::map<std::string, double>>& counts) {
    j.begin_object();
    for (const auto& [outer, inner] : counts) {
        j.key(outer);
        write_number_object(j, inner);
    }
    j.end_object();
}

void write_demographics(JsonWriter& j, const EmpireDemographicRollup& demographics, NameDiagnostics* diagnostics = nullptr) {
    std::string dominant_species_id;
    double dominant_species_count = -1.0;
    for (const auto& [sid, sp] : demographics.species) {
        if (sp.total_pops > dominant_species_count) {
            dominant_species_id = sid;
            dominant_species_count = sp.total_pops;
        }
    }

    j.key("demographics");
    j.begin_object();
    j.key("total_sapient_pops"); j.raw_number(json_number(demographics.total_sapient_pops));
    j.key("species_count"); j.raw_number(std::to_string(demographics.species.size()));
    j.key("dominant_species_id"); write_id(j, dominant_species_id);
    j.key("dominant_species_name");
    auto dom_it = demographics.species.find(dominant_species_id);
    if (dom_it != demographics.species.end()) j.value(dom_it->second.name);
    else j.value("");
    j.key("species");
    j.begin_array();
    for (const auto& [_, sp] : demographics.species) {
        j.begin_object();
        j.key("species_id"); write_id(j, sp.species_id);
        write_name_text(j, sp.name, diagnostics, "species", sp.species_id, "demographics.species[].name");
        j.key("plural"); j.value(sp.plural);
        j.key("class"); j.value(sp.species_class);
        j.key("portrait"); j.value(sp.portrait);
        j.key("traits");
        j.begin_array();
        for (const std::string& trait : sp.traits) j.value(trait);
        j.end_array();
        j.key("total_pops"); j.raw_number(json_number(sp.total_pops));
        j.key("empire_share");
        if (demographics.total_sapient_pops > 0.0) j.raw_number(json_number(sp.total_pops / demographics.total_sapient_pops));
        else j.raw_number("0");
        if (sp.enslaved_pops > 0.0) {
            j.key("enslaved_pops");
            j.raw_number(json_number(sp.enslaved_pops));
        }
        j.key("planet_distribution");
        j.begin_array();
        for (const auto& dist : sp.planet_distribution) {
            j.begin_object();
            j.key("planet_id"); write_id(j, dist.planet_id);
            write_display_text(j, "planet_name", dist.planet_name, "planet_name_unresolved", diagnostics, "planet", dist.planet_id, "demographics.species[].planet_distribution[].planet_name");
            j.key("pops"); j.raw_number(json_number(dist.pops));
            j.end_object();
        }
        j.end_array();
        j.end_object();
    }
    j.end_array();
    j.end_object();
}

void write_workforce_summary(JsonWriter& j, const EmpireWorkforceRollup& workforce) {
    j.key("workforce_summary");
    j.begin_object();
    j.key("job_counts_by_type"); write_count_object(j, workforce.job_counts_by_type);
    j.key("active_job_counts_by_type"); write_count_object(j, workforce.job_counts_by_type);
    j.key("workforce_by_job_type"); write_number_object(j, workforce.workforce_by_job_type);
    j.key("pop_category_counts"); write_number_object(j, workforce.pop_category_counts);
    j.key("pop_category_share"); write_number_object(j, workforce.pop_category_share);
    j.key("jobs_by_planet"); write_nested_count_object(j, workforce.jobs_by_planet);
    j.key("active_jobs_by_planet"); write_nested_count_object(j, workforce.jobs_by_planet);
    j.key("pop_categories_by_planet"); write_nested_number_object(j, workforce.pop_categories_by_planet);
    j.end_object();
}

void write_colony_derived_summary(JsonWriter& j,
                                         const std::string& planet_id,
                                         const PdxValue* planet,
                                         const SaveIndexes& ix,
                                         const ColonyDemographicRollup* colony_rollup,
                                         const std::string& capital_id,
                                         NameDiagnostics* diagnostics = nullptr) {
    std::string planet_name = localized_name(child(planet, "name"));
    std::string system_id = scalar_or(child(child(planet, "coordinate"), "origin"));
    std::string system_name;
    auto sys_it = ix.galactic_objects.find(system_id);
    if (sys_it != ix.galactic_objects.end()) system_name = localized_name(child(sys_it->second, "name"));

    std::map<std::string, size_t> district_counts;
    std::map<std::string, size_t> building_counts;
    std::map<std::string, size_t> deposit_counts;
    add_type_counts(district_counts, scalar_id_list_from_child(planet, "districts"), ix.districts);
    add_type_counts(building_counts, scalar_id_list_from_child(planet, "buildings_cache"), ix.buildings);
    add_type_counts(deposit_counts, scalar_id_list_from_child(planet, "deposits"), ix.deposits);

    std::map<std::string, double> species_counts;
    if (const PdxValue* species_info = child(planet, "species_information")) {
        for (const auto& e : species_info->entries) {
            if (e.key.empty()) continue;
            if (auto d = numeric_child_or_scalar(e.value, "num_pops")) species_counts[e.key] += *d;
        }
    }
    if (species_counts.empty()) {
        for (const std::string& pgid : scalar_id_list_from_child(planet, "pop_groups")) {
            auto it = ix.pop_groups.find(pgid);
            if (it == ix.pop_groups.end()) continue;
            std::string sid = scalar_or(child(child(it->second, "key"), "species"));
            auto size = scalar_double(child(it->second, "size"));
            if (!sid.empty() && size) species_counts[sid] += *size;
        }
    }
    if (colony_rollup && !colony_rollup->species_counts_by_id.empty()) {
        species_counts = colony_rollup->species_counts_by_id;
    }

    std::string dominant_species_id;
    double dominant_species_count = -1.0;
    for (const auto& [sid, count] : species_counts) {
        if (count > dominant_species_count) {
            dominant_species_id = sid;
            dominant_species_count = count;
        }
    }
    std::string dominant_species_name;
    auto sp_it = ix.species.find(dominant_species_id);
    if (sp_it != ix.species.end()) dominant_species_name = localized_name(child(sp_it->second, "name"));

    const std::string role = infer_colony_role(planet_id, planet, capital_id, district_counts, building_counts);
    const std::string planet_class = scalar_or(child(planet, "planet_class"));
    const std::string planet_size = scalar_or(child(planet, "planet_size"));
    const std::string designation = scalar_or(child(planet, "final_designation"), scalar_or(child(planet, "designation")));

    j.key("derived_summary");
    j.begin_object();
    j.key("planet_id"); write_id(j, planet_id);
    if (!planet_name.empty()) write_display_text(j, "planet_name", planet_name, "planet_name_unresolved", diagnostics, "planet", planet_id, "colonies[].derived_summary.planet_name");
    if (!system_id.empty()) { j.key("system_id"); write_id(j, system_id); }
    if (!system_name.empty()) write_display_text(j, "system_name", system_name, "system_name_unresolved", diagnostics, "system", system_id, "colonies[].derived_summary.system_name");
    j.key("map");
    j.begin_object();
    j.key("system_id"); write_id(j, system_id);
    write_display_text(j, "system_name", system_name, "system_name_unresolved", diagnostics, "system", system_id, "colonies[].derived_summary.map.system_name");
    j.end_object();
    write_optional_child(j, planet, "planet_class", "planet_class");
    write_optional_child(j, planet, "planet_size", "planet_size");
    write_optional_child(j, planet, "designation", "designation");
    write_optional_child(j, planet, "final_designation", "final_designation");
    write_optional_child(j, planet, "owner", "owner");
    write_optional_child(j, planet, "controller", "controller");
    for (const std::string& k : {"stability", "crime", "amenities", "amenities_usage", "free_amenities", "total_housing", "housing_usage", "free_housing", "num_sapient_pops", "employable_pops"}) {
        write_optional_child(j, planet, k, k);
    }
    write_optional_child(j, planet, "produces", "production");
    write_optional_child(j, planet, "upkeep", "upkeep");
    write_optional_child(j, planet, "profits", "profit");
    j.key("district_counts_by_type"); write_count_object(j, district_counts);
    j.key("building_counts_by_type"); write_count_object(j, building_counts);
    j.key("deposit_counts_by_type"); write_count_object(j, deposit_counts);
    j.key("species_counts_by_id"); write_species_counts(j, species_counts);
    j.key("pop_category_counts");
    if (colony_rollup) write_number_object(j, colony_rollup->pop_category_counts);
    else { j.begin_object(); j.end_object(); }
    j.key("job_counts_by_type");
    if (colony_rollup) write_count_object(j, colony_rollup->job_counts_by_type);
    else { j.begin_object(); j.end_object(); }
    j.key("active_job_counts_by_type");
    if (colony_rollup) write_count_object(j, colony_rollup->job_counts_by_type);
    else { j.begin_object(); j.end_object(); }
    j.key("workforce_by_job_type");
    if (colony_rollup) write_number_object(j, colony_rollup->workforce_by_job_type);
    else { j.begin_object(); j.end_object(); }
    j.key("species_counts_by_name");
    if (colony_rollup) write_number_object(j, colony_rollup->species_counts_by_name);
    else { j.begin_object(); j.end_object(); }
    if (!dominant_species_id.empty()) { j.key("dominant_species_id"); write_id(j, dominant_species_id); }
    if (!dominant_species_name.empty()) { j.key("dominant_species_name"); j.value(dominant_species_name); }
    j.key("species_count"); j.raw_number(std::to_string(species_counts.size()));
    j.key("warning_count"); j.raw_number("0");

    j.key("presentation_card");
    j.begin_object();
    j.key("title"); j.value(!planet_name.empty() ? planet_name : planet_id);
    std::vector<std::string> subtitle_parts;
    if (!designation.empty()) subtitle_parts.push_back(display_token(designation));
    if (!planet_class.empty()) subtitle_parts.push_back(display_token(planet_class));
    if (!planet_size.empty()) subtitle_parts.push_back("Size " + planet_size);
    std::string subtitle;
    for (size_t i = 0; i < subtitle_parts.size(); ++i) {
        if (i) subtitle += " | ";
        subtitle += subtitle_parts[i];
    }
    j.key("subtitle"); j.value(subtitle);
    j.key("system"); j.value(system_name);
    j.key("role"); j.value(role);
    j.key("primary_metric_label"); j.value("Pops");
    j.key("primary_metric_value");
    if (const PdxValue* pops = child(planet, "num_sapient_pops")) write_pdx_as_schema_json(j, pops, "num_sapient_pops");
    else j.value(nullptr);
    j.key("secondary_metrics");
    j.begin_object();
    write_metric_or_null(j, planet, "stability");
    write_metric_or_null(j, planet, "crime");
    write_metric_or_null(j, planet, "free_housing");
    write_metric_or_null(j, planet, "free_amenities");
    j.end_object();
    j.end_object();

    j.end_object();
}

void write_planet(JsonWriter& j, const std::string& planet_id, const PdxValue* planet, const SaveIndexes& ix, const Settings& st, const DefinitionIndex* defs, const std::string& capital_id, const ColonyDemographicRollup* colony_rollup, std::set<std::string>& referenced_species, std::set<std::string>& referenced_leaders, const std::string& game_date, NameDiagnostics* diagnostics = nullptr) {
    j.begin_object();
    j.key("planet_id"); write_id(j, planet_id);
    write_name_text(j, localized_name(child(planet, "name")), diagnostics, "planet", planet_id, "colonies[].name");
    json_optional_scalar(j, planet, "planet_class");
    json_optional_scalar(j, planet, "planet_size");
    json_optional_scalar(j, planet, "owner");
    json_optional_scalar(j, planet, "controller");
    json_optional_scalar(j, planet, "original_owner");
    json_optional_scalar(j, planet, "final_designation");
    json_optional_scalar(j, planet, "designation");
    json_optional_scalar(j, planet, "orbit");
    if (const PdxValue* coord = child(planet, "coordinate")) { j.key("coordinate"); write_pdx_as_schema_json(j, coord, "coordinate"); }

    j.key("planet_stats");
    j.begin_object();
    for (const std::string& k : {"stability", "crime", "amenities", "amenities_usage", "free_amenities", "free_housing", "total_housing", "housing_usage", "employable_pops", "num_sapient_pops", "ascension_tier"}) {
        if (const PdxValue* v = child(planet, k)) { j.key(k); write_pdx_as_schema_json(j, v, k); }
    }
    j.end_object();

    if (const PdxValue* species_info = child(planet, "species_information")) {
        j.key("species_information"); write_pdx_as_schema_json(j, species_info, "species_information");
        for (const auto& e : species_info->entries) if (!e.key.empty()) referenced_species.insert(e.key);
    }
    for (const std::string& sid : scalar_id_list_from_child(planet, "species_refs")) referenced_species.insert(sid);
    for (const std::string& sid : scalar_id_list_from_child(planet, "enslaved_species_refs")) referenced_species.insert(sid);

    j.key("districts");
    j.begin_array();
    for (const std::string& did : scalar_id_list_from_child(planet, "districts")) {
        auto it = ix.districts.find(did);
        if (it == ix.districts.end()) { j.begin_object(); j.key("district_id"); write_id(j, did); j.key("resolved"); j.value(false); j.end_object(); continue; }
        j.begin_object();
        j.key("district_id"); write_id(j, did);
        std::string type = scalar_or(child(it->second, "type"));
        j.key("type"); j.value(type);
        if (defs && !type.empty()) { j.key("definition_source"); write_definition_source(j, defs, type); }
        json_optional_scalar(j, it->second, "level");
        j.key("zones");
        j.begin_array();
        for (const std::string& zid : scalar_id_list_from_child(it->second, "zones")) {
            auto zit = ix.zones.find(zid);
            j.begin_object();
            j.key("zone_id"); write_id(j, zid);
            if (zit != ix.zones.end()) {
                json_optional_scalar(j, zit->second, "type");
                j.key("buildings");
                j.begin_array();
                for (const std::string& bid : scalar_id_list_from_child(zit->second, "buildings")) {
                    auto bit = ix.buildings.find(bid);
                    if (bit != ix.buildings.end()) write_instance_with_type(j, bid, bit->second, "building_id", st, defs);
                    else { j.begin_object(); j.key("building_id"); write_id(j, bid); j.key("resolved"); j.value(false); j.end_object(); }
                }
                j.end_array();
            } else {
                j.key("resolved"); j.value(false);
            }
            j.end_object();
        }
        j.end_array();
        if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
        if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_schema_json(j, it->second, "raw"); }
        j.end_object();
    }
    j.end_array();

    j.key("buildings_cache");
    j.begin_array();
    for (const std::string& bid : scalar_id_list_from_child(planet, "buildings_cache")) {
        auto it = ix.buildings.find(bid);
        if (it != ix.buildings.end()) write_instance_with_type(j, bid, it->second, "building_id", st, defs);
        else { j.begin_object(); j.key("building_id"); write_id(j, bid); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_array();

    j.key("deposits");
    j.begin_array();
    for (const std::string& did : scalar_id_list_from_child(planet, "deposits")) {
        auto it = ix.deposits.find(did);
        if (it != ix.deposits.end()) write_instance_with_type(j, did, it->second, "deposit_id", st, defs);
        else { j.begin_object(); j.key("deposit_id"); write_id(j, did); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_array();

    j.key("pop_groups");
    j.begin_array();
    for (const std::string& pgid : scalar_id_list_from_child(planet, "pop_groups")) {
        auto it = ix.pop_groups.find(pgid);
        j.begin_object();
        j.key("pop_group_id"); write_id(j, pgid);
        if (it != ix.pop_groups.end()) {
            if (const PdxValue* key = child(it->second, "key")) {
                j.key("key"); write_pdx_as_schema_json(j, key, "key");
                std::string sid = scalar_or(child(key, "species"));
                if (!sid.empty()) referenced_species.insert(sid);
            }
            for (const std::string& k : {"planet", "size", "fraction", "habitability", "happiness", "power", "crime", "amenities_usage", "housing_usage", "last_month_growth", "month_start_size"}) {
                if (const PdxValue* v = child(it->second, k)) { j.key(k); write_pdx_as_schema_json(j, v, k); }
            }
            if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
            if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_schema_json(j, it->second, "raw"); }
        } else {
            j.key("resolved"); j.value(false);
        }
        j.end_object();
    }
    j.end_array();

    j.key("jobs");
    j.begin_array();
    for (const std::string& jid : scalar_id_list_from_child(planet, "pop_jobs")) {
        auto it = ix.pop_jobs.find(jid);
        if (it == ix.pop_jobs.end() || !is_exportable_pop_job(it->second)) continue;
        j.begin_object();
        j.key("job_id"); write_id(j, jid);
        std::string type = scalar_or(child(it->second, "type"));
        j.key("type"); j.value(type);
        if (defs && !type.empty()) { j.key("definition_source"); write_definition_source(j, defs, type); }
        for (const std::string& k : {"workforce", "max_workforce", "bonus_workforce", "workforce_limit", "automated_workforce", "planet"}) {
            if (const PdxValue* v = child(it->second, k)) { j.key(k); write_pdx_as_schema_json(j, v, k); }
        }
        if (const PdxValue* pg = child(it->second, "pop_groups")) { j.key("pop_groups"); write_pdx_as_schema_json(j, pg, "pop_groups"); }
        if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
        if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_schema_json(j, it->second, "raw"); }
        j.end_object();
    }
    j.end_array();

    j.key("economy");
    j.begin_object();
    for (const std::string& k : {"produces", "upkeep", "profits", "trade_value"}) {
        if (const PdxValue* v = child(planet, k)) { j.key(k); write_pdx_as_schema_json(j, v, k); }
    }
    j.end_object();

    j.key("construction");
    j.begin_object();
    for (const std::string& qk : {"build_queue", "army_build_queue"}) {
        std::string qid = scalar_or(child(planet, qk));
        j.key(qk);
        if (qid.empty()) { j.value(nullptr); continue; }
        auto qit = ix.construction_queues.find(qid);
        j.begin_object();
        j.key("queue_id"); write_id(j, qid);
        if (qit != ix.construction_queues.end()) {
            j.key("queue"); write_pdx_as_schema_json(j, qit->second, "queue");
            j.key("items_resolved");
            j.begin_array();
            for (const std::string& item_id : scalar_id_list_from_child(qit->second, "items")) {
                auto iit = ix.construction_items.find(item_id);
                j.begin_object();
                j.key("item_id"); write_id(j, item_id);
                if (iit != ix.construction_items.end()) {
                    j.key("resolved"); j.value(true);
                    j.key("item");
                    write_pdx_as_schema_json(j, iit->second, "item");
                } else {
                    j.key("resolved"); j.value(false);
                }
                j.end_object();
            }
            j.end_array();
        } else {
            j.key("resolved"); j.value(false);
        }
        j.end_object();
    }
    j.end_object();

    std::string governor = scalar_or(child(planet, "governor"));
    if (!governor.empty() && governor != "4294967295") {
        referenced_leaders.insert(governor);
        j.key("governor");
        auto git = ix.leaders.find(governor);
        if (git != ix.leaders.end()) write_resolved_leader(j, governor, git->second, st, ix, defs, game_date, diagnostics);
        else { j.begin_object(); j.key("leader_id"); write_id(j, governor); j.key("resolved"); j.value(false); j.end_object(); }
    }

    j.key("armies");
    j.begin_array();
    for (const std::string& aid : scalar_id_list_from_child(planet, "army")) {
        auto it = ix.armies.find(aid);
        if (it != ix.armies.end() && is_defense_army(it->second)) continue;
        j.begin_object();
        j.key("army_id"); write_id(j, aid);
        if (it != ix.armies.end()) {
            write_name_text(j, localized_name(child(it->second, "name")), diagnostics, "army_formation", aid, "colonies[].armies[].name");
            std::string type = scalar_or(child(it->second, "type"));
            j.key("type"); j.value(type);
            if (defs && !type.empty()) { j.key("definition_source"); write_definition_source(j, defs, type); }
            for (const std::string& k : {"health", "max_health", "morale", "owner", "species", "planet", "spawning_planet"}) {
                if (const PdxValue* v = child(it->second, k)) { j.key(k); write_pdx_as_schema_json(j, v, k); }
            }
            std::string sid = scalar_or(child(it->second, "species"));
            if (!sid.empty()) referenced_species.insert(sid);
            if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
        } else {
            j.key("resolved"); j.value(false);
        }
        j.end_object();
    }
    j.end_array();

    if (const PdxValue* mods = child(planet, "timed_modifier")) { j.key("timed_modifiers"); write_pdx_as_schema_json(j, mods, "timed_modifiers"); }
    if (const PdxValue* mods = child(planet, "planet_modifier")) { j.key("planet_modifiers"); write_pdx_as_schema_json(j, mods, "planet_modifiers"); }
    if (const PdxValue* flags = child(planet, "flags")) { j.key("flags"); write_pdx_as_schema_json(j, flags, "flags"); }

    write_colony_derived_summary(j, planet_id, planet, ix, colony_rollup, capital_id, diagnostics);

    if (st.include_source_locations) { j.key("source"); write_source(j, planet); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_schema_json(j, planet, "raw"); }
    j.end_object();
}

bool is_non_military_ship_class(const std::string& ship_class) {
    static const std::set<std::string> excluded = {
        "shipclass_starbase",
        "shipclass_mining_station",
        "shipclass_research_station",
        "shipclass_science_ship",
        "shipclass_constructor",
        "shipclass_colonizer",
        "shipclass_transport",
        "shipclass_military_transport",
    };
    return excluded.find(ship_class) != excluded.end();
}

bool fleet_has_non_military_flag(const PdxValue* fleet) {
    return pdx_truthy(child(fleet, "station")) ||
           pdx_truthy(child(fleet, "orbital_station")) ||
           pdx_truthy(child(fleet, "civilian"));
}

std::set<std::string> fleet_ship_classes(const PdxValue* fleet, const SaveIndexes& ix) {
    std::set<std::string> classes;
    const std::string direct = scalar_or(child(fleet, "ship_class"));
    if (!direct.empty()) classes.insert(direct);
    for (const std::string& sid : scalar_id_list_from_child(fleet, "ships")) {
        auto sit = ix.ships.find(sid);
        if (sit == ix.ships.end()) continue;
        const std::string ship_class = scalar_or(child(sit->second, "ship_class"));
        if (!ship_class.empty()) classes.insert(ship_class);
    }
    return classes;
}

bool is_dashboard_military_fleet(const PdxValue* fleet, const SaveIndexes& ix) {
    if (!fleet) return false;
    if (fleet_has_non_military_flag(fleet)) return false;

    const std::set<std::string> classes = fleet_ship_classes(fleet, ix);
    for (const std::string& ship_class : classes) {
        if (is_non_military_ship_class(ship_class)) return false;
    }

    const double military_power = scalar_double(child(fleet, "military_power")).value_or(0.0);
    const double combat_power = scalar_double(child(fleet, "combat_power")).value_or(0.0);
    const double fleet_size = scalar_double(child(fleet, "fleet_size")).value_or(0.0);
    const double command_limit = scalar_double(child(fleet, "command_limit")).value_or(0.0);
    if (military_power == 0.0 && combat_power == 0.0 && fleet_size == 0.0 && command_limit == 0.0) return false;
    return true;
}

struct StatRollup {
    double total = 0.0;
    size_t count = 0;

    void add(std::optional<double> value) {
        if (!value) return;
        total += *value;
        count++;
    }
};

struct MilitaryRollup {
    size_t fleet_count = 0;
    size_t ship_count = 0;
    size_t resolved_ship_count = 0;
    size_t unresolved_ship_count = 0;
    size_t unresolved_ship_references = 0;
    size_t unresolved_design_references = 0;
    std::map<std::string, size_t> ship_sizes;
    std::map<std::string, size_t> ship_classes;
    std::map<std::string, size_t> design_counts;
    std::map<std::string, size_t> component_counts;
    std::map<std::string, size_t> weapon_component_counts;
    std::map<std::string, size_t> utility_component_counts;
    StatRollup hull;
    StatRollup max_hull;
    StatRollup armor;
    StatRollup max_armor;
    StatRollup shields;
    StatRollup max_shields;
    StatRollup experience;
    std::set<std::string> referenced_design_ids;
    std::set<std::string> resolved_design_ids;
};

std::string non_sentinel_id(const std::string& id) {
    return (id.empty() || id == "4294967295") ? "" : id;
}

std::string ship_design_id(const PdxValue* ship) {
    return non_sentinel_id(scalar_or(nested_child(ship, {"ship_design_implementation", "design"})));
}

std::string ship_upgrade_design_id(const PdxValue* ship) {
    return non_sentinel_id(scalar_or(nested_child(ship, {"ship_design_implementation", "upgrade"})));
}

const PdxValue* first_growth_stage(const PdxValue* design) {
    const PdxValue* stages = child(design, "growth_stages");
    if (!stages || stages->kind != PdxValue::Kind::Container) return nullptr;
    for (const auto& e : stages->entries) {
        if (e.key.empty() && e.value && e.value->kind == PdxValue::Kind::Container) return e.value;
    }
    return nullptr;
}

std::string design_ship_size(const PdxValue* design) {
    return scalar_or(child(first_growth_stage(design), "ship_size"));
}

std::string component_kind_for_slot(const std::string& slot) {
    std::string s = slot;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (const std::string& token : {"GUN", "WEAPON", "TORPEDO", "MISSILE", "HANGAR", "POINT_DEFENSE", "POINT_DEFENCE"}) {
        if (s.find(token) != std::string::npos) return "weapon";
    }
    for (const std::string& token : {"UTILITY", "AUX"}) {
        if (s.find(token) != std::string::npos) return "utility";
    }
    return "unknown";
}

std::string generated_key_display_name(std::string key) {
    key = trim(key);
    for (const std::string& marker : {"_SHIP_", "_CLASS_", "_PLANET_"}) {
        const size_t pos = key.find(marker);
        if (pos != std::string::npos && pos + marker.size() < key.size()) {
            key = key.substr(pos + marker.size());
            break;
        }
    }
    if (starts_with_ci(key, "NAME_") && key.size() > 5) key = key.substr(5);
    std::replace(key.begin(), key.end(), '_', ' ');
    return trim(key);
}

std::string localised_variable_key(const PdxValue* vars, const std::string& wanted_key) {
    if (!vars || vars->kind != PdxValue::Kind::Container) return "";
    for (const auto& e : vars->entries) {
        if (!e.key.empty() || !e.value || e.value->kind != PdxValue::Kind::Container) continue;
        if (scalar_or(child(e.value, "key")) != wanted_key) continue;
        const PdxValue* value = child(e.value, "value");
        return scalar_or(child(value, "key"), localized_name(value));
    }
    return "";
}

std::string expanded_localized_name(const PdxValue* name_node) {
    const std::string key = localized_name(name_node);
    if (key != "SUFFIX_NAME_FORMAT") return key;

    const PdxValue* vars = child(name_node, "variables");
    const std::string base_key = localised_variable_key(vars, "NAME");
    const std::string suffix_key = localised_variable_key(vars, "SUFFIX");
    std::string base = is_generated_name_key(base_key) ? generated_key_display_name(base_key) : base_key;
    std::string suffix = is_generated_name_key(suffix_key) ? generated_key_display_name(suffix_key) : suffix_key;
    const std::string joined = trim(base + (suffix.empty() ? "" : " " + suffix));
    return joined.empty() ? key : joined;
}

void write_ship_display_name(JsonWriter& j,
                             const PdxValue* name_node,
                             NameDiagnostics* diagnostics,
                             const std::string& ship_id) {
    const std::string raw = localized_name(name_node);
    const std::string display = expanded_localized_name(name_node);
    if (raw == "SUFFIX_NAME_FORMAT") {
        j.key("name_raw"); j.value(raw);
        j.key("name"); j.value(display);
        j.key("name_generated_from_key"); j.value(true);
        if (diagnostics) diagnostics->generated_kinds["ship"]++;
        return;
    }
    if (is_generated_name_key(raw)) {
        j.key("name_raw"); j.value(raw);
        j.key("name"); j.value(generated_key_display_name(raw));
        j.key("name_generated_from_key"); j.value(true);
        if (diagnostics) diagnostics->add_generated("ship", raw);
        return;
    }
    write_name_text(j, display, diagnostics, "ship", ship_id, "fleets[].ships[].name");
}

void write_design_display_name(JsonWriter& j,
                               const PdxValue* name_node,
                               NameDiagnostics* diagnostics,
                               const std::string& design_id) {
    const std::string raw = localized_name(name_node);
    if (is_generated_name_key(raw)) {
        j.key("name_raw"); j.value(raw);
        j.key("name"); j.value(generated_key_display_name(raw));
        j.key("name_generated_from_key"); j.value(true);
        if (diagnostics) diagnostics->add_generated("ship_design", raw);
        return;
    }
    write_name_text(j, raw, diagnostics, "ship_design", design_id, "ship_designs[].name");
}

void write_optional_number_alias(JsonWriter& j, const PdxValue* obj, const std::string& source_key, const std::string& output_key) {
    if (const PdxValue* v = child(obj, source_key)) {
        j.key(output_key);
        write_pdx_as_schema_json(j, v, output_key);
    }
}

void write_ship_sections(JsonWriter& j, const PdxValue* ship) {
    const std::vector<const PdxValue*> sections = children(ship, "section");
    if (sections.empty()) return;
    j.key("sections");
    j.begin_array();
    for (const PdxValue* section : sections) {
        j.begin_object();
        write_optional_child(j, section, "slot", "slot");
        write_optional_child(j, section, "design", "design");
        const std::vector<const PdxValue*> weapons = children(section, "weapon");
        if (!weapons.empty()) {
            j.key("weapons");
            j.begin_array();
            for (const PdxValue* weapon : weapons) {
                j.begin_object();
                write_optional_child(j, weapon, "index", "index");
                write_optional_child(j, weapon, "template", "template");
                write_optional_child(j, weapon, "component_slot", "component_slot");
                j.end_object();
            }
            j.end_array();
        }
        j.end_object();
    }
    j.end_array();
}

void accumulate_design_components(const PdxValue* design, MilitaryRollup& rollup) {
    const PdxValue* stage = first_growth_stage(design);
    if (!stage) return;
    for (const PdxValue* section : children(stage, "section")) {
        for (const PdxValue* component : children(section, "component")) {
            const std::string slot = scalar_or(child(component, "slot"));
            const std::string templ = scalar_or(child(component, "template"));
            if (templ.empty()) continue;
            const std::string kind = component_kind_for_slot(slot);
            rollup.component_counts[templ]++;
            if (kind == "weapon") rollup.weapon_component_counts[templ]++;
            else if (kind == "utility") rollup.utility_component_counts[templ]++;
        }
    }
    for (const PdxValue* required : children(stage, "required_component")) {
        const std::string templ = scalar_or(required);
        if (!templ.empty()) rollup.component_counts[templ]++;
    }
}

void merge_rollup(MilitaryRollup& total, const MilitaryRollup& fleet) {
    total.fleet_count += fleet.fleet_count;
    total.ship_count += fleet.ship_count;
    total.resolved_ship_count += fleet.resolved_ship_count;
    total.unresolved_ship_count += fleet.unresolved_ship_count;
    total.unresolved_ship_references += fleet.unresolved_ship_references;
    total.unresolved_design_references += fleet.unresolved_design_references;
    auto add_counts = [](std::map<std::string, size_t>& dst, const std::map<std::string, size_t>& src) {
        for (const auto& [k, v] : src) dst[k] += v;
    };
    add_counts(total.ship_sizes, fleet.ship_sizes);
    add_counts(total.ship_classes, fleet.ship_classes);
    add_counts(total.design_counts, fleet.design_counts);
    add_counts(total.component_counts, fleet.component_counts);
    add_counts(total.weapon_component_counts, fleet.weapon_component_counts);
    add_counts(total.utility_component_counts, fleet.utility_component_counts);
    auto add_stat = [](StatRollup& dst, const StatRollup& src) { dst.total += src.total; dst.count += src.count; };
    add_stat(total.hull, fleet.hull);
    add_stat(total.max_hull, fleet.max_hull);
    add_stat(total.armor, fleet.armor);
    add_stat(total.max_armor, fleet.max_armor);
    add_stat(total.shields, fleet.shields);
    add_stat(total.max_shields, fleet.max_shields);
    add_stat(total.experience, fleet.experience);
    total.referenced_design_ids.insert(fleet.referenced_design_ids.begin(), fleet.referenced_design_ids.end());
    total.resolved_design_ids.insert(fleet.resolved_design_ids.begin(), fleet.resolved_design_ids.end());
}

void write_military_rollup_summary(JsonWriter& j, const MilitaryRollup& rollup) {
    j.begin_object();
    j.key("ship_count"); j.raw_number(std::to_string(rollup.ship_count));
    j.key("resolved_ship_count"); j.raw_number(std::to_string(rollup.resolved_ship_count));
    j.key("unresolved_ship_count"); j.raw_number(std::to_string(rollup.unresolved_ship_count));
    if (!rollup.ship_sizes.empty()) { j.key("ship_sizes"); write_count_object(j, rollup.ship_sizes); }
    if (!rollup.ship_classes.empty()) { j.key("ship_classes"); write_count_object(j, rollup.ship_classes); }
    if (!rollup.design_counts.empty()) { j.key("design_counts"); write_count_object(j, rollup.design_counts); }
    if (!rollup.component_counts.empty()) { j.key("component_counts"); write_count_object(j, rollup.component_counts); }
    if (!rollup.weapon_component_counts.empty()) { j.key("weapon_component_counts"); write_count_object(j, rollup.weapon_component_counts); }
    if (!rollup.utility_component_counts.empty()) { j.key("utility_component_counts"); write_count_object(j, rollup.utility_component_counts); }
    if (rollup.hull.count) { j.key("total_hull"); j.raw_number(json_number(rollup.hull.total)); }
    if (rollup.max_hull.count) { j.key("total_max_hull"); j.raw_number(json_number(rollup.max_hull.total)); }
    if (rollup.armor.count) { j.key("total_armor"); j.raw_number(json_number(rollup.armor.total)); }
    if (rollup.max_armor.count) { j.key("total_max_armor"); j.raw_number(json_number(rollup.max_armor.total)); }
    if (rollup.shields.count) { j.key("total_shields"); j.raw_number(json_number(rollup.shields.total)); }
    if (rollup.max_shields.count) { j.key("total_max_shields"); j.raw_number(json_number(rollup.max_shields.total)); }
    if (rollup.experience.count) {
        j.key("average_experience");
        j.raw_number(json_number(rollup.experience.total / static_cast<double>(rollup.experience.count)));
    }
    j.key("estimated_resource_value");
    j.begin_object();
    j.key("available"); j.value(false);
    j.key("reason"); j.value("component cost catalog not loaded");
    j.end_object();
    j.end_object();
}

void write_ship_object(JsonWriter& j,
                       const std::string& ship_id,
                       const PdxValue* ship,
                       const std::string& fallback_fleet_id,
                       const SaveIndexes& ix,
                       const Settings& st,
                       std::set<std::string>& referenced_leaders,
                       NameDiagnostics* diagnostics,
                       std::vector<UnresolvedReference>* unresolved_refs,
                       MilitaryRollup& rollup) {
    (void)st;
    j.begin_object();
    j.key("ship_id"); write_id(j, ship_id);
    if (!ship) {
        j.key("resolved"); j.value(false);
        rollup.unresolved_ship_count++;
        rollup.unresolved_ship_references++;
        if (unresolved_refs) unresolved_refs->push_back(UnresolvedReference{"unresolved_ship_reference", ship_id, "fleets[].ships", ""});
        j.end_object();
        return;
    }

    rollup.resolved_ship_count++;
    const std::string fleet_id = non_sentinel_id(scalar_or(child(ship, "fleet"), fallback_fleet_id));
    if (!fleet_id.empty()) { j.key("fleet_id"); write_id(j, fleet_id); }
    write_ship_display_name(j, child(ship, "name"), diagnostics, ship_id);

    const std::string design_id = ship_design_id(ship);
    if (!design_id.empty()) {
        j.key("design_id"); write_id(j, design_id);
        rollup.design_counts[design_id]++;
        rollup.referenced_design_ids.insert(design_id);
    }
    const std::string upgrade_design_id = ship_upgrade_design_id(ship);
    if (!upgrade_design_id.empty()) { j.key("upgrade_design_id"); write_id(j, upgrade_design_id); }

    write_optional_child(j, ship, "ship_class", "ship_class");
    const PdxValue* design = nullptr;
    if (!design_id.empty()) {
        auto dit = ix.ship_designs.find(design_id);
        if (dit != ix.ship_designs.end()) {
            design = dit->second;
            rollup.resolved_design_ids.insert(design_id);
            const std::string ship_size = design_ship_size(design);
            if (!ship_size.empty()) {
                j.key("ship_size"); j.value(ship_size);
                rollup.ship_sizes[ship_size]++;
            }
            accumulate_design_components(design, rollup);
        } else {
            rollup.unresolved_design_references++;
            if (unresolved_refs) unresolved_refs->push_back(UnresolvedReference{"unresolved_design_reference", design_id, "fleets[].ships[].design_id", ""});
        }
    }

    const std::string ship_class = scalar_or(child(ship, "ship_class"));
    if (!ship_class.empty()) rollup.ship_classes[ship_class]++;
    write_optional_child(j, ship, "graphical_culture", "graphical_culture");
    write_optional_child(j, ship, "experience", "experience");
    write_optional_child(j, ship, "rank", "rank");
    write_optional_child(j, ship, "veterancy", "veterancy");
    const std::string leader = non_sentinel_id(scalar_or(child(ship, "leader")));
    if (!leader.empty()) {
        referenced_leaders.insert(leader);
        j.key("leader_id"); write_id(j, leader);
    }
    write_optional_child(j, ship, "construction_date", "construction_date");
    write_optional_number_alias(j, ship, "hitpoints", "hull");
    write_optional_number_alias(j, ship, "max_hitpoints", "max_hull");
    write_optional_number_alias(j, ship, "armor_hitpoints", "armor");
    write_optional_number_alias(j, ship, "max_armor_hitpoints", "max_armor");
    write_optional_number_alias(j, ship, "shield_hitpoints", "shields");
    write_optional_number_alias(j, ship, "max_shield_hitpoints", "max_shields");
    write_ship_sections(j, ship);

    rollup.hull.add(scalar_double(child(ship, "hitpoints")));
    rollup.max_hull.add(scalar_double(child(ship, "max_hitpoints")));
    rollup.armor.add(scalar_double(child(ship, "armor_hitpoints")));
    rollup.max_armor.add(scalar_double(child(ship, "max_armor_hitpoints")));
    rollup.shields.add(scalar_double(child(ship, "shield_hitpoints")));
    rollup.max_shields.add(scalar_double(child(ship, "max_shield_hitpoints")));
    rollup.experience.add(scalar_double(child(ship, "experience")));
    j.end_object();
}

void write_fleet(JsonWriter& j,
                 const std::string& fleet_id,
                 const PdxValue* fleet,
                 const SaveIndexes& ix,
                 const Settings& st,
                 const DefinitionIndex* defs,
                 const std::string& game_date,
                 std::set<std::string>& referenced_leaders,
                 NameDiagnostics* diagnostics,
                 std::vector<UnresolvedReference>* unresolved_refs,
                 MilitaryRollup& total_rollup) {
    (void)defs;
    MilitaryRollup fleet_rollup;
    fleet_rollup.fleet_count = 1;
    j.begin_object();
    j.key("fleet_id"); write_id(j, fleet_id);
    write_name_text(j, localized_name(child(fleet, "name")), diagnostics, "fleet", fleet_id, "fleets[].name");
    json_optional_scalar(j, fleet, "owner");
    if (const std::string fleet_template_id = non_sentinel_id(scalar_or(child(fleet, "fleet_template"))); !fleet_template_id.empty()) {
        j.key("fleet_template_id"); write_id(j, fleet_template_id);
    }
    json_optional_scalar(j, fleet, "ship_class");
    json_optional_scalar(j, fleet, "military_power");
    json_optional_scalar(j, fleet, "combat_power");
    json_optional_scalar(j, fleet, "fleet_size");
    json_optional_scalar(j, fleet, "command_limit");
    json_optional_scalar(j, fleet, "diplomacy_weight");
    json_optional_scalar(j, fleet, "hit_points");
    json_optional_scalar(j, fleet, "weapon");
    json_optional_scalar(j, fleet, "mobile");
    json_optional_scalar(j, fleet, "valid_for_combat");
    json_optional_scalar(j, fleet, "ground_support_stance");
    const std::set<std::string> ship_classes = fleet_ship_classes(fleet, ix);
    j.key("ship_classes");
    j.begin_array();
    for (const std::string& ship_class : ship_classes) j.value(ship_class);
    j.end_array();
    std::string admiral = scalar_or(child(fleet, "leader"));
    if (admiral.empty()) admiral = scalar_or(child(fleet, "commander"));
    if (!admiral.empty() && admiral != "4294967295") {
        referenced_leaders.insert(admiral);
        j.key("commander");
        auto it = ix.leaders.find(admiral);
        if (it != ix.leaders.end()) write_resolved_leader(j, admiral, it->second, st, ix, defs, game_date, diagnostics);
        else { j.begin_object(); j.key("leader_id"); write_id(j, admiral); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.key("ships");
    j.begin_array();
    for (const std::string& sid : scalar_id_list_from_child(fleet, "ships")) {
        fleet_rollup.ship_count++;
        auto sit = ix.ships.find(sid);
        write_ship_object(j, sid, sit == ix.ships.end() ? nullptr : sit->second, fleet_id, ix, st, referenced_leaders, diagnostics, unresolved_refs, fleet_rollup);
    }
    j.end_array();
    j.key("summary");
    write_military_rollup_summary(j, fleet_rollup);
    merge_rollup(total_rollup, fleet_rollup);
    if (st.include_source_locations) { j.key("source"); write_source(j, fleet); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_schema_json(j, fleet, "raw"); }
    j.end_object();
}

void write_ship_designs(JsonWriter& j,
                        const MilitaryRollup& military_rollup,
                        const SaveIndexes& ix,
                        const Settings& st,
                        NameDiagnostics* diagnostics) {
    j.key("ship_designs");
    j.begin_object();
    for (const std::string& design_id : military_rollup.referenced_design_ids) {
        j.key(design_id);
        auto dit = ix.ship_designs.find(design_id);
        if (dit == ix.ship_designs.end()) {
            j.begin_object();
            j.key("design_id"); write_id(j, design_id);
            j.key("resolved"); j.value(false);
            j.key("resource_cost");
            j.begin_object();
            j.key("available"); j.value(false);
            j.key("reason"); j.value("component cost catalog not loaded");
            j.end_object();
            j.end_object();
            continue;
        }

        const PdxValue* design = dit->second;
        const PdxValue* stage = first_growth_stage(design);
        j.begin_object();
        j.key("design_id"); write_id(j, design_id);
        write_design_display_name(j, child(design, "name"), diagnostics, design_id);
        write_optional_child(j, design, "graphical_culture", "graphical_culture");
        if (const std::string ship_size = design_ship_size(design); !ship_size.empty()) {
            j.key("ship_size"); j.value(ship_size);
        }
        if (stage) {
            const std::vector<const PdxValue*> sections = children(stage, "section");
            if (!sections.empty()) {
                j.key("sections");
                j.begin_array();
                for (const PdxValue* section : sections) {
                    j.begin_object();
                    write_optional_child(j, section, "slot", "slot");
                    write_optional_child(j, section, "template", "template");
                    const std::vector<const PdxValue*> components = children(section, "component");
                    if (!components.empty()) {
                        j.key("components");
                        j.begin_array();
                        for (const PdxValue* component : components) {
                            j.begin_object();
                            const std::string slot = scalar_or(child(component, "slot"));
                            const std::string templ = scalar_or(child(component, "template"));
                            if (!slot.empty()) { j.key("slot"); j.value(slot); }
                            if (!templ.empty()) { j.key("template"); j.value(templ); }
                            j.key("component_kind"); j.value(component_kind_for_slot(slot));
                            j.end_object();
                        }
                        j.end_array();
                    }
                    j.end_object();
                }
                j.end_array();
            }
            const std::vector<const PdxValue*> required_components = children(stage, "required_component");
            if (!required_components.empty()) {
                j.key("required_components");
                j.begin_array();
                for (const PdxValue* component : required_components) {
                    const std::string templ = scalar_or(component);
                    if (!templ.empty()) j.value(templ);
                }
                j.end_array();
            }
        }
        j.key("resource_cost");
        j.begin_object();
        j.key("available"); j.value(false);
        j.key("reason"); j.value("component cost catalog not loaded");
        j.end_object();
        if (st.include_source_locations) { j.key("source"); write_source(j, design); }
        j.end_object();
    }
    j.end_object();
}

bool is_normal_country_for_v1(const PdxValue* c) {
    const std::string type = scalar_or(child(c, "type"));
    if (type != "default") return false;
    if (!child(c, "name")) return false;
    // Normal empires usually have a capital or owned planets. This filters many internal countries.
    if (child(c, "capital")) return true;
    if (child(c, "owned_planets")) return true;
    return false;
}

std::vector<std::string> select_country_ids(const Settings& st, const SaveIndexes& ix) {
    std::vector<std::string> ids;
    if (st.parse_all_nations) {
        for (const auto& [id, c] : ix.countries) {
            if (is_normal_country_for_v1(c)) ids.push_back(id);
            else if (st.include_all_special_nations) ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), [](const std::string& a, const std::string& b) {
            try { return std::stoll(a) < std::stoll(b); } catch (...) { return a < b; }
        });
        return ids;
    }
    if (st.player_only) {
        std::string pid = detect_player_country_id(ix.root);
        if (!pid.empty()) ids.push_back(pid);
        return ids;
    }
    return st.nation_ids;
}

struct ArmyFormation {
    std::string formation_name;
    std::string owner;
    std::string planet;
    std::map<std::string, size_t> composition_by_type;
    std::map<std::string, size_t> composition_by_species;
    std::vector<std::string> army_ids;
    double total_health = 0.0;
    double total_morale = 0.0;
    double total_experience = 0.0;
    size_t health_count = 0;
    size_t morale_count = 0;
    size_t experience_count = 0;
};

struct ArmyExportContext {
    std::vector<ArmyFormation> formations;
    std::vector<std::string> non_defense_army_ids;
    std::vector<std::string> unresolved_army_ids;
    size_t defense_armies_suppressed = 0;
};

ArmyExportContext build_army_export_context(const PdxValue* country, const SaveIndexes& ix);
void write_army_formations(JsonWriter& j, const ArmyExportContext& army_context, NameDiagnostics* diagnostics = nullptr);

void write_capital_planet_stub(JsonWriter& j,
                                      const std::string& capital_id,
                                      const PdxValue* capital,
                                      const MapExportContext& map_context,
                                      const SaveIndexes& ix,
                                      NameDiagnostics* diagnostics = nullptr) {
    if (capital_id.empty() || !capital) {
        j.value(nullptr);
        return;
    }

    const std::string system_id = !map_context.capital_system_id.empty()
        ? map_context.capital_system_id
        : scalar_or(child(child(capital, "coordinate"), "origin"));
    std::string system_name = map_context.capital_system_name;
    if (system_name.empty()) {
        auto sys_it = ix.galactic_objects.find(system_id);
        if (sys_it != ix.galactic_objects.end()) system_name = localized_name(child(sys_it->second, "name"));
    }

    j.begin_object();
    j.key("planet_id"); write_id(j, capital_id);
    write_name_text(j, localized_name(child(capital, "name")), diagnostics, "capital_planet", capital_id, "capital_planet.name");
    j.key("system_id"); write_id(j, system_id);
    write_display_text(j, "system_name", system_name, "system_name_unresolved", diagnostics, "system", system_id, "capital_planet.system_name");
    j.end_object();
}

std::pair<CountryExportSummary, TimelinePoint> write_country_output(const fs::path& out_path,
                                 const std::string& save_file_name,
                                 const std::string& game_date,
                                 const std::string& country_id,
                                 const PdxValue* country,
                                 const SaveIndexes& ix,
                                 const Settings& st,
                                 const DefinitionIndex* defs) {
    CountryExportSummary summary;
    TimelinePoint timeline;
    summary.country_id = country_id;
    summary.country_name = get_country_name(country);
    summary.output_file = out_path;
    timeline.country_id = country_id;
    timeline.country_name = summary.country_name;
    timeline.game_date = game_date;
    timeline.save_file = save_file_name;
    timeline.output_file = out_path;
    fs::create_directories(out_path.parent_path());
    std::ofstream out(out_path, std::ios::binary);
    if (!out) throw std::runtime_error("Could not write output: " + out_path.string());
    JsonWriter j(out, st.pretty_json);
    std::set<std::string> referenced_species;
    std::set<std::string> referenced_leaders;
    std::vector<UnresolvedReference> unresolved_refs;
    NameDiagnostics name_diagnostics;
    name_diagnostics.warnings = &unresolved_refs;
    LeaderExportStats leader_stats;

    std::string founder = scalar_or(child(country, "founder_species_ref"));
    std::string built = scalar_or(child(country, "built_species_ref"));
    if (!founder.empty()) referenced_species.insert(founder);
    if (!built.empty()) referenced_species.insert(built);
    for (const std::string& sid : scalar_id_list_from_child(country, "owned_species_refs")) referenced_species.insert(sid);
    for (const std::string& sid : scalar_id_list_from_child(country, "enslaved_species_refs")) referenced_species.insert(sid);
    std::string ruler = scalar_or(child(country, "ruler"));
    if (!ruler.empty() && ruler != "4294967295") referenced_leaders.insert(ruler);

    const std::vector<std::string> owned_planets = scalar_id_list_from_child(country, "owned_planets");
    const std::vector<std::string> controlled_planets = scalar_id_list_from_child(country, "controlled_planets");
    const std::vector<std::string> owned_fleet_ids = country_owned_fleet_ids(country);
    std::string capital_id = scalar_or(child(country, "capital"));
    summary.capital_id = capital_id;
    EmpireRollups rollups = build_empire_rollups(owned_planets, ix);
    for (const auto& [sid, _] : rollups.demographics.species) referenced_species.insert(sid);
    MapExportContext map_context = build_map_export_context(owned_planets, controlled_planets, owned_fleet_ids, capital_id, ix);
    std::vector<std::string> military_fleet_ids;
    size_t suppressed_non_military_fleet_count = 0;
    MilitaryRollup military_rollup;
    for (const std::string& fid : owned_fleet_ids) {
        auto fit = ix.fleets.find(fid);
        if (fit == ix.fleets.end()) continue;
        if (is_dashboard_military_fleet(fit->second, ix)) military_fleet_ids.push_back(fid);
        else suppressed_non_military_fleet_count++;
    }
    ArmyExportContext army_context = build_army_export_context(country, ix);
    for (const ArmyFormation& formation : army_context.formations) {
        for (const auto& [species_id, _] : formation.composition_by_species) {
            if (!species_id.empty() && species_id != "unknown") referenced_species.insert(species_id);
        }
    }
    const PdxValue* stored_resources = nested_child(country, {"standard_economy_module", "resources"});
    if (!stored_resources) stored_resources = nested_child(country, {"modules", "standard_economy_module", "resources"});
    const size_t stored_resource_count = direct_named_child_count(stored_resources);
    summary.systems_exported = map_context.systems.size();

    j.begin_object();
    j.key("schema_version"); j.value("dashboard-country-v0.1");
    j.key("parser_version"); j.value(STELLARIS_PARSER_VERSION);
    j.key("save");
    j.begin_object();
    j.key("file"); j.value(save_file_name);
    j.key("game_date"); j.value(game_date);
    j.key("version"); write_pdx_as_schema_json(j, child(ix.root, "version"), "version");
    j.key("save_name"); write_pdx_as_schema_json(j, child(ix.root, "name"), "save_name");
    j.end_object();

    j.key("country");
    j.begin_object();
    j.key("country_id"); write_id(j, country_id);
    write_name_text(j, summary.country_name, &name_diagnostics, "country", country_id, "country.name");
    write_display_text(j, "adjective", localized_name(child(country, "adjective")), "adjective_unresolved", &name_diagnostics, "country_adjective", country_id, "country.adjective");
    for (const std::string& k : {"type", "personality", "capital", "starting_system", "military_power", "economy_power", "tech_power", "victory_rank", "victory_score", "fleet_size", "used_naval_capacity", "empire_size", "num_sapient_pops", "employable_pops", "starbase_capacity", "num_upgraded_starbase", "graphical_culture", "city_graphical_culture", "room"}) {
        if (const PdxValue* v = child(country, k)) { j.key(k); write_pdx_as_schema_json(j, v, k); }
    }
    j.key("founder_species_ref"); write_id(j, founder);
    j.key("built_species_ref"); write_id(j, built);
    if (const PdxValue* flag = child(country, "flag")) { j.key("flag"); write_pdx_as_schema_json(j, flag, "flag"); }
    if (const PdxValue* ethos = child(country, "ethos")) { j.key("ethics"); write_pdx_as_schema_json(j, ethos, "ethics"); }
    if (const PdxValue* gov = child(country, "government")) { j.key("government"); write_pdx_as_schema_json(j, gov, "government"); }
    if (const PdxValue* policies = child(country, "active_policies")) { j.key("active_policies"); write_pdx_as_schema_json(j, policies, "active_policies"); }
    if (const PdxValue* subjects = child(country, "subjects")) { j.key("subjects"); write_pdx_as_schema_json(j, subjects, "subjects"); }
    if (const PdxValue* trade = child(country, "trade_conversions")) { j.key("trade_conversions"); write_pdx_as_schema_json(j, trade, "trade_conversions"); }
    if (st.include_source_locations) { j.key("source"); write_source(j, country); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_schema_json(j, country, "raw"); }
    j.end_object();

    write_nat_finance_economy(j, country, stored_resources);

    j.key("capital_planet");

    auto add_unresolved = [&](const std::string& kind, const std::string& id, const std::string& ctx) {
        unresolved_refs.push_back(UnresolvedReference{kind, id, ctx, ""});
    };

    auto capital_it = ix.planets.find(capital_id);
    if (!capital_id.empty() && capital_it != ix.planets.end()) {
        summary.capital_name = localized_name(child(capital_it->second, "name"));
        write_capital_planet_stub(j, capital_id, capital_it->second, map_context, ix, &name_diagnostics);
    } else {
        if (!capital_id.empty()) add_unresolved("planet", capital_id, "country.capital");
        j.value(nullptr);
    }

    j.key("colonies");
    j.begin_array();
    summary.owned_planets = owned_planets.size();
    std::vector<std::string> colonies_missing_derived_summary;
    std::vector<std::string> colonies_missing_demographic_summary;
    for (const std::string& pid : owned_planets) {
        auto it = ix.planets.find(pid);
        if (it != ix.planets.end()) {
            const ColonyDemographicRollup* colony_rollup = nullptr;
            auto rollup_it = rollups.colonies.find(pid);
            if (rollup_it != rollups.colonies.end()) colony_rollup = &rollup_it->second;
            else colonies_missing_demographic_summary.push_back(pid);
            write_planet(j, pid, it->second, ix, st, defs, capital_id, colony_rollup, referenced_species, referenced_leaders, game_date, &name_diagnostics);
            summary.exported_colonies++;
        } else {
            add_unresolved("planet", pid, "country.owned_planets");
            colonies_missing_derived_summary.push_back(pid);
            colonies_missing_demographic_summary.push_back(pid);
            j.begin_object(); j.key("planet_id"); write_id(j, pid); j.key("resolved"); j.value(false); j.end_object();
        }
    }
    j.end_array();

    j.key("controlled_planet_ids");
    j.begin_array();
    for (const std::string& pid : controlled_planets) write_id(j, pid);
    j.end_array();

    write_demographics(j, rollups.demographics, &name_diagnostics);
    write_workforce_summary(j, rollups.workforce);
    write_systems_block(j, map_context, ix, st, &name_diagnostics);
    write_map_summary(j, map_context);

    j.key("fleets");
    j.begin_array();
    for (const std::string& fid : military_fleet_ids) {
        auto it = ix.fleets.find(fid);
        if (it != ix.fleets.end()) write_fleet(j, fid, it->second, ix, st, defs, game_date, referenced_leaders, &name_diagnostics, &unresolved_refs, military_rollup);
        else { add_unresolved("fleet", fid, "country.fleets_manager.owned_fleets"); j.begin_object(); j.key("fleet_id"); write_id(j, fid); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_array();

    write_ship_designs(j, military_rollup, ix, st, &name_diagnostics);

    j.key("owned_armies");
    j.begin_array();
    for (const std::string& aid : army_context.unresolved_army_ids) {
        add_unresolved("army", aid, "country.owned_armies");
        j.begin_object(); j.key("army_id"); write_id(j, aid); j.key("resolved"); j.value(false); j.end_object();
    }
    j.end_array();
    write_army_formations(j, army_context, &name_diagnostics);

    j.key("species");
    j.begin_object();
    for (const std::string& sid : referenced_species) {
        j.key(sid);
        auto it = ix.species.find(sid);
        if (it != ix.species.end()) write_resolved_species(j, sid, it->second, st, defs, &name_diagnostics);
        else { add_unresolved("species", sid, "country.species"); j.begin_object(); j.key("species_id"); write_id(j, sid); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_object();

    j.key("leaders");
    j.begin_object();
    for (const std::string& lid : referenced_leaders) {
        j.key(lid);
        auto it = ix.leaders.find(lid);
        if (it != ix.leaders.end()) {
            write_resolved_leader(j, lid, it->second, st, ix, defs, game_date, &name_diagnostics, &leader_stats, &unresolved_refs);
        } else {
            leader_stats.leader_count++;
            leader_stats.leaders_missing_names++;
            add_unresolved("leader", lid, "country.leaders");
            add_warning(&unresolved_refs, "missing_leader_name", lid, "leaders[].name", "");
            j.begin_object();
            j.key("leader_id"); write_id(j, lid);
            j.key("name"); j.value(lid);
            j.key("resolved"); j.value(false);
            j.end_object();
        }
    }
    j.end_object();

    j.key("derived_summary");
    j.begin_object();
    j.key("identity");
    j.begin_object();
    j.key("country_id"); write_id(j, country_id);
    j.key("country_name"); j.value(summary.country_name);
    j.key("game_date"); j.value(game_date);
    j.key("capital_planet_id"); write_id(j, capital_id);
    j.key("capital_planet_name"); j.value(summary.capital_name);
    j.end_object();
    j.key("colonies");
    j.begin_object();
    j.key("owned_planet_count"); j.raw_number(std::to_string(summary.owned_planets));
    j.key("exported_colony_count"); j.raw_number(std::to_string(summary.exported_colonies));
    j.end_object();
    j.key("economy");
    j.begin_object();
    for (const std::string& k : {"economy_power", "tech_power", "empire_size", "victory_score", "victory_rank", "num_sapient_pops", "employable_pops"}) { if (const PdxValue* v = child(country, k)) { j.key(k); write_pdx_as_schema_json(j, v, k); } }
    j.end_object();
    j.key("military");
    j.begin_object();
    for (const std::string& k : {"military_power", "fleet_size", "used_naval_capacity"}) { if (const PdxValue* v = child(country, k)) { j.key(k); write_pdx_as_schema_json(j, v, k); } }
    j.key("fleet_count"); j.raw_number(std::to_string(military_fleet_ids.size()));
    j.key("ship_count"); j.raw_number(std::to_string(military_rollup.ship_count));
    j.key("resolved_ship_count"); j.raw_number(std::to_string(military_rollup.resolved_ship_count));
    j.key("unresolved_ship_count"); j.raw_number(std::to_string(military_rollup.unresolved_ship_count));
    if (!military_rollup.ship_sizes.empty()) { j.key("ship_sizes"); write_count_object(j, military_rollup.ship_sizes); }
    if (military_rollup.hull.count) { j.key("total_hull"); j.raw_number(json_number(military_rollup.hull.total)); }
    if (military_rollup.armor.count) { j.key("total_armor"); j.raw_number(json_number(military_rollup.armor.total)); }
    if (military_rollup.shields.count) { j.key("total_shields"); j.raw_number(json_number(military_rollup.shields.total)); }
    j.key("army_count"); j.raw_number(std::to_string(army_context.non_defense_army_ids.size()));
    j.key("suppressed_non_military_fleet_count"); j.raw_number(std::to_string(suppressed_non_military_fleet_count));
    j.end_object();
    j.key("map");
    j.begin_object();
    j.key("system_count"); j.raw_number(std::to_string(map_context.systems.size()));
    j.key("colony_system_count"); j.raw_number(std::to_string(colony_system_count(map_context)));
    j.key("capital_system_id"); write_id(j, map_context.capital_system_id);
    j.key("capital_system_name"); j.value(map_context.capital_system_name);
    j.key("hyperlane_edge_count"); j.raw_number(std::to_string(map_context.hyperlane_edge_count));
    j.end_object();
    j.key("validation");
    j.begin_object();
    j.key("owned_planets_match_exported_colonies"); j.value(summary.owned_planets == summary.exported_colonies);
    j.key("capital_in_colonies"); j.value(capital_id.empty() || std::find(owned_planets.begin(), owned_planets.end(), capital_id) != owned_planets.end());
    j.key("demographics_species_count"); j.raw_number(std::to_string(rollups.demographics.species.size()));
    j.key("demographics_total_pops"); j.raw_number(json_number(rollups.demographics.total_sapient_pops));
    j.key("demographics_matches_country_pop_count");
    if (auto country_pops = scalar_double(child(country, "num_sapient_pops"))) {
        const double tolerance = std::max(1.0, std::abs(*country_pops) * 0.001);
        j.value(std::abs(rollups.demographics.total_sapient_pops - *country_pops) <= tolerance);
    } else {
        j.value(false);
    }
    j.key("systems_exported_count"); j.raw_number(std::to_string(map_context.systems.size()));
    j.key("colony_systems_exported_count"); j.raw_number(std::to_string(colony_system_count(map_context)));
    j.key("unresolved_name_count"); j.raw_number(std::to_string(name_diagnostics.unresolved_count()));
    j.key("unresolved_name_kinds"); write_unresolved_name_kinds(j, name_diagnostics);
    j.key("generated_name_key_count"); j.raw_number(std::to_string(name_diagnostics.generated_count()));
    j.key("generated_name_key_kinds"); write_generated_name_key_kinds(j, name_diagnostics);
    j.key("leader_count"); j.raw_number(std::to_string(leader_stats.leader_count));
    j.key("leaders_with_generated_names"); j.raw_number(std::to_string(leader_stats.leaders_with_generated_names));
    j.key("leaders_missing_names"); j.raw_number(std::to_string(leader_stats.leaders_missing_names));
    j.key("leaders_with_calculated_age"); j.raw_number(std::to_string(leader_stats.leaders_with_calculated_age));
    j.key("leaders_with_service_length"); j.raw_number(std::to_string(leader_stats.leaders_with_service_length));
    j.key("leader_date_parse_warnings"); j.raw_number(std::to_string(leader_stats.leader_date_parse_warnings));
    j.key("fleet_count"); j.raw_number(std::to_string(military_fleet_ids.size()));
    j.key("ship_count"); j.raw_number(std::to_string(military_rollup.ship_count));
    j.key("resolved_ship_count"); j.raw_number(std::to_string(military_rollup.resolved_ship_count));
    j.key("unresolved_ship_count"); j.raw_number(std::to_string(military_rollup.unresolved_ship_count));
    j.key("unresolved_ship_references"); j.raw_number(std::to_string(military_rollup.unresolved_ship_references));
    j.key("ship_design_count"); j.raw_number(std::to_string(military_rollup.referenced_design_ids.size()));
    j.key("resolved_design_count"); j.raw_number(std::to_string(military_rollup.resolved_design_ids.size()));
    j.key("unresolved_design_references"); j.raw_number(std::to_string(military_rollup.unresolved_design_references));
    j.key("resource_value_available"); j.value(false);
    j.end_object();
    j.end_object();

    j.key("validation");
    j.begin_object();
    j.key("owned_planets_match_exported_colonies"); j.value(summary.owned_planets == summary.exported_colonies);
    j.key("capital_in_colonies"); j.value(capital_id.empty() || std::find(owned_planets.begin(), owned_planets.end(), capital_id) != owned_planets.end());
    j.key("unresolved_reference_count"); j.raw_number(std::to_string(unresolved_refs.size()));
    j.key("warning_count"); j.raw_number(std::to_string(unresolved_refs.size()));
    j.key("unresolved_name_count"); j.raw_number(std::to_string(name_diagnostics.unresolved_count()));
    j.key("unresolved_name_kinds"); write_unresolved_name_kinds(j, name_diagnostics);
    j.key("generated_name_key_count"); j.raw_number(std::to_string(name_diagnostics.generated_count()));
    j.key("generated_name_key_kinds"); write_generated_name_key_kinds(j, name_diagnostics);
    j.key("leader_count"); j.raw_number(std::to_string(leader_stats.leader_count));
    j.key("leaders_with_generated_names"); j.raw_number(std::to_string(leader_stats.leaders_with_generated_names));
    j.key("leaders_missing_names"); j.raw_number(std::to_string(leader_stats.leaders_missing_names));
    j.key("leaders_with_calculated_age"); j.raw_number(std::to_string(leader_stats.leaders_with_calculated_age));
    j.key("leaders_with_service_length"); j.raw_number(std::to_string(leader_stats.leaders_with_service_length));
    j.key("leader_date_parse_warnings"); j.raw_number(std::to_string(leader_stats.leader_date_parse_warnings));
    j.key("fleet_count"); j.raw_number(std::to_string(military_fleet_ids.size()));
    j.key("ship_count"); j.raw_number(std::to_string(military_rollup.ship_count));
    j.key("resolved_ship_count"); j.raw_number(std::to_string(military_rollup.resolved_ship_count));
    j.key("unresolved_ship_count"); j.raw_number(std::to_string(military_rollup.unresolved_ship_count));
    j.key("unresolved_ship_references"); j.raw_number(std::to_string(military_rollup.unresolved_ship_references));
    j.key("ship_design_count"); j.raw_number(std::to_string(military_rollup.referenced_design_ids.size()));
    j.key("resolved_design_count"); j.raw_number(std::to_string(military_rollup.resolved_design_ids.size()));
    j.key("unresolved_design_references"); j.raw_number(std::to_string(military_rollup.unresolved_design_references));
    j.key("resource_value_available"); j.value(false);
    j.key("systems_exported_count"); j.raw_number(std::to_string(map_context.systems.size()));
    j.key("colony_systems_exported_count"); j.raw_number(std::to_string(colony_system_count(map_context)));
    j.key("systems_missing_coordinates"); write_string_array(j, map_context.systems_missing_coordinates);
    j.key("colonies_missing_systems"); write_string_array(j, map_context.colonies_missing_systems);
    j.key("colonies_with_unexported_systems"); write_string_array(j, map_context.colonies_with_unexported_systems);
    j.key("hyperlane_targets_missing_from_export"); write_string_array(j, map_context.hyperlane_targets_missing_from_export);
    j.key("colonies_with_owner_mismatch"); j.begin_array(); j.end_array();
    j.key("colonies_missing_derived_summary");
    j.begin_array();
    for (const std::string& pid : colonies_missing_derived_summary) write_id(j, pid);
    j.end_array();
    j.key("demographics_species_count"); j.raw_number(std::to_string(rollups.demographics.species.size()));
    j.key("demographics_total_pops"); j.raw_number(json_number(rollups.demographics.total_sapient_pops));
    j.key("demographics_matches_country_pop_count");
    if (auto country_pops = scalar_double(child(country, "num_sapient_pops"))) {
        const double tolerance = std::max(1.0, std::abs(*country_pops) * 0.001);
        j.value(std::abs(rollups.demographics.total_sapient_pops - *country_pops) <= tolerance);
    } else {
        j.value(false);
    }
    j.key("colonies_missing_demographic_summary");
    j.begin_array();
    for (const std::string& pid : colonies_missing_demographic_summary) write_id(j, pid);
    j.end_array();
    j.key("species_without_resolution");
    j.begin_array();
    for (const std::string& sid : rollups.demographics.species_without_resolution) write_id(j, sid);
    j.end_array();
    j.key("inactive_job_records_suppressed"); j.raw_number(std::to_string(rollups.workforce.inactive_job_records_suppressed));
    j.key("non_military_fleet_records_suppressed"); j.raw_number(std::to_string(suppressed_non_military_fleet_count));
    j.key("defense_armies_suppressed"); j.raw_number(std::to_string(army_context.defense_armies_suppressed));
    j.key("army_formations_count"); j.raw_number(std::to_string(army_context.formations.size()));
    j.key("has_stored_resources"); j.value(stored_resource_count > 0);
    j.key("stored_resource_count"); j.raw_number(std::to_string(stored_resource_count));
    j.end_object();

    j.key("references");
    j.begin_object();
    j.key("raw_country_id"); write_id(j, country_id);
    j.key("raw_capital_planet_id"); write_id(j, capital_id);
    j.key("referenced_species_ids"); write_id_array(j, referenced_species);
    j.key("referenced_leader_ids"); write_id_array(j, referenced_leaders);
    j.end_object();

    j.key("warnings");
    j.begin_object();
    j.key("unresolved_references");
    j.begin_array();
    for (const auto& ur : unresolved_refs) {
        j.begin_object();
        j.key("kind"); j.value(ur.kind);
        j.key("id"); j.value(ur.id);
        j.key("context"); j.value(ur.context);
        if (!ur.value.empty()) { j.key("value"); j.value(ur.value); }
        j.end_object();
    }
    j.end_array();
    j.end_object();
    summary.unresolved_references = unresolved_refs.size();
    summary.warnings = unresolved_refs.size();
    timeline.unresolved_references = unresolved_refs.size();
    timeline.warnings = unresolved_refs.size();
    timeline.colony_count = std::to_string(summary.exported_colonies);
    timeline.total_pops = scalar_or(child(country, "num_sapient_pops"));
    timeline.military_power = scalar_or(child(country, "military_power"));
    timeline.economy_power = scalar_or(child(country, "economy_power"));
    timeline.tech_power = scalar_or(child(country, "tech_power"));
    timeline.victory_score = scalar_or(child(country, "victory_score"));
    timeline.victory_rank = scalar_or(child(country, "victory_rank"));
    timeline.fleet_size = scalar_or(child(country, "fleet_size"));
    timeline.used_naval_capacity = scalar_or(child(country, "used_naval_capacity"));
    timeline.empire_size = scalar_or(child(country, "empire_size"));

    if (st.include_debug_sections) {
        j.key("debug");
        j.begin_object();
        j.key("index_counts");
        j.begin_object();
        j.key("countries"); j.raw_number(std::to_string(ix.countries.size()));
        j.key("planets"); j.raw_number(std::to_string(ix.planets.size()));
        j.key("species"); j.raw_number(std::to_string(ix.species.size()));
        j.key("leaders"); j.raw_number(std::to_string(ix.leaders.size()));
        j.key("buildings"); j.raw_number(std::to_string(ix.buildings.size()));
        j.key("districts"); j.raw_number(std::to_string(ix.districts.size()));
        j.key("zones"); j.raw_number(std::to_string(ix.zones.size()));
        j.key("pop_groups"); j.raw_number(std::to_string(ix.pop_groups.size()));
        j.key("pop_jobs"); j.raw_number(std::to_string(ix.pop_jobs.size()));
        j.key("fleets"); j.raw_number(std::to_string(ix.fleets.size()));
        j.key("ships"); j.raw_number(std::to_string(ix.ships.size()));
        j.key("ship_designs"); j.raw_number(std::to_string(ix.ship_designs.size()));
        j.key("armies"); j.raw_number(std::to_string(ix.armies.size()));
        j.end_object();
        j.end_object();
    }

    j.end_object();
    out.flush();
    out.close();

    // Lightweight post-write JSON sanity check (detect malformed output early).
    const std::string emitted = read_text_file(out_path);
    if (!looks_like_valid_json_object(emitted)) {
        throw std::runtime_error("Exported invalid JSON: " + out_path.string());
    }
    return {summary, timeline};
}

bool is_defense_army(const PdxValue* army) {
    return lower_copy(scalar_or(child(army, "type"))) == "defense_army";
}

std::string army_formation_name(const std::string& army_id, const PdxValue* army) {
    const std::string fleet_name = localized_name(child(army, "fleet_name"));
    if (!fleet_name.empty()) return fleet_name;
    const std::string planet = scalar_or(child(army, "planet"), scalar_or(child(army, "spawning_planet")));
    const std::string type = scalar_or(child(army, "type"), "army");
    if (!planet.empty()) return "Planet " + planet + " " + type;
    return "Army Formation " + army_id;
}

ArmyExportContext build_army_export_context(const PdxValue* country, const SaveIndexes& ix) {
    ArmyExportContext ctx;
    std::map<std::string, ArmyFormation> grouped;
    for (const std::string& aid : scalar_id_list_from_child(country, "owned_armies")) {
        auto it = ix.armies.find(aid);
        if (it == ix.armies.end()) {
            ctx.unresolved_army_ids.push_back(aid);
            continue;
        }
        const PdxValue* army = it->second;
        if (is_defense_army(army)) {
            ctx.defense_armies_suppressed++;
            continue;
        }

        ctx.non_defense_army_ids.push_back(aid);
        const std::string name = army_formation_name(aid, army);
        const std::string owner = scalar_or(child(army, "owner"));
        const std::string planet = scalar_or(child(army, "planet"), scalar_or(child(army, "spawning_planet")));
        const std::string type = scalar_or(child(army, "type"), "unknown");
        const std::string species = scalar_or(child(army, "species"), "unknown");
        const std::string group_key = !localized_name(child(army, "fleet_name")).empty()
            ? ("fleet:" + localized_name(child(army, "fleet_name")) + ":owner:" + owner)
            : ("planet:" + planet + ":type:" + type + ":owner:" + owner);

        ArmyFormation& f = grouped[group_key];
        if (f.formation_name.empty()) f.formation_name = name;
        if (f.owner.empty()) f.owner = owner;
        if (f.planet.empty()) f.planet = planet;
        f.army_ids.push_back(aid);
        f.composition_by_type[type]++;
        f.composition_by_species[species]++;
        if (auto health = scalar_double(child(army, "health"))) { f.total_health += *health; f.health_count++; }
        if (auto morale = scalar_double(child(army, "morale"))) { f.total_morale += *morale; f.morale_count++; }
        if (auto experience = scalar_double(child(army, "experience"))) { f.total_experience += *experience; f.experience_count++; }
    }

    for (auto& [_, formation] : grouped) ctx.formations.push_back(std::move(formation));
    std::sort(ctx.formations.begin(), ctx.formations.end(), [](const ArmyFormation& a, const ArmyFormation& b) {
        return a.formation_name < b.formation_name;
    });
    return ctx;
}

void write_army_formations(JsonWriter& j, const ArmyExportContext& army_context, NameDiagnostics* diagnostics) {
    j.key("army_formations");
    j.begin_array();
    for (const ArmyFormation& f : army_context.formations) {
        j.begin_object();
        const std::string id = !f.army_ids.empty() ? f.army_ids.front() : "";
        write_display_text(j, "formation_name", f.formation_name, "formation_name_unresolved", diagnostics, "army_formation", id, "army_formations[].formation_name");
        j.key("owner"); write_id(j, f.owner);
        j.key("planet"); write_id(j, f.planet);
        j.key("army_count"); j.raw_number(std::to_string(f.army_ids.size()));
        j.key("composition_by_type"); write_count_object(j, f.composition_by_type);
        j.key("composition_by_species"); write_count_object(j, f.composition_by_species);
        j.key("total_health"); j.raw_number(json_number(f.total_health));
        j.key("average_health"); j.raw_number(json_number(f.health_count ? f.total_health / static_cast<double>(f.health_count) : 0.0));
        j.key("total_morale"); j.raw_number(json_number(f.total_morale));
        j.key("average_morale"); j.raw_number(json_number(f.morale_count ? f.total_morale / static_cast<double>(f.morale_count) : 0.0));
        if (f.experience_count) {
            j.key("average_experience");
            j.raw_number(json_number(f.total_experience / static_cast<double>(f.experience_count)));
        }
        j.key("army_ids");
        j.begin_array();
        for (const std::string& aid : f.army_ids) write_id(j, aid);
        j.end_array();
        j.end_object();
    }
    j.end_array();
}
