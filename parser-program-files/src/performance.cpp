#include "performance.hpp"
#include "json_writer.hpp"

double elapsed_seconds(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

double elapsed_seconds_since(std::chrono::steady_clock::time_point start) {
    return elapsed_seconds(start, std::chrono::steady_clock::now());
}

std::string format_seconds(double seconds) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3) << seconds << "s";
    return ss.str();
}

void print_perf_line(const std::string& label, double seconds) {
    std::cout << "  " << label << ": " << format_seconds(seconds) << "\n";
}

void print_save_performance(const SavePerformance& perf) {
    std::cout << "[perf] " << perf.save_file << "\n";
    if (perf.skipped) {
        std::cout << "  skipped: true\n";
        std::cout << "  skip_reason: " << perf.skip_reason << "\n";
    }
    std::cout << "  gamestate_bytes: " << perf.gamestate_bytes << "\n";
    std::cout << "  indexed_countries: " << perf.indexed_countries << "\n";
    std::cout << "  indexed_planets: " << perf.indexed_planets << "\n";
    std::cout << "  indexed_species: " << perf.indexed_species << "\n";
    std::cout << "  indexed_fleets: " << perf.indexed_fleets << "\n";
    std::cout << "  indexed_armies: " << perf.indexed_armies << "\n";
    std::cout << "  selected_countries: " << perf.selected_countries << "\n";
    print_perf_line("manifest_check", perf.manifest_check_seconds);
    print_perf_line("load_gamestate", perf.load_gamestate_seconds);
    print_perf_line("parse_document", perf.parse_document_seconds);
    print_perf_line("build_indexes", perf.build_indexes_seconds);
    print_perf_line("select_countries", perf.select_countries_seconds);
    for (const auto& country : perf.countries) {
        std::cout << "  export_country[" << country.country_id << "]: " << format_seconds(country.export_seconds) << "\n";
        std::cout << "    name: " << country.country_name << "\n";
        std::cout << "    exported_colonies: " << country.exported_colonies << "\n";
        std::cout << "    systems_exported: " << country.systems_exported << "\n";
        std::cout << "    output_path: " << country.output_file << "\n";
    }
    print_perf_line("export_countries_total", perf.export_countries_seconds);
    print_perf_line("timeline_point_record", perf.timeline_point_seconds);
    print_perf_line("manifest_write", perf.manifest_write_seconds);
    print_perf_line("save_total", perf.save_total_seconds);
}

void print_total_performance(const RunPerformance& perf) {
    std::cout << "[perf] total\n";
    std::cout << "  saves_considered: " << perf.saves_considered << "\n";
    std::cout << "  saves_parsed: " << perf.saves_parsed << "\n";
    std::cout << "  saves_skipped: " << perf.saves_skipped << "\n";
    std::cout << "  total_gamestate_bytes: " << perf.total_gamestate_bytes << "\n";
    print_perf_line("total_manifest_check", perf.total_manifest_check_seconds);
    print_perf_line("total_skipped_time", perf.total_skipped_seconds);
    print_perf_line("total_load_gamestate", perf.total_load_gamestate_seconds);
    print_perf_line("total_parse_document", perf.total_parse_document_seconds);
    print_perf_line("total_build_indexes", perf.total_build_indexes_seconds);
    print_perf_line("total_select_countries", perf.total_select_countries_seconds);
    print_perf_line("total_export_countries", perf.total_export_countries_seconds);
    print_perf_line("total_timeline_point_record", perf.total_timeline_point_seconds);
    print_perf_line("total_manifest_write", perf.total_manifest_write_seconds);
    print_perf_line("total_timeline_write", perf.total_timeline_write_seconds);
    print_perf_line("total_run", perf.total_run_seconds);
}

void write_duration_field(JsonWriter& j, const std::string& key, double seconds) {
    j.key(key);
    j.raw_number(json_number(seconds));
}

void write_performance_log(const fs::path& path, const std::vector<SavePerformance>& saves, const RunPerformance& total, bool pretty) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Could not write performance log: " + path.string());
    JsonWriter j(out, pretty);
    j.begin_object();
    j.key("schema_version"); j.value("stellaris-parser-performance-v0.1");
    j.key("saves"); j.begin_array();
    for (const auto& save : saves) {
        j.begin_object();
        j.key("save_file"); j.value(save.save_file);
        j.key("skipped"); j.value(save.skipped);
        j.key("skip_reason"); j.value(save.skip_reason);
        j.key("gamestate_bytes"); j.raw_number(std::to_string(save.gamestate_bytes));
        j.key("indexed_countries"); j.raw_number(std::to_string(save.indexed_countries));
        j.key("indexed_planets"); j.raw_number(std::to_string(save.indexed_planets));
        j.key("indexed_species"); j.raw_number(std::to_string(save.indexed_species));
        j.key("indexed_fleets"); j.raw_number(std::to_string(save.indexed_fleets));
        j.key("indexed_armies"); j.raw_number(std::to_string(save.indexed_armies));
        j.key("selected_countries"); j.raw_number(std::to_string(save.selected_countries));
        j.key("timings_seconds"); j.begin_object();
        write_duration_field(j, "manifest_check", save.manifest_check_seconds);
        write_duration_field(j, "load_gamestate", save.load_gamestate_seconds);
        write_duration_field(j, "parse_document", save.parse_document_seconds);
        write_duration_field(j, "build_indexes", save.build_indexes_seconds);
        write_duration_field(j, "select_countries", save.select_countries_seconds);
        write_duration_field(j, "export_countries_total", save.export_countries_seconds);
        write_duration_field(j, "timeline_point_record", save.timeline_point_seconds);
        write_duration_field(j, "manifest_write", save.manifest_write_seconds);
        write_duration_field(j, "save_total", save.save_total_seconds);
        j.end_object();
        j.key("countries"); j.begin_array();
        for (const auto& country : save.countries) {
            j.begin_object();
            j.key("country_id"); j.value(country.country_id);
            j.key("country_name"); j.value(country.country_name);
            j.key("exported_colonies"); j.raw_number(std::to_string(country.exported_colonies));
            j.key("systems_exported"); j.raw_number(std::to_string(country.systems_exported));
            j.key("output_path"); j.value(fs::absolute(country.output_file).string());
            write_duration_field(j, "export_seconds", country.export_seconds);
            j.end_object();
        }
        j.end_array();
        j.end_object();
    }
    j.end_array();
    j.key("total"); j.begin_object();
    j.key("saves_considered"); j.raw_number(std::to_string(total.saves_considered));
    j.key("saves_parsed"); j.raw_number(std::to_string(total.saves_parsed));
    j.key("saves_skipped"); j.raw_number(std::to_string(total.saves_skipped));
    j.key("total_gamestate_bytes"); j.raw_number(std::to_string(total.total_gamestate_bytes));
    j.key("timings_seconds"); j.begin_object();
    write_duration_field(j, "total_manifest_check", total.total_manifest_check_seconds);
    write_duration_field(j, "total_skipped_time", total.total_skipped_seconds);
    write_duration_field(j, "total_load_gamestate", total.total_load_gamestate_seconds);
    write_duration_field(j, "total_parse_document", total.total_parse_document_seconds);
    write_duration_field(j, "total_build_indexes", total.total_build_indexes_seconds);
    write_duration_field(j, "total_select_countries", total.total_select_countries_seconds);
    write_duration_field(j, "total_export_countries", total.total_export_countries_seconds);
    write_duration_field(j, "total_timeline_point_record", total.total_timeline_point_seconds);
    write_duration_field(j, "total_manifest_write", total.total_manifest_write_seconds);
    write_duration_field(j, "total_timeline_write", total.total_timeline_write_seconds);
    write_duration_field(j, "total_run", total.total_run_seconds);
    j.end_object();
    j.end_object();
}
