#pragma once

#include "common.hpp"

std::string trim(std::string s);
std::string lower_copy(std::string s);
bool starts_with_ci(const std::string& s, const std::string& prefix);
std::vector<std::string> split_csv(const std::string& input);
bool parse_bool(const std::string& s, bool fallback = false);
std::string normalize_slashes(std::string s);
fs::path resolve_path(const fs::path& base, const std::string& raw);
std::string read_text_file(const fs::path& p);
void write_text_file(const fs::path& p, const std::string& data);
std::string sanitize_filename(std::string s);
std::string now_iso8601();
uint64_t fnv1a64_bytes(const char* data, size_t len, uint64_t seed = 1469598103934665603ULL);
std::string hex64(uint64_t h);
std::string file_hash_fnv1a64(const fs::path& p);
std::string string_hash_fnv1a64(const std::string& s);
bool looks_like_valid_json_object(const std::string& s);
std::string output_date_folder(const std::string& date);
std::string output_date_suffix(const std::string& date);
bool is_hard_unresolved_name(const std::string& value);
bool is_generated_name_key(const std::string& value);
std::string make_display_name_from_key(const std::string& value);
std::string make_leader_name_part_from_key(const std::string& value);
bool is_unresolved_name(const std::string& value);

struct StellarisDate {
    int year = 0;
    int month = 0;
    int day = 0;
};

std::optional<StellarisDate> parse_stellaris_date(const std::string& value);
std::optional<int> years_between_stellaris_dates(const std::string& start, const std::string& end);
std::optional<int> days_between_stellaris_dates(const std::string& start, const std::string& end);
