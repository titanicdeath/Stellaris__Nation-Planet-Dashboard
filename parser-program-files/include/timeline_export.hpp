#pragma once

#include "common.hpp"
#include "config.hpp"

struct TimelinePoint {
    std::string game_date;
    std::string save_file;
    std::string country_id;
    std::string country_name;
    std::string colony_count;
    std::string total_pops;
    std::string military_power;
    std::string economy_power;
    std::string tech_power;
    std::string victory_score;
    std::string victory_rank;
    std::string fleet_size;
    std::string used_naval_capacity;
    std::string empire_size;
    std::string monthly_net;
    size_t unresolved_references = 0;
    size_t warnings = 0;
    fs::path output_file;
};

double write_timeline_outputs(const Settings& st, const std::vector<fs::path>& saves, std::unordered_map<std::string, std::vector<TimelinePoint>>& timeline_by_country);
