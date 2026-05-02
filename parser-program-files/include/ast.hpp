#pragma once

#include "common.hpp"
#include "json_writer.hpp"

struct PdxValue;

struct PdxEntry {
    std::string key;
    PdxValue* value = nullptr;
    size_t line = 0;
};

struct PdxValue {
    enum class Kind { Scalar, Container } kind = Kind::Scalar;
    std::string scalar;
    std::vector<PdxEntry> entries;
    size_t line_start = 0;
    size_t line_end = 0;
};

class PdxDocument {
public:
    PdxValue root;
    std::vector<std::unique_ptr<PdxValue>> arena;

    PdxValue* make_scalar(std::string s, size_t line);
    PdxValue* make_container(size_t line);
};

const PdxValue* child(const PdxValue* obj, const std::string& key);
std::vector<const PdxValue*> children(const PdxValue* obj, const std::string& key);
std::optional<std::string> scalar(const PdxValue* v);
std::string scalar_or(const PdxValue* v, const std::string& fallback = "");
std::optional<double> scalar_double(const PdxValue* v);
std::string localized_name(const PdxValue* v);
std::vector<std::string> primitive_list(const PdxValue* v);
std::unordered_map<std::string, const PdxValue*> index_numeric_children(const PdxValue* obj);
const PdxValue* nested_child(const PdxValue* root, std::initializer_list<std::string> keys);
std::string detect_player_country_id(const PdxValue* root);
void write_source(JsonWriter& j, const PdxValue* v);
void write_pdx_as_json(JsonWriter& j, const PdxValue* v, int max_depth = 40);
