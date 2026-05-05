#include "country_export_helpers.hpp"

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
