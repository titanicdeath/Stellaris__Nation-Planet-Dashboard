#include "country_export_helpers.hpp"

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

void write_string_array(JsonWriter& j, const std::set<std::string>& values) {
    j.begin_array();
    for (const std::string& value : values) j.value(value);
    j.end_array();
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

