#include "country_export_helpers.hpp"

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
