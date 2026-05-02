#include "ast.hpp"
#include "utils.hpp"

PdxValue* PdxDocument::make_scalar(std::string s, size_t line) {
        auto v = std::make_unique<PdxValue>();
        v->kind = PdxValue::Kind::Scalar;
        v->scalar = std::move(s);
        v->line_start = line;
        v->line_end = line;
        PdxValue* ptr = v.get();
        arena.push_back(std::move(v));
        return ptr;
}

PdxValue* PdxDocument::make_container(size_t line) {
        auto v = std::make_unique<PdxValue>();
        v->kind = PdxValue::Kind::Container;
        v->line_start = line;
        PdxValue* ptr = v.get();
        arena.push_back(std::move(v));
        return ptr;
}

const PdxValue* child(const PdxValue* obj, const std::string& key) {
    if (!obj || obj->kind != PdxValue::Kind::Container) return nullptr;
    for (const auto& e : obj->entries) if (e.key == key) return e.value;
    return nullptr;
}

std::vector<const PdxValue*> children(const PdxValue* obj, const std::string& key) {
    std::vector<const PdxValue*> out;
    if (!obj || obj->kind != PdxValue::Kind::Container) return out;
    for (const auto& e : obj->entries) if (e.key == key) out.push_back(e.value);
    return out;
}

std::optional<std::string> scalar(const PdxValue* v) {
    if (!v || v->kind != PdxValue::Kind::Scalar) return std::nullopt;
    return v->scalar;
}

std::string scalar_or(const PdxValue* v, const std::string& fallback) {
    auto s = scalar(v);
    return s ? *s : fallback;
}

std::string localized_name(const PdxValue* v) {
    if (!v) return "";
    if (v->kind == PdxValue::Kind::Scalar) return v->scalar;
    if (const PdxValue* key = child(v, "key")) return scalar_or(key);
    if (const PdxValue* name = child(v, "name")) return localized_name(name);
    return "";
}

std::vector<std::string> primitive_list(const PdxValue* v) {
    std::vector<std::string> out;
    if (!v || v->kind != PdxValue::Kind::Container) return out;
    for (const auto& e : v->entries) {
        if (!e.key.empty()) continue;
        if (e.value && e.value->kind == PdxValue::Kind::Scalar) out.push_back(e.value->scalar);
    }
    return out;
}

std::unordered_map<std::string, const PdxValue*> index_numeric_children(const PdxValue* obj) {
    std::unordered_map<std::string, const PdxValue*> out;
    if (!obj || obj->kind != PdxValue::Kind::Container) return out;
    for (const auto& e : obj->entries) {
        if (!e.key.empty()) out.emplace(e.key, e.value);
    }
    return out;
}

const PdxValue* nested_child(const PdxValue* root, std::initializer_list<std::string> keys) {
    const PdxValue* cur = root;
    for (const auto& k : keys) cur = child(cur, k);
    return cur;
}

std::string detect_player_country_id(const PdxValue* root) {
    // Supports both:
    //   player={ country=0 }
    // and Stellaris saves that wrap player data in anonymous objects:
    //   player={ { name="Titanic" country=0 } }
    const PdxValue* player = child(root, "player");
    if (!player || player->kind != PdxValue::Kind::Container) return "";

    std::string direct = scalar_or(child(player, "country"));
    if (!direct.empty()) return direct;

    for (const auto& e : player->entries) {
        if (e.key.empty()) {
            std::string nested = scalar_or(child(e.value, "country"));
            if (!nested.empty()) return nested;
        }
    }
    return "";
}

void write_source(JsonWriter& j, const PdxValue* v) {
    if (!v) { j.value(nullptr); return; }
    j.begin_object();
    j.key("line_start"); j.raw_number(std::to_string(v->line_start));
    j.key("line_end"); j.raw_number(std::to_string(v->line_end));
    j.end_object();
}

void write_pdx_as_json(JsonWriter& j, const PdxValue* v, int max_depth) {
    if (!v) { j.value(nullptr); return; }
    if (max_depth <= 0) { j.value("<max_depth_reached>"); return; }
    if (v->kind == PdxValue::Kind::Scalar) { json_scalar(j, v->scalar); return; }

    bool has_keys = false;
    bool has_anon = false;
    std::map<std::string, int> counts;
    for (const auto& e : v->entries) {
        if (e.key.empty()) has_anon = true;
        else { has_keys = true; counts[e.key]++; }
    }
    if (!has_keys) {
        j.begin_array();
        for (const auto& e : v->entries) write_pdx_as_json(j, e.value, max_depth - 1);
        j.end_array();
        return;
    }

    j.begin_object();
    if (has_anon) {
        j.key("_values");
        j.begin_array();
        for (const auto& e : v->entries) if (e.key.empty()) write_pdx_as_json(j, e.value, max_depth - 1);
        j.end_array();
    }
    std::unordered_set<std::string> emitted;
    for (const auto& e : v->entries) {
        if (e.key.empty() || emitted.count(e.key)) continue;
        emitted.insert(e.key);
        j.key(e.key);
        if (counts[e.key] == 1) {
            write_pdx_as_json(j, e.value, max_depth - 1);
        } else {
            j.begin_array();
            for (const auto& d : v->entries) if (d.key == e.key) write_pdx_as_json(j, d.value, max_depth - 1);
            j.end_array();
        }
    }
    j.end_object();
}

