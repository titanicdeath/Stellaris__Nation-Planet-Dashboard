#pragma once

#include "common.hpp"
#include "config.hpp"

class JsonWriter;

struct CountryPerformance {
    std::string country_id;
    std::string country_name;
    size_t exported_colonies = 0;
    size_t systems_exported = 0;
    fs::path output_file;
    double export_seconds = 0.0;
};

struct SavePerformance {
    std::string save_file;
    bool skipped = false;
    std::string skip_reason;
    double manifest_check_seconds = 0.0;
    double load_gamestate_seconds = 0.0;
    double parse_document_seconds = 0.0;
    double build_indexes_seconds = 0.0;
    double select_countries_seconds = 0.0;
    double export_countries_seconds = 0.0;
    double timeline_point_seconds = 0.0;
    double manifest_write_seconds = 0.0;
    double save_total_seconds = 0.0;
    size_t gamestate_bytes = 0;
    size_t indexed_countries = 0;
    size_t indexed_planets = 0;
    size_t indexed_species = 0;
    size_t indexed_fleets = 0;
    size_t indexed_armies = 0;
    size_t selected_countries = 0;
    std::vector<CountryPerformance> countries;
};

struct RunPerformance {
    size_t saves_considered = 0;
    size_t saves_parsed = 0;
    size_t saves_skipped = 0;
    double total_manifest_check_seconds = 0.0;
    double total_skipped_seconds = 0.0;
    double total_load_gamestate_seconds = 0.0;
    double total_parse_document_seconds = 0.0;
    double total_build_indexes_seconds = 0.0;
    double total_select_countries_seconds = 0.0;
    double total_export_countries_seconds = 0.0;
    double total_timeline_point_seconds = 0.0;
    double total_manifest_write_seconds = 0.0;
    double total_timeline_write_seconds = 0.0;
    double total_run_seconds = 0.0;
    size_t total_gamestate_bytes = 0;
};

double elapsed_seconds(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end);
double elapsed_seconds_since(std::chrono::steady_clock::time_point start);
std::string format_seconds(double seconds);
void print_perf_line(const std::string& label, double seconds);
void print_save_performance(const SavePerformance& perf);
void print_total_performance(const RunPerformance& perf);
void write_duration_field(JsonWriter& j, const std::string& key, double seconds);
void write_performance_log(const fs::path& path, const std::vector<SavePerformance>& saves, const RunPerformance& total, bool pretty);
