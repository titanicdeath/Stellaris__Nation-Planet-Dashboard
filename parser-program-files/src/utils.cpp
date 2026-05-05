#include "utils.hpp"

std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool starts_with_ci(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

std::vector<std::string> split_csv(const std::string& input) {
    std::vector<std::string> out;
    std::stringstream ss(input);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

bool parse_bool(const std::string& s, bool fallback) {
    const std::string v = lower_copy(trim(s));
    if (v == "true" || v == "yes" || v == "1" || v == "on") return true;
    if (v == "false" || v == "no" || v == "0" || v == "off") return false;
    return fallback;
}

std::string normalize_slashes(std::string s) {
#ifdef _WIN32
    std::replace(s.begin(), s.end(), '/', '\\');
#else
    std::replace(s.begin(), s.end(), '\\', '/');
#endif
    return s;
}

fs::path resolve_path(const fs::path& base, const std::string& raw) {
    fs::path p(normalize_slashes(trim(raw)));
    if (p.is_absolute()) return p;
    return base / p;
}

std::string read_text_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open file for reading: " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_text_file(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    if (!out) throw std::runtime_error("Could not open file for writing: " + p.string());
    out << data;
}

std::string sanitize_filename(std::string s) {
    const std::string bad = "<>:\"/\\|?*";
    for (char& c : s) {
        if (bad.find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) c = '_';
    }
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    if (s.empty()) s = "unnamed";
    return s;
}

std::string now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

uint64_t fnv1a64_bytes(const char* data, size_t len, uint64_t seed) {
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<unsigned char>(data[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

std::string hex64(uint64_t h) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << h;
    return ss.str();
}

std::string file_hash_fnv1a64(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open file for hashing: " + p.string());
    uint64_t h = 1469598103934665603ULL;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto n = static_cast<size_t>(in.gcount());
        if (n) h = fnv1a64_bytes(buf.data(), n, h);
    }
    return hex64(h);
}

std::string string_hash_fnv1a64(const std::string& s) {
    return hex64(fnv1a64_bytes(s.data(), s.size()));
}

bool looks_like_valid_json_object(const std::string& s) {
    bool in_string = false;
    bool escape = false;
    int brace_depth = 0;
    int bracket_depth = 0;
    for (char c : s) {
        if (in_string) {
            if (escape) { escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '{') brace_depth++;
        else if (c == '}') brace_depth--;
        else if (c == '[') bracket_depth++;
        else if (c == ']') bracket_depth--;
        if (brace_depth < 0 || bracket_depth < 0) return false;
    }
    return !in_string && brace_depth == 0 && bracket_depth == 0;
}

std::string output_date_folder(const std::string& date) {
    if (date.empty()) return "unknown-date";
    return sanitize_filename(date);
}

std::string output_date_suffix(const std::string& date) {
    if (date.empty()) return "unknown-date";
    std::string out = date;
    std::replace(out.begin(), out.end(), '.', '-');
    return sanitize_filename(out);
}

bool is_hard_unresolved_name(const std::string& value) {
    const std::string s = trim(value);
    if (s.empty()) return false;
    if (s.find('$') != std::string::npos || s.find('%') != std::string::npos) return true;

    if (s.find('_') == std::string::npos) return false;
    bool has_alpha = false;
    for (unsigned char c : s) {
        if (std::islower(c)) return false;
        if (std::isalpha(c)) has_alpha = true;
    }
    return has_alpha;
}

bool is_generated_name_key(const std::string& value) {
    const std::string s = trim(value);
    return !is_hard_unresolved_name(s) && s.find('_') != std::string::npos;
}

std::string make_display_name_from_key(const std::string& value) {
    std::string s = trim(value);
    if (!is_generated_name_key(s)) return s;

    const std::string planet_marker = "_PLANET_";
    const size_t planet_pos = s.find(planet_marker);
    if (planet_pos != std::string::npos && planet_pos + planet_marker.size() < s.size()) {
        s = s.substr(planet_pos + planet_marker.size());
    } else if (starts_with_ci(s, "SPEC_") && s.size() > 5) {
        s = s.substr(5);
    } else {
        const size_t first_underscore = s.find('_');
        if (first_underscore != std::string::npos && first_underscore + 1 < s.size()) {
            bool token_prefix = false;
            for (size_t i = 0; i < first_underscore; ++i) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                if (std::isupper(c)) token_prefix = true;
                else if (!std::isdigit(c)) {
                    token_prefix = false;
                    break;
                }
            }
            if (token_prefix) s = s.substr(first_underscore + 1);
        }
    }

    std::replace(s.begin(), s.end(), '_', ' ');
    return trim(s);
}

bool is_unresolved_name(const std::string& value) {
    return is_hard_unresolved_name(value);
}

// ================================================================
// Settings
// ================================================================
