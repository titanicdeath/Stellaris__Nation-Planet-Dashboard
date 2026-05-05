#pragma once

#include "country_export.hpp"
#include "json_writer.hpp"
#include "utils.hpp"

struct NameDiagnostics {
    std::map<std::string, size_t> unresolved_kinds;
    std::map<std::string, size_t> generated_kinds;
    std::vector<UnresolvedReference>* warnings = nullptr;

    size_t unresolved_count() const {
        size_t total = 0;
        for (const auto& [_, n] : unresolved_kinds) total += n;
        return total;
    }

    size_t generated_count() const {
        size_t total = 0;
        for (const auto& [_, n] : generated_kinds) total += n;
        return total;
    }

    void add_unresolved(const std::string& kind, const std::string& id, const std::string& context, const std::string& value) {
        if (value.empty() || !is_hard_unresolved_name(value)) return;
        unresolved_kinds[kind]++;
        if (warnings) warnings->push_back(UnresolvedReference{"unresolved_name", id, context, value});
    }

    void add_generated(const std::string& kind, const std::string& value) {
        if (value.empty() || !is_generated_name_key(value)) return;
        generated_kinds[kind]++;
    }
};

struct LeaderExportStats {
    size_t leader_count = 0;
    size_t leaders_with_generated_names = 0;
    size_t leaders_missing_names = 0;
    size_t leaders_with_calculated_age = 0;
    size_t leaders_with_service_length = 0;
    size_t leader_date_parse_warnings = 0;
};

struct MarketExportStats {
    size_t market_resource_count = 0;
    size_t market_unknown_resource_indices = 0;
    size_t market_array_length_warnings = 0;
};

struct MapSystemContext {
    std::set<std::string> colony_planet_ids;
    std::set<std::string> owned_planet_ids;
    std::set<std::string> controlled_planet_ids;
    std::set<std::string> starbase_ids;
    bool is_colony_system = false;
    bool has_capital = false;
    std::string capital_planet_id;
};

struct MapExportContext {
    std::map<std::string, MapSystemContext> systems;
    std::string capital_system_id;
    std::string capital_system_name;
    size_t hyperlane_edge_count = 0;
    std::set<std::string> systems_missing_coordinates;
    std::set<std::string> colonies_missing_systems;
    std::set<std::string> colonies_with_unexported_systems;
    std::set<std::string> hyperlane_targets_missing_from_export;
};

struct ColonyDemographicRollup {
    std::map<std::string, double> pop_category_counts;
    std::map<std::string, size_t> job_counts_by_type;
    std::map<std::string, double> workforce_by_job_type;
    std::map<std::string, double> species_counts_by_id;
    std::map<std::string, double> species_counts_by_name;
    std::map<std::string, double> enslaved_by_species_id;
    size_t suppressed_job_record_count = 0;
};

struct PlanetSpeciesDistribution {
    std::string planet_id;
    std::string planet_name;
    double pops = 0.0;
};

struct SpeciesDemographicRollup {
    std::string species_id;
    std::string name;
    std::string plural;
    std::string species_class;
    std::string portrait;
    std::vector<std::string> traits;
    double total_pops = 0.0;
    double enslaved_pops = 0.0;
    std::vector<PlanetSpeciesDistribution> planet_distribution;
};

struct EmpireDemographicRollup {
    double total_sapient_pops = 0.0;
    std::map<std::string, SpeciesDemographicRollup> species;
    std::set<std::string> species_without_resolution;
};

struct EmpireWorkforceRollup {
    std::map<std::string, size_t> job_counts_by_type;
    std::map<std::string, double> workforce_by_job_type;
    std::map<std::string, double> pop_category_counts;
    std::map<std::string, double> pop_category_share;
    std::map<std::string, std::map<std::string, size_t>> jobs_by_planet;
    std::map<std::string, std::map<std::string, double>> pop_categories_by_planet;
    size_t inactive_job_records_suppressed = 0;
};

struct EmpireRollups {
    EmpireDemographicRollup demographics;
    EmpireWorkforceRollup workforce;
    std::map<std::string, ColonyDemographicRollup> colonies;
};

struct StatRollup {
    double total = 0.0;
    size_t count = 0;

    void add(std::optional<double> value) {
        if (!value) return;
        total += *value;
        count++;
    }
};

struct MilitaryRollup {
    size_t fleet_count = 0;
    size_t ship_count = 0;
    size_t resolved_ship_count = 0;
    size_t unresolved_ship_count = 0;
    size_t unresolved_ship_references = 0;
    size_t unresolved_design_references = 0;
    std::map<std::string, size_t> ship_sizes;
    std::map<std::string, size_t> ship_classes;
    std::map<std::string, size_t> design_counts;
    std::map<std::string, size_t> component_counts;
    std::map<std::string, size_t> weapon_component_counts;
    std::map<std::string, size_t> utility_component_counts;
    StatRollup hull;
    StatRollup max_hull;
    StatRollup armor;
    StatRollup max_armor;
    StatRollup shields;
    StatRollup max_shields;
    StatRollup experience;
    std::set<std::string> referenced_design_ids;
    std::set<std::string> resolved_design_ids;
};

struct ArmyFormation {
    std::string formation_name;
    std::string owner;
    std::string planet;
    std::map<std::string, size_t> composition_by_type;
    std::map<std::string, size_t> composition_by_species;
    std::vector<std::string> army_ids;
    double total_health = 0.0;
    double total_morale = 0.0;
    double total_experience = 0.0;
    size_t health_count = 0;
    size_t morale_count = 0;
    size_t experience_count = 0;
};

struct ArmyExportContext {
    std::vector<ArmyFormation> formations;
    std::vector<std::string> non_defense_army_ids;
    std::vector<std::string> unresolved_army_ids;
    size_t defense_armies_suppressed = 0;
};

void json_optional_scalar(JsonWriter& j, const PdxValue* obj, const std::string& key);
void write_id_name_object(JsonWriter& j, const std::string& id, const PdxValue* obj, const Settings& st, const std::string& block_name);
std::vector<std::string> country_owned_fleet_ids(const PdxValue* country);
std::vector<std::string> scalar_id_list_from_child(const PdxValue* obj, const std::string& key);
bool pdx_truthy(const PdxValue* v);
bool scalar_is_exact_number(const PdxValue* v, double expected);
double assigned_pop_group_amount_sum(const PdxValue* job);
bool is_known_sentinel_job_type(const std::string& type);
bool contains_banned_job_export_token(const std::string& value);
bool all_workforce_fields_are_sentinel(const PdxValue* job);
bool is_exportable_pop_job(const PdxValue* job);
bool is_defense_army(const PdxValue* army);
void add_warning(std::vector<UnresolvedReference>* warnings,
                 const std::string& kind,
                 const std::string& id,
                 const std::string& context,
                 const std::string& value);
void write_display_text(JsonWriter& j,
                        const std::string& field,
                        const std::string& value,
                        const std::string& unresolved_field,
                        NameDiagnostics* diagnostics,
                        const std::string& kind,
                        const std::string& id,
                        const std::string& context);
void write_name_text(JsonWriter& j,
                     const std::string& value,
                     NameDiagnostics* diagnostics,
                     const std::string& kind,
                     const std::string& id,
                     const std::string& context);
void write_unresolved_name_kinds(JsonWriter& j, const NameDiagnostics& diagnostics);
void write_generated_name_key_kinds(JsonWriter& j, const NameDiagnostics& diagnostics);
void write_string_array(JsonWriter& j, const std::set<std::string>& values);
void write_optional_child(JsonWriter& j, const PdxValue* obj, const std::string& source_key, const std::string& output_key);
void write_metric_or_null(JsonWriter& j, const PdxValue* obj, const std::string& key);
void write_count_object(JsonWriter& j, const std::map<std::string, size_t>& counts);
void write_number_object(JsonWriter& j, const std::map<std::string, double>& counts);
void write_species_counts(JsonWriter& j, const std::map<std::string, double>& counts);
std::optional<double> numeric_child_or_scalar(const PdxValue* v, const std::string& child_key);
size_t direct_named_child_count(const PdxValue* v);
std::string display_token(std::string token);
void add_type_counts(std::map<std::string, size_t>& counts,
                     const std::vector<std::string>& ids,
                     const std::unordered_map<std::string, const PdxValue*>& index);
std::string infer_colony_role(const std::string& planet_id,
                              const PdxValue* planet,
                              const std::string& capital_id,
                              const std::map<std::string, size_t>& district_counts,
                              const std::map<std::string, size_t>& building_counts);
