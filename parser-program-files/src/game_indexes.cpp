#include "game_indexes.hpp"
#include "pdx_parser.hpp"
#include "utils.hpp"

const DefinitionInfo* DefinitionIndex::find(const std::string& token) const {
    auto it = by_token.find(token);
    return it == by_token.end() ? nullptr : &it->second;
}

void scan_definition_dir(DefinitionIndex& idx, const fs::path& dir, const std::string& category) {
    if (!fs::exists(dir)) return;
    for (const auto& ent : fs::recursive_directory_iterator(dir)) {
        if (!ent.is_regular_file()) continue;
        const auto ext = lower_copy(ent.path().extension().string());
        if (ext != ".txt") continue;
        try {
            const std::string data = read_text_file(ent.path());
            PdxDocument doc = parse_document(data);
            for (const auto& e : doc.root.entries) {
                if (e.key.empty()) continue;
                idx.by_token.emplace(e.key, DefinitionInfo{category, ent.path().string(), e.line});
            }
        } catch (const std::exception& ex) {
            std::cerr << "[warn] Could not parse definition file " << ent.path() << ": " << ex.what() << "\n";
        }
    }
}

DefinitionIndex build_definition_index(const Settings& st) {
    DefinitionIndex idx;
    const fs::path common = st.stellaris_game_path / "common";
    scan_definition_dir(idx, common / "buildings", "building");
    scan_definition_dir(idx, common / "districts", "district");
    scan_definition_dir(idx, common / "pop_jobs", "pop_job");
    scan_definition_dir(idx, common / "deposits", "deposit");
    scan_definition_dir(idx, common / "ethics", "ethic");
    scan_definition_dir(idx, common / "governments", "government");
    scan_definition_dir(idx, common / "civics", "civic");
    scan_definition_dir(idx, common / "traits", "trait");
    scan_definition_dir(idx, common / "species_classes", "species_class");
    scan_definition_dir(idx, common / "armies", "army_type");
    scan_definition_dir(idx, common / "policies", "policy");
    return idx;
}

void write_definition_source(JsonWriter& j, const DefinitionIndex* defs, const std::string& token) {
    if (!defs) { j.value(nullptr); return; }
    const DefinitionInfo* info = defs->find(token);
    if (!info) { j.value(nullptr); return; }
    j.begin_object();
    j.key("category"); j.value(info->category);
    j.key("file"); j.value(info->file);
    j.key("line"); j.raw_number(std::to_string(info->line));
    j.end_object();
}

// ================================================================
// Manifest
// ================================================================

SaveIndexes build_indexes(const PdxValue* root) {
    SaveIndexes ix;
    ix.root = root;
    ix.species = index_numeric_children(child(root, "species_db"));
    ix.countries = index_numeric_children(child(root, "country"));
    ix.galactic_objects = index_numeric_children(child(root, "galactic_object"));
    ix.leaders = index_numeric_children(child(root, "leaders"));
    ix.buildings = index_numeric_children(child(root, "buildings"));
    ix.districts = index_numeric_children(child(root, "districts"));
    ix.zones = index_numeric_children(child(root, "zones"));
    ix.deposits = index_numeric_children(child(root, "deposit"));
    ix.pop_groups = index_numeric_children(child(root, "pop_groups"));
    ix.pop_jobs = index_numeric_children(child(root, "pop_jobs"));
    ix.armies = index_numeric_children(child(root, "army"));
    ix.fleets = index_numeric_children(child(root, "fleet"));
    ix.ships = index_numeric_children(child(root, "ships"));
    ix.ship_designs = index_numeric_children(child(root, "ship_design"));
    ix.sectors = index_numeric_children(child(root, "sectors"));

    const PdxValue* planet_parent = nested_child(root, {"planets", "planet"});
    ix.planets = index_numeric_children(planet_parent);

    const PdxValue* queues = nested_child(root, {"construction", "queue_mgr", "queues"});
    ix.construction_queues = index_numeric_children(queues);
    const PdxValue* items = nested_child(root, {"construction", "item_mgr", "items"});
    ix.construction_items = index_numeric_children(items);
    return ix;
}
