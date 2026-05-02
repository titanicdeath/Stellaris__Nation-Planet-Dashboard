#pragma once

#include "ast.hpp"
#include "config.hpp"
#include "json_writer.hpp"

struct DefinitionInfo {
    std::string category;
    std::string file;
    size_t line = 0;
};

struct DefinitionIndex {
    std::unordered_map<std::string, DefinitionInfo> by_token;

    const DefinitionInfo* find(const std::string& token) const;
};

struct SaveIndexes {
    const PdxValue* root = nullptr;
    std::unordered_map<std::string, const PdxValue*> species;
    std::unordered_map<std::string, const PdxValue*> countries;
    std::unordered_map<std::string, const PdxValue*> planets;
    std::unordered_map<std::string, const PdxValue*> galactic_objects;
    std::unordered_map<std::string, const PdxValue*> leaders;
    std::unordered_map<std::string, const PdxValue*> buildings;
    std::unordered_map<std::string, const PdxValue*> districts;
    std::unordered_map<std::string, const PdxValue*> zones;
    std::unordered_map<std::string, const PdxValue*> deposits;
    std::unordered_map<std::string, const PdxValue*> pop_groups;
    std::unordered_map<std::string, const PdxValue*> pop_jobs;
    std::unordered_map<std::string, const PdxValue*> armies;
    std::unordered_map<std::string, const PdxValue*> fleets;
    std::unordered_map<std::string, const PdxValue*> ships;
    std::unordered_map<std::string, const PdxValue*> sectors;
    std::unordered_map<std::string, const PdxValue*> construction_queues;
    std::unordered_map<std::string, const PdxValue*> construction_items;
};

DefinitionIndex build_definition_index(const Settings& st);
void write_definition_source(JsonWriter& j, const DefinitionIndex* defs, const std::string& token);
SaveIndexes build_indexes(const PdxValue* root);
