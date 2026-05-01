#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <zlib.h>

namespace fs = std::filesystem;

// ================================================================
// Small utilities
// ================================================================

static std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool starts_with_ci(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

static std::vector<std::string> split_csv(const std::string& input) {
    std::vector<std::string> out;
    std::stringstream ss(input);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static bool parse_bool(const std::string& s, bool fallback = false) {
    const std::string v = lower_copy(trim(s));
    if (v == "true" || v == "yes" || v == "1" || v == "on") return true;
    if (v == "false" || v == "no" || v == "0" || v == "off") return false;
    return fallback;
}

static std::string normalize_slashes(std::string s) {
#ifdef _WIN32
    std::replace(s.begin(), s.end(), '/', '\\');
#else
    std::replace(s.begin(), s.end(), '\\', '/');
#endif
    return s;
}

static fs::path resolve_path(const fs::path& base, const std::string& raw) {
    fs::path p(normalize_slashes(trim(raw)));
    if (p.is_absolute()) return p;
    return base / p;
}

static std::string read_text_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open file for reading: " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void write_text_file(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    if (!out) throw std::runtime_error("Could not open file for writing: " + p.string());
    out << data;
}

static std::string sanitize_filename(std::string s) {
    const std::string bad = "<>:\"/\\|?*";
    for (char& c : s) {
        if (bad.find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) c = '_';
    }
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    if (s.empty()) s = "unnamed";
    return s;
}

static std::string now_iso8601() {
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

static uint64_t fnv1a64_bytes(const char* data, size_t len, uint64_t seed = 1469598103934665603ULL) {
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<unsigned char>(data[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

static std::string hex64(uint64_t h) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << h;
    return ss.str();
}

static std::string file_hash_fnv1a64(const fs::path& p) {
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

static std::string string_hash_fnv1a64(const std::string& s) {
    return hex64(fnv1a64_bytes(s.data(), s.size()));
}

// ================================================================
// Settings
// ================================================================

struct Settings {
    fs::path config_path;
    fs::path project_root;

    fs::path stellaris_game_path;
    fs::path save_files_path;
    fs::path output_path;
    fs::path manifest_path;

    bool parse_all_save_files = true;
    std::vector<std::string> specific_save_files;
    bool ignore_autosaves = true;
    bool force_reparse = false;
    bool reparse_when_settings_change = true;
    bool retain_extracted_gamestate = false;
    fs::path retained_gamestate_path;

    bool parse_all_nations = false;
    bool player_only = true;
    std::vector<std::string> nation_ids;
    std::string nation_name_filter;

    bool include_all_special_nations = false;
    bool include_fallen_empires = true;
    bool include_subjects = true;
    bool include_primitives = false;
    bool include_enclaves = false;
    bool include_crisis = false;
    bool include_event_countries = false;

    bool parse_game_definitions = false;
    bool include_definition_sources = true;

    bool pretty_json = true;
    bool include_raw_ids = true;
    bool include_debug_sections = true;
    bool include_source_locations = true;
    bool include_raw_pdx_objects = false;

    std::string settings_hash;
};

static Settings load_settings(const fs::path& config_path) {
    Settings st;
    st.config_path = fs::absolute(config_path);
    st.project_root = st.config_path.parent_path();
    const std::string content = read_text_file(st.config_path);
    st.settings_hash = string_hash_fnv1a64(content);

    std::map<std::string, std::map<std::string, std::string>> ini;
    std::string section;
    std::istringstream in(content);
    std::string line;
    while (std::getline(in, line)) {
        // Strip comments only if # or ; starts the line after whitespace.
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = lower_copy(trim(t.substr(1, t.size() - 2)));
            continue;
        }
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower_copy(trim(t.substr(0, eq)));
        std::string val = trim(t.substr(eq + 1));
        ini[section][key] = val;
    }

    auto get = [&](const std::string& sec, const std::string& key, const std::string& def = "") -> std::string {
        auto si = ini.find(sec);
        if (si == ini.end()) return def;
        auto ki = si->second.find(key);
        if (ki == si->second.end()) return def;
        return ki->second;
    };
    auto getb = [&](const std::string& sec, const std::string& key, bool def) -> bool {
        return parse_bool(get(sec, key, def ? "true" : "false"), def);
    };

    st.stellaris_game_path = resolve_path(st.project_root, get("paths", "stellaris_game_path"));
    st.save_files_path = resolve_path(st.project_root, get("paths", "save_files_path", "save_files"));
    st.output_path = resolve_path(st.project_root, get("paths", "output_path", "output"));
    st.manifest_path = resolve_path(st.project_root, get("paths", "manifest_path", "output/manifest.json"));

    st.parse_all_save_files = getb("save_selection", "parse_all_save_files", true);
    st.specific_save_files = split_csv(get("save_selection", "specific_save_files"));
    st.ignore_autosaves = getb("save_selection", "ignore_autosaves", true);
    st.force_reparse = getb("save_selection", "force_reparse", false);
    st.reparse_when_settings_change = getb("save_selection", "reparse_when_settings_change", true);
    st.retain_extracted_gamestate = getb("save_selection", "retain_extracted_gamestate", false);
    st.retained_gamestate_path = resolve_path(st.project_root, get("save_selection", "retained_gamestate_path", "output/_extracted_gamestate"));

    st.parse_all_nations = getb("nation_selection", "parse_all_nations", false);
    st.player_only = getb("nation_selection", "player_only", true);
    st.nation_ids = split_csv(get("nation_selection", "nation_ids", "0"));
    st.nation_name_filter = get("nation_selection", "nation_name_filter");

    st.include_all_special_nations = getb("special_nations", "include_all_special_nations", false);
    st.include_fallen_empires = getb("special_nations", "include_fallen_empires", true);
    st.include_subjects = getb("special_nations", "include_subjects", true);
    st.include_primitives = getb("special_nations", "include_primitives", false);
    st.include_enclaves = getb("special_nations", "include_enclaves", false);
    st.include_crisis = getb("special_nations", "include_crisis", false);
    st.include_event_countries = getb("special_nations", "include_event_countries", false);

    st.parse_game_definitions = getb("game_definitions", "parse_game_definitions", false);
    st.include_definition_sources = getb("game_definitions", "include_definition_sources", true);

    st.pretty_json = getb("output", "pretty_json", true);
    st.include_raw_ids = getb("output", "include_raw_ids", true);
    st.include_debug_sections = getb("output", "include_debug_sections", true);
    st.include_source_locations = getb("output", "include_source_locations", true);
    st.include_raw_pdx_objects = getb("output", "include_raw_pdx_objects", false);

    return st;
}

// ================================================================
// Simple JSON writer
// ================================================================

class JsonWriter {
public:
    explicit JsonWriter(std::ostream& os, bool pretty) : os_(os), pretty_(pretty) {}

    void begin_object() { before_value(); os_ << "{"; stack_.push_back({true, true}); }
    void end_object() { newline_before_close(); os_ << "}"; stack_.pop_back(); mark_value_done(); }
    void begin_array() { before_value(); os_ << "["; stack_.push_back({false, true}); }
    void end_array() { newline_before_close(); os_ << "]"; stack_.pop_back(); mark_value_done(); }

    void key(const std::string& k) {
        if (stack_.empty() || !stack_.back().is_object) throw std::runtime_error("JSON key outside object");
        if (!stack_.back().first) os_ << ",";
        newline_indent();
        write_escaped(k);
        os_ << (pretty_ ? ": " : ":");
        stack_.back().first = false;
        expecting_value_after_key_ = true;
    }

    void value(const std::string& v) { before_value(); write_escaped(v); mark_value_done(); }
    void value(const char* v) { value(std::string(v)); }
    void value(bool v) { before_value(); os_ << (v ? "true" : "false"); mark_value_done(); }
    void value(std::nullptr_t) { before_value(); os_ << "null"; mark_value_done(); }
    void raw_number(const std::string& s) { before_value(); os_ << s; mark_value_done(); }

private:
    struct Frame { bool is_object; bool first; };
    std::ostream& os_;
    bool pretty_ = true;
    std::vector<Frame> stack_;
    bool expecting_value_after_key_ = false;

    void before_value() {
        if (expecting_value_after_key_) { expecting_value_after_key_ = false; return; }
        if (!stack_.empty() && !stack_.back().is_object) {
            if (!stack_.back().first) os_ << ",";
            newline_indent();
            stack_.back().first = false;
        }
    }
    void mark_value_done() {}
    void newline_indent() {
        if (!pretty_) return;
        os_ << "\n";
        for (size_t i = 0; i < stack_.size(); ++i) os_ << "  ";
    }
    void newline_before_close() {
        if (!pretty_) return;
        if (!stack_.empty() && !stack_.back().first) {
            os_ << "\n";
            for (size_t i = 1; i < stack_.size(); ++i) os_ << "  ";
        }
    }
    void write_escaped(const std::string& s) {
        os_ << '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': os_ << "\\\""; break;
                case '\\': os_ << "\\\\"; break;
                case '\b': os_ << "\\b"; break;
                case '\f': os_ << "\\f"; break;
                case '\n': os_ << "\\n"; break;
                case '\r': os_ << "\\r"; break;
                case '\t': os_ << "\\t"; break;
                default:
                    if (c < 0x20) {
                        os_ << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                    } else {
                        os_ << static_cast<char>(c);
                    }
            }
        }
        os_ << '"';
    }
};

static bool looks_int(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

static bool looks_float(const std::string& s) {
    if (s.empty()) return false;
    bool dot = false, digit = false;
    size_t i = (s[0] == '-') ? 1 : 0;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c))) { digit = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        return false;
    }
    return dot && digit;
}

static void json_scalar(JsonWriter& j, const std::string& raw) {
    const std::string low = lower_copy(raw);
    if (low == "yes" || low == "true") { j.value(true); return; }
    if (low == "no" || low == "false") { j.value(false); return; }
    if (raw == "none" || raw == "null") { j.value(nullptr); return; }
    // Dates such as 2343.06.20 contain two dots and must remain strings.
    if (looks_int(raw) || looks_float(raw)) { j.raw_number(raw); return; }
    j.value(raw);
}

// ================================================================
// Minimal ZIP reader for Stellaris .sav files
// ================================================================

static uint16_t le16(const std::vector<unsigned char>& b, size_t off) {
    if (off + 2 > b.size()) throw std::runtime_error("ZIP read past end");
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}
static uint32_t le32(const std::vector<unsigned char>& b, size_t off) {
    if (off + 4 > b.size()) throw std::runtime_error("ZIP read past end");
    return static_cast<uint32_t>(b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24));
}

static std::vector<unsigned char> read_binary_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open file: " + p.string());
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(size);
    if (size) in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
}

static std::string inflate_raw_deflate(const unsigned char* data, size_t compressed_size, size_t expected_size) {
    std::string out;
    out.resize(expected_size);

    z_stream zs{};
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    zs.avail_in = static_cast<uInt>(compressed_size);
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    int rc = inflateInit2(&zs, -MAX_WBITS); // ZIP uses raw deflate streams.
    if (rc != Z_OK) throw std::runtime_error("inflateInit2 failed");
    rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) throw std::runtime_error("zlib inflate failed while extracting gamestate from .sav");
    out.resize(zs.total_out);
    return out;
}

static std::string extract_gamestate_from_sav(const fs::path& sav_path) {
    const auto bytes = read_binary_file(sav_path);
    if (bytes.size() < 22) throw std::runtime_error("File too small to be a ZIP/.sav: " + sav_path.string());

    // End of central directory can have a variable-length comment; search from the back.
    size_t eocd = std::string::npos;
    const size_t start = bytes.size() > (65535 + 22) ? bytes.size() - (65535 + 22) : 0;
    for (size_t i = bytes.size() - 22; i >= start; --i) {
        if (le32(bytes, i) == 0x06054b50) { eocd = i; break; }
        if (i == 0) break;
    }
    if (eocd == std::string::npos) throw std::runtime_error("Could not find ZIP central directory in .sav: " + sav_path.string());

    const uint16_t entries = le16(bytes, eocd + 10);
    const uint32_t cd_size = le32(bytes, eocd + 12);
    const uint32_t cd_offset = le32(bytes, eocd + 16);
    if (static_cast<size_t>(cd_offset) + cd_size > bytes.size()) throw std::runtime_error("Invalid central directory in .sav");

    size_t off = cd_offset;
    for (uint16_t n = 0; n < entries; ++n) {
        if (le32(bytes, off) != 0x02014b50) throw std::runtime_error("Bad ZIP central directory record");
        const uint16_t method = le16(bytes, off + 10);
        const uint32_t comp_size = le32(bytes, off + 20);
        const uint32_t uncomp_size = le32(bytes, off + 24);
        const uint16_t name_len = le16(bytes, off + 28);
        const uint16_t extra_len = le16(bytes, off + 30);
        const uint16_t comment_len = le16(bytes, off + 32);
        const uint32_t local_header = le32(bytes, off + 42);
        std::string name(reinterpret_cast<const char*>(bytes.data() + off + 46), name_len);
        off += 46 + name_len + extra_len + comment_len;

        if (name != "gamestate" && name != "meta" && name.find("gamestate") == std::string::npos) continue;
        if (name != "gamestate" && name.find("gamestate") == std::string::npos) continue;

        if (le32(bytes, local_header) != 0x04034b50) throw std::runtime_error("Bad ZIP local header for gamestate");
        const uint16_t local_name_len = le16(bytes, local_header + 26);
        const uint16_t local_extra_len = le16(bytes, local_header + 28);
        const size_t data_off = static_cast<size_t>(local_header) + 30 + local_name_len + local_extra_len;
        if (data_off + comp_size > bytes.size()) throw std::runtime_error("Compressed gamestate extends past end of .sav");

        if (method == 0) {
            return std::string(reinterpret_cast<const char*>(bytes.data() + data_off), comp_size);
        }
        if (method == 8) {
            return inflate_raw_deflate(bytes.data() + data_off, comp_size, uncomp_size);
        }
        throw std::runtime_error("Unsupported ZIP compression method for gamestate: " + std::to_string(method));
    }
    throw std::runtime_error("No gamestate entry found in .sav: " + sav_path.string());
}

// ================================================================
// Paradox key=value parser
// ================================================================

struct PdxValue;

struct PdxEntry {
    std::string key; // empty means anonymous value inside braces.
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

    PdxValue* make_scalar(std::string s, size_t line) {
        auto v = std::make_unique<PdxValue>();
        v->kind = PdxValue::Kind::Scalar;
        v->scalar = std::move(s);
        v->line_start = line;
        v->line_end = line;
        PdxValue* ptr = v.get();
        arena.push_back(std::move(v));
        return ptr;
    }
    PdxValue* make_container(size_t line) {
        auto v = std::make_unique<PdxValue>();
        v->kind = PdxValue::Kind::Container;
        v->line_start = line;
        PdxValue* ptr = v.get();
        arena.push_back(std::move(v));
        return ptr;
    }
};

struct Token {
    enum class Type { End, Atom, LBrace, RBrace, Equal } type = Type::End;
    std::string text;
    size_t line = 1;
};

class Tokenizer {
public:
    explicit Tokenizer(std::string_view data) : data_(data) {}

    Token peek(size_t n = 0) {
        while (buffer_.size() <= n) buffer_.push_back(next_token_internal());
        return buffer_[n];
    }

    Token next() {
        if (!buffer_.empty()) {
            Token t = buffer_.front();
            buffer_.erase(buffer_.begin());
            return t;
        }
        return next_token_internal();
    }

private:
    std::string_view data_;
    size_t pos_ = 0;
    size_t line_ = 1;
    std::vector<Token> buffer_;

    void skip_ws_and_comments() {
        while (pos_ < data_.size()) {
            char c = data_[pos_];
            if (c == '\n') { ++line_; ++pos_; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) { ++pos_; continue; }
            // Game definition files use comments. Save files usually do not.
            if (c == '#') {
                while (pos_ < data_.size() && data_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }
    }

    Token next_token_internal() {
        skip_ws_and_comments();
        if (pos_ >= data_.size()) return {Token::Type::End, "", line_};
        const size_t tok_line = line_;
        char c = data_[pos_];
        if (c == '{') { ++pos_; return {Token::Type::LBrace, "{", tok_line}; }
        if (c == '}') { ++pos_; return {Token::Type::RBrace, "}", tok_line}; }
        if (c == '=') { ++pos_; return {Token::Type::Equal, "=", tok_line}; }
        if (c == '"') {
            ++pos_;
            std::string out;
            while (pos_ < data_.size()) {
                char ch = data_[pos_++];
                if (ch == '"') break;
                if (ch == '\\' && pos_ < data_.size()) {
                    char esc = data_[pos_++];
                    switch (esc) {
                        case 'n': out.push_back('\n'); break;
                        case 'r': out.push_back('\r'); break;
                        case 't': out.push_back('\t'); break;
                        case '"': out.push_back('"'); break;
                        case '\\': out.push_back('\\'); break;
                        default: out.push_back(esc); break;
                    }
                } else {
                    if (ch == '\n') ++line_;
                    out.push_back(ch);
                }
            }
            return {Token::Type::Atom, out, tok_line};
        }

        std::string out;
        while (pos_ < data_.size()) {
            char ch = data_[pos_];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '{' || ch == '}' || ch == '=') break;
            if (ch == '#') break;
            out.push_back(ch);
            ++pos_;
        }
        return {Token::Type::Atom, out, tok_line};
    }
};

class PdxParser {
public:
    explicit PdxParser(std::string_view data) : tok_(data) {}

    PdxDocument parse_document() {
        PdxDocument doc;
        doc.root.kind = PdxValue::Kind::Container;
        doc.root.line_start = 1;
        while (tok_.peek().type != Token::Type::End) {
            Token k = tok_.next();
            if (k.type != Token::Type::Atom) throw std::runtime_error("Expected top-level key at line " + std::to_string(k.line));
            Token eq = tok_.next();
            if (eq.type != Token::Type::Equal) throw std::runtime_error("Expected '=' after key '" + k.text + "' at line " + std::to_string(k.line));
            PdxValue* val = parse_value(doc);
            doc.root.entries.push_back({k.text, val, k.line});
        }
        doc.root.line_end = tok_.peek().line;
        return doc;
    }

private:
    Tokenizer tok_;

    PdxValue* parse_value(PdxDocument& doc) {
        Token t = tok_.next();
        if (t.type == Token::Type::Atom) return doc.make_scalar(t.text, t.line);
        if (t.type == Token::Type::LBrace) {
            PdxValue* c = doc.make_container(t.line);
            while (true) {
                Token p = tok_.peek();
                if (p.type == Token::Type::End) throw std::runtime_error("Unclosed '{' starting at line " + std::to_string(t.line));
                if (p.type == Token::Type::RBrace) {
                    Token r = tok_.next();
                    c->line_end = r.line;
                    break;
                }
                if (p.type == Token::Type::LBrace) {
                    PdxValue* anon = parse_value(doc);
                    c->entries.push_back({"", anon, anon->line_start});
                    continue;
                }
                if (p.type == Token::Type::Atom) {
                    Token a = tok_.next();
                    if (tok_.peek().type == Token::Type::Equal) {
                        tok_.next();
                        PdxValue* val = parse_value(doc);
                        c->entries.push_back({a.text, val, a.line});
                    } else {
                        c->entries.push_back({"", doc.make_scalar(a.text, a.line), a.line});
                    }
                    continue;
                }
                throw std::runtime_error("Unexpected token inside container at line " + std::to_string(p.line));
            }
            return c;
        }
        throw std::runtime_error("Expected value at line " + std::to_string(t.line));
    }
};

static const PdxValue* child(const PdxValue* obj, const std::string& key) {
    if (!obj || obj->kind != PdxValue::Kind::Container) return nullptr;
    for (const auto& e : obj->entries) if (e.key == key) return e.value;
    return nullptr;
}

static std::vector<const PdxValue*> children(const PdxValue* obj, const std::string& key) {
    std::vector<const PdxValue*> out;
    if (!obj || obj->kind != PdxValue::Kind::Container) return out;
    for (const auto& e : obj->entries) if (e.key == key) out.push_back(e.value);
    return out;
}

static std::optional<std::string> scalar(const PdxValue* v) {
    if (!v || v->kind != PdxValue::Kind::Scalar) return std::nullopt;
    return v->scalar;
}

static std::string scalar_or(const PdxValue* v, const std::string& fallback = "") {
    auto s = scalar(v);
    return s ? *s : fallback;
}

static std::string localized_name(const PdxValue* v) {
    if (!v) return "";
    if (v->kind == PdxValue::Kind::Scalar) return v->scalar;
    if (const PdxValue* key = child(v, "key")) return scalar_or(key);
    if (const PdxValue* name = child(v, "name")) return localized_name(name);
    return "";
}

static std::vector<std::string> primitive_list(const PdxValue* v) {
    std::vector<std::string> out;
    if (!v || v->kind != PdxValue::Kind::Container) return out;
    for (const auto& e : v->entries) {
        if (!e.key.empty()) continue;
        if (e.value && e.value->kind == PdxValue::Kind::Scalar) out.push_back(e.value->scalar);
    }
    return out;
}

static std::unordered_map<std::string, const PdxValue*> index_numeric_children(const PdxValue* obj) {
    std::unordered_map<std::string, const PdxValue*> out;
    if (!obj || obj->kind != PdxValue::Kind::Container) return out;
    for (const auto& e : obj->entries) {
        if (!e.key.empty()) out.emplace(e.key, e.value);
    }
    return out;
}

static const PdxValue* nested_child(const PdxValue* root, std::initializer_list<std::string> keys) {
    const PdxValue* cur = root;
    for (const auto& k : keys) cur = child(cur, k);
    return cur;
}

static std::string detect_player_country_id(const PdxValue* root) {
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

static void write_source(JsonWriter& j, const PdxValue* v) {
    if (!v) { j.value(nullptr); return; }
    j.begin_object();
    j.key("line_start"); j.raw_number(std::to_string(v->line_start));
    j.key("line_end"); j.raw_number(std::to_string(v->line_end));
    j.end_object();
}

static void write_pdx_as_json(JsonWriter& j, const PdxValue* v, int max_depth = 40) {
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

// ================================================================
// Definition source index. This is intentionally light for v1.
// ================================================================

struct DefinitionInfo {
    std::string category;
    std::string file;
    size_t line = 0;
};

struct DefinitionIndex {
    std::unordered_map<std::string, DefinitionInfo> by_token;

    const DefinitionInfo* find(const std::string& token) const {
        auto it = by_token.find(token);
        return it == by_token.end() ? nullptr : &it->second;
    }
};

static void scan_definition_dir(DefinitionIndex& idx, const fs::path& dir, const std::string& category) {
    if (!fs::exists(dir)) return;
    for (const auto& ent : fs::recursive_directory_iterator(dir)) {
        if (!ent.is_regular_file()) continue;
        const auto ext = lower_copy(ent.path().extension().string());
        if (ext != ".txt") continue;
        try {
            const std::string data = read_text_file(ent.path());
            PdxParser parser(data);
            PdxDocument doc = parser.parse_document();
            for (const auto& e : doc.root.entries) {
                if (e.key.empty()) continue;
                idx.by_token.emplace(e.key, DefinitionInfo{category, ent.path().string(), e.line});
            }
        } catch (const std::exception& ex) {
            std::cerr << "[warn] Could not parse definition file " << ent.path() << ": " << ex.what() << "\n";
        }
    }
}

static DefinitionIndex build_definition_index(const Settings& st) {
    DefinitionIndex idx;
    const fs::path common = st.stellaris_game_path / "common";
    scan_definition_dir(idx, common / "buildings", "building");
    scan_definition_dir(idx, common / "districts", "district");
    scan_definition_dir(idx, common / "pop_jobs", "pop_job");
    scan_definition_dir(idx, common / "deposits", "deposit");
    scan_definition_dir(idx, common / "ethics", "ethic");
    scan_definition_dir(idx, common / "governments", "government");
    scan_definition_dir(idx, common / "civics", "civic");
    scan_definition_dir(idx, common / "traits", "trait");
    scan_definition_dir(idx, common / "species_classes", "species_class");
    scan_definition_dir(idx, common / "armies", "army_type");
    scan_definition_dir(idx, common / "policies", "policy");
    return idx;
}

static void write_definition_source(JsonWriter& j, const DefinitionIndex* defs, const std::string& token) {
    if (!defs) { j.value(nullptr); return; }
    const DefinitionInfo* info = defs->find(token);
    if (!info) { j.value(nullptr); return; }
    j.begin_object();
    j.key("category"); j.value(info->category);
    j.key("file"); j.value(info->file);
    j.key("line"); j.raw_number(std::to_string(info->line));
    j.end_object();
}

// ================================================================
// Manifest
// ================================================================

struct ManifestEntry {
    std::string save_path;
    std::string save_hash;
    std::string settings_hash;
    std::string game_date;
    std::vector<std::string> outputs;
    std::string parsed_at;
};

static std::string json_extract_string_field(const std::string& obj, const std::string& field) {
    const std::string needle = "\"" + field + "\"";
    size_t p = obj.find(needle);
    if (p == std::string::npos) return "";
    p = obj.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    p = obj.find('"', p + 1);
    if (p == std::string::npos) return "";
    ++p;
    std::string out;
    bool esc = false;
    for (; p < obj.size(); ++p) {
        char c = obj[p];
        if (esc) { out.push_back(c); esc = false; continue; }
        if (c == '\\') { esc = true; continue; }
        if (c == '"') break;
        out.push_back(c);
    }
    return out;
}

static std::vector<ManifestEntry> load_manifest(const fs::path& p) {
    std::vector<ManifestEntry> out;
    if (!fs::exists(p)) return out;
    std::string s = read_text_file(p);
    size_t pos = 0;
    while (true) {
        size_t sp = s.find("\"save_path\"", pos);
        if (sp == std::string::npos) break;
        size_t obj_start = s.rfind('{', sp);
        size_t obj_end = s.find('}', sp);
        if (obj_start == std::string::npos || obj_end == std::string::npos) break;
        std::string obj = s.substr(obj_start, obj_end - obj_start + 1);
        ManifestEntry e;
        e.save_path = json_extract_string_field(obj, "save_path");
        e.save_hash = json_extract_string_field(obj, "save_hash");
        e.settings_hash = json_extract_string_field(obj, "settings_hash");
        e.game_date = json_extract_string_field(obj, "game_date");
        e.parsed_at = json_extract_string_field(obj, "parsed_at");
        if (!e.save_path.empty()) out.push_back(std::move(e));
        pos = obj_end + 1;
    }
    return out;
}

static void save_manifest(const fs::path& p, const std::vector<ManifestEntry>& entries, bool pretty) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    JsonWriter j(out, pretty);
    j.begin_object();
    j.key("schema_version"); j.value("1.0");
    j.key("hash_algorithm"); j.value("fnv1a64");
    j.key("entries");
    j.begin_array();
    for (const auto& e : entries) {
        j.begin_object();
        j.key("save_path"); j.value(e.save_path);
        j.key("save_hash"); j.value(e.save_hash);
        j.key("settings_hash"); j.value(e.settings_hash);
        j.key("game_date"); j.value(e.game_date);
        j.key("parsed_at"); j.value(e.parsed_at);
        j.key("outputs");
        j.begin_array();
        for (const auto& o : e.outputs) j.value(o);
        j.end_array();
        j.end_object();
    }
    j.end_array();
    j.end_object();
}

static bool should_skip_from_manifest(const Settings& st, const std::vector<ManifestEntry>& manifest, const fs::path& save, const std::string& save_hash) {
    if (st.force_reparse) return false;
    const std::string abs = fs::absolute(save).string();
    for (const auto& e : manifest) {
        if (e.save_path == abs && e.save_hash == save_hash) {
            if (st.reparse_when_settings_change && e.settings_hash != st.settings_hash) return false;
            return true;
        }
    }
    return false;
}

// ================================================================
// Save context and emit helpers
// ================================================================

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

struct UnresolvedReference {
    std::string kind;
    std::string id;
    std::string context;
};

struct CountryExportSummary {
    std::string country_id;
    std::string country_name;
    std::string capital_id;
    std::string capital_name;
    size_t owned_planets = 0;
    size_t exported_colonies = 0;
    size_t unresolved_references = 0;
    size_t warnings = 0;
    fs::path output_file;
};

static SaveIndexes build_indexes(const PdxValue* root) {
    SaveIndexes ix;
    ix.root = root;
    ix.species = index_numeric_children(child(root, "species_db"));
    ix.countries = index_numeric_children(child(root, "country"));
    ix.galactic_objects = index_numeric_children(child(root, "galactic_object"));
    ix.leaders = index_numeric_children(child(root, "leaders"));
    ix.buildings = index_numeric_children(child(root, "buildings"));
    ix.districts = index_numeric_children(child(root, "districts"));
    ix.zones = index_numeric_children(child(root, "zones"));
    ix.deposits = index_numeric_children(child(root, "deposit"));
    ix.pop_groups = index_numeric_children(child(root, "pop_groups"));
    ix.pop_jobs = index_numeric_children(child(root, "pop_jobs"));
    ix.armies = index_numeric_children(child(root, "army"));
    ix.fleets = index_numeric_children(child(root, "fleet"));
    ix.ships = index_numeric_children(child(root, "ships"));
    ix.sectors = index_numeric_children(child(root, "sectors"));

    const PdxValue* planet_parent = nested_child(root, {"planets", "planet"});
    ix.planets = index_numeric_children(planet_parent);

    const PdxValue* queues = nested_child(root, {"construction", "queue_mgr", "queues"});
    ix.construction_queues = index_numeric_children(queues);
    const PdxValue* items = nested_child(root, {"construction", "item_mgr", "items"});
    ix.construction_items = index_numeric_children(items);
    return ix;
}

static void json_optional_scalar(JsonWriter& j, const PdxValue* obj, const std::string& key) {
    j.key(key);
    if (const PdxValue* v = child(obj, key)) write_pdx_as_json(j, v);
    else j.value(nullptr);
}

static void write_id_name_object(JsonWriter& j, const std::string& id, const PdxValue* obj, const Settings& st, const std::string& block_name) {
    j.begin_object();
    j.key("id"); j.value(id);
    j.key("block"); j.value(block_name);
    j.key("name"); j.value(localized_name(child(obj, "name")));
    if (st.include_source_locations) { j.key("source"); write_source(j, obj); }
    j.end_object();
}

static std::string get_country_name(const PdxValue* country) {
    std::string n = localized_name(child(country, "name"));
    if (!n.empty()) return n;
    return scalar_or(child(country, "name"), "Unnamed Country");
}

static std::vector<std::string> country_owned_fleet_ids(const PdxValue* country) {
    std::vector<std::string> ids;
    const PdxValue* owned = nested_child(country, {"fleets_manager", "owned_fleets"});
    if (!owned || owned->kind != PdxValue::Kind::Container) return ids;
    for (const auto& e : owned->entries) {
        if (e.value && e.value->kind == PdxValue::Kind::Container) {
            if (const PdxValue* f = child(e.value, "fleet")) {
                auto s = scalar(f);
                if (s) ids.push_back(*s);
            }
        }
    }
    return ids;
}

static std::vector<std::string> scalar_id_list_from_child(const PdxValue* obj, const std::string& key) {
    return primitive_list(child(obj, key));
}

static void write_resolved_species(JsonWriter& j, const std::string& id, const PdxValue* sp, const Settings& st, const DefinitionIndex* defs) {
    j.begin_object();
    j.key("species_id"); j.value(id);
    j.key("name"); j.value(localized_name(child(sp, "name")));
    j.key("plural"); j.value(localized_name(child(sp, "plural")));
    j.key("adjective"); j.value(localized_name(child(sp, "adjective")));
    json_optional_scalar(j, sp, "class");
    json_optional_scalar(j, sp, "portrait");
    json_optional_scalar(j, sp, "name_list");
    json_optional_scalar(j, sp, "home_planet");
    j.key("traits");
    j.begin_array();
    for (const PdxValue* tv : children(child(sp, "traits"), "trait")) {
        std::string tok = scalar_or(tv);
        j.begin_object();
        j.key("trait"); j.value(tok);
        if (defs) { j.key("definition_source"); write_definition_source(j, defs, tok); }
        j.end_object();
    }
    j.end_array();
    if (st.include_source_locations) { j.key("source"); write_source(j, sp); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, sp); }
    j.end_object();
}

static void write_resolved_leader(JsonWriter& j, const std::string& id, const PdxValue* leader, const Settings& st, const SaveIndexes& ix, const DefinitionIndex* defs) {
    (void)ix;
    j.begin_object();
    j.key("leader_id"); j.value(id);
    j.key("name"); j.value(localized_name(child(leader, "name")));
    json_optional_scalar(j, leader, "class");
    json_optional_scalar(j, leader, "level");
    json_optional_scalar(j, leader, "species");
    json_optional_scalar(j, leader, "country");
    json_optional_scalar(j, leader, "portrait");
    j.key("traits");
    j.begin_array();
    for (const PdxValue* tv : children(leader, "traits")) {
        std::string tok = scalar_or(tv);
        j.begin_object();
        j.key("trait"); j.value(tok);
        if (defs) { j.key("definition_source"); write_definition_source(j, defs, tok); }
        j.end_object();
    }
    j.end_array();
    if (const PdxValue* loc = child(leader, "location")) { j.key("location"); write_pdx_as_json(j, loc); }
    if (st.include_source_locations) { j.key("source"); write_source(j, leader); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, leader); }
    j.end_object();
}

static void write_instance_with_type(JsonWriter& j, const std::string& id, const PdxValue* obj, const std::string& id_name, const Settings& st, const DefinitionIndex* defs) {
    j.begin_object();
    j.key(id_name); j.value(id);
    std::string type = scalar_or(child(obj, "type"));
    if (!type.empty()) { j.key("type"); j.value(type); }
    if (defs && !type.empty()) { j.key("definition_source"); write_definition_source(j, defs, type); }
    if (const PdxValue* pos = child(obj, "position")) { j.key("position"); write_pdx_as_json(j, pos); }
    if (const PdxValue* lvl = child(obj, "level")) { j.key("level"); write_pdx_as_json(j, lvl); }
    if (st.include_source_locations) { j.key("source"); write_source(j, obj); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, obj); }
    j.end_object();
}

static void write_system_summary(JsonWriter& j, const std::string& id, const PdxValue* sys, const Settings& st) {
    j.begin_object();
    j.key("system_id"); j.value(id);
    j.key("name"); j.value(localized_name(child(sys, "name")));
    if (const PdxValue* coord = child(sys, "coordinate")) { j.key("coordinate"); write_pdx_as_json(j, coord); }
    json_optional_scalar(j, sys, "type");
    json_optional_scalar(j, sys, "star_class");
    json_optional_scalar(j, sys, "sector");
    j.key("planet_ids");
    j.begin_array();
    for (const PdxValue* pv : children(sys, "planet")) j.value(scalar_or(pv));
    j.end_array();
    if (const PdxValue* h = child(sys, "hyperlane")) { j.key("hyperlanes"); write_pdx_as_json(j, h); }
    if (const PdxValue* sb = child(sys, "starbases")) { j.key("starbases"); write_pdx_as_json(j, sb); }
    if (st.include_source_locations) { j.key("source"); write_source(j, sys); }
    j.end_object();
}

static void write_planet(JsonWriter& j, const std::string& planet_id, const PdxValue* planet, const SaveIndexes& ix, const Settings& st, const DefinitionIndex* defs, std::set<std::string>& referenced_species, std::set<std::string>& referenced_leaders) {
    j.begin_object();
    j.key("planet_id"); j.value(planet_id);
    j.key("name"); j.value(localized_name(child(planet, "name")));
    json_optional_scalar(j, planet, "planet_class");
    json_optional_scalar(j, planet, "planet_size");
    json_optional_scalar(j, planet, "owner");
    json_optional_scalar(j, planet, "controller");
    json_optional_scalar(j, planet, "original_owner");
    json_optional_scalar(j, planet, "final_designation");
    json_optional_scalar(j, planet, "designation");
    json_optional_scalar(j, planet, "orbit");
    if (const PdxValue* coord = child(planet, "coordinate")) { j.key("coordinate"); write_pdx_as_json(j, coord); }

    std::string system_id = scalar_or(child(child(planet, "coordinate"), "origin"));
    if (!system_id.empty()) {
        j.key("system");
        auto it = ix.galactic_objects.find(system_id);
        if (it != ix.galactic_objects.end()) write_system_summary(j, system_id, it->second, st);
        else { j.begin_object(); j.key("system_id"); j.value(system_id); j.key("resolved"); j.value(false); j.end_object(); }
    }

    j.key("planet_stats");
    j.begin_object();
    for (const std::string& k : {"stability", "crime", "amenities", "amenities_usage", "free_amenities", "free_housing", "total_housing", "housing_usage", "employable_pops", "num_sapient_pops", "ascension_tier"}) {
        if (const PdxValue* v = child(planet, k)) { j.key(k); write_pdx_as_json(j, v); }
    }
    j.end_object();

    if (const PdxValue* species_info = child(planet, "species_information")) {
        j.key("species_information"); write_pdx_as_json(j, species_info);
        for (const auto& e : species_info->entries) if (!e.key.empty()) referenced_species.insert(e.key);
    }
    for (const std::string& sid : scalar_id_list_from_child(planet, "species_refs")) referenced_species.insert(sid);
    for (const std::string& sid : scalar_id_list_from_child(planet, "enslaved_species_refs")) referenced_species.insert(sid);

    j.key("districts");
    j.begin_array();
    for (const std::string& did : scalar_id_list_from_child(planet, "districts")) {
        auto it = ix.districts.find(did);
        if (it == ix.districts.end()) { j.begin_object(); j.key("district_id"); j.value(did); j.key("resolved"); j.value(false); j.end_object(); continue; }
        j.begin_object();
        j.key("district_id"); j.value(did);
        std::string type = scalar_or(child(it->second, "type"));
        j.key("type"); j.value(type);
        if (defs && !type.empty()) { j.key("definition_source"); write_definition_source(j, defs, type); }
        json_optional_scalar(j, it->second, "level");
        j.key("zones");
        j.begin_array();
        for (const std::string& zid : scalar_id_list_from_child(it->second, "zones")) {
            auto zit = ix.zones.find(zid);
            j.begin_object();
            j.key("zone_id"); j.value(zid);
            if (zit != ix.zones.end()) {
                json_optional_scalar(j, zit->second, "type");
                j.key("buildings");
                j.begin_array();
                for (const std::string& bid : scalar_id_list_from_child(zit->second, "buildings")) {
                    auto bit = ix.buildings.find(bid);
                    if (bit != ix.buildings.end()) write_instance_with_type(j, bid, bit->second, "building_id", st, defs);
                    else { j.begin_object(); j.key("building_id"); j.value(bid); j.key("resolved"); j.value(false); j.end_object(); }
                }
                j.end_array();
            } else {
                j.key("resolved"); j.value(false);
            }
            j.end_object();
        }
        j.end_array();
        if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
        if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, it->second); }
        j.end_object();
    }
    j.end_array();

    j.key("buildings_cache");
    j.begin_array();
    for (const std::string& bid : scalar_id_list_from_child(planet, "buildings_cache")) {
        auto it = ix.buildings.find(bid);
        if (it != ix.buildings.end()) write_instance_with_type(j, bid, it->second, "building_id", st, defs);
        else { j.begin_object(); j.key("building_id"); j.value(bid); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_array();

    j.key("deposits");
    j.begin_array();
    for (const std::string& did : scalar_id_list_from_child(planet, "deposits")) {
        auto it = ix.deposits.find(did);
        if (it != ix.deposits.end()) write_instance_with_type(j, did, it->second, "deposit_id", st, defs);
        else { j.begin_object(); j.key("deposit_id"); j.value(did); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_array();

    j.key("pop_groups");
    j.begin_array();
    for (const std::string& pgid : scalar_id_list_from_child(planet, "pop_groups")) {
        auto it = ix.pop_groups.find(pgid);
        j.begin_object();
        j.key("pop_group_id"); j.value(pgid);
        if (it != ix.pop_groups.end()) {
            if (const PdxValue* key = child(it->second, "key")) {
                j.key("key"); write_pdx_as_json(j, key);
                std::string sid = scalar_or(child(key, "species"));
                if (!sid.empty()) referenced_species.insert(sid);
            }
            for (const std::string& k : {"planet", "size", "fraction", "habitability", "happiness", "power", "crime", "amenities_usage", "housing_usage", "last_month_growth", "month_start_size"}) {
                if (const PdxValue* v = child(it->second, k)) { j.key(k); write_pdx_as_json(j, v); }
            }
            if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
            if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, it->second); }
        } else {
            j.key("resolved"); j.value(false);
        }
        j.end_object();
    }
    j.end_array();

    j.key("jobs");
    j.begin_array();
    for (const std::string& jid : scalar_id_list_from_child(planet, "pop_jobs")) {
        auto it = ix.pop_jobs.find(jid);
        j.begin_object();
        j.key("job_id"); j.value(jid);
        if (it != ix.pop_jobs.end()) {
            std::string type = scalar_or(child(it->second, "type"));
            j.key("type"); j.value(type);
            if (defs && !type.empty()) { j.key("definition_source"); write_definition_source(j, defs, type); }
            for (const std::string& k : {"workforce", "max_workforce", "bonus_workforce", "workforce_limit", "automated_workforce", "planet"}) {
                if (const PdxValue* v = child(it->second, k)) { j.key(k); write_pdx_as_json(j, v); }
            }
            if (const PdxValue* pg = child(it->second, "pop_groups")) { j.key("pop_groups"); write_pdx_as_json(j, pg); }
            if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
            if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, it->second); }
        } else {
            j.key("resolved"); j.value(false);
        }
        j.end_object();
    }
    j.end_array();

    j.key("economy");
    j.begin_object();
    for (const std::string& k : {"produces", "upkeep", "profits", "trade_value"}) {
        if (const PdxValue* v = child(planet, k)) { j.key(k); write_pdx_as_json(j, v); }
    }
    j.end_object();

    j.key("construction");
    j.begin_object();
    for (const std::string& qk : {"build_queue", "army_build_queue"}) {
        std::string qid = scalar_or(child(planet, qk));
        j.key(qk);
        if (qid.empty()) { j.value(nullptr); continue; }
        auto qit = ix.construction_queues.find(qid);
        j.begin_object();
        j.key("queue_id"); j.value(qid);
        if (qit != ix.construction_queues.end()) {
            j.key("queue"); write_pdx_as_json(j, qit->second);
            j.key("items_resolved");
            j.begin_array();
            for (const std::string& item_id : scalar_id_list_from_child(qit->second, "items")) {
                auto iit = ix.construction_items.find(item_id);
                j.begin_object();
                j.key("item_id"); j.value(item_id);
                if (iit != ix.construction_items.end()) write_pdx_as_json(j, iit->second);
                else { j.key("resolved"); j.value(false); }
                j.end_object();
            }
            j.end_array();
        } else {
            j.key("resolved"); j.value(false);
        }
        j.end_object();
    }
    j.end_object();

    std::string governor = scalar_or(child(planet, "governor"));
    if (!governor.empty() && governor != "4294967295") {
        referenced_leaders.insert(governor);
        j.key("governor");
        auto git = ix.leaders.find(governor);
        if (git != ix.leaders.end()) write_resolved_leader(j, governor, git->second, st, ix, defs);
        else { j.begin_object(); j.key("leader_id"); j.value(governor); j.key("resolved"); j.value(false); j.end_object(); }
    }

    j.key("armies");
    j.begin_array();
    for (const std::string& aid : scalar_id_list_from_child(planet, "army")) {
        auto it = ix.armies.find(aid);
        j.begin_object();
        j.key("army_id"); j.value(aid);
        if (it != ix.armies.end()) {
            j.key("name"); j.value(localized_name(child(it->second, "name")));
            std::string type = scalar_or(child(it->second, "type"));
            j.key("type"); j.value(type);
            if (defs && !type.empty()) { j.key("definition_source"); write_definition_source(j, defs, type); }
            for (const std::string& k : {"health", "max_health", "morale", "owner", "species", "planet", "spawning_planet"}) {
                if (const PdxValue* v = child(it->second, k)) { j.key(k); write_pdx_as_json(j, v); }
            }
            std::string sid = scalar_or(child(it->second, "species"));
            if (!sid.empty()) referenced_species.insert(sid);
            if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
        } else {
            j.key("resolved"); j.value(false);
        }
        j.end_object();
    }
    j.end_array();

    if (const PdxValue* mods = child(planet, "timed_modifier")) { j.key("timed_modifiers"); write_pdx_as_json(j, mods); }
    if (const PdxValue* mods = child(planet, "planet_modifier")) { j.key("planet_modifiers"); write_pdx_as_json(j, mods); }
    if (const PdxValue* flags = child(planet, "flags")) { j.key("flags"); write_pdx_as_json(j, flags); }

    if (st.include_source_locations) { j.key("source"); write_source(j, planet); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, planet); }
    j.end_object();
}

static void write_fleet(JsonWriter& j, const std::string& fleet_id, const PdxValue* fleet, const SaveIndexes& ix, const Settings& st, const DefinitionIndex* defs, std::set<std::string>& referenced_leaders) {
    (void)defs;
    j.begin_object();
    j.key("fleet_id"); j.value(fleet_id);
    j.key("name"); j.value(localized_name(child(fleet, "name")));
    json_optional_scalar(j, fleet, "owner");
    json_optional_scalar(j, fleet, "military_power");
    json_optional_scalar(j, fleet, "combat_power");
    json_optional_scalar(j, fleet, "fleet_size");
    json_optional_scalar(j, fleet, "command_limit");
    if (const PdxValue* coord = child(fleet, "coordinate")) { j.key("coordinate"); write_pdx_as_json(j, coord); }
    std::string admiral = scalar_or(child(fleet, "leader"));
    if (admiral.empty()) admiral = scalar_or(child(fleet, "commander"));
    if (!admiral.empty() && admiral != "4294967295") {
        referenced_leaders.insert(admiral);
        j.key("commander");
        auto it = ix.leaders.find(admiral);
        if (it != ix.leaders.end()) write_resolved_leader(j, admiral, it->second, st, ix, defs);
        else { j.begin_object(); j.key("leader_id"); j.value(admiral); j.key("resolved"); j.value(false); j.end_object(); }
    }
    if (const PdxValue* ships = child(fleet, "ships")) {
        j.key("ships"); write_pdx_as_json(j, ships);
    }
    if (st.include_source_locations) { j.key("source"); write_source(j, fleet); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, fleet); }
    j.end_object();
}

static bool is_normal_country_for_v1(const PdxValue* c) {
    const std::string type = scalar_or(child(c, "type"));
    if (type != "default") return false;
    if (!child(c, "name")) return false;
    // Normal empires usually have a capital or owned planets. This filters many internal countries.
    if (child(c, "capital")) return true;
    if (child(c, "owned_planets")) return true;
    return false;
}

static std::vector<std::string> select_country_ids(const Settings& st, const SaveIndexes& ix) {
    std::vector<std::string> ids;
    if (st.parse_all_nations) {
        for (const auto& [id, c] : ix.countries) {
            if (is_normal_country_for_v1(c)) ids.push_back(id);
            else if (st.include_all_special_nations) ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), [](const std::string& a, const std::string& b) {
            try { return std::stoll(a) < std::stoll(b); } catch (...) { return a < b; }
        });
        return ids;
    }
    if (st.player_only) {
        std::string pid = detect_player_country_id(ix.root);
        if (!pid.empty()) ids.push_back(pid);
        return ids;
    }
    return st.nation_ids;
}

static CountryExportSummary write_country_output(const fs::path& out_path,
                                 const std::string& save_file_name,
                                 const std::string& game_date,
                                 const std::string& country_id,
                                 const PdxValue* country,
                                 const SaveIndexes& ix,
                                 const Settings& st,
                                 const DefinitionIndex* defs) {
    CountryExportSummary summary;
    summary.country_id = country_id;
    summary.country_name = get_country_name(country);
    summary.output_file = out_path;
    fs::create_directories(out_path.parent_path());
    std::ofstream out(out_path, std::ios::binary);
    if (!out) throw std::runtime_error("Could not write output: " + out_path.string());
    JsonWriter j(out, st.pretty_json);
    std::set<std::string> referenced_species;
    std::set<std::string> referenced_leaders;

    std::string founder = scalar_or(child(country, "founder_species_ref"));
    std::string built = scalar_or(child(country, "built_species_ref"));
    if (!founder.empty()) referenced_species.insert(founder);
    if (!built.empty()) referenced_species.insert(built);
    for (const std::string& sid : scalar_id_list_from_child(country, "owned_species_refs")) referenced_species.insert(sid);
    for (const std::string& sid : scalar_id_list_from_child(country, "enslaved_species_refs")) referenced_species.insert(sid);
    std::string ruler = scalar_or(child(country, "ruler"));
    if (!ruler.empty() && ruler != "4294967295") referenced_leaders.insert(ruler);

    j.begin_object();
    j.key("schema_version"); j.value("dashboard-country-v0.1");
    j.key("parser_version"); j.value(STELLARIS_PARSER_VERSION);
    j.key("save");
    j.begin_object();
    j.key("file"); j.value(save_file_name);
    j.key("game_date"); j.value(game_date);
    j.key("version"); write_pdx_as_json(j, child(ix.root, "version"));
    j.key("save_name"); write_pdx_as_json(j, child(ix.root, "name"));
    j.end_object();

    j.key("country");
    j.begin_object();
    j.key("country_id"); j.value(country_id);
    j.key("name"); j.value(summary.country_name);
    j.key("adjective"); j.value(localized_name(child(country, "adjective")));
    for (const std::string& k : {"type", "personality", "capital", "starting_system", "military_power", "economy_power", "tech_power", "victory_rank", "victory_score", "fleet_size", "used_naval_capacity", "empire_size", "num_sapient_pops", "employable_pops", "starbase_capacity", "num_upgraded_starbase", "graphical_culture", "city_graphical_culture", "room"}) {
        if (const PdxValue* v = child(country, k)) { j.key(k); write_pdx_as_json(j, v); }
    }
    j.key("founder_species_ref"); j.value(founder);
    j.key("built_species_ref"); j.value(built);
    if (const PdxValue* flag = child(country, "flag")) { j.key("flag"); write_pdx_as_json(j, flag); }
    if (const PdxValue* ethos = child(country, "ethos")) { j.key("ethics"); write_pdx_as_json(j, ethos); }
    if (const PdxValue* gov = child(country, "government")) { j.key("government"); write_pdx_as_json(j, gov); }
    if (const PdxValue* budget = child(country, "budget")) { j.key("budget"); write_pdx_as_json(j, budget); }
    if (const PdxValue* policies = child(country, "active_policies")) { j.key("active_policies"); write_pdx_as_json(j, policies); }
    if (const PdxValue* subjects = child(country, "subjects")) { j.key("subjects"); write_pdx_as_json(j, subjects); }
    if (const PdxValue* trade = child(country, "trade_conversions")) { j.key("trade_conversions"); write_pdx_as_json(j, trade); }
    if (st.include_source_locations) { j.key("source"); write_source(j, country); }
    if (st.include_raw_pdx_objects) { j.key("raw"); write_pdx_as_json(j, country); }
    j.end_object();

    j.key("capital_planet");
    std::vector<UnresolvedReference> unresolved_refs;

    auto add_unresolved = [&](const std::string& kind, const std::string& id, const std::string& ctx) {
        unresolved_refs.push_back(UnresolvedReference{kind, id, ctx});
    };

    std::string capital_id = scalar_or(child(country, "capital"));
    summary.capital_id = capital_id;
    auto capital_it = ix.planets.find(capital_id);
    if (!capital_id.empty() && capital_it != ix.planets.end()) {
        summary.capital_name = localized_name(child(capital_it->second, "name"));
        write_planet(j, capital_id, capital_it->second, ix, st, defs, referenced_species, referenced_leaders);
    } else {
        if (!capital_id.empty()) add_unresolved("planet", capital_id, "country.capital");
        j.value(nullptr);
    }

    j.key("colonies");
    j.begin_array();
    const std::vector<std::string> owned_planets = scalar_id_list_from_child(country, "owned_planets");
    summary.owned_planets = owned_planets.size();
    for (const std::string& pid : owned_planets) {
        auto it = ix.planets.find(pid);
        if (it != ix.planets.end()) {
            write_planet(j, pid, it->second, ix, st, defs, referenced_species, referenced_leaders);
            summary.exported_colonies++;
        } else {
            add_unresolved("planet", pid, "country.owned_planets");
            j.begin_object(); j.key("planet_id"); j.value(pid); j.key("resolved"); j.value(false); j.end_object();
        }
    }
    j.end_array();

    j.key("controlled_planet_ids");
    j.begin_array();
    for (const std::string& pid : scalar_id_list_from_child(country, "controlled_planets")) j.value(pid);
    j.end_array();

    j.key("fleets");
    j.begin_array();
    for (const std::string& fid : country_owned_fleet_ids(country)) {
        auto it = ix.fleets.find(fid);
        if (it != ix.fleets.end()) write_fleet(j, fid, it->second, ix, st, defs, referenced_leaders);
        else { add_unresolved("fleet", fid, "country.fleets_manager.owned_fleets"); j.begin_object(); j.key("fleet_id"); j.value(fid); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_array();

    j.key("owned_armies");
    j.begin_array();
    for (const std::string& aid : scalar_id_list_from_child(country, "owned_armies")) {
        auto it = ix.armies.find(aid);
        j.begin_object();
        j.key("army_id"); j.value(aid);
        if (it != ix.armies.end()) {
            j.key("name"); j.value(localized_name(child(it->second, "name")));
            std::string type = scalar_or(child(it->second, "type"));
            j.key("type"); j.value(type);
            if (defs && !type.empty()) { j.key("definition_source"); write_definition_source(j, defs, type); }
            for (const std::string& k : {"owner", "species", "planet", "health", "max_health", "morale"}) {
                if (const PdxValue* v = child(it->second, k)) { j.key(k); write_pdx_as_json(j, v); }
            }
            std::string sid = scalar_or(child(it->second, "species"));
            if (!sid.empty()) referenced_species.insert(sid);
            if (st.include_source_locations) { j.key("source"); write_source(j, it->second); }
        } else {
            add_unresolved("army", aid, "country.owned_armies");
            j.key("resolved"); j.value(false);
        }
        j.end_object();
    }
    j.end_array();

    j.key("species");
    j.begin_object();
    for (const std::string& sid : referenced_species) {
        j.key(sid);
        auto it = ix.species.find(sid);
        if (it != ix.species.end()) write_resolved_species(j, sid, it->second, st, defs);
        else { add_unresolved("species", sid, "country.species"); j.begin_object(); j.key("species_id"); j.value(sid); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_object();

    j.key("leaders");
    j.begin_object();
    for (const std::string& lid : referenced_leaders) {
        j.key(lid);
        auto it = ix.leaders.find(lid);
        if (it != ix.leaders.end()) write_resolved_leader(j, lid, it->second, st, ix, defs);
        else { add_unresolved("leader", lid, "country.leaders"); j.begin_object(); j.key("leader_id"); j.value(lid); j.key("resolved"); j.value(false); j.end_object(); }
    }
    j.end_object();

    j.key("references");
    j.begin_object();
    j.key("raw_country_id"); j.value(country_id);
    j.key("raw_capital_planet_id"); j.value(capital_id);
    j.key("referenced_species_ids"); j.begin_array(); for (const auto& sid : referenced_species) j.value(sid); j.end_array();
    j.key("referenced_leader_ids"); j.begin_array(); for (const auto& lid : referenced_leaders) j.value(lid); j.end_array();
    j.end_object();

    j.key("warnings");
    j.begin_object();
    j.key("unresolved_references");
    j.begin_array();
    for (const auto& ur : unresolved_refs) {
        j.begin_object();
        j.key("kind"); j.value(ur.kind);
        j.key("id"); j.value(ur.id);
        j.key("context"); j.value(ur.context);
        j.end_object();
    }
    j.end_array();
    j.end_object();
    summary.unresolved_references = unresolved_refs.size();
    summary.warnings = unresolved_refs.size();

    if (st.include_debug_sections) {
        j.key("debug");
        j.begin_object();
        j.key("index_counts");
        j.begin_object();
        j.key("countries"); j.raw_number(std::to_string(ix.countries.size()));
        j.key("planets"); j.raw_number(std::to_string(ix.planets.size()));
        j.key("species"); j.raw_number(std::to_string(ix.species.size()));
        j.key("leaders"); j.raw_number(std::to_string(ix.leaders.size()));
        j.key("buildings"); j.raw_number(std::to_string(ix.buildings.size()));
        j.key("districts"); j.raw_number(std::to_string(ix.districts.size()));
        j.key("zones"); j.raw_number(std::to_string(ix.zones.size()));
        j.key("pop_groups"); j.raw_number(std::to_string(ix.pop_groups.size()));
        j.key("pop_jobs"); j.raw_number(std::to_string(ix.pop_jobs.size()));
        j.key("fleets"); j.raw_number(std::to_string(ix.fleets.size()));
        j.key("armies"); j.raw_number(std::to_string(ix.armies.size()));
        j.end_object();
        j.end_object();
    }

    j.end_object();
    return summary;
}

static bool run_parser_self_tests() {
    struct Case { std::string name; std::string input; };
    const std::vector<Case> cases = {
        {"duplicate keys", "planet=1 planet=2 planet=3"},
        {"anonymous objects", "player={ { name=\"Titanic\" country=0 } }"},
        {"primitive lists", "owned_planets={ 2 3 4 }"},
        {"nested objects", "country={ capital=2 stats={ pops=42 } }"},
        {"quoted strings", "name=\"Tetran Sacrosanct Imperium\""},
        {"bare identifiers", "type=default ethos={ ethic_fanatic_materialist }"},
        {"yes/no bools", "is_ai=no has_gateway=yes"},
        {"empty objects", "flags={}"},
        {"player wrapper", "player={ { name=\"Titanic\" country=0 } }"},
    };
    bool ok = true;
    for (const auto& tc : cases) {
        try {
            PdxParser parser(tc.input);
            PdxDocument doc = parser.parse_document();
            (void)doc;
            std::cout << "[self-test] PASS: " << tc.name << "\n";
        } catch (const std::exception& ex) {
            ok = false;
            std::cout << "[self-test] FAIL: " << tc.name << " -> " << ex.what() << "\n";
        }
    }
    return ok;
}

// ================================================================
// Main workflow
// ================================================================

static std::vector<fs::path> discover_saves(const Settings& st) {
    std::vector<fs::path> saves;
    if (!fs::exists(st.save_files_path)) throw std::runtime_error("save_files_path does not exist: " + st.save_files_path.string());
    if (st.parse_all_save_files) {
        for (const auto& ent : fs::directory_iterator(st.save_files_path)) {
            if (!ent.is_regular_file()) continue;
            const std::string filename = ent.path().filename().string();
            const std::string ext = lower_copy(ent.path().extension().string());
            if (ext != ".sav" && filename != "gamestate" && ext != ".txt") continue;
            if (st.ignore_autosaves && starts_with_ci(filename, "autosave")) continue;
            saves.push_back(ent.path());
        }
    } else {
        for (const auto& f : st.specific_save_files) saves.push_back(st.save_files_path / f);
    }
    std::sort(saves.begin(), saves.end());
    return saves;
}

static std::string load_gamestate_for_save(const Settings& st, const fs::path& save_path) {
    const std::string ext = lower_copy(save_path.extension().string());
    std::string data;
    if (ext == ".sav") data = extract_gamestate_from_sav(save_path);
    else data = read_text_file(save_path);

    if (st.retain_extracted_gamestate) {
        fs::create_directories(st.retained_gamestate_path);
        fs::path out = st.retained_gamestate_path / (save_path.filename().string() + ".gamestate");
        write_text_file(out, data);
    }
    return data;
}

static std::string output_date_folder(const std::string& date) {
    std::string d = date;
    std::replace(d.begin(), d.end(), '.', '-');
    if (d.empty()) d = "unknown-date";
    return d;
}

int main(int argc, char** argv) {
    try {
        fs::path config = "settings.config";
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "--config" || arg == "-c") && i + 1 < argc) config = argv[++i];
            else if (arg == "--self-test") {
                return run_parser_self_tests() ? 0 : 1;
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: stellaris_parser --config settings.config [--self-test]\n";
                return 0;
            }
        }

        Settings st = load_settings(config);
        std::cout << "Stellaris dashboard parser " << STELLARIS_PARSER_VERSION << "\n";
        std::cout << "Project root: " << st.project_root << "\n";
        std::cout << "Save folder:  " << st.save_files_path << "\n";
        std::cout << "Output:       " << st.output_path << "\n";

        std::optional<DefinitionIndex> defs_storage;
        const DefinitionIndex* defs = nullptr;
        if (st.parse_game_definitions) {
            std::cout << "Scanning Stellaris game definitions under " << st.stellaris_game_path << "...\n";
            defs_storage = build_definition_index(st);
            defs = &*defs_storage;
            std::cout << "Indexed " << defs->by_token.size() << " definition tokens.\n";
        }

        auto manifest = load_manifest(st.manifest_path);
        auto saves = discover_saves(st);
        if (saves.empty()) {
            std::cout << "No save files found to parse.\n";
            return 0;
        }

        for (const fs::path& save_path : saves) {
            std::cout << "\n=== " << save_path.filename().string() << " ===\n";
            const std::string save_hash = file_hash_fnv1a64(save_path);
            if (should_skip_from_manifest(st, manifest, save_path, save_hash)) {
                std::cout << "Skipping; manifest says this save/settings combination was already parsed.\n";
                continue;
            }

            std::cout << "Loading gamestate...\n";
            std::string gamestate = load_gamestate_for_save(st, save_path);
            std::cout << "Parsing " << gamestate.size() << " bytes...\n";
            PdxParser parser(gamestate);
            PdxDocument doc = parser.parse_document();
            gamestate.clear();
            gamestate.shrink_to_fit();

            SaveIndexes ix = build_indexes(&doc.root);
            std::string game_date = scalar_or(child(&doc.root, "date"));
            std::cout << "Game date: " << game_date << "\n";
            std::cout << "Indexed countries=" << ix.countries.size() << " planets=" << ix.planets.size() << " species=" << ix.species.size() << "\n";

            std::vector<std::string> selected = select_country_ids(st, ix);
            std::cout << "Selected countries: " << selected.size() << "\n";

            ManifestEntry me;
            me.save_path = fs::absolute(save_path).string();
            me.save_hash = save_hash;
            me.settings_hash = st.settings_hash;
            me.game_date = game_date;
            me.parsed_at = now_iso8601();

            fs::path date_dir = st.output_path / output_date_folder(game_date);
            for (const std::string& cid : selected) {
                auto it = ix.countries.find(cid);
                if (it == ix.countries.end()) {
                    std::cerr << "[warn] Country ID not found: " << cid << "\n";
                    continue;
                }
                std::string cname = get_country_name(it->second);
                fs::path out_file = date_dir / (cid + "-(" + sanitize_filename(cname) + ").json");
                CountryExportSummary s = write_country_output(out_file, save_path.filename().string(), game_date, cid, it->second, ix, st, defs);
                std::cout << "Save: " << game_date << "\n";
                std::cout << "Selected countries: " << selected.size() << "\n";
                std::cout << "  Country " << s.country_id << ": " << s.country_name << "\n";
                if (!s.capital_id.empty()) {
                    std::cout << "    Capital: " << s.capital_id << " / " << (s.capital_name.empty() ? "<unknown>" : s.capital_name) << "\n";
                } else {
                    std::cout << "    Capital: <none>\n";
                }
                std::cout << "    Owned planets: " << s.owned_planets << "\n";
                std::cout << "    Exported colonies: " << s.exported_colonies << "\n";
                std::cout << "    Unresolved references: " << s.unresolved_references << "\n";
                std::cout << "    Warnings: " << s.warnings << "\n";
                std::cout << "    Wrote: " << out_file << "\n";
                me.outputs.push_back(fs::absolute(out_file).string());
            }

            // Replace older manifest entry for this save hash/settings combo, then append the new one.
            manifest.erase(std::remove_if(manifest.begin(), manifest.end(), [&](const ManifestEntry& e) {
                return e.save_path == me.save_path && e.save_hash == me.save_hash && e.settings_hash == me.settings_hash;
            }), manifest.end());
            manifest.push_back(std::move(me));
            save_manifest(st.manifest_path, manifest, st.pretty_json);
        }

        std::cout << "\nDone. Manifest: " << st.manifest_path << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
