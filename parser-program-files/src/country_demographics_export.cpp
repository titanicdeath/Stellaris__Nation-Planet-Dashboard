#include "country_export_helpers.hpp"

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
