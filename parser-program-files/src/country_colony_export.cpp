#include "country_export_helpers.hpp"
#include "country_leader_export.hpp"

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
