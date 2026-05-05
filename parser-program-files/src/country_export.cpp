#include "country_export.hpp"
#include "country_colony_export.hpp"
#include "country_demographics_export.hpp"
#include "country_export_helpers.hpp"
#include "country_finance_export.hpp"
#include "country_leader_export.hpp"
#include "country_map_export.hpp"
#include "country_market_export.hpp"
#include "country_military_export.hpp"
#include "utils.hpp"

std::string get_country_name(const PdxValue* country) {
    std::string n = localized_name(child(country, "name"));
    if (!n.empty()) return n;
    return scalar_or(child(country, "name"), "Unnamed Country");
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


std::pair<CountryExportSummary, TimelinePoint> write_country_output(const fs::path& out_path,
                                 const std::string& save_file_name,
                                 const std::string& game_date,
                                 const std::string& country_id,
                                 const PdxValue* country,
                                 const SaveIndexes& ix,
                                 const Settings& st,
                                 const DefinitionIndex* defs,
                                 const LocalizationDb* localization_db) {
    CountryExportSummary summary;
    TimelinePoint timeline;
    summary.country_id = country_id;
    summary.country_name = localize_display_name(get_country_name(country), "country.name", localization_db).display;
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
    name_diagnostics.localization = localization_db;
    LeaderExportStats leader_stats;
    MarketExportStats market_stats;

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
    const PdxValue* market = child(ix.root, "market");
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
    write_market(j, market, country_id, market_stats);

    j.key("capital_planet");

    auto add_unresolved = [&](const std::string& kind, const std::string& id, const std::string& ctx) {
        unresolved_refs.push_back(UnresolvedReference{kind, id, ctx, ""});
    };

    auto capital_it = ix.planets.find(capital_id);
    if (!capital_id.empty() && capital_it != ix.planets.end()) {
        summary.capital_name = localize_display_name(localized_name(child(capital_it->second, "name")), "capital_planet.name", localization_db).display;
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
    j.key("localized_field_count"); j.raw_number(std::to_string(name_diagnostics.localized_field_count));
    j.key("generated_fallback_count"); j.raw_number(std::to_string(name_diagnostics.generated_fallback_count));
    j.key("unresolved_localization_count"); j.raw_number(std::to_string(name_diagnostics.unresolved_localization_count));
    j.key("localization_entry_count"); j.raw_number(std::to_string(localization_db ? localization_db->entry_count : 0));
    j.key("localization_file_count"); j.raw_number(std::to_string(localization_db ? localization_db->source_files.size() : 0));
    j.key("localization_warnings"); j.raw_number(std::to_string((localization_db ? localization_db->warning_count : 0) + name_diagnostics.localization_missing_key_warnings));
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
    j.key("market_resource_count"); j.raw_number(std::to_string(market_stats.market_resource_count));
    j.key("market_unknown_resource_indices"); j.raw_number(std::to_string(market_stats.market_unknown_resource_indices));
    j.key("market_array_length_warnings"); j.raw_number(std::to_string(market_stats.market_array_length_warnings));
    j.key("resource_value_available"); j.value(false);
    j.end_object();
    j.end_object();

    j.key("localization");
    write_localization_status_block(j,
                                    localization_db,
                                    name_diagnostics.localized_field_count,
                                    name_diagnostics.generated_fallback_count,
                                    name_diagnostics.unresolved_localization_count);

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
    j.key("localized_field_count"); j.raw_number(std::to_string(name_diagnostics.localized_field_count));
    j.key("generated_fallback_count"); j.raw_number(std::to_string(name_diagnostics.generated_fallback_count));
    j.key("unresolved_localization_count"); j.raw_number(std::to_string(name_diagnostics.unresolved_localization_count));
    j.key("localization_entry_count"); j.raw_number(std::to_string(localization_db ? localization_db->entry_count : 0));
    j.key("localization_file_count"); j.raw_number(std::to_string(localization_db ? localization_db->source_files.size() : 0));
    j.key("localization_warnings"); j.raw_number(std::to_string((localization_db ? localization_db->warning_count : 0) + name_diagnostics.localization_missing_key_warnings));
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
    j.key("market_resource_count"); j.raw_number(std::to_string(market_stats.market_resource_count));
    j.key("market_unknown_resource_indices"); j.raw_number(std::to_string(market_stats.market_unknown_resource_indices));
    j.key("market_array_length_warnings"); j.raw_number(std::to_string(market_stats.market_array_length_warnings));
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
