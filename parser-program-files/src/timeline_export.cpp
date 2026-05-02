#include "timeline_export.hpp"
#include "json_writer.hpp"
#include "performance.hpp"
#include "utils.hpp"

// Writes per-country timeline JSON and preserves existing timeline files during single-save targeted runs.
double write_timeline_outputs(const Settings& st, const std::vector<fs::path>& saves, std::unordered_map<std::string, std::vector<TimelinePoint>>& timeline_by_country) {
    const auto timeline_write_start = std::chrono::steady_clock::now();
    fs::path timeline_dir = st.output_path / "timeline";
    bool existing_timeline_outputs = false;
    if (saves.size() == 1 && fs::exists(timeline_dir)) {
        for (const auto& ent : fs::directory_iterator(timeline_dir)) {
            if (ent.is_regular_file() && lower_copy(ent.path().extension().string()) == ".json") {
                existing_timeline_outputs = true;
                break;
            }
        }
    }
    if (existing_timeline_outputs && !timeline_by_country.empty()) {
        std::cout << "Timeline export: preserving existing timeline files during single-save targeted run.\n";
    } else {
        for (auto& kv : timeline_by_country) {
            if (kv.second.empty()) continue;
            std::sort(kv.second.begin(), kv.second.end(), [](const TimelinePoint& a, const TimelinePoint& b) { return a.game_date < b.game_date; });
            const TimelinePoint& last = kv.second.back();
            fs::path tfile = timeline_dir / (kv.first + "-(" + sanitize_filename(last.country_name) + ").timeline.json");
            fs::create_directories(tfile.parent_path());
            std::ofstream tout(tfile, std::ios::binary);
            JsonWriter tj(tout, st.pretty_json);
            tj.begin_object();
            tj.key("schema_version"); tj.value("dashboard-country-timeline-v0.1");
            tj.key("country_id"); tj.value(kv.first);
            tj.key("country_name"); tj.value(last.country_name);
            tj.key("snapshots"); tj.begin_array();
            for (const auto& p : kv.second) {
                tj.begin_object();
                tj.key("game_date"); tj.value(p.game_date);
                tj.key("save_file"); tj.value(p.save_file);
                tj.key("country_id"); tj.value(p.country_id);
                tj.key("country_name"); tj.value(p.country_name);
                tj.key("colony_count"); tj.value(p.colony_count);
                tj.key("total_pops"); tj.value(p.total_pops);
                tj.key("military_power"); tj.value(p.military_power);
                tj.key("economy_power"); tj.value(p.economy_power);
                tj.key("tech_power"); tj.value(p.tech_power);
                tj.key("victory_score"); tj.value(p.victory_score);
                tj.key("victory_rank"); tj.value(p.victory_rank);
                tj.key("fleet_size"); tj.value(p.fleet_size);
                tj.key("used_naval_capacity"); tj.value(p.used_naval_capacity);
                tj.key("empire_size"); tj.value(p.empire_size);
                tj.key("warning_count"); tj.raw_number(std::to_string(p.warnings));
                tj.key("unresolved_reference_count"); tj.raw_number(std::to_string(p.unresolved_references));
                tj.key("output_json_path"); tj.value(fs::absolute(p.output_file).string());
                tj.end_object();
            }
            tj.end_array();
            tj.end_object();
            tout.flush();
            tout.close();
            const std::string timeline_json = read_text_file(tfile);
            if (!looks_like_valid_json_object(timeline_json)) {
                throw std::runtime_error("Exported invalid timeline JSON: " + tfile.string());
            }
        }
    }
    return elapsed_seconds_since(timeline_write_start);
}
