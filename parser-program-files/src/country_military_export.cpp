#include "country_export_helpers.hpp"
#include "country_leader_export.hpp"

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


bool is_defense_army(const PdxValue* army) {
    const std::string type = lower_copy(scalar_or(child(army, "type")));
    return type == "defense_army" || type.find("_defense_army") != std::string::npos;
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
